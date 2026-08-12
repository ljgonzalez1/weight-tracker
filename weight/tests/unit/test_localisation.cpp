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

        // -- no locale configured -> English ---------------------------------
        QTest::newRow("C")            << QStringLiteral("C")             << en;
        QTest::newRow("POSIX")        << QStringLiteral("POSIX")         << en;
        QTest::newRow("C.UTF-8")      << QStringLiteral("C.UTF-8")       << en;
        QTest::newRow("empty")        << QString()                       << en;

        // -- every es_*, not just the one this was first written for ---------
        //
        // There are twenty-odd Spanish locales in the wild and the program
        // must speak Spanish to all of them. es_CL is simply the one the
        // author happens to use.
        QTest::newRow("es")           << QStringLiteral("es")            << es;
        QTest::newRow("es_CL")        << QStringLiteral("es_CL.UTF-8")   << es;
        QTest::newRow("es_ES")        << QStringLiteral("es_ES")         << es;
        QTest::newRow("es_MX")        << QStringLiteral("es_MX.UTF-8")   << es;
        QTest::newRow("es_AR")        << QStringLiteral("es_AR")         << es;
        QTest::newRow("es_US")        << QStringLiteral("es_US.UTF-8")   << es;
        QTest::newRow("es_419")       << QStringLiteral("es-419")        << es;
        QTest::newRow("es-CL BCP47")  << QStringLiteral("es-CL")         << es;
        QTest::newRow("es_ES@euro")   << QStringLiteral("es_ES@euro")    << es;
        QTest::newRow("es-Latn-MX")   << QStringLiteral("es-Latn-MX")    << es;
        QTest::newRow("valencian")    << QStringLiteral("es_ES.UTF-8@valencia") << es;
        QTest::newRow("uppercase")    << QStringLiteral("ES_CL")         << es;
        QTest::newRow("iso 639-3")    << QStringLiteral("spa")           << es;
        QTest::newRow("spa_CL")       << QStringLiteral("spa_CL")        << es;

        // -- every en_* ------------------------------------------------------
        QTest::newRow("en")           << QStringLiteral("en")            << en;
        QTest::newRow("en_US")        << QStringLiteral("en_US.UTF-8")   << en;
        QTest::newRow("en_GB")        << QStringLiteral("en_GB")         << en;
        QTest::newRow("en_AU")        << QStringLiteral("en_AU")         << en;
        QTest::newRow("en-IN BCP47")  << QStringLiteral("en-IN")         << en;

        // -- anything unsupported falls back to English, never to nothing ----
        QTest::newRow("de_DE")        << QStringLiteral("de_DE.UTF-8")   << en;
        QTest::newRow("ja_JP")        << QStringLiteral("ja_JP")         << en;
        QTest::newRow("pt_BR")        << QStringLiteral("pt_BR")         << en;
        QTest::newRow("zh-Hans-CN")   << QStringLiteral("zh-Hans-CN")    << en;
        QTest::newRow("nonsense")     << QStringLiteral("zz_ZZ")         << en;

        // -- the traps a prefix test falls into ------------------------------
        //
        // "starts with es" says yes to all of these, and all of them are
        // wrong. The mapping compares whole subtags for exactly this reason.
        QTest::newRow("estonian 2")   << QStringLiteral("et_EE")         << en;
        QTest::newRow("estonian 3")   << QStringLiteral("est_EE")        << en;
        QTest::newRow("esperanto")    << QStringLiteral("eo")            << en;
        QTest::newRow("esperanto 3")  << QStringLiteral("epo")           << en;
    }

    void tagsMapToTheRightLanguage() {
        QFETCH(QString, tag);
        QFETCH(int, expected);
        QCOMPARE(static_cast<int>(Strings::languageForTag(tag)), expected);
    }

    void thePrimarySubtagIsExtractedNotGuessed() {
        // POSIX and BCP-47 spellings, codesets, modifiers, scripts and
        // regions all fall away; what is left is the language.
        QCOMPARE(Strings::primaryLanguageSubtag(QStringLiteral("es_CL.UTF-8")),
                 QStringLiteral("es"));
        QCOMPARE(Strings::primaryLanguageSubtag(QStringLiteral("es-419")),
                 QStringLiteral("es"));
        QCOMPARE(Strings::primaryLanguageSubtag(QStringLiteral("es_ES.UTF-8@valencia")),
                 QStringLiteral("es"));
        QCOMPARE(Strings::primaryLanguageSubtag(QStringLiteral("EN_gb")),
                 QStringLiteral("en"));
        QCOMPARE(Strings::primaryLanguageSubtag(QStringLiteral("zh-Hans-CN")),
                 QStringLiteral("zh"));
        QCOMPARE(Strings::primaryLanguageSubtag(QString()), QString());
    }

    void supportIsNarrowerThanFallback() {
        // languageForTag always answers; tagIsSupported says whether the
        // answer was chosen or merely fallen back to. That distinction is what
        // lets a ranked list "de, es, en" skip German and land on Spanish
        // instead of stopping at the first entry and reporting English.
        QVERIFY(Strings::tagIsSupported(QStringLiteral("es_CL")));
        QVERIFY(Strings::tagIsSupported(QStringLiteral("en-GB")));
        QVERIFY(!Strings::tagIsSupported(QStringLiteral("de_DE")));
        QVERIFY(!Strings::tagIsSupported(QStringLiteral("C")));

        QCOMPARE(static_cast<int>(Strings::languageForTag(QStringLiteral("de_DE"))),
                 static_cast<int>(Language::English));
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

    void theEnvironmentChainIsHonouredInOrder_data() {
        QTest::addColumn<QByteArray>("lcAll");
        QTest::addColumn<QByteArray>("lcMessages");
        QTest::addColumn<QByteArray>("language");
        QTest::addColumn<QByteArray>("lang");
        QTest::addColumn<int>("expected");
        const int en = static_cast<int>(Language::English);
        const int es = static_cast<int>(Language::Spanish);

        const QByteArray none;
        QTest::newRow("LANG only")
            << none << none << none << QByteArray("es_CL.UTF-8") << es;
        QTest::newRow("LC_ALL wins over LANG")
            << QByteArray("en_US.UTF-8") << none << none << QByteArray("es_CL.UTF-8") << en;
        QTest::newRow("LC_MESSAGES wins over LANG")
            << none << QByteArray("es_ES") << none << QByteArray("en_US") << es;
        QTest::newRow("LANGUAGE outranks the ladder")
            << none << none << QByteArray("es:en") << QByteArray("en_US.UTF-8") << es;
        // The documented GNU exception: LC_ALL=C means untranslated output
        // even for someone whose LANGUAGE lists Spanish first, because that is
        // the contract every script relies on.
        QTest::newRow("C ignores LANGUAGE")
            << QByteArray("C") << none << QByteArray("es:fr") << none << en;
        QTest::newRow("unsupported LANGUAGE falls through to Spanish LANG")
            << none << none << QByteArray("de:fr") << QByteArray("es_CL.UTF-8") << es;
    }

    void theEnvironmentChainIsHonouredInOrder() {
        QFETCH(QByteArray, lcAll);
        QFETCH(QByteArray, lcMessages);
        QFETCH(QByteArray, language);
        QFETCH(QByteArray, lang);
        QFETCH(int, expected);

        const auto apply = [](const char* name, const QByteArray& value) {
            if (value.isEmpty()) {
                qunsetenv(name);
            } else {
                qputenv(name, value);
            }
        };
        // WEIGHT_LANG outranks everything by design, so it has to be out of
        // the way for the rest of the chain to be observable at all. The test
        // harness sets it, which is itself the proof that the override works.
        const QByteArray savedOverride = qgetenv("WEIGHT_LANG");
        qunsetenv("WEIGHT_LANG");

        apply("LC_ALL", lcAll);
        apply("LC_MESSAGES", lcMessages);
        apply("LANGUAGE", language);
        apply("LANG", lang);

        Strings::detectAndInstall();
        const int detected = static_cast<int>(Strings::current());

        for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANGUAGE", "LANG"}) {
            qunsetenv(name);
        }
        if (!savedOverride.isEmpty()) {
            qputenv("WEIGHT_LANG", savedOverride);
        }
        Strings::install(Language::English);

        QCOMPARE(detected, expected);
    }

    void theOverrideBeatsEverythingElse() {
        const QByteArray savedOverride = qgetenv("WEIGHT_LANG");
        qputenv("LANG", QByteArray("en_US.UTF-8"));
        qputenv("WEIGHT_LANG", QByteArray("es_CL.UTF-8"));

        Strings::detectAndInstall();
        const int detected = static_cast<int>(Strings::current());
        const QString source = Strings::detectionSource();

        qunsetenv("LANG");
        if (savedOverride.isEmpty()) {
            qunsetenv("WEIGHT_LANG");
        } else {
            qputenv("WEIGHT_LANG", savedOverride);
        }
        Strings::install(Language::English);

        QCOMPARE(detected, static_cast<int>(Language::Spanish));
        QCOMPARE(source, QStringLiteral("WEIGHT_LANG"));
    }

    void argumentsAreSubstituted() {
        Strings::install(Language::English);
        QCOMPARE(Strings::get(QStringLiteral("instance.pid"), QStringLiteral("1234")),
                 QStringLiteral("(already running as process 1234)"));
    }
};

QTEST_APPLESS_MAIN(TestLocalisation)
#include "test_localisation.moc"
