#include "data/CsvSampleRepository.hpp"

#include "settings/Strings.hpp"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

#include "core/Logging.hpp"
#include "data/AtomicFileWriter.hpp"
#include "data/CsvDialect.hpp"
#include "data/NumberParser.hpp"
#include "data/NumericTextRepairChain.hpp"
#include "data/TextSanitizer.hpp"

namespace weight::data {
namespace {

using core::ErrorCode;
using core::makeError;

/// Parses one cell in two passes: strictly first, then after repair.
///
/// Trying the strict form first guarantees that a cell the previous
/// implementation accepted keeps its exact interpretation; the repair pass only
/// ever rescues cells that would otherwise have been thrown away. `repaired`
/// receives the rewritten text when the second pass was the one that worked.
std::optional<double> parseCell(const QString& cell, bool allowSign, double minimum,
                                double maximum, QString* repaired) {
    const auto parse = [&](const QString& text) {
        return allowSign ? NumberParser::parseDay(text)
                         : NumberParser::parseMass(text, minimum, maximum);
    };

    if (const std::optional<double> direct = parse(cell); direct.has_value()) {
        return direct;
    }

    const QString candidate = NumericTextRepairChain::standard().repair(cell, allowSign);
    if (candidate == cell) {
        return std::nullopt;  // nothing changed, so nothing new to try
    }
    const std::optional<double> recovered = parse(candidate);
    if (recovered.has_value() && repaired != nullptr) {
        *repaired = candidate;
    }
    return recovered;
}

/// Is this the first line, and is it a header rather than data?
///
/// Permissive on purpose and only ever applied to line one: if the line does
/// not parse as a measurement, it is treated as the header.
bool looksLikeLeadingHeader(const QStringList& cells, const settings::Settings& configuration) {
    if (cells.size() < 2) {
        return true;
    }
    QString ignored;
    const auto day = parseCell(cells.at(0), true, 0.0, 0.0, &ignored);
    const auto mass = parseCell(cells.at(1), false, configuration.limits.minimumMass,
                                configuration.limits.maximumMass, &ignored);
    return !day.has_value() || !mass.has_value();
}

/// Is this a header repeated in the middle of the file?
///
/// Strict on purpose: both leading cells must be recognised header words. A
/// merely unreadable line is corrupt data, not a header, and corrupt data is
/// preserved rather than deleted.
bool looksLikeRepeatedHeader(const QStringList& cells, const settings::CsvFormat& format) {
    if (cells.size() < 2) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        const QString token = TextSanitizer::headerToken(cells.at(i));
        if (token.isEmpty() || !format.headerTokens.contains(token)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<math::SamplePoint> LoadedHistory::samples() const {
    std::vector<math::SamplePoint> points;
    points.reserve(records.size());
    for (const CsvRecord& record : records) {
        if (record.isValid()) {
            points.push_back(record.toSample());
        }
    }
    return points;
}

CsvSampleRepository::CsvSampleRepository(QString path, const settings::Settings& configuration)
    : path_(std::move(path)), configuration_(&configuration) {}

LoadedHistory CsvSampleRepository::parse(const QString& text,
                                         const settings::Settings& configuration) {
    const settings::CsvFormat& format = configuration.csv;

    LoadedHistory history;
    LoadReport& report = history.report;

    const QStringList rawLines = text.split(QRegularExpression(QStringLiteral("\\r\\n|\\r|\\n")));
    QStringList lines;
    lines.reserve(rawLines.size());
    for (const QString& line : rawLines) {
        if (line.trimmed().isEmpty()) {
            ++report.blankLines;
        } else {
            lines.append(line);
        }
    }
    if (lines.isEmpty()) {
        return history;
    }

    const QString separator = CsvDialect::detectSeparator(lines, format);
    report.detectedSeparator = separator;
    report.separatorWasNonCanonical = (separator != format.separator);

    const QChar separatorChar = separator.at(0);

    for (int index = 0; index < lines.size(); ++index) {
        const int lineNumber = index + 1;
        QStringList cells = CsvDialect::splitRecord(lines.at(index), separatorChar);
        for (QString& cell : cells) {
            cell = TextSanitizer::sanitize(cell);
        }

        if (index == 0 && looksLikeLeadingHeader(cells, configuration)) {
            ++report.skippedHeaders;
            const QString joined = cells.join(format.separator);
            for (const QString& legacy : format.legacyHeaders) {
                if (TextSanitizer::headerToken(joined) == TextSanitizer::headerToken(legacy)) {
                    report.legacyHeaderSeen = true;
                }
            }
            continue;
        }
        if (index > 0 && looksLikeRepeatedHeader(cells, format)) {
            ++report.skippedHeaders;
            report.repairs.append(
                QStringLiteral("line %1: a repeated header was skipped").arg(lineNumber));
            continue;
        }

        const QString rawJoined = cells.join(format.separator);

        // Trailing empty cells ("12;92.4;;;") do not invalidate a row.
        while (cells.size() > 2 && cells.last().trimmed().isEmpty()) {
            cells.removeLast();
        }
        if (cells.size() < 2) {
            history.records.push_back(CsvRecord::makeInvalid(rawJoined, lineNumber));
            core::log::warning(QStringLiteral("Line %1: missing columns; kept verbatim: %2")
                                   .arg(lineNumber)
                                   .arg(rawJoined));
            continue;
        }

        QString dayRepair;
        const std::optional<double> day = parseCell(cells.at(0), true, 0.0, 0.0, &dayRepair);
        if (!day.has_value()) {
            history.records.push_back(CsvRecord::makeInvalid(rawJoined, lineNumber));
            core::log::warning(QStringLiteral("Line %1: unreadable day; kept verbatim: %2")
                                   .arg(lineNumber)
                                   .arg(rawJoined));
            continue;
        }

        QString massRepair;
        const std::optional<double> mass =
            parseCell(cells.at(1), false, configuration.limits.minimumMass,
                      configuration.limits.maximumMass, &massRepair);
        if (!mass.has_value()) {
            history.records.push_back(CsvRecord::makeInvalid(rawJoined, lineNumber));
            core::log::warning(QStringLiteral("Line %1: unreadable mass; kept verbatim: %2")
                                   .arg(lineNumber)
                                   .arg(rawJoined));
            continue;
        }

        if (!dayRepair.isEmpty()) {
            report.repairs.append(QStringLiteral("line %1: day \"%2\" read as \"%3\"")
                                      .arg(lineNumber)
                                      .arg(cells.at(0), dayRepair));
        }
        if (!massRepair.isEmpty()) {
            report.repairs.append(QStringLiteral("line %1: mass \"%2\" read as \"%3\"")
                                      .arg(lineNumber)
                                      .arg(cells.at(1), massRepair));
        }

        const QString extra =
            cells.size() > 2 ? QStringList(cells.mid(2)).join(format.separator) : QString();
        CsvRecord record = CsvRecord::makeValid(*day, *mass, extra, lineNumber);

        // Duplicates are compared on parsed values, so differently spelled
        // copies of one measurement collapse. Invalid rows are never
        // deduplicated: each one is a distinct thing the user must see.
        const bool duplicate =
            std::any_of(history.records.begin(), history.records.end(),
                        [&record](const CsvRecord& existing) {
                            return existing.sameMeasurementAs(record);
                        });
        if (duplicate) {
            ++report.discardedDuplicates;
            report.repairs.append(
                QStringLiteral("line %1: duplicate measurement discarded").arg(lineNumber));
            continue;
        }

        history.records.push_back(std::move(record));
    }

    for (const CsvRecord& record : history.records) {
        if (record.isValid()) {
            ++report.validRecords;
        } else {
            ++report.invalidRecords;
        }
    }
    return history;
}

core::Result<LoadedHistory> CsvSampleRepository::load() {
    const QFileInfo info(path_);
    if (!info.exists()) {
        core::log::info(QStringLiteral("No history at %1 yet; it will be created on save.")
                            .arg(path_));
        return LoadedHistory{};
    }
    if (!info.isFile()) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Reading the history"),
                         QStringLiteral("%1: exists but is not a regular file").arg(path_));
    }
    if (!info.isReadable()) {
        return makeError(ErrorCode::PermissionDenied, QStringLiteral("Reading the history"),
                         QStringLiteral("%1: permission denied").arg(path_));
    }

    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeError(ErrorCode::ReadFailed, QStringLiteral("Reading the history"),
                         QStringLiteral("%1: %2").arg(path_, file.errorString()));
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return makeError(ErrorCode::ReadFailed, QStringLiteral("Reading the history"),
                         QStringLiteral("%1: %2").arg(path_, file.errorString()));
    }

    QString encoding;
    const QString text = CsvDialect::decode(bytes, &encoding);

    LoadedHistory history = parse(text, *configuration_);
    history.report.detectedEncoding = encoding;
    history.report.encodingWasNonCanonical = (encoding != QLatin1String("UTF-8"));

    if (history.report.encodingWasNonCanonical) {
        core::log::repair(
            QStringLiteral("History decoded as %1; it will be rewritten as UTF-8 on save.")
                .arg(encoding));
    }
    if (history.report.separatorWasNonCanonical) {
        const QString shown = history.report.detectedSeparator == QLatin1String("\t")
                                  ? QStringLiteral("TAB")
                                  : history.report.detectedSeparator;
        core::log::repair(QStringLiteral("Delimiter \"%1\" detected instead of \"%2\"; it will "
                                         "be rewritten on save.")
                              .arg(shown, configuration_->csv.separator));
    }
    if (history.report.legacyHeaderSeen) {
        core::log::repair(QStringLiteral("A header from an earlier release was found; it will "
                                         "be replaced by \"%1\" on save.")
                              .arg(configuration_->csv.header));
    }
    for (const QString& repair : history.report.repairs) {
        core::log::repair(repair);
    }
    core::log::success(settings::Strings::get(
        QStringLiteral("log.history.read"), QString::number(history.report.validRecords),
        QString::number(history.report.invalidRecords),
        QString::number(history.report.skippedHeaders),
        QString::number(history.report.discardedDuplicates)));
    return history;
}

std::vector<CsvRecord> CsvSampleRepository::sortForWriting(std::vector<CsvRecord> records,
                                                           const settings::CsvFormat& format,
                                                           QStringList* repairs) {
    std::vector<CsvRecord> valid;
    std::vector<CsvRecord> invalid;
    valid.reserve(records.size());

    const double scale = std::pow(10.0, format.dayFractionDecimals);
    for (CsvRecord& record : records) {
        if (!record.isValid()) {
            invalid.push_back(std::move(record));
            continue;
        }
        const double rounded = std::round(record.day() * scale) / scale;
        if (rounded != record.day() && repairs != nullptr) {
            repairs->append(QStringLiteral("day %1 rounded to %2 (%3 decimals)")
                                .arg(record.day())
                                .arg(rounded)
                                .arg(format.dayFractionDecimals));
        }
        record.setDay(rounded);
        valid.push_back(std::move(record));
    }

    std::stable_sort(valid.begin(), valid.end(),
                     [](const CsvRecord& a, const CsvRecord& b) { return a.day() < b.day(); });

    valid.insert(valid.end(), std::make_move_iterator(invalid.begin()),
                 std::make_move_iterator(invalid.end()));
    return valid;
}

QByteArray CsvSampleRepository::serialise(const std::vector<CsvRecord>& records,
                                          const settings::CsvFormat& format) {
    QString text;
    text.reserve(static_cast<qsizetype>(records.size()) * 32 + format.header.size());
    text += format.header;
    text += QLatin1Char('\n');

    for (const CsvRecord& record : records) {
        if (!record.isValid()) {
            text += record.rawText();
            text += QLatin1Char('\n');
            continue;
        }
        text += NumberParser::format(record.day(), format.dayFractionDecimals);
        text += format.separator;
        text += NumberParser::format(record.mass(), format.massDecimals);
        if (!record.extra().isEmpty()) {
            text += format.separator;
            text += record.extra();
        }
        text += QLatin1Char('\n');
    }
    return text.toUtf8();
}

core::Status CsvSampleRepository::save(const std::vector<CsvRecord>& records) {
    const QByteArray payload = serialise(records, configuration_->csv);
    const core::Status status = AtomicFileWriter::write(path_, payload);
    if (!status) {
        core::log::error(status.error().toString());
        return status;
    }
    core::log::success(settings::Strings::get(QStringLiteral("log.history.written"), path_,
                                              QString::number(records.size())));
    return status;
}

core::Status CsvSampleRepository::ensureExists() {
    if (QFileInfo::exists(path_)) {
        return core::Status::success();
    }
    core::log::info(settings::Strings::get(QStringLiteral("log.history.created"), path_));
    const QByteArray header = (configuration_->csv.header + QLatin1Char('\n')).toUtf8();
    const core::Status status = AtomicFileWriter::write(path_, header);
    if (!status) {
        core::log::error(status.error().toString());
    }
    return status;
}

}  // namespace weight::data
