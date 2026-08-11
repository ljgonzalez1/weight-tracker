#pragma once

#include <optional>

#include <QString>

#include "math/common/TrendEstimator.hpp"

namespace weight::data {

/// One line of the history file, valid or not.
///
/// Invalid lines are kept rather than dropped. A line the parser cannot read is
/// still something the person wrote, so it is preserved verbatim and written
/// back at the end of the file, where it is visible and can be corrected by
/// hand. Deleting it would be the one unrecoverable outcome.
class CsvRecord {
public:
    /// Builds a valid record from parsed values.
    static CsvRecord makeValid(double day, double mass, QString extra, int lineNumber = 0);

    /// Builds an unreadable record, keeping the original text intact.
    static CsvRecord makeInvalid(QString rawText, int lineNumber);

    [[nodiscard]] bool isValid() const noexcept { return valid_; }
    [[nodiscard]] double day() const noexcept { return day_; }
    [[nodiscard]] double mass() const noexcept { return mass_; }
    [[nodiscard]] const QString& extra() const noexcept { return extra_; }
    [[nodiscard]] const QString& rawText() const noexcept { return rawText_; }
    [[nodiscard]] int lineNumber() const noexcept { return lineNumber_; }

    void setDay(double day) noexcept { day_ = day; }

    /// The measurement as the estimators consume it.
    [[nodiscard]] math::SamplePoint toSample() const noexcept { return {day_, mass_}; }

    /// Identity used to detect exact duplicates. Compared on the parsed values
    /// rather than the text, so "0;92,4" and "0;92.40" are recognised as the
    /// same measurement.
    [[nodiscard]] bool sameMeasurementAs(const CsvRecord& other) const noexcept;

private:
    CsvRecord() = default;

    double day_ = 0.0;
    double mass_ = 0.0;
    QString extra_;
    QString rawText_;
    bool valid_ = false;
    int lineNumber_ = 0;
};

}  // namespace weight::data
