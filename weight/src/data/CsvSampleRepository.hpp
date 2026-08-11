#pragma once

#include <vector>

#include <QString>
#include <QStringList>

#include "settings/Settings.hpp"
#include "core/Result.hpp"
#include "data/CsvRecord.hpp"

namespace weight::data {

/// What happened while reading a file, so the user can be told rather than
/// left guessing why a row disappeared.
struct LoadReport {
    int validRecords = 0;
    int invalidRecords = 0;
    int skippedHeaders = 0;
    int discardedDuplicates = 0;
    int blankLines = 0;
    QString detectedSeparator;
    QString detectedEncoding;
    bool separatorWasNonCanonical = false;
    bool encodingWasNonCanonical = false;
    bool legacyHeaderSeen = false;
    QStringList repairs;  ///< One entry per repaired cell, quoting both forms.
};

/// The measurement history together with the diagnostics of how it was read.
struct LoadedHistory {
    std::vector<CsvRecord> records;
    LoadReport report;

    /// Valid measurements, in the order they appear.
    [[nodiscard]] std::vector<math::SamplePoint> samples() const;
};

/// Abstract access to the stored history.
///
/// The interface exists so the application logic and the tests can work against
/// an in-memory implementation, without a filesystem and without the timing and
/// permission surprises that come with one. It also states plainly what the
/// rest of the program is allowed to do with the history: load it, save it,
/// make sure it exists. Nothing else.
class SampleRepository {
public:
    SampleRepository() = default;
    virtual ~SampleRepository() = default;

    SampleRepository(const SampleRepository&) = delete;
    SampleRepository& operator=(const SampleRepository&) = delete;
    SampleRepository(SampleRepository&&) = delete;
    SampleRepository& operator=(SampleRepository&&) = delete;

    [[nodiscard]] virtual core::Result<LoadedHistory> load() = 0;
    [[nodiscard]] virtual core::Status save(const std::vector<CsvRecord>& records) = 0;
    [[nodiscard]] virtual core::Status ensureExists() = 0;
    [[nodiscard]] virtual QString location() const = 0;
};

/// The semicolon-separated file implementation.
class CsvSampleRepository final : public SampleRepository {
public:
    CsvSampleRepository(QString path, const settings::Settings& configuration);

    [[nodiscard]] core::Result<LoadedHistory> load() override;
    [[nodiscard]] core::Status save(const std::vector<CsvRecord>& records) override;
    [[nodiscard]] core::Status ensureExists() override;
    [[nodiscard]] QString location() const override { return path_; }

    /// Orders records for writing: valid ones by ascending day, then every
    /// unreadable line, preserved verbatim, at the end where it is visible.
    /// Days are rounded to the configured precision on the way through.
    [[nodiscard]] static std::vector<CsvRecord> sortForWriting(std::vector<CsvRecord> records,
                                                               const settings::CsvFormat& format,
                                                               QStringList* repairs = nullptr);

    /// Renders records as the exact bytes that would be written. Separated from
    /// the write so it can be tested without touching a disk.
    [[nodiscard]] static QByteArray serialise(const std::vector<CsvRecord>& records,
                                              const settings::CsvFormat& format);

    /// Parses decoded text. Exposed so the parser can be exercised directly on
    /// hostile input.
    [[nodiscard]] static LoadedHistory parse(const QString& text,
                                             const settings::Settings& configuration);

private:
    QString path_;
    const settings::Settings* configuration_;
};

}  // namespace weight::data
