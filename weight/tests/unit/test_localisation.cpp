#include <QtTest>

#include "settings/Strings.hpp"

using namespace weight::settings;

/// The brief is specific about language selection: English by default, Spanish
/// only when the machine both supports and uses it, and a bare "C" counts as
/// "no locale configured" rather than as a language.
class TestLocalisation : public QObject {
    Q_OBJECT

private slots:
    void neutralTagsMeanNoLocale() {
        QVERIFY(Strings::tagIsNeutral(QStringLiteral("C")));
        QVERIFY(Strings::tagIsNeutral(QStringLiteral("POSIX")));
        QVERIFY(Strings::tagIsNeutral(QStringLiteral("C.UTF-8")));
        QVERIFY(Strings::tagIsNeutral(QString()));
        QVERIFY(!Strings::tagIsNeutral(QStringLiteral("es_CL.UTF-8")));
        QVERIFY(!Strings::tagIsNeutral(QStringLiteral("en_GB")));
    }

    void tagsMapToTheRightLanguage_data() {
        QTest::addColumn<QString>("tag");
        QTest::addColumn<int>("expected");
        const int en = static_cast<int>(Language::English);
        const int es = static_cast<int>(Language::Spanish);

        QTest::newRow("C")            << QStringLiteral("C")             << en;
        QTest::newRow("POSIX")        << QStringLiteral("POSIX")         << en;
        QTest::newRow("empty")        << QString()                       << en;
        QTest::newRow("es_CL")        << QStringLiteral("es_CL.UTF-8")   << es;
        QTest::newRow("es_ES")        << QStringLiteral("es_ES")         << es;
        QTest::newRow("es")           << QStringLiteral("es")            << es;
        QTest::newRow("en_US")        << QStringLiteral("en_US.UTF-8")   << en;
        QTest::newRow("en_GB")        << QStringLiteral("en_GB")         << en;
        // Anything unsupported falls back to English rather than to nothing.
        QTest::newRow("de_DE")        << QStringLiteral("de_DE.UTF-8")   << en;
        QTest::newRow("ja_JP")        << QStringLiteral("ja_JP")         << en;
        QTest::newRow("nonsense")     << QStringLiteral("zz_ZZ")         << en;
        // Not Spanish: the prefix must be the language, not any substring.
        QTest::newRow("estonian")     << QStringLiteral("et_EE")         << en;
    }

    void tagsMapToTheRightLanguage() {
        QFETCH(QString, tag);
        QFETCH(int, expected);
        QCOMPARE(static_cast<int>(Strings::languageForTag(tag)), expected);
    }

    void bothLanguagesCoverTheSameVocabulary() {
        // A half-translated release would show English text inside a Spanish
        // window; asserting the key sets match makes that impossible to ship.
        const QStringList english = Strings::keys(Language::English);
        const QStringList spanish = Strings::keys(Language::Spanish);

        QStringList missingFromSpanish;
        for (const QString& key : english) {
            if (!spanish.contains(key)) {
                missingFromSpanish.append(key);
            }
        }
        QStringList extraInSpanish;
        for (const QString& key : spanish) {
            if (!english.contains(key)) {
                extraInSpanish.append(key);
            }
        }
        QVERIFY2(missingFromSpanish.isEmpty(),
                 qPrintable(QStringLiteral("not translated: %1")
                                .arg(missingFromSpanish.join(QLatin1String(", ")))));
        QVERIFY2(extraInSpanish.isEmpty(),
                 qPrintable(QStringLiteral("only in Spanish: %1")
                                .arg(extraInSpanish.join(QLatin1String(", ")))));
    }

    void everyStringIsNonEmpty() {
        for (const Language language : {Language::English, Language::Spanish}) {
            Strings::install(language);
            for (const QString& key : Strings::keys(language)) {
                QVERIFY2(!Strings::get(key).isEmpty(), qPrintable(key));
            }
        }
        Strings::install(Language::English);
    }

    void aMissingKeyReturnsTheKey() {
        // Obvious on screen, rather than a silently blank label.
        QCOMPARE(Strings::get(QStringLiteral("no.such.key")), QStringLiteral("no.such.key"));
    }

    void textActuallyDiffersBetweenLanguages() {
        Strings::install(Language::English);
        const QString english = Strings::get(QStringLiteral("button.save"));
        Strings::install(Language::Spanish);
        const QString spanish = Strings::get(QStringLiteral("button.save"));
        Strings::install(Language::English);

        QCOMPARE(english, QStringLiteral("Save image"));
        QCOMPARE(spanish, QStringLiteral("Guardar imagen"));
        QVERIFY(english != spanish);
    }

    void argumentsAreSubstituted() {
        Strings::install(Language::English);
        QCOMPARE(Strings::get(QStringLiteral("instance.pid"), QStringLiteral("1234")),
                 QStringLiteral("(already running as process 1234)"));
    }
};

QTEST_APPLESS_MAIN(TestLocalisation)
#include "test_localisation.moc"
