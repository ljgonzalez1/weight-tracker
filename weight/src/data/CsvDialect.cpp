#include "data/CsvDialect.hpp"

#include <QStringConverter>

namespace weight::data {
namespace {

/// Windows-1252 differs from Latin-1 only in the 0x80..0x9F range, where it
/// places the punctuation that word processors emit. Decoding those bytes
/// correctly is what rescues files that were saved from a Windows editor.
///
/// Qt 6 dropped QTextCodec from QtCore, and the alternative would be a
/// dependency on the Qt5Compat module for a twenty-seven entry table. The table
/// is reproduced here instead, which keeps the dependency list shorter without
/// giving anything up.
const char16_t kWindows1252High[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

QString decodeWindows1252(const QByteArray& bytes) {
    QString result;
    result.reserve(bytes.size());
    for (const char byte : bytes) {
        const auto value = static_cast<unsigned char>(byte);
        result.append(value >= 0x80 && value <= 0x9F
                          ? QChar(kWindows1252High[value - 0x80])
                          : QChar(static_cast<char16_t>(value)));
    }
    return result;
}

}  // namespace

QString CsvDialect::decode(const QByteArray& bytes, QString* usedEncoding) {
    const auto report = [usedEncoding](const char* name) {
        if (usedEncoding != nullptr) {
            *usedEncoding = QString::fromLatin1(name);
        }
    };

    // UTF-8, tolerating a byte order mark, is the canonical encoding.
    {
        QStringDecoder decoder(QStringDecoder::Utf8,
                               QStringDecoder::Flag::ConvertInvalidToNull);
        QString text = decoder.decode(bytes);
        if (!decoder.hasError() && !text.contains(QChar(0))) {
            report("UTF-8");
            if (text.startsWith(QChar(0xFEFF))) {
                text.remove(0, 1);
            }
            return text;
        }
    }

    // UTF-16 is only accepted when the file actually starts with a BOM;
    // without one the heuristic misfires on ordinary single-byte text.
    if (bytes.size() >= 2) {
        const auto first = static_cast<unsigned char>(bytes[0]);
        const auto second = static_cast<unsigned char>(bytes[1]);
        const bool littleEndian = first == 0xFF && second == 0xFE;
        const bool bigEndian = first == 0xFE && second == 0xFF;
        if (littleEndian || bigEndian) {
            QStringDecoder decoder(QStringDecoder::Utf16);
            QString text = decoder.decode(bytes);
            if (!decoder.hasError()) {
                report("UTF-16");
                if (text.startsWith(QChar(0xFEFF))) {
                    text.remove(0, 1);
                }
                return text;
            }
        }
    }

    // Windows-1252 before Latin-1: it is a strict superset in practice and
    // recovers the typographic characters Latin-1 would render as controls.
    report("Windows-1252");
    return decodeWindows1252(bytes);
}

QString CsvDialect::detectSeparator(const QStringList& lines, const settings::CsvFormat& format) {
    if (lines.isEmpty() || format.candidateSeparators.isEmpty()) {
        return format.separator;
    }

    const qsizetype sampleSize =
        std::min<qsizetype>(lines.size(), std::max(1, format.separatorVoteSampleLines));

    QString best = format.separator;
    int bestScore = -1;

    for (const QString& candidate : format.candidateSeparators) {
        if (candidate.isEmpty()) {
            continue;
        }
        int score = 0;
        for (qsizetype i = 0; i < sampleSize; ++i) {
            const QStringList cells = lines.at(i).split(candidate);
            if (cells.size() < 2) {
                continue;
            }
            // At least one of the two leading cells must carry content;
            // otherwise a line of separators would vote for every candidate.
            if (!cells.at(0).trimmed().isEmpty() || !cells.at(1).trimmed().isEmpty()) {
                ++score;
            }
        }
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    }

    return bestScore <= 0 ? format.separator : best;
}

QStringList CsvDialect::splitRecord(const QString& line, QChar separator) {
    QStringList cells;
    QString current;
    bool insideQuotes = false;

    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar character = line.at(i);
        if (insideQuotes) {
            if (character == QLatin1Char('"')) {
                // A doubled quote inside a quoted field is a literal quote.
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    current.append(QLatin1Char('"'));
                    ++i;
                } else {
                    insideQuotes = false;
                }
            } else {
                current.append(character);
            }
        } else if (character == QLatin1Char('"') && current.trimmed().isEmpty()) {
            current.clear();
            insideQuotes = true;
        } else if (character == separator) {
            cells.append(current);
            current.clear();
        } else {
            current.append(character);
        }
    }
    cells.append(current);
    return cells;
}

}  // namespace weight::data
