#pragma once

#include <QDateTime>
#include <QImage>
#include <QString>

#include "core/Result.hpp"
#include "settings/Settings.hpp"

namespace weight::plot {

/// Names and writes chart files.
///
/// Rendering used to live here; it now belongs to app::RenderEngine, which
/// owns the worker thread. What is left is the part that genuinely concerns
/// files, which keeps this class usable from any thread and trivial to test.
class PlotImageWriter {
public:
    /// File name for an exported chart, for instance
    /// weight_2026-08-10_14.03.27_smoothness_0.50.png
    [[nodiscard]] static QString buildFileName(const settings::Settings& settings,
                                               double smoothness, const QDateTime& moment);

    /// Writes a PNG. The image is encoded in memory first and then committed
    /// atomically, so a failure part-way through cannot leave a truncated file
    /// where a chart is expected.
    [[nodiscard]] static core::Status writePng(const QImage& image, const QString& path);
};

}  // namespace weight::plot
