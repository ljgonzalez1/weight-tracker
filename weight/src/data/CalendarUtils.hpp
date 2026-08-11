#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>

#include "settings/Settings.hpp"

namespace weight::data {

/// Conversions between calendar time and the fractional day axis of the chart.
///
/// Day 0.0 is midnight of the configured origin date. 1.5 is noon of the next
/// day, 2.25 is 06:00 two days later, and negative values denote dates before
/// the origin, which are entirely legitimate.
class CalendarUtils {
public:
    /// Fractional day of `moment` relative to midnight of `origin`, rounded to
    /// the configured precision.
    ///
    /// The arithmetic is done on local civil time deliberately. A body-mass log
    /// is a record of when the person stood on the scale, so a reading taken at
    /// 07:00 must stay at 07:00 on the axis regardless of the time zone the
    /// file is later opened in, and must not shift by an hour when daylight
    /// saving changes.
    [[nodiscard]] static double fractionalDay(const QDateTime& moment, const QDate& origin,
                                              const settings::CsvFormat& format);

    /// Inverse of fractionalDay, for turning an axis position back into a date.
    [[nodiscard]] static QDateTime fromFractionalDay(double day, const QDate& origin,
                                                     const settings::CsvFormat& format);

    /// Number of days in a month, falling back to the configured maximum when
    /// the month or year is outside the accepted range.
    [[nodiscard]] static int daysInMonth(int month, int year, const settings::InputLimits& limits);

    /// Abbreviated axis label for a fractional day, for instance "Aug-08".
    [[nodiscard]] static QString axisLabel(double day, const QDate& origin,
                                           const settings::UiStrings& strings);

    /// Weekday name for a date, or an empty string when the date is invalid.
    [[nodiscard]] static QString weekdayName(const QDate& date, const settings::UiStrings& strings);
};

}  // namespace weight::data
