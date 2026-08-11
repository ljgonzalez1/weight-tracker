#include "data/NumberParser.hpp"

#include <cmath>

namespace weight::data {
namespace {

bool isAsciiDigits(QStringView text) {
    if (text.isEmpty()) {
        return false;
    }
    for (const QChar character : text) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9')) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<double> NumberParser::parseDecimal(const QString& text, bool allowSign) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    // Whitespace inside a number is ambiguous ("9 8" could be 98 or two
    // values), so it invalidates the cell rather than being guessed at.
    for (const QChar character : trimmed) {
        if (character.isSpace()) {
            return std::nullopt;
        }
    }

    QStringView body(trimmed);
    QString sign;
    if (allowSign && (body.front() == QLatin1Char('+') || body.front() == QLatin1Char('-'))) {
        sign = body.front() == QLatin1Char('-') ? QStringLiteral("-") : QString();
        body = body.mid(1);
        if (body.isEmpty()) {
            return std::nullopt;
        }
    }

    const qsizetype dots = body.count(QLatin1Char('.'));
    const qsizetype commas = body.count(QLatin1Char(','));
    const qsizetype separators = dots + commas;

    if (separators > 1) {
        return std::nullopt;  // thousands separators or a mixed format
    }

    QString canonical;
    if (separators == 0) {
        if (!isAsciiDigits(body)) {
            return std::nullopt;
        }
        canonical = sign + body.toString();
    } else {
        const QChar separator = (dots == 1) ? QLatin1Char('.') : QLatin1Char(',');
        const qsizetype position = body.indexOf(separator);
        const QStringView left = body.left(position);
        const QStringView right = body.mid(position + 1);
        if (!isAsciiDigits(left) || !isAsciiDigits(right)) {
            return std::nullopt;
        }
        canonical = sign + left.toString() + QLatin1Char('.') + right.toString();
    }

    bool ok = false;
    const double value = canonical.toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> NumberParser::parseDay(const QString& text) {
    return parseDecimal(text, /*allowSign=*/true);
}

std::optional<double> NumberParser::parseMass(const QString& text, double minimum,
                                              double maximum) {
    const std::optional<double> value = parseDecimal(text, /*allowSign=*/false);
    if (!value.has_value()) {
        return std::nullopt;
    }
    if (*value < minimum || *value > maximum) {
        return std::nullopt;
    }
    return value;
}

QString NumberParser::format(double value, int maximumDecimals) {
    QString text = QString::number(value, 'f', maximumDecimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    return text;
}

}  // namespace weight::data
