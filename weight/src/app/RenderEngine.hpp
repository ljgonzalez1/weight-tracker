#pragma once

#include <memory>

#include <QImage>
#include <QObject>

#include "math/common/TrendService.hpp"
#include "settings/Settings.hpp"

namespace weight::app {

/// One request to draw a chart.
struct RenderRequest {
    std::vector<math::SamplePoint> samples;
    settings::RenderOptions options;
    double smoothness = 0.5;
    int widthPixels = 0;
    int heightPixels = 0;
    int dotsPerInch = 0;

    /// Monotonically increasing; lets a late result from a superseded request
    /// be discarded rather than drawn over a newer one.
    quint64 sequence = 0;
};

/// A finished chart.
struct RenderResult {
    QImage image;
    std::vector<std::optional<double>> coefficients;
    quint64 sequence = 0;
    bool succeeded = false;
    QString failureReason;
};

/// Renders charts on a worker thread.
///
/// Three things had to be true at once, and they shape the design:
///
///   * the window must stay responsive while a chart is computed, so no fit
///     and no painting may happen on the UI thread;
///   * dragging the smoothness slider produces requests far faster than they
///     can be served, so requests must be *coalesced*: only the newest one
///     matters, and the intermediate ones are dropped rather than queued;
///   * a result that arrives after a newer request was made must not be shown,
///     which is what the sequence number is for.
///
/// Inside one render, the individual curves are fitted concurrently by
/// `math::computeTrends`; this class provides the outer thread that keeps all
/// of that off the UI.
///
/// QImage is used rather than QPixmap deliberately: QPixmap may only be
/// touched on the GUI thread, whereas QImage is a plain raster buffer and can
/// be painted anywhere. The conversion happens once, in the UI thread, when the
/// finished image arrives.
class RenderEngine : public QObject {
    Q_OBJECT

public:
    explicit RenderEngine(const settings::Settings& settings, QObject* parent = nullptr);
    ~RenderEngine() override;

    /// Queues a request, replacing any that has not started yet.
    void submit(RenderRequest request);

    /// Renders synchronously on the calling thread. Used for the final
    /// full-resolution export, where the user is already waiting and the
    /// result must not be superseded, and by the tests.
    [[nodiscard]] static RenderResult renderNow(const RenderRequest& request,
                                                const settings::Settings& settings);

    /// Blocks until the worker is idle. Called before shutdown.
    void shutdown();

signals:
    /// Emitted on the UI thread when a chart is ready.
    void finished(const weight::app::RenderResult& result);

private:
    void workerLoop();

    struct Private;
    std::unique_ptr<Private> d_;
};

}  // namespace weight::app

Q_DECLARE_METATYPE(weight::app::RenderResult)
