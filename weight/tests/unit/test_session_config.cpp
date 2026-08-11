#include <QtTest>

#include <QTemporaryDir>

#include "core/SessionConfig.hpp"
#include "settings/CurveCatalog.hpp"

using namespace weight;

/// config.txt holds what the person clicked. Two properties matter: it must
/// round-trip exactly, and it must survive the catalogue changing underneath
/// it, because curves are meant to be added freely.
class TestSessionConfig : public QObject {
    Q_OBJECT

private:
    settings::Settings settings_ = settings::Settings::defaults();

private slots:
    void roundTripsExactly() {
        core::SessionState state = core::SessionState::fromDefaults(settings_);
        state.smoothness = 1.35;
        state.showSamples = false;
        state.showSampleConnector = true;
        for (std::size_t i = 0; i < state.showCurve.size(); ++i) {
            state.showCurve[i] = (i % 2 == 0);
            state.showDerivative[i] = (i % 2 == 1);
        }

        const QByteArray written = core::SessionConfigStore::serialise(state);
        const core::SessionState back = core::SessionConfigStore::parse(
            QString::fromUtf8(written), core::SessionState::fromDefaults(settings_));

        QCOMPARE(back.smoothness, state.smoothness);
        QCOMPARE(back.showSamples, state.showSamples);
        QCOMPARE(back.showSampleConnector, state.showSampleConnector);
        QCOMPARE(back.showCurve, state.showCurve);
        QCOMPARE(back.showDerivative, state.showDerivative);
    }

    void curvesAreKeyedByIdNotByPosition() {
        // Writing positions would silently reassign someone's saved choices to
        // the wrong curves the moment a curve is inserted in the middle.
        core::SessionState state = core::SessionState::fromDefaults(settings_);
        const QByteArray written = core::SessionConfigStore::serialise(state);
        for (const settings::CurveDescriptor& curve : settings::curveCatalogue()) {
            const QString key = QStringLiteral("curve.%1").arg(curve.id);
            QVERIFY2(written.contains(key.toUtf8()), qPrintable(key));
        }
    }

    void unknownKeysAndCurvesAreIgnored() {
        const QString text = QStringLiteral(
            "; a comment\n"
            "# another comment\n"
            "smoothness = 0.75\n"
            "not.a.real.key = 42\n"
            "curve.a_curve_that_was_removed = true\n"
            "show.samples = false\n");
        const core::SessionState state = core::SessionConfigStore::parse(
            text, core::SessionState::fromDefaults(settings_));

        // A stale file must never be fatal; the keys it does understand apply.
        QCOMPARE(state.smoothness, 0.75);
        QCOMPARE(state.showSamples, false);
    }

    void malformedValuesKeepTheFallback() {
        core::SessionState fallback = core::SessionState::fromDefaults(settings_);
        fallback.smoothness = 0.5;
        const core::SessionState state = core::SessionConfigStore::parse(
            QStringLiteral("smoothness = not-a-number\nshow.samples = perhaps\n"), fallback);
        QCOMPARE(state.smoothness, 0.5);
        QCOMPARE(state.showSamples, fallback.showSamples);
    }

    void aShorterFileLeavesNewCurvesAtTheirDefaults() {
        // Simulates a config written before a curve existed.
        core::SessionState older = core::SessionState::fromDefaults(settings_);
        older.showCurve.resize(1);
        older.showDerivative.resize(1);
        older.showCurve[0] = true;

        settings::RenderOptions options = settings_.render;
        options.resizeToCatalogue();
        const std::vector<bool> before = options.showTrendCurve;
        older.applyTo(options);

        QCOMPARE(options.showTrendCurve[0], true);
        for (std::size_t i = 1; i < options.showTrendCurve.size(); ++i) {
            QCOMPARE(options.showTrendCurve[i], before[i]);
        }
    }

    void writesAreAtomicAndReadable() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("config.txt"));

        core::SessionConfigStore store(path, settings_);
        core::SessionState state = core::SessionState::fromDefaults(settings_);
        state.smoothness = 0.9;
        QVERIFY(bool(store.saveNow(state)));

        const core::Result<core::SessionState> loaded = store.load();
        QVERIFY(bool(loaded));
        QCOMPARE(loaded.value().smoothness, 0.9);
    }

    void aMissingFileIsNotAnError() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        core::SessionConfigStore store(directory.filePath(QStringLiteral("absent.txt")),
                                       settings_);
        const core::Result<core::SessionState> loaded = store.load();
        QVERIFY2(bool(loaded), "a first run must not be reported as a failure");
    }

    void rapidChangesCoalesceIntoTheFinalState() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("config.txt"));
        core::SessionConfigStore store(path, settings_);

        // Four clicks in quick succession: the file must end up holding the
        // last one, whatever order the worker got to them in.
        for (const double value : {0.2, 0.4, 0.6, 0.8}) {
            core::SessionState state = core::SessionState::fromDefaults(settings_);
            state.smoothness = value;
            store.scheduleSave(state);
        }
        store.flush();

        const core::Result<core::SessionState> loaded = store.load();
        QVERIFY(bool(loaded));
        QCOMPARE(loaded.value().smoothness, 0.8);
    }
};

QTEST_APPLESS_MAIN(TestSessionConfig)
#include "test_session_config.moc"
