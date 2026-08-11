#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "settings/Settings.hpp"

namespace weight::data {

/// Detection of how a particular file is actually encoded and delimited.
///
/// The canonical format is UTF-8 with semicolons, but files get moved between
/// machines, opened in spreadsheets and saved again. Reading such a file
/// correctly is worth more than insisting on the canonical form, so the dialect
/// is detected on read and the canonical form is restored on write.
class CsvDialect {
public:
    /// Decodes a file's bytes, trying UTF-8 first and falling back through
    /// UTF-16 and the single-byte encodings.
    ///
    /// Latin-1 never fails, so it acts as the safety net: reading one accented
    /// character wrongly is a far better outcome than discarding a person's
    /// entire measurement history because the file is not valid UTF-8.
    ///
    /// `usedEncoding` receives the name of whatever succeeded, so the caller
    /// can report a non-canonical encoding to the user.
    [[nodiscard]] static QString decode(const QByteArray& bytes, QString* usedEncoding);

    /// Guesses the delimiter by voting over the file's lines: the candidate
    /// that yields at least two non-empty leading columns on the most lines
    /// wins. Ties go to the earliest candidate, which is the canonical one.
    ///
    /// Only the first `sampleLines` lines are inspected; a delimiter that is
    /// consistent over two hundred lines is consistent over the whole file.
    [[nodiscard]] static QString detectSeparator(const QStringList& lines,
                                                 const settings::CsvFormat& format);

    /// Splits one line into cells, honouring double-quoted fields and doubled
    /// quotes inside them, as RFC 4180 describes.
    [[nodiscard]] static QStringList splitRecord(const QString& line, QChar separator);
};

}  // namespace weight::data
