#include "plot/PainterPlotRenderer.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace weight::plot {
namespace {

/// True when a point can be handed to QPainter.
///
/// Last line of defence. The scene builder already refuses non-finite input,
/// but this renderer is a public seam: a second producer of scenes — a test, a
/// future live widget — must not be able to make QPainter emit "arcTo: a
/// parameter is NaN" and silently drop the drawing. Checking here costs one
/// comparison per point and makes the renderer total: every scene it is given
/// produces an image.
[[nodiscard]] inline bool drawable(const QPointF& point) noexcept {
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

/// Copies the drawable points of a run, discarding the rest.
[[nodiscard]] QPolygonF drawableRun(const std::vector<QPointF>& points) {
    QPolygonF polygon;
    polygon.reserve(static_cast<qsizetype>(points.size()));
    for (const QPointF& point : points) {
        if (drawable(point)) {
            polygon.append(point);
        }
    }
    return polygon;
}

QPen makePen(const QColor& color, double width, double opacity,
             const std::vector<double>& dashPattern) {
    QColor pen = color;
    pen.setAlphaF(static_cast<float>(std::clamp(opacity, 0.0, 1.0)));

    QPen result(pen);
    result.setWidthF(std::max(width, 0.1));
    result.setCapStyle(Qt::FlatCap);
    result.setJoinStyle(Qt::RoundJoin);
    if (!dashPattern.empty()) {
        QList<qreal> pattern;
        pattern.reserve(static_cast<qsizetype>(dashPattern.size()));
        for (const double segment : dashPattern) {
            pattern.append(std::max(segment, 0.01));
        }
        // Qt expresses dash segments in multiples of the pen width, which is
        // the same convention the configuration uses.
        result.setDashPattern(pattern);
    }
    return result;
}

void drawPolyline(QPainter& painter, const PlotScene::Polyline& line) {
    if (line.points.size() < 2) {
        return;
    }
    const QPolygonF polygon = drawableRun(line.points);
    if (polygon.size() < 2) {
        return;
    }
    painter.setPen(makePen(line.color, line.width, line.opacity, line.dashPattern));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(polygon);
}

void drawBand(QPainter& painter, const PlotScene::Band& band) {
    if (band.upper.size() < 2 || band.lower.size() < 2) {
        return;
    }
    // A closed ring: forward along the upper edge, back along the lower one.
    QPolygonF polygon;
    polygon.reserve(static_cast<qsizetype>(band.upper.size() + band.lower.size()));
    for (const QPointF& point : band.upper) {
        if (drawable(point)) {
            polygon.append(point);
        }
    }
    for (auto it = band.lower.rbegin(); it != band.lower.rend(); ++it) {
        if (drawable(*it)) {
            polygon.append(*it);
        }
    }
    if (polygon.size() < 3) {
        return;  // not a fillable ring
    }

    QColor fill = band.color;
    fill.setAlphaF(static_cast<float>(std::clamp(band.opacity, 0.0, 1.0)));
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawPolygon(polygon);
}

void drawMarkers(QPainter& painter, const PlotScene::Markers& markers) {
    QColor fill = markers.color;
    fill.setAlphaF(static_cast<float>(std::clamp(markers.opacity, 0.0, 1.0)));
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);

    const double radius = std::max(markers.diameter, 0.5) / 2.0;
    for (const QPointF& point : markers.points) {
        // drawEllipse builds an arc; a NaN centre is what produced the
        // "QPainterPath::arcTo: a parameter is NaN" warnings.
        if (!drawable(point)) {
            continue;
        }
        painter.drawEllipse(point, radius, radius);
    }
}

/// Places a text run relative to its anchor, then draws it.
///
/// Qt draws text from a baseline, while the scene positions labels by the edge
/// that must touch the axis. Translating between the two here keeps every label
/// aligned the same way and keeps the builder free of font metrics.
void drawText(QPainter& painter, const PlotScene::Text& label, const QString& fontFamily,
              int dotsPerInch) {
    if (label.content.isEmpty() || !drawable(label.position)) {
        return;
    }

    QFont font(fontFamily);
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(
                                     label.pointSize * static_cast<double>(dotsPerInch) / 72.0))));
    font.setBold(label.bold);
    painter.setFont(font);
    painter.setPen(label.color);

    const QFontMetricsF metrics(font);
    const double textWidth = metrics.horizontalAdvance(label.content);
    const double ascent = metrics.ascent();
    const double descent = metrics.descent();

    painter.save();
    painter.translate(label.position);
    if (label.rotationDegrees != 0.0) {
        painter.rotate(label.rotationDegrees);
    }

    QPointF origin;
    switch (label.anchor) {
        case PlotScene::Anchor::CenterTop:
            // Rotated tick labels hang below the axis: after a 90 degree
            // rotation the text runs downwards, so its start sits at the axis.
            origin = (label.rotationDegrees != 0.0) ? QPointF(0.0, ascent / 2.0)
                                                    : QPointF(-textWidth / 2.0, ascent);
            break;
        case PlotScene::Anchor::RightMiddle:
            origin = QPointF(-textWidth, (ascent - descent) / 2.0);
            break;
        case PlotScene::Anchor::LeftMiddle:
            origin = QPointF(0.0, (ascent - descent) / 2.0);
            break;
        case PlotScene::Anchor::CenterBottom:
            origin = QPointF(-textWidth / 2.0, 0.0);
            break;
        case PlotScene::Anchor::CenterRotated:
            origin = QPointF(-textWidth / 2.0, ascent);
            break;
        case PlotScene::Anchor::CenterRotatedRight:
            // Under a -90 degree rotation the glyph box grows towards larger
            // screen x. Anchoring on the descent instead of the ascent makes it
            // grow the other way, so a caption at the right margin stays inside
            // the canvas instead of being clipped by it.
            origin = QPointF(-textWidth / 2.0, -descent);
            break;
    }
    painter.drawText(origin, label.content);
    painter.restore();
}

}  // namespace

QImage PainterPlotRenderer::render(const PlotScene& scene, const QString& fontFamily,
                                   int dotsPerInch) {
    const int width = std::max(1, static_cast<int>(std::lround(scene.canvas.width())));
    const int height = std::max(1, static_cast<int>(std::lround(scene.canvas.height())));

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return image;  // allocation refused; the caller reports it
    }
    image.fill(scene.background);

    // Record the resolution in the file so viewers and print pipelines size the
    // image correctly. QImage stores dots per metre.
    const int dotsPerMetre = static_cast<int>(std::lround(dotsPerInch / 0.0254));
    image.setDotsPerMeterX(dotsPerMetre);
    image.setDotsPerMeterY(dotsPerMetre);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // Everything inside the axes is clipped, so a curve that leaves the mass
    // range stops at the frame instead of bleeding across the margins.
    painter.save();
    painter.setClipRect(scene.axes);
    for (const PlotScene::Polyline& line : scene.gridLines) {
        drawPolyline(painter, line);
    }
    for (const PlotScene::Band& band : scene.bands) {
        drawBand(painter, band);
    }
    for (const PlotScene::Polyline& line : scene.curves) {
        drawPolyline(painter, line);
    }
    for (const PlotScene::Markers& markers : scene.markers) {
        drawMarkers(painter, markers);
    }
    painter.restore();

    // Frame around the drawing area.
    painter.setPen(makePen(scene.axisLineColor, scene.axisLineWidth, 1.0, {}));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(scene.axes);

    for (const PlotScene::Polyline& decoration : scene.axisDecorations) {
        drawPolyline(painter, decoration);
    }
    for (const PlotScene::Text& label : scene.labels) {
        drawText(painter, label, fontFamily, dotsPerInch);
    }

    painter.end();
    return image;
}

}  // namespace weight::plot
