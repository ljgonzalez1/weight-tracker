#pragma once

#include <vector>

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace weight::plot {

/// A chart described purely as geometry, with every number already resolved to
/// device pixels and every colour already chosen.
///
/// This type is the seam between computing a chart and drawing one. The builder
/// turns measurements, fitted curves and configuration into a PlotScene; the
/// renderer turns a PlotScene into pixels and knows nothing about trends,
/// smoothness or kilograms. Two consequences matter in practice: the layout can
/// be tested by inspecting a scene, with no image and no display server
/// involved, and a second renderer (SVG, PDF, a live widget) could be added
/// without touching a line of the layout logic.
struct PlotScene {
    /// A filled band between two polylines sharing their abscissae, used for
    /// the tolerance corridors around each trend curve.
    struct Band {
        std::vector<QPointF> upper;
        std::vector<QPointF> lower;
        QColor color;
        double opacity = 1.0;
    };

    struct Polyline {
        std::vector<QPointF> points;
        QColor color;
        double width = 1.0;
        double opacity = 1.0;
        std::vector<double> dashPattern;  ///< Empty means a solid line.
    };

    struct Markers {
        std::vector<QPointF> points;
        QColor color;
        double diameter = 1.0;
        double opacity = 1.0;
    };

    enum class Anchor {
        CenterTop,      ///< X tick labels, rotated.
        RightMiddle,    ///< Left axis tick labels.
        LeftMiddle,     ///< Right axis tick labels.
        CenterBottom,   ///< Titles and the X axis caption.
        CenterRotated,       ///< Rotated caption growing rightwards from its position.
        CenterRotatedRight,  ///< Rotated caption growing leftwards, for the right margin.
    };

    struct Text {
        QString content;
        QPointF position;
        QColor color;
        double pointSize = 10.0;
        double rotationDegrees = 0.0;
        Anchor anchor = Anchor::CenterBottom;
        bool bold = false;
    };

    /// Full image extent in pixels.
    QRectF canvas;

    /// Drawing area bounded by the axes, in pixels.
    QRectF axes;

    QColor background;
    QColor axisLineColor;
    double axisLineWidth = 1.0;

    std::vector<Polyline> gridLines;
    std::vector<Band> bands;
    std::vector<Polyline> curves;
    std::vector<Markers> markers;
    std::vector<Polyline> axisDecorations;  ///< Tick marks and spines.
    std::vector<Text> labels;

    /// True when the secondary axis was laid out, which the renderer uses to
    /// decide whether to draw its spine.
    bool hasSecondaryAxis = false;
};

}  // namespace weight::plot
