#include "plot/CurveStyle.hpp"

namespace weight::plot {

CurveStyle::CurveStyle(QColor base, const settings::PlotLineStyle& lines)
    : base_(std::move(base)),
      lineOpacity_(lines.trendCurveOpacity),
      outerOpacity_(lines.trendOuterBandOpacity),
      innerOpacity_(lines.trendInnerBandOpacity),
      outerHalfWidth_(lines.trendOuterBandHalfWidth),
      innerHalfWidth_(lines.trendInnerBandHalfWidth) {}

QColor CurveStyle::lineColour() const { return base_; }
QColor CurveStyle::outerBandColour() const { return base_; }
QColor CurveStyle::innerBandColour() const { return base_; }
QColor CurveStyle::derivativeColour() const { return base_; }

}  // namespace weight::plot
