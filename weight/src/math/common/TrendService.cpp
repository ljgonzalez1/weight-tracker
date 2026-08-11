#include "math/common/TrendService.hpp"

#include <QtConcurrent/QtConcurrentRun>
#include <QFuture>
#include <QFutureSynchronizer>
#include <QThreadPool>

#include "math/common/CurveFactory.hpp"

namespace weight::math {
namespace {

/// Fits one curve into its own slot. Called either directly or on a pool task.
void fitOne(std::size_t index, const PreparedSamples& prepared, const TrendParameters& parameters,
            bool wantDerivative, std::optional<TrendCurve>* curveSlot,
            std::optional<DerivativeCurve>* derivativeSlot) {
    const std::unique_ptr<TrendEstimator> estimator = createCurveEstimator(index);
    if (!estimator) {
        return;
    }
    *curveSlot = estimator->estimatePrepared(prepared, parameters);
    if (wantDerivative) {
        *derivativeSlot = differentiate(**curveSlot, parameters.derivative);
    }
}

TrendResults makeEmpty(std::size_t count) {
    TrendResults results;
    results.curves.resize(count);
    results.derivatives.resize(count);
    return results;
}

}  // namespace

bool TrendRequest::anyWanted() const {
    for (std::size_t i = 0; i < curveWanted.size(); ++i) {
        if (curveWanted[i] || derivativeWanted[i]) {
            return true;
        }
    }
    return false;
}

std::size_t TrendRequest::activeCount() const {
    std::size_t active = 0;
    for (std::size_t i = 0; i < curveWanted.size(); ++i) {
        if (curveWanted[i] || derivativeWanted[i]) {
            ++active;
        }
    }
    return active;
}

TrendResults computeTrendsSerial(std::span<const SamplePoint> samples,
                                 const TrendRequest& request,
                                 const TrendParameters& parameters) {
    TrendResults results = makeEmpty(request.curveWanted.size());
    if (!request.anyWanted()) {
        return results;
    }
    const std::optional<PreparedSamples> prepared = prepareSamples(samples, parameters);
    if (!prepared.has_value()) {
        return results;
    }
    for (std::size_t i = 0; i < request.curveWanted.size(); ++i) {
        if (!request.curveWanted[i] && !request.derivativeWanted[i]) {
            continue;
        }
        fitOne(i, *prepared, parameters, request.derivativeWanted[i], &results.curves[i],
               &results.derivatives[i]);
    }
    return results;
}

TrendResults computeTrends(std::span<const SamplePoint> samples, const TrendRequest& request,
                           const TrendParameters& parameters) {
    TrendResults results = makeEmpty(request.curveWanted.size());
    if (!request.anyWanted()) {
        return results;
    }

    // One shared preprocessing pass, completed before any task starts. From
    // here on `prepared` is read-only and can be shared without a lock.
    const std::optional<PreparedSamples> prepared = prepareSamples(samples, parameters);
    if (!prepared.has_value()) {
        return results;
    }

    // A single active curve is not worth a task hand-off; run it here.
    if (request.activeCount() <= 1) {
        for (std::size_t i = 0; i < request.curveWanted.size(); ++i) {
            if (request.curveWanted[i] || request.derivativeWanted[i]) {
                fitOne(i, *prepared, parameters, request.derivativeWanted[i], &results.curves[i],
                       &results.derivatives[i]);
            }
        }
        return results;
    }

    // Each task writes only to its own slot in vectors that were sized before
    // any task started, so no two tasks ever touch the same memory and no
    // reallocation can invalidate a pointer mid-flight.
    QFutureSynchronizer<void> synchroniser;
    synchroniser.setCancelOnWait(false);

    for (std::size_t i = 0; i < request.curveWanted.size(); ++i) {
        if (!request.curveWanted[i] && !request.derivativeWanted[i]) {
            continue;
        }
        const bool wantDerivative = request.derivativeWanted[i];
        std::optional<TrendCurve>* curveSlot = &results.curves[i];
        std::optional<DerivativeCurve>* derivativeSlot = &results.derivatives[i];
        const PreparedSamples* shared = &(*prepared);

        synchroniser.addFuture(QtConcurrent::run(
            [i, shared, &parameters, wantDerivative, curveSlot, derivativeSlot] {
                fitOne(i, *shared, parameters, wantDerivative, curveSlot, derivativeSlot);
            }));
    }

    // Blocks until every fit has finished. The caller is a worker thread, not
    // the UI thread, so blocking here does not freeze the window.
    synchroniser.waitForFinished();
    return results;
}

}  // namespace weight::math
