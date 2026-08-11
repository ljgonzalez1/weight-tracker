#pragma once

#include <QImage>

#include "plot/PlotScene.hpp"

namespace weight::plot {

/// Draws a PlotScene onto a raster image with QPainter.
///
/// This replaces the matplotlib process the previous implementation launched.
/// Everything is drawn with Qt's own raster engine, so the application has no
/// Python interpreter, no plotting library and no subprocess: rendering a chart
/// is a function call on the same thread, which is what removes the visible
/// pause the old version had when the smoothness slider moved.
///
/// The renderer is deliberately dumb. It receives pixels and colours and draws
/// them; it does not know what a trend is, and cannot change one.
class PainterPlotRenderer {
public:
    /// Renders into a new ARGB32 image.
    ///
    /// `fontFamily` is resolved by Qt's font matching; when it is unavailable
    /// the platform substitutes the closest match, which changes the metrics
    /// slightly but never fails.
    [[nodiscard]] static QImage render(const PlotScene& scene, const QString& fontFamily,
                                       int dotsPerInch);
};

}  // namespace weight::plot
