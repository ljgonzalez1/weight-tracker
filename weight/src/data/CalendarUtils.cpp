#include "data/CalendarUtils.hpp"

#include <cmath>

#include "settings/Strings.hpp"

namespace weight::data {

double CalendarUtils::fractionalDay(const QDateTime& moment, const QDate& origin,
                                    const settings::CsvFormat& format) {
    if (!moment.isValid() || !origin.isValid()) {
        return 0.0;
    }
    // Both sides are pinned to local civil time: the axis must show the hour
    // the person actually stood on the scale, not an instant re-expressed in
    // whatever zone the file is later opened in.
    const QDateTime localMoment = moment.toLocalTime();
    const QDateTime start(origin, QTime(0, 0));
    const double seconds = static_cast<double>(start.secsTo(localMoment));
    const double days = seconds / format.secondsPerDay;

    const double scale = std::pow(10.0, format.dayFractionDecimals);
    return std::round(days * scale) / scale;
}

QDateTime CalendarUtils::fromFractionalDay(double day, const QDate& origin,
                                           const settings::CsvFormat& format) {
    if (!origin.isValid() || !std::isfinite(day)) {
        return {};
    }
    const QDateTime start(origin, QTime(0, 0));
    return start.addSecs(static_cast<qint64>(std::llround(day * format.secondsPerDay)));
}

int CalendarUtils::daysInMonth(int month, int year, const settings::InputLimits& limits) {
    if (month >= 1 && month <= 12 && year >= limits.minimumYear && year <= limits.maximumYear) {
        return QDate(year, month, 1).daysInMonth();
    }
    return limits.maximumDayOfMonth;
}

QString CalendarUtils::axisLabel(double day, const QDate& origin,
                                 const settings::UiStrings& /*strings*/) {
    if (!origin.isValid() || !std::isfinite(day)) {
        return {};
    }
    const QDate date = origin.addDays(static_cast<qint64>(std::llround(day)));
    if (!date.isValid()) {
        return {};
    }
    return QStringLiteral("%1-%2")
        .arg(settings::Strings::get(QStringLiteral("month.abbr.%1").arg(date.month())))
        .arg(date.day(), 2, 10, QLatin1Char('0'));
}

QString CalendarUtils::weekdayName(const QDate& date, const settings::UiStrings& /*strings*/) {
    if (!date.isValid()) {
        return {};
    }
    // QDate::dayOfWeek() returns 1 for Monday, matching the key numbering.
    return settings::Strings::get(QStringLiteral("weekday.%1").arg(date.dayOfWeek()));
}

}  // namespace weight::data
