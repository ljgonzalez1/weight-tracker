#include "math/common/TrendEstimator.hpp"

#include <algorithm>
#include <cmath>

namespace weight::math {
namespace {

/// Number of nodes of the dense evaluation grid.
///
/// Resolution is requested per day and then clamped to the configured bounds:
/// a short history still gets a grid fine enough for a smooth derivative, and
/// a very long one does not grow without limit.
std::size_t denseGridSize(double start, double end, const TrendCommonParameters& common) {
    const double span = std::max(0.0, end - start);
    const double perDay = static_cast<double>(common.denseGridPointsPerDay);
    const double proposed = std::ceil(span * perDay) + 1.0;

    const auto lower = static_cast<double>(common.denseGridMinPoints);
    const auto upper = static_cast<double>(common.denseGridMaxPoints);
    const double clamped = std::min(std::max(proposed, lower), upper);
    return static_cast<std::size_t>(std::max(3.0, clamped));
}

}  // namespace

std::optional<PreparedSamples> prepareSamples(std::span<const SamplePoint> samples,
                                              const TrendParameters& parameters) {
    const TrendCommonParameters& common = parameters.common;
    if (samples.size() < common.minimumPoints) {
        return std::nullopt;
    }

    PreparedSamples prepared;
    prepared.rawX.reserve(samples.size());
    prepared.rawY.reserve(samples.size());

    std::vector<SamplePoint> ordered(samples.begin(), samples.end());
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const SamplePoint& a, const SamplePoint& b) { return a.day < b.day; });
    for (const SamplePoint& point : ordered) {
        prepared.rawX.push_back(point.day);
        prepared.rawY.push_back(point.mass);
    }

    prepared.aggregated = aggregateByX(prepared.rawX, prepared.rawY);
    if (prepared.aggregated.x.size() < 2) {
        return std::nullopt;  // a trend through a single day is not defined
    }

    const double start = prepared.aggregated.x.front();
    const double end = prepared.aggregated.x.back();
    if (!std::isfinite(start) || !std::isfinite(end) || !(end > start)) {
        return std::nullopt;
    }

    prepared.span = end - start;
    prepared.medianSpacing = medianSpacing(prepared.aggregated.x);
    if (!std::isfinite(prepared.medianSpacing) || prepared.medianSpacing <= 0.0) {
        // Degenerate spacing: fall back to an average gap, never to zero, so
        // that later divisions stay well defined.
        prepared.medianSpacing =
            std::max(prepared.span / static_cast<double>(prepared.aggregated.x.size()), 1.0);
    }

    prepared.grid = linspace(start, end, denseGridSize(start, end, common));
    prepared.gridSpacing = meanSpacing(prepared.grid);
    return prepared;
}

TrendCurve TrendEstimator::estimatePrepared(const PreparedSamples& prepared,
                                            const TrendParameters& parameters) const {
    FitOutcome outcome = fit(prepared, parameters);

    // Every estimator is scored the same way, against the raw samples rather
    // than the daily averages: R-squared then answers the question the reader
    // actually asks, "how well does this curve describe my measurements".
    TrendCurve curve;
    curve.x = prepared.grid;
    curve.y = std::move(outcome.values);
    curve.coefficientOfDetermination =
        coefficientOfDetermination(prepared.rawX, prepared.rawY, curve.x, curve.y);
    curve.diagnostics = std::move(outcome.diagnostics);
    return curve;
}

std::optional<TrendCurve> TrendEstimator::estimate(std::span<const SamplePoint> samples,
                                                   const TrendParameters& parameters) const {
    const std::optional<PreparedSamples> prepared = prepareSamples(samples, parameters);
    if (!prepared.has_value()) {
        return std::nullopt;
    }
    return estimatePrepared(*prepared, parameters);
}

std::optional<DerivativeCurve> differentiate(const TrendCurve& curve,
                                             const DerivativeParameters& parameters) {
    if (curve.x.size() < 2 || curve.y.size() < 2) {
        return std::nullopt;
    }
    const double start = curve.x.front();
    const double end = curve.x.back();
    if (!std::isfinite(start) || !std::isfinite(end) || !(end > start)) {
        return std::nullopt;
    }

    const double step = std::max(parameters.sampleStepDays, 1e-9);

    // Half a step of slack on the upper bound reproduces the inclusive
    // resampling of the original implementation; the endpoint is appended
    // explicitly when the regular grid stops short of it, so the derivative
    // always covers the full curve.
    DerivativeCurve derivative;
    derivative.x = arange(start, end + step * 0.5, step);
    if (derivative.x.size() < 2) {
        derivative.x = {start, end};
    } else if (derivative.x.back() < end) {
        derivative.x.push_back(end);
    }

    const std::vector<double> resampled = interpolateLinear(derivative.x, curve.x, curve.y);
    derivative.y = gradient(resampled, derivative.x);
    for (double& slope : derivative.y) {
        slope *= parameters.daysPerDisplayedPeriod;
    }
    return derivative;
}

}  // namespace weight::math
