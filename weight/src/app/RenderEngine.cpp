#include "app/RenderEngine.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include "core/Logging.hpp"
#include "plot/PainterPlotRenderer.hpp"
#include "plot/AxisWindow.hpp"
#include "plot/PlotSceneBuilder.hpp"
#include "settings/CurveCatalog.hpp"

namespace weight::app {

struct RenderEngine::Private {
    const settings::Settings* settings = nullptr;

    QThread thread;
    QMutex mutex;
    QWaitCondition wakeup;
    QWaitCondition idle;

    /// The one request waiting to be served. Replacing it rather than queueing
    /// is what keeps the slider responsive: intermediate positions the user
    /// dragged through carry no information once they have moved on.
    std::optional<RenderRequest> pending;
    bool busy = false;
    bool stopping = false;
};

RenderEngine::RenderEngine(const settings::Settings& settings, QObject* parent)
    : QObject(parent), d_(std::make_unique<Private>()) {
    qRegisterMetaType<weight::app::RenderResult>("weight::app::RenderResult");
    d_->settings = &settings;

    // The worker is a plain thread running our own loop rather than a
    // QThreadPool task, because it must live for the whole session and sleep
    // between requests instead of occupying a pool slot.
    QObject::connect(&d_->thread, &QThread::started, this, [this] { workerLoop(); },
                     Qt::DirectConnection);
    d_->thread.start();
}

RenderEngine::~RenderEngine() { shutdown(); }

void RenderEngine::shutdown() {
    if (!d_ || !d_->thread.isRunning()) {
        return;
    }
    {
        const QMutexLocker locker(&d_->mutex);
        d_->stopping = true;
        d_->pending.reset();
        d_->wakeup.wakeAll();
    }
    d_->thread.quit();
    if (!d_->thread.wait(5000)) {
        core::log::warning(QStringLiteral("The render thread did not stop in time."));
        d_->thread.terminate();
        d_->thread.wait(1000);
    }
}

void RenderEngine::submit(RenderRequest request) {
    const QMutexLocker locker(&d_->mutex);
    if (d_->stopping) {
        return;
    }
    // Overwrite rather than append: only the newest request matters.
    d_->pending = std::move(request);
    d_->wakeup.wakeAll();
}

void RenderEngine::workerLoop() {
    for (;;) {
        RenderRequest request;
        {
            QMutexLocker locker(&d_->mutex);
            while (!d_->pending.has_value() && !d_->stopping) {
                d_->busy = false;
                d_->idle.wakeAll();
                d_->wakeup.wait(&d_->mutex);
            }
            if (d_->stopping) {
                d_->busy = false;
                d_->idle.wakeAll();
                return;
            }
            request = std::move(*d_->pending);
            d_->pending.reset();
            d_->busy = true;
        }

        // Rendering happens outside the lock, so a new request can arrive and
        // replace the pending slot while this one is still being drawn.
        RenderResult result = renderNow(request, *d_->settings);

        {
            const QMutexLocker locker(&d_->mutex);
            if (d_->stopping) {
                return;
            }
            // If something newer arrived meanwhile, drop this result and go
            // straight to the newer one: drawing a superseded chart would make
            // the preview visibly lag the slider.
            if (d_->pending.has_value()) {
                continue;
            }
        }

        // Queued connection: the signal is delivered on the receiver's thread,
        // which is the UI thread, so the window never touches worker memory
        // while the worker is still writing to it.
        emit finished(result);
    }
}

RenderResult RenderEngine::renderNow(const RenderRequest& request,
                                     const settings::Settings& settings) {
    RenderResult result;
    result.sequence = request.sequence;
    result.coefficients.assign(weight::settings::curveCount(), std::nullopt);

    // A private copy of the settings carries the smoothness for this render, so
    // two renders in flight can never see each other's value.
    settings::Settings effective = settings;
    effective.trend.common.generalSmoothness = request.smoothness;

    // The visible window follows the data: it starts at the oldest measurement,
    // or today when there is none, and reaches whichever is further away, a
    // year from the first or three months from the last. Applied to this
    // private copy, so two renders in flight cannot see each other's window.
    const plot::AxisWindow window = plot::computeAxisWindow(request.samples, effective);
    effective.plot.geometry.xMinimum = window.xMinimum;
    effective.plot.geometry.xMaximum = window.xMaximum;
    effective.plot.geometry.xLabelStepDays = window.labelStepDays;

    math::TrendRequest trendRequest(weight::settings::curveCount());
    for (std::size_t i = 0; i < weight::settings::curveCount(); ++i) {
        trendRequest.curveWanted[i] =
            i < request.options.showTrendCurve.size() && request.options.showTrendCurve[i];
        trendRequest.derivativeWanted[i] =
            i < request.options.showDerivative.size() && request.options.showDerivative[i];
    }

    // The individual curves are fitted concurrently inside here.
    const math::TrendResults trends =
        math::computeTrends(request.samples, trendRequest, effective.trend);

    for (std::size_t i = 0; i < trends.size(); ++i) {
        result.coefficients[i] = trends.coefficient(i);
    }

    plot::PlotInput input;
    input.samples = request.samples;
    input.trends = &trends;
    input.settings = &effective;
    input.options = &request.options;
    input.widthPixels = request.widthPixels;
    input.heightPixels = request.heightPixels;
    input.dotsPerInch = request.dotsPerInch;

    const plot::PlotScene scene = plot::PlotSceneBuilder::build(input);
    result.image = plot::PainterPlotRenderer::render(scene, effective.plot.typography.fontFamily,
                                                     request.dotsPerInch);
    if (result.image.isNull()) {
        result.failureReason =
            QStringLiteral("the image could not be allocated at %1x%2 pixels")
                .arg(request.widthPixels)
                .arg(request.heightPixels);
        return result;
    }
    result.succeeded = true;
    return result;
}

}  // namespace weight::app
