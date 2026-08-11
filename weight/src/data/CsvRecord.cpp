#include "data/CsvRecord.hpp"

namespace weight::data {

CsvRecord CsvRecord::makeValid(double day, double mass, QString extra, int lineNumber) {
    CsvRecord record;
    record.day_ = day;
    record.mass_ = mass;
    record.extra_ = std::move(extra);
    record.valid_ = true;
    record.lineNumber_ = lineNumber;
    return record;
}

CsvRecord CsvRecord::makeInvalid(QString rawText, int lineNumber) {
    CsvRecord record;
    record.rawText_ = std::move(rawText);
    record.valid_ = false;
    record.lineNumber_ = lineNumber;
    return record;
}

bool CsvRecord::sameMeasurementAs(const CsvRecord& other) const noexcept {
    return valid_ && other.valid_ && day_ == other.day_ && mass_ == other.mass_
           && extra_ == other.extra_;
}

}  // namespace weight::data
