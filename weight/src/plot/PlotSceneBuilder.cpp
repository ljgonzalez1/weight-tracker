#include "plot/PlotSceneBuilder.hpp"

#include <algorithm>
#include <cmath>

#include "data/CalendarUtils.hpp"
#include "plot/CurveStyle.hpp"
#include "settings/Strings.hpp"

namespace weight::plot {
namespace {

/// Maps data coordinates onto device pixels for one pair of axes.
///
/// The Y axis is inverted because image rows grow downwards while mass grows
/// upwards. Keeping that single inversion in one place is what stops it from
/// being applied twice, or forgotten, somewhere further down.
class AxisMapping {
public:
    AxisMapping(const QRectF& area, double xMinimum, double xMaximum, double yMinimum,
                double yMaximum)
        : area_(area),
          xMinimum_(xMinimum),
          yMinimum_(yMinimum),
          xScale_((xMaximum > xMinimum) ? area.width() / (xMaximum - xMinimum) : 0.0),
          yScale_((yMaximum > yMinimum) ? area.height() / (yMaximum - yMinimum) : 0.0) {}

    [[nodiscard]] double toPixelX(double value) const {
        return area_.left() + (value - xMinimum_) * xScale_;
    }
    [[nodiscard]] double toPixelY(double value) const {
        return area_.bottom() - (value - yMinimum_) * yScale_;
    }
    [[nodiscard]] QPointF toPixel(double x, double y) const {
        return {toPixelX(x), toPixelY(y)};
    }

private:
    QRectF area_;
    double xMinimum_;
    double yMinimum_;
    double xScale_;
    double yScale_;
};

/// Clips a curve to the horizontal axis range and converts it to pixels.
///
/// Only the abscissa is clipped. A trend that leaves the mass range vertically
/// should visibly run off the top or bottom edge, which is information; the
/// renderer clips it to the drawing area when painting.
std::vector<QPointF> projectCurve(std::span<const double> xs, std::span<const double> ys,
                                  const AxisMapping& mapping, double xMinimum, double xMaximum) {
    std::vector<QPointF> points;
    points.reserve(xs.size());
    for (std::size_t i = 0; i < xs.size() && i < ys.size(); ++i) {
        if (!std::isfinite(xs[i]) || !std::isfinite(ys[i])) {
            continue;
        }
        if (xs[i] < xMinimum || xs[i] > xMaximum) {
            continue;
        }
        points.push_back(mapping.toPixel(xs[i], ys[i]));
    }
    return points;
}

/// Formats a tick value without trailing zeros, so 2.0 prints as "2" and 0.5
/// as "0.5", matching how the axis steps are written in the configuration.
QString formatTick(double value) {
    QString text = QString::number(value, 'f', 4);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    if (text == QLatin1String("-0")) {
        text = QStringLiteral("0");
    }
    return text;
}

}  // namespace

std::vector<double> PlotSceneBuilder::tickPositions(double minimum, double maximum, double step) {
    std::vector<double> ticks;
    if (!(step > 0.0) || !(maximum > minimum) || !std::isfinite(step)) {
        return ticks;
    }

    // Generated from multiples of the step rather than by repeated addition;
    // over a 300-day axis accumulation would visibly displace the last ticks.
    const double firstIndex = std::ceil(minimum / step);
    const double lastIndex = std::floor(maximum / step);
    if (!std::isfinite(firstIndex) || !std::isfinite(lastIndex) || lastIndex < firstIndex) {
        return ticks;
    }
    const auto count = static_cast<std::size_t>(lastIndex - firstIndex) + 1;
    if (count > 100000) {
        return ticks;  // a nonsensical step; refuse rather than allocate wildly
    }

    ticks.reserve(count);
    for (double index = firstIndex; index <= lastIndex; index += 1.0) {
        ticks.push_back(index * step);
    }
    return ticks;
}

PlotScene PlotSceneBuilder::build(const PlotInput& input) {
    PlotScene scene;
    if (input.settings == nullptr || input.options == nullptr) {
        return scene;
    }

    // The axis range has to be finite before anything is mapped through it: a
    // zero or infinite span turns the pixel scale into 0 or inf, and every
    // coordinate that passes through becomes NaN. Refusing here produces an
    // empty chart with an honest error rather than an image full of gaps.
    {
        const settings::PlotGeometry& range = input.settings->plot.geometry;
        const bool finite = std::isfinite(range.xMinimum) && std::isfinite(range.xMaximum)
                            && std::isfinite(range.yMinimum) && std::isfinite(range.yMaximum);
        if (!finite || !(range.xMaximum > range.xMinimum)
            || !(range.yMaximum > range.yMinimum)) {
            return scene;
        }
    }

    const settings::Settings& config = *input.settings;
    const settings::PlotStyle& style = config.plot;
    const settings::PlotGeometry& geometry = style.geometry;
    const settings::PlotLineStyle& lines = style.lines;
    const settings::RenderOptions& options = *input.options;
    const int dpi = std::max(1, input.dotsPerInch);

    const auto width = static_cast<double>(std::max(1, input.widthPixels));
    const auto height = static_cast<double>(std::max(1, input.heightPixels));
    scene.canvas = QRectF(0.0, 0.0, width, height);
    scene.background = style.palette.background;
    scene.axisLineColor = style.palette.axisLine;
    scene.axisLineWidth = pointsToPixels(lines.axisLineWidth, dpi);

    // Is a secondary axis needed? Only if some derivative was both requested
    // and successfully produced.
    bool hasDerivative = false;
    if (input.trends != nullptr) {
        for (std::size_t index = 0; index < input.trends->size(); ++index) {
            if (index < options.showDerivative.size() && options.showDerivative[index]
                && input.trends->derivative(index).has_value()) {
                hasDerivative = true;
                break;
            }
        }
    }
    scene.hasSecondaryAxis = hasDerivative;

    // Axes rectangle from the figure-fraction margins.
    const double rightMargin = hasDerivative
                                   ? std::min(geometry.marginRight,
                                              geometry.marginRightWithSecondaryAxis)
                                   : geometry.marginRight;
    const double left = geometry.marginLeft * width;
    const double right = rightMargin * width;
    const double top = (1.0 - geometry.marginTop) * height;
    const double bottom = (1.0 - geometry.marginBottom) * height;
    scene.axes = QRectF(QPointF(left, top), QPointF(right, bottom));

    const AxisMapping primary(scene.axes, geometry.xMinimum, geometry.xMaximum,
                              geometry.yMinimum, geometry.yMaximum);

    const double tickFontSize = style.typography.tickLabelPointSize;
    const double labelFontSize = style.typography.axisLabelPointSize;
    const double tickLength = pointsToPixels(lines.tickLength, dpi);
    const double tickPad = pointsToPixels(lines.tickLabelPadding, dpi);
    const double labelPad = pointsToPixels(lines.axisLabelPadding, dpi);

    // ---------------------------------------------------------------------
    // Grid: vertical lines at the date ticks, horizontal at the fine mass step
    // ---------------------------------------------------------------------
    const std::vector<double> xTicks =
        tickPositions(geometry.xMinimum, geometry.xMaximum, geometry.xLabelStepDays);
    const std::vector<double> yMajorTicks =
        tickPositions(geometry.yMinimum, geometry.yMaximum, geometry.yLabelStep);
    const std::vector<double> yMinorTicks =
        tickPositions(geometry.yMinimum, geometry.yMaximum, geometry.yGridStep);

    for (const double tick : xTicks) {
        PlotScene::Polyline line;
        line.points = {primary.toPixel(tick, geometry.yMinimum),
                       primary.toPixel(tick, geometry.yMaximum)};
        line.color = style.palette.majorGrid;
        line.width = pointsToPixels(lines.majorGridWidth, dpi);
        scene.gridLines.push_back(std::move(line));
    }
    for (const double tick : yMinorTicks) {
        PlotScene::Polyline line;
        line.points = {primary.toPixel(geometry.xMinimum, tick),
                       primary.toPixel(geometry.xMaximum, tick)};
        line.color = style.palette.minorGrid;
        line.width = pointsToPixels(lines.minorGridWidth, dpi);
        scene.gridLines.push_back(std::move(line));
    }

    // ---------------------------------------------------------------------
    // Sample connector, drawn under the trend curves
    // ---------------------------------------------------------------------
    std::vector<QPointF> visibleSamples;
    visibleSamples.reserve(input.samples.size());
    for (const math::SamplePoint& sample : input.samples) {
        // A non-finite coordinate multiplied by the axis scale becomes NaN and
        // travels all the way into QPainter. Every comparison below is false
        // for NaN, so it would slip through the range test unnoticed; the
        // finiteness test has to be explicit and has to come first.
        if (!std::isfinite(sample.day) || !std::isfinite(sample.mass)) {
            continue;
        }
        if (sample.day < geometry.xMinimum || sample.day > geometry.xMaximum) {
            continue;
        }
        if (sample.mass < geometry.yMinimum || sample.mass > geometry.yMaximum) {
            continue;
        }
        visibleSamples.push_back(primary.toPixel(sample.day, sample.mass));
    }

    if (options.showSampleConnector && visibleSamples.size() >= 2) {
        PlotScene::Polyline connector;
        connector.points = visibleSamples;
        connector.color = style.palette.samplePoints;
        connector.width = pointsToPixels(lines.sampleConnectorWidth, dpi);
        connector.opacity = lines.sampleConnectorOpacity;
        scene.curves.push_back(std::move(connector));
    }

    // ---------------------------------------------------------------------
    // Trend curves: outer band, inner band, then the line itself
    // ---------------------------------------------------------------------
    if (input.trends != nullptr) {
        for (std::size_t index = 0; index < input.trends->size(); ++index) {
            if (index >= options.showTrendCurve.size() || !options.showTrendCurve[index]) {
                continue;
            }
            const auto& fitted = input.trends->curve(index);
            if (!fitted.has_value()) {
                continue;
            }
            // One declared colour per curve; the bands are derived from it.
            const CurveStyle curveStyle(config.colourFor(index), lines);
            const QColor color = curveStyle.lineColour();

            const auto addBand = [&](double halfWidth, double opacity) {
                std::vector<double> upperValues(fitted->y.size());
                std::vector<double> lowerValues(fitted->y.size());
                for (std::size_t i = 0; i < fitted->y.size(); ++i) {
                    upperValues[i] = fitted->y[i] + halfWidth;
                    lowerValues[i] = fitted->y[i] - halfWidth;
                }
                PlotScene::Band band;
                band.upper = projectCurve(fitted->x, upperValues, primary, geometry.xMinimum,
                                          geometry.xMaximum);
                band.lower = projectCurve(fitted->x, lowerValues, primary, geometry.xMinimum,
                                          geometry.xMaximum);
                band.color = color;
                band.opacity = opacity;
                if (band.upper.size() >= 2) {
                    scene.bands.push_back(std::move(band));
                }
            };

            addBand(curveStyle.outerBandHalfWidth(), curveStyle.outerBandOpacity());
            addBand(curveStyle.innerBandHalfWidth(), curveStyle.innerBandOpacity());

            PlotScene::Polyline line;
            line.points =
                projectCurve(fitted->x, fitted->y, primary, geometry.xMinimum, geometry.xMaximum);
            line.color = color;
            line.width = pointsToPixels(lines.trendCurveWidth, dpi);
            line.opacity = curveStyle.lineOpacity();
            if (line.points.size() >= 2) {
                scene.curves.push_back(std::move(line));
            }
        }
    }

    // ---------------------------------------------------------------------
    // Secondary axis with the rates of change
    // ---------------------------------------------------------------------
    if (hasDerivative) {
        const settings::DerivativeAxisStyle& axis = style.derivativeAxis;
        const AxisMapping secondary(scene.axes, geometry.xMinimum, geometry.xMaximum,
                                    axis.yMinimum, axis.yMaximum);

        PlotScene::Polyline zeroLine;
        zeroLine.points = {secondary.toPixel(geometry.xMinimum, 0.0),
                           secondary.toPixel(geometry.xMaximum, 0.0)};
        zeroLine.color = style.palette.derivativeZeroLine;
        zeroLine.width = pointsToPixels(axis.zeroLineWidth, dpi);
        zeroLine.opacity = axis.zeroLineOpacity;
        zeroLine.dashPattern.assign(axis.zeroLineDashPattern.begin(),
                                    axis.zeroLineDashPattern.end());
        scene.curves.push_back(std::move(zeroLine));

        for (std::size_t index = 0; index < input.trends->size(); ++index) {
            if (index >= options.showDerivative.size() || !options.showDerivative[index]) {
                continue;
            }
            const auto& rate = input.trends->derivative(index);
            if (!rate.has_value()) {
                continue;
            }
            PlotScene::Polyline line;
            line.points = projectCurve(rate->x, rate->y, secondary, geometry.xMinimum,
                                       geometry.xMaximum);
            line.color = CurveStyle(config.colourFor(index), lines).derivativeColour();
            line.width = pointsToPixels(lines.trendCurveWidth * axis.lineWidthRatio, dpi);
            line.opacity = axis.lineOpacity;
            line.dashPattern.assign(axis.lineDashPattern.begin(), axis.lineDashPattern.end());
            if (line.points.size() >= 2) {
                scene.curves.push_back(std::move(line));
            }
        }

        // Right-hand ticks and labels.
        for (const double tick : tickPositions(axis.yMinimum, axis.yMaximum, axis.yTickStep)) {
            const double y = secondary.toPixelY(tick);
            PlotScene::Polyline mark;
            mark.points = {QPointF(scene.axes.right(), y),
                           QPointF(scene.axes.right() + tickLength, y)};
            mark.color = style.palette.axisLine;
            mark.width = pointsToPixels(lines.tickWidth, dpi);
            scene.axisDecorations.push_back(std::move(mark));

            PlotScene::Text label;
            label.content = formatTick(tick);
            label.position = QPointF(scene.axes.right() + tickLength + tickPad, y);
            label.color = style.palette.axisText;
            label.pointSize = tickFontSize;
            label.anchor = PlotScene::Anchor::LeftMiddle;
            scene.labels.push_back(std::move(label));
        }

        PlotScene::Text caption;
        caption.content = settings::Strings::get(QStringLiteral("chart.axis.derivative"));
        caption.position = QPointF(scene.canvas.right() - labelPad, scene.axes.center().y());
        caption.color = style.palette.axisText;
        caption.pointSize = labelFontSize;
        caption.rotationDegrees = -90.0;
        caption.anchor = PlotScene::Anchor::CenterRotatedRight;
        scene.labels.push_back(std::move(caption));
    }

    // ---------------------------------------------------------------------
    // Sample markers, drawn last so they stay legible over every curve
    // ---------------------------------------------------------------------
    if (options.showSamples && !visibleSamples.empty()) {
        PlotScene::Markers markers;
        markers.points = std::move(visibleSamples);
        markers.color = style.palette.samplePoints;
        // Marker area is given in points squared, matching scatter-plot
        // convention, so the drawn diameter is its square root.
        markers.diameter = pointsToPixels(std::sqrt(lines.sampleMarkerArea), dpi);
        markers.opacity = lines.sampleMarkerOpacity;
        scene.markers.push_back(std::move(markers));
    }

    // ---------------------------------------------------------------------
    // Axis decorations and text
    // ---------------------------------------------------------------------
    for (const double tick : xTicks) {
        const double x = primary.toPixelX(tick);
        PlotScene::Polyline mark;
        mark.points = {QPointF(x, scene.axes.bottom()),
                       QPointF(x, scene.axes.bottom() + tickLength)};
        mark.color = style.palette.axisLine;
        mark.width = pointsToPixels(lines.tickWidth, dpi);
        scene.axisDecorations.push_back(std::move(mark));

        PlotScene::Text label;
        label.content = data::CalendarUtils::axisLabel(tick, geometry.originDate, config.strings);
        label.position = QPointF(x, scene.axes.bottom() + tickLength + tickPad);
        label.color = style.palette.axisText;
        label.pointSize = tickFontSize;
        label.rotationDegrees = style.typography.xTickRotationDegrees;
        label.anchor = PlotScene::Anchor::CenterTop;
        scene.labels.push_back(std::move(label));
    }

    for (const double tick : yMajorTicks) {
        const double y = primary.toPixelY(tick);
        PlotScene::Polyline mark;
        mark.points = {QPointF(scene.axes.left() - tickLength, y), QPointF(scene.axes.left(), y)};
        mark.color = style.palette.axisLine;
        mark.width = pointsToPixels(lines.tickWidth, dpi);
        scene.axisDecorations.push_back(std::move(mark));

        PlotScene::Text label;
        label.content = formatTick(tick);
        label.position = QPointF(scene.axes.left() - tickLength - tickPad, y);
        label.color = style.palette.axisText;
        label.pointSize = tickFontSize;
        label.anchor = PlotScene::Anchor::RightMiddle;
        scene.labels.push_back(std::move(label));
    }

    PlotScene::Text title;
    title.content = settings::Strings::get(QStringLiteral("chart.title"));
    title.position = QPointF(scene.axes.center().x(),
                             scene.axes.top() - pointsToPixels(lines.titlePadding, dpi));
    title.color = style.palette.titleText;
    title.pointSize = style.typography.titlePointSize;
    title.anchor = PlotScene::Anchor::CenterBottom;
    scene.labels.push_back(std::move(title));

    PlotScene::Text xCaption;
    xCaption.content = settings::Strings::get(QStringLiteral("chart.axis.x"));
    xCaption.position = QPointF(scene.axes.center().x(), scene.canvas.bottom() - labelPad);
    xCaption.color = style.palette.axisText;
    xCaption.pointSize = labelFontSize;
    xCaption.anchor = PlotScene::Anchor::CenterBottom;
    scene.labels.push_back(std::move(xCaption));

    PlotScene::Text yCaption;
    yCaption.content = settings::Strings::get(QStringLiteral("chart.axis.y"));
    yCaption.position = QPointF(scene.canvas.left() + labelPad, scene.axes.center().y());
    yCaption.color = style.palette.axisText;
    yCaption.pointSize = labelFontSize;
    yCaption.rotationDegrees = -90.0;
    yCaption.anchor = PlotScene::Anchor::CenterRotated;
    scene.labels.push_back(std::move(yCaption));

    return scene;
}

}  // namespace weight::plot
