#include "data/NumericTextRepairChain.hpp"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

#include "data/TextSanitizer.hpp"

namespace weight::data {
namespace {

/// Unit suffixes people attach to a mass: "92,4 kg", "92.4Kg.", "88 kilos".
const QRegularExpression& massUnitSuffix() {
    static const QRegularExpression expression(
        QStringLiteral("\\s*(kg|kgs|kilos?|kilogramos?|kilograms?)\\.?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return expression;
}

/// Fully written scientific notation, which is unambiguous and therefore safe
/// to expand: "9.24e1" can only mean 92.4.
const QRegularExpression& scientificNotation() {
    static const QRegularExpression expression(
        QStringLiteral("^[+-]?\\d+(?:[.,]\\d+)?e[+-]?\\d+$"),
        QRegularExpression::CaseInsensitiveOption);
    return expression;
}

/// Thousands grouped with points and a comma decimal: "1.234,5".
/// Unambiguous because the two separators play different roles in one string.
const QRegularExpression& thousandsWithDots() {
    static const QRegularExpression expression(
        QStringLiteral("^([+-]?)(\\d{1,3}(?:\\.\\d{3})+)(,\\d+)?$"));
    return expression;
}

/// Thousands grouped with commas and a point decimal: "1,234.5".
const QRegularExpression& thousandsWithCommas() {
    static const QRegularExpression expression(
        QStringLiteral("^([+-]?)(\\d{1,3}(?:,\\d{3})+)(\\.\\d+)?$"));
    return expression;
}

/// Spaces used as a thousands separator: "1 234,5".
const QRegularExpression& thousandsWithSpaces() {
    static const QRegularExpression expression(
        QStringLiteral("^[+-]?\\d{1,3}(?: \\d{3})+(?:[.,]\\d+)?$"));
    return expression;
}

}  // namespace

const NumericTextRepairChain& NumericTextRepairChain::standard() {
    static const NumericTextRepairChain chain = [] {
        NumericTextRepairChain built;

        // 1. Typographic normalisation. Runs first so every later rule sees
        //    plain ASCII separators and a plain ASCII minus sign.
        built.addRule({QStringLiteral("unicode normalisation"),
                       [](const QString& text, bool) -> QString { return TextSanitizer::sanitize(text); }});

        // 2. Unit suffix attached to the value.
        built.addRule({QStringLiteral("unit suffix"), [](const QString& text, bool) -> QString {
                           QString result = text;
                           return result.remove(massUnitSuffix());
                       }});

        // 3. Redundant sign markers. A doubled minus written by hand cancels
        //    out; a leading plus on an unsigned column is simply noise.
        built.addRule({QStringLiteral("redundant sign"), [](const QString& text, bool allowSign) -> QString {
                           QString result = text;
                           if (allowSign) {
                               while (result.startsWith(QLatin1String("--"))) {
                                   result.remove(0, 2);
                               }
                           } else {
                               while (result.startsWith(QLatin1Char('+'))) {
                                   result.remove(0, 1);
                               }
                           }
                           return result;
                       }});

        // 4. Space-grouped thousands. Only applied when the whole string
        //    matches the grouping pattern, so "9 8" stays invalid.
        built.addRule({QStringLiteral("space-grouped thousands"),
                       [](const QString& text, bool) -> QString {
                           if (!thousandsWithSpaces().match(text).hasMatch()) {
                               return text;
                           }
                           QString result = text;
                           return result.remove(QLatin1Char(' '));
                       }});

        // 5. Thousands grouped with a separator that differs from the decimal
        //    mark. Both variants are unambiguous; a string using a single
        //    separator type is deliberately not touched.
        built.addRule({QStringLiteral("grouped thousands"), [](const QString& text, bool) -> QString {
                           auto match = thousandsWithDots().match(text);
                           if (match.hasMatch()) {
                               QString integral = match.captured(2);
                               integral.remove(QLatin1Char('.'));
                               return match.captured(1) + integral + match.captured(3);
                           }
                           match = thousandsWithCommas().match(text);
                           if (match.hasMatch()) {
                               QString integral = match.captured(2);
                               integral.remove(QLatin1Char(','));
                               return match.captured(1) + integral + match.captured(3);
                           }
                           return text;
                       }});

        // 6. Scientific notation expanded to a plain decimal, with enough
        //    digits to preserve the precision the file format keeps.
        built.addRule({QStringLiteral("scientific notation"), [](const QString& text, bool) -> QString {
                           if (!scientificNotation().match(text).hasMatch()) {
                               return text;
                           }
                           QString normalised = text;
                           normalised.replace(QLatin1Char(','), QLatin1Char('.'));
                           bool ok = false;
                           const double value = normalised.toDouble(&ok);
                           if (!ok || !std::isfinite(value)) {
                               return text;
                           }
                           QString expanded = QString::number(value, 'f', 5);
                           if (expanded.contains(QLatin1Char('.'))) {
                               while (expanded.endsWith(QLatin1Char('0'))) {
                                   expanded.chop(1);
                               }
                               if (expanded.endsWith(QLatin1Char('.'))) {
                                   expanded.chop(1);
                               }
                           }
                           return expanded;
                       }});

        // 7. Missing integral or fractional part: ".5" means 0.5 and "5."
        //    means 5. Neither reading is in doubt.
        built.addRule({QStringLiteral("bare decimal point"), [](const QString& text, bool) -> QString {
                           if (text.isEmpty()) {
                               return text;
                           }
                           QString sign;
                           QString body = text;
                           if (body.front() == QLatin1Char('+') || body.front() == QLatin1Char('-')) {
                               sign = body.left(1);
                               body.remove(0, 1);
                           }
                           for (const QChar separator : {QLatin1Char('.'), QLatin1Char(',')}) {
                               if (body.startsWith(separator)) {
                                   const QString rest = body.mid(1);
                                   if (!rest.isEmpty()
                                       && std::all_of(rest.begin(), rest.end(),
                                                      [](QChar c) { return c.isDigit(); })) {
                                       body = QLatin1String("0") + separator + rest;
                                   }
                               }
                               if (body.endsWith(separator)) {
                                   const QString rest = body.left(body.size() - 1);
                                   if (!rest.isEmpty()
                                       && std::all_of(rest.begin(), rest.end(),
                                                      [](QChar c) { return c.isDigit(); })) {
                                       body = rest;
                                   }
                               }
                           }
                           return sign + body;
                       }});

        return built;
    }();
    return chain;
}

QString NumericTextRepairChain::repair(const QString& text, bool allowSign) const {
    QString current = text;
    for (const Rule& rule : rules_) {
        current = rule.apply(current, allowSign);
        if (current.isEmpty()) {
            break;  // nothing left to repair
        }
    }
    return current;
}

}  // namespace weight::data
