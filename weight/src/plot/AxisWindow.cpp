#include "plot/AxisWindow.hpp"

#include <QDateTime>

#include <algorithm>
#include <cmath>

#include "data/CalendarUtils.hpp"

namespace weight::plot {
namespace {

/// Smallest configured step that keeps the number of labels within bounds.
///
/// Walking a list of sensible multiples of a week, rather than computing a
/// "nice" number arithmetically, keeps every label on the same weekday, which
/// is what makes a body-mass chart easy to read: the reader can count weeks
/// along the axis.
double chooseLabelStep(double span, const settings::PlotGeometry& geometry) {
    const int maximumLabels = std::max(2, geometry.maximumXLabels);
    for (const double candidate : geometry.labelStepCandidatesDays) {
        if (candidate <= 0.0) {
            continue;
        }
        if (span / candidate <= static_cast<double>(maximumLabels)) {
            return candidate;
        }
    }
    // Longer than every candidate allows for: fall back to an even division so
    // the axis stays labelled rather than becoming blank.
    return std::max(1.0, span / static_cast<double>(maximumLabels));
}

}  // namespace

AxisWindow computeAxisWindow(std::span<const math::SamplePoint> samples,
                             const settings::Settings& settings, const QDate& today) {
    const settings::PlotGeometry& geometry = settings.plot.geometry;
    AxisWindow window;

    // Only finite days may take part. A NaN loses every comparison, so
    // std::minmax_element silently returns a meaningless element rather than
    // failing, and an infinity survives into the axis range where it makes the
    // pixel scale zero and every mapped coordinate NaN. That NaN then reaches
    // QPainter, which reports "arcTo: a parameter is NaN" and draws nothing.
    // Filtering here means a single corrupt row can never blank the chart.
    double firstFinite = 0.0;
    double lastFinite = 0.0;
    bool sawFinite = false;
    for (const math::SamplePoint& sample : samples) {
        if (!std::isfinite(sample.day) || !std::isfinite(sample.mass)) {
            continue;
        }
        if (!sawFinite) {
            firstFinite = sample.day;
            lastFinite = sample.day;
            sawFinite = true;
        } else {
            firstFinite = std::min(firstFinite, sample.day);
            lastFinite = std::max(lastFinite, sample.day);
        }
    }

    if (!sawFinite) {
        // Nothing usable: open on today rather than on the storage epoch.
        const double todayDay = data::CalendarUtils::fractionalDay(
            QDateTime(today, QTime(0, 0)), geometry.originDate, settings.csv);
        window.firstDay = todayDay;
        window.lastDay = todayDay;
        window.hasData_ = false;
    } else {
        // Days before the origin are perfectly legitimate — they simply move
        // the start of the chart to the left — so nothing is clamped here.
        window.firstDay = firstFinite;
        window.lastDay = lastFinite;
        window.hasData_ = true;
    }

    window.xMinimum = window.firstDay;
    window.xMaximum = std::max(window.firstDay + geometry.horizonFromFirstDays,
                               window.lastDay + geometry.horizonFromLastDays);

    // A degenerate range would divide by zero in the coordinate mapping.
    if (!(window.xMaximum > window.xMinimum)) {
        window.xMaximum = window.xMinimum + std::max(1.0, geometry.horizonFromFirstDays);
    }

    window.labelStepDays = chooseLabelStep(window.span(), geometry);
    return window;
}

AxisWindow computeAxisWindow(std::span<const math::SamplePoint> samples,
                             const settings::Settings& settings) {
    return computeAxisWindow(samples, settings, QDate::currentDate());
}

}  // namespace weight::plot
