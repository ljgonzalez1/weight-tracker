#include <QtTest>

#include <QTemporaryDir>

#include "settings/Settings.hpp"
#include "data/CsvSampleRepository.hpp"
#include "data/NumberParser.hpp"
#include "data/NumericTextRepairChain.hpp"
#include "data/TextSanitizer.hpp"

using namespace weight;
using namespace weight::data;

/// The parser has to survive files that were edited by hand, exported from a
/// spreadsheet and moved between machines. These tests fix the two rules that
/// matter: repair only what cannot change a value, and never lose a line.
class TestCsvParsing : public QObject {
    Q_OBJECT

private:
    settings::Settings config_ = settings::Settings::defaults();

    LoadedHistory parse(const QString& text) {
        return CsvSampleRepository::parse(text, config_);
    }

private slots:
    // -- strict parsing ----------------------------------------------------
    void acceptsBothDecimalSeparators() {
        QCOMPARE(NumberParser::parseDecimal(QStringLiteral("92.4"), false).value(), 92.4);
        QCOMPARE(NumberParser::parseDecimal(QStringLiteral("92,4"), false).value(), 92.4);
    }

    void rejectsAmbiguousGrouping() {
        // Could be 1222333 or 1.222333 depending on the writer's locale.
        QVERIFY(!NumberParser::parseDecimal(QStringLiteral("1.222.333"), false).has_value());
        QVERIFY(!NumberParser::parseDecimal(QStringLiteral("1,222,333"), false).has_value());
        QVERIFY(!NumberParser::parseDecimal(QStringLiteral("1.222,333"), false).has_value());
    }

    void rejectsInternalWhitespace() {
        // "9 8" could be 98 or two separate values; neither reading is safe.
        QVERIFY(!NumberParser::parseDecimal(QStringLiteral("9 8"), false).has_value());
    }

    void rejectsSignOnTheMassColumn() {
        QVERIFY(!NumberParser::parseMass(QStringLiteral("-92.4"), 30.0, 300.0).has_value());
        QVERIFY(NumberParser::parseDay(QStringLiteral("-92.4")).has_value());
    }

    void enforcesThePlausibleMassRange() {
        QVERIFY(!NumberParser::parseMass(QStringLiteral("5"), 30.0, 300.0).has_value());
        QVERIFY(!NumberParser::parseMass(QStringLiteral("900"), 30.0, 300.0).has_value());
        QVERIFY(NumberParser::parseMass(QStringLiteral("92.4"), 30.0, 300.0).has_value());
    }

    // -- repair chain ------------------------------------------------------
    void repairsUnambiguousText_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<double>("expected");

        QTest::newRow("unit suffix")       << QStringLiteral("92,4 kg")   << 92.4;
        QTest::newRow("unit no space")     << QStringLiteral("92.4Kg")    << 92.4;
        QTest::newRow("spelled unit")      << QStringLiteral("88 kilos")  << 88.0;
        QTest::newRow("no-break space")    << QString::fromUtf8("92,4\u00A0kg") << 92.4;
        QTest::newRow("unicode minus day") << QString::fromUtf8("\u22125") << -5.0;
        QTest::newRow("fullwidth stop")    << QString::fromUtf8("92\uFF0E4") << 92.4;
        QTest::newRow("grouped thousands") << QStringLiteral("1.234,5")   << 1234.5;
        QTest::newRow("grouped commas")    << QStringLiteral("1,234.5")   << 1234.5;
        QTest::newRow("space grouping")    << QStringLiteral("1 234,5")   << 1234.5;
        QTest::newRow("scientific")        << QStringLiteral("9.24e1")    << 92.4;
        QTest::newRow("bare point left")   << QStringLiteral(".5")        << 0.5;
        QTest::newRow("bare point right")  << QStringLiteral("92.")       << 92.0;
        QTest::newRow("quoted")            << QStringLiteral("\"92.4\"")  << 92.4;
    }

    void repairsUnambiguousText() {
        QFETCH(QString, input);
        QFETCH(double, expected);

        const QString repaired = NumericTextRepairChain::standard().repair(input, true);
        const auto value = NumberParser::parseDecimal(repaired, true);
        QVERIFY2(value.has_value(),
                 qPrintable(QStringLiteral("\"%1\" repaired to \"%2\", still unreadable")
                                .arg(input, repaired)));
        QVERIFY(std::abs(*value - expected) < 1e-9);
    }

    void leavesUnrescuableTextAlone() {
        // No rule can recover these, and inventing a reading would silently
        // alter someone's recorded mass. They must stay unreadable so the row
        // is preserved verbatim for the user to correct.
        for (const QString& text : {QStringLiteral("9 8"), QStringLiteral("abc"),
                                    QStringLiteral("92..4"), QStringLiteral("--")}) {
            const QString repaired = NumericTextRepairChain::standard().repair(text, false);
            QVERIFY2(!NumberParser::parseDecimal(repaired, false).has_value(),
                     qPrintable(QStringLiteral("\"%1\" was wrongly rescued as \"%2\"")
                                    .arg(text, repaired)));
        }
    }

    /// The one genuinely ambiguous form is a single separator with exactly
    /// three digits after it. The strict pass settles it as a decimal before
    /// any repair rule can see it, which is what makes the reading predictable
    /// rather than locale-dependent.
    void aSingleSeparatorIsAlwaysReadAsADecimal() {
        QCOMPARE(NumberParser::parseDecimal(QStringLiteral("1.222"), false).value(), 1.222);
        QCOMPARE(NumberParser::parseDecimal(QStringLiteral("1,222"), false).value(), 1.222);
    }

    /// Two different separators leave only one valid reading, so repairing them
    /// cannot change a value.
    void mixedSeparatorsAreRecoveredUnambiguously() {
        const auto repair = [](const QString& text) {
            return NumberParser::parseDecimal(
                NumericTextRepairChain::standard().repair(text, false), false);
        };
        QCOMPARE(repair(QStringLiteral("1.222,333")).value(), 1222.333);
        QCOMPARE(repair(QStringLiteral("1,222.333")).value(), 1222.333);
        // Repeated dot groups rule out a decimal reading entirely.
        QCOMPARE(repair(QStringLiteral("1.222.333")).value(), 1222333.0);
    }

    // -- file level --------------------------------------------------------
    void skipsTheHeaderAndReadsTheRows() {
        const LoadedHistory history = parse(QStringLiteral(
            "day;mass;timestamp\n0;92.4;2026-03-31 08:00\n1;92.1;2026-04-01 08:00\n"));
        QCOMPARE(history.report.skippedHeaders, 1);
        QCOMPARE(history.report.validRecords, 2);
        QCOMPARE(history.report.invalidRecords, 0);
        QCOMPARE(history.samples().size(), std::size_t(2));
    }

    void acceptsAFileWithNoHeader() {
        const LoadedHistory history = parse(QStringLiteral("0;92.4\n1;92.1\n"));
        QCOMPARE(history.report.skippedHeaders, 0);
        QCOMPARE(history.report.validRecords, 2);
    }

    void skipsHeadersRepeatedMidFile() {
        // Produced by concatenating two exports; the second header is not data.
        const LoadedHistory history =
            parse(QStringLiteral("day;mass\n0;92.4\nday;mass\n1;92.1\n"));
        QCOMPARE(history.report.skippedHeaders, 2);
        QCOMPARE(history.report.validRecords, 2);
    }

    void preservesUnreadableLinesInsteadOfDroppingThem() {
        const LoadedHistory history =
            parse(QStringLiteral("day;mass\n0;92.4\nthis line is broken\n2;91.8\n"));
        QCOMPARE(history.report.validRecords, 2);
        QCOMPARE(history.report.invalidRecords, 1);

        // The original text survives verbatim so the user can repair it.
        const QByteArray written =
            CsvSampleRepository::serialise(history.records, config_.csv);
        QVERIFY(written.contains("this line is broken"));
    }

    void discardsExactDuplicates() {
        const LoadedHistory history =
            parse(QStringLiteral("day;mass\n0;92.4\n0;92.40\n0;92.4\n1;90\n"));
        // "92.4" and "92.40" are the same measurement written two ways.
        QCOMPARE(history.report.discardedDuplicates, 2);
        QCOMPARE(history.report.validRecords, 2);
    }

    void detectsAnAlternativeDelimiter() {
        const LoadedHistory history = parse(QStringLiteral("day,mass\n0,92.4\n1,92.1\n"));
        QCOMPARE(history.report.detectedSeparator, QStringLiteral(","));
        QVERIFY(history.report.separatorWasNonCanonical);
        QCOMPARE(history.report.validRecords, 2);
    }

    void honoursQuotedFields() {
        const LoadedHistory history =
            parse(QStringLiteral("day;mass;note\n0;92.4;\"a note; with a delimiter\"\n"));
        QCOMPARE(history.report.validRecords, 1);
        QVERIFY(history.records.front().extra().contains(
            QStringLiteral("a note; with a delimiter")));
    }

    void toleratesTrailingEmptyColumns() {
        const LoadedHistory history = parse(QStringLiteral("day;mass\n0;92.4;;;\n"));
        QCOMPARE(history.report.validRecords, 1);
        QCOMPARE(history.report.invalidRecords, 0);
    }

    void sortsValidRowsAndKeepsBrokenOnesLast() {
        const LoadedHistory history =
            parse(QStringLiteral("day;mass\n5;90\nbroken\n1;92\n3;91\n"));
        const std::vector<CsvRecord> ordered =
            CsvSampleRepository::sortForWriting(history.records, config_.csv);

        QCOMPARE(ordered.size(), std::size_t(4));
        QCOMPARE(ordered[0].day(), 1.0);
        QCOMPARE(ordered[1].day(), 3.0);
        QCOMPARE(ordered[2].day(), 5.0);
        QVERIFY(!ordered[3].isValid());  // the broken line sits at the end
    }

    void serialisationRoundTrips() {
        const QString original =
            QStringLiteral("day;mass;timestamp\n0;92.4;2026-03-31 08:00\n2.5;91.25;\n");
        const LoadedHistory first = parse(original);
        const QByteArray written = CsvSampleRepository::serialise(first.records, config_.csv);
        const LoadedHistory second = parse(QString::fromUtf8(written));

        QCOMPARE(second.report.validRecords, first.report.validRecords);
        const std::vector<math::SamplePoint> a = first.samples();
        const std::vector<math::SamplePoint> b = second.samples();
        QCOMPARE(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            QCOMPARE(a[i].day, b[i].day);
            QCOMPARE(a[i].mass, b[i].mass);
        }
    }

    void negativeDaysAreValid() {
        // Days before the plot origin are legitimate and must survive.
        const LoadedHistory history = parse(QStringLiteral("day;mass\n-12.5;95\n0;92.4\n"));
        QCOMPARE(history.report.validRecords, 2);
        QCOMPARE(history.samples().front().day, -12.5);
    }

    void sanitizerFoldsTypographicCharacters() {
        QCOMPARE(TextSanitizer::sanitize(QString::fromUtf8("\u201C92,4\u201D")),
                 QStringLiteral("92,4"));
        QCOMPARE(TextSanitizer::headerToken(QString::fromUtf8("D\u00EDa")),
                 QStringLiteral("dia"));
        QCOMPARE(TextSanitizer::headerToken(QStringLiteral("Mass [kg]")),
                 QStringLiteral("mass"));
    }

    void atomicWriteReplacesTheFileCompletely() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("history.csv"));

        CsvSampleRepository repository(path, config_);
        QVERIFY(bool(repository.ensureExists()));
        QVERIFY(QFile::exists(path));

        std::vector<CsvRecord> records;
        records.push_back(CsvRecord::makeValid(0.0, 92.4, QString()));
        records.push_back(CsvRecord::makeValid(1.0, 92.1, QString()));
        QVERIFY(bool(repository.save(records)));

        core::Result<LoadedHistory> reloaded = repository.load();
        QVERIFY(bool(reloaded));
        QCOMPARE(reloaded.value().report.validRecords, 2);
    }
};

QTEST_APPLESS_MAIN(TestCsvParsing)
#include "test_csv_parsing.moc"
