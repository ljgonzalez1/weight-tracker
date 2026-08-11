#pragma once

#include <QColor>

#include "settings/Settings.hpp"

namespace weight::plot {

/// Derives every colour a curve needs from the single r, g, b it declares.
///
/// A curve is drawn as three things: a wide, very faint outer band, a narrower
/// and slightly stronger inner band, and the line itself. Listing three
/// colours per curve in the settings would mean three chances to get a new
/// curve's palette subtly wrong, and would make "change the blue curve" a
/// three-line edit. Instead the catalogue states one colour and this helper
/// produces the rest, so adding a curve needs exactly one `Rgb{...}`.
///
/// Bands are the same hue at reduced opacity rather than a lightened tint,
/// because opacity composites correctly when two curves overlap: the region
/// where they cross reads as a blend of both, which is the information the
/// reader wants, whereas opaque tints would simply hide whichever was drawn
/// first.
class CurveStyle {
public:
    CurveStyle(QColor base, const settings::PlotLineStyle& lines);

    /// The line itself.
    [[nodiscard]] QColor lineColour() const;
    [[nodiscard]] double lineOpacity() const noexcept { return lineOpacity_; }

    /// Wide outer band; the half-width is in kilograms.
    [[nodiscard]] QColor outerBandColour() const;
    [[nodiscard]] double outerBandOpacity() const noexcept { return outerOpacity_; }
    [[nodiscard]] double outerBandHalfWidth() const noexcept { return outerHalfWidth_; }

    /// Narrower inner band.
    [[nodiscard]] QColor innerBandColour() const;
    [[nodiscard]] double innerBandOpacity() const noexcept { return innerOpacity_; }
    [[nodiscard]] double innerBandHalfWidth() const noexcept { return innerHalfWidth_; }

    /// Colour of the same curve drawn on the rate-of-change axis. Identical
    /// hue, so the reader connects the two without a second legend.
    [[nodiscard]] QColor derivativeColour() const;

private:
    QColor base_;
    double lineOpacity_;
    double outerOpacity_;
    double innerOpacity_;
    double outerHalfWidth_;
    double innerHalfWidth_;
};

}  // namespace weight::plot
