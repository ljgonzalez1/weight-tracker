#pragma once

#include <optional>

#include <QString>

namespace weight::data {

/// Strict decimal parsing, locale independent by design.
///
/// A recorded body mass must never be silently reinterpreted. The parser
/// therefore accepts one, and only one, decimal separator (either a point or a
/// comma), rejects thousands separators, rejects internal whitespace and
/// rejects anything ambiguous. Recovery of malformed text is the separate
/// responsibility of NumericTextRepairChain, which only ever applies
/// transformations that cannot change the value.
class NumberParser {
public:
    /// Parses a plain decimal. `allowSign` permits a leading + or -, which is
    /// needed for the day column since days before the plot origin are
    /// negative, and refused for the mass column where a sign is meaningless.
    ///
    /// Rejected on purpose: "1.222.333", "1,222,333", "1.222,333", "9 8",
    /// "1e3", "5." with allowSign off is accepted only after repair.
    [[nodiscard]] static std::optional<double> parseDecimal(const QString& text, bool allowSign);

    /// Day column: a signed fractional number of days.
    [[nodiscard]] static std::optional<double> parseDay(const QString& text);

    /// Mass column: unsigned, and additionally checked against the configured
    /// plausible range so a mistyped reading is caught at the point of entry.
    [[nodiscard]] static std::optional<double> parseMass(const QString& text, double minimum,
                                                         double maximum);

    /// Formats a value with at most `maximumDecimals` digits, trailing zeros
    /// and a trailing separator removed. Always uses the C locale so the file
    /// reads the same on every machine.
    [[nodiscard]] static QString format(double value, int maximumDecimals);
};

}  // namespace weight::data
