#pragma once

#include <span>

#include <QDate>

#include "math/common/TrendEstimator.hpp"
#include "settings/Settings.hpp"

namespace weight::plot {

/// The horizontal extent of the chart, derived from the data rather than fixed.
struct AxisWindow {
    double firstDay = 0.0;  ///< Oldest measurement, or today when there is none.
    double lastDay = 0.0;   ///< Newest measurement, or today when there is none.

    double xMinimum = 0.0;
    double xMaximum = 0.0;

    /// Days between labelled ticks, chosen so the axis stays readable however
    /// long the history is.
    double labelStepDays = 7.0;

    [[nodiscard]] double span() const noexcept { return xMaximum - xMinimum; }
    [[nodiscard]] bool hasData() const noexcept { return hasData_; }

    bool hasData_ = false;
};

/// Works out where the day axis starts and ends.
///
/// The rules, in the order they matter:
///
///   * **No valid measurements** — the chart starts today. There is nothing to
///     look back at, and an axis anchored to some fixed epoch would open on a
///     window the person has no data in.
///
///   * **At least one measurement** — the chart starts at the oldest one.
///     Everything recorded is visible; nothing is cropped off the left.
///
///   * **The right edge** is whichever is further away: one year from the first
///     measurement, or three months from the last. The first keeps a young
///     record from being drawn on a comically short axis; the second keeps a
///     long record from ending exactly at today's point, which leaves no room
///     to see where the trend is heading. Taking the maximum satisfies both
///     without a special case.
///
/// The tick spacing adapts to the resulting span. A weekly tick is right for a
/// year, but a three-year record would produce a hundred and fifty labels
/// overlapping into a grey band, so the step grows through the configured
/// candidates until the label count fits.
///
/// `today` is passed in rather than read from the clock so the behaviour can be
/// tested deterministically.
[[nodiscard]] AxisWindow computeAxisWindow(std::span<const math::SamplePoint> samples,
                                           const settings::Settings& settings,
                                           const QDate& today);

/// Convenience overload using the current date.
[[nodiscard]] AxisWindow computeAxisWindow(std::span<const math::SamplePoint> samples,
                                           const settings::Settings& settings);

}  // namespace weight::plot
