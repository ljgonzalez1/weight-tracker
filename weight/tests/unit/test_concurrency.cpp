#include <QtTest>

#include <QFile>
#include <QTextStream>

#include "core/Concurrency.hpp"
#include "math/common/CurveFactory.hpp"
#include "math/common/TrendService.hpp"
#include "settings/CurveCatalog.hpp"

using namespace weight;

/// Concurrency is only worth having if it changes nothing about the answer.
/// These tests exist to prove that, and to pin the thread policy the brief
/// asked for: sized by work, not by cores.
class TestConcurrency : public QObject {
    Q_OBJECT

private:
    std::vector<math::SamplePoint> samples_;

    static math::TrendRequest everything() {
        math::TrendRequest request(settings::curveCount());
        for (std::size_t i = 0; i < settings::curveCount(); ++i) {
            request.curveWanted[i] = true;
            request.derivativeWanted[i] = true;
        }
        return request;
    }

private slots:
    void initTestCase() {
        QFile file(QStringLiteral(WEIGHT_TEST_DATA_DIR "/golden_samples.csv"));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            const QStringList parts = stream.readLine().trimmed().split(QLatin1Char(';'));
            if (parts.size() == 2) {
                samples_.push_back({parts.at(0).toDouble(), parts.at(1).toDouble()});
            }
        }
        QVERIFY(samples_.size() > 100);

        core::Concurrency::configure(static_cast<int>(settings::curveCount()), 3, 8);
    }

    void theThreadPoolIsSizedByWorkNotByCores() {
        // The whole point: a two-core machine still runs every curve at once
        // and lets the operating system schedule them.
        QVERIFY2(core::Concurrency::maxThreadCount()
                     >= static_cast<int>(settings::curveCount()),
                 "the pool must have at least one worker per curve");
        QVERIFY(core::Concurrency::maxThreadCount() >= 8);
    }

    void parallelAndSerialAgreeExactly() {
        const math::TrendParameters parameters;
        const math::TrendResults parallel =
            math::computeTrends(samples_, everything(), parameters);
        const math::TrendResults serial =
            math::computeTrendsSerial(samples_, everything(), parameters);

        QCOMPARE(parallel.size(), serial.size());
        for (std::size_t i = 0; i < parallel.size(); ++i) {
            QVERIFY(parallel.curve(i).has_value());
            QVERIFY(serial.curve(i).has_value());
            // Bit-identical, not merely close: any difference would mean the
            // tasks were sharing something they should not.
            QCOMPARE(parallel.curve(i)->y, serial.curve(i)->y);
            QCOMPARE(parallel.derivative(i)->y, serial.derivative(i)->y);
            QCOMPARE(*parallel.coefficient(i), *serial.coefficient(i));
        }
    }

    void repeatedRunsAreDeterministic() {
        const math::TrendParameters parameters;
        const math::TrendResults first = math::computeTrends(samples_, everything(), parameters);
        for (int attempt = 0; attempt < 5; ++attempt) {
            const math::TrendResults again =
                math::computeTrends(samples_, everything(), parameters);
            for (std::size_t i = 0; i < first.size(); ++i) {
                QCOMPARE(again.curve(i)->y, first.curve(i)->y);
            }
        }
    }

    void unselectedCurvesAreNotComputed() {
        math::TrendRequest request(settings::curveCount());
        request.curveWanted[0] = true;
        QCOMPARE(request.activeCount(), std::size_t(1));

        const math::TrendResults results =
            math::computeTrends(samples_, request, math::TrendParameters{});
        QVERIFY(results.curve(0).has_value());
        for (std::size_t i = 1; i < results.size(); ++i) {
            QVERIFY2(!results.curve(i).has_value(),
                     "a curve that was switched off must cost nothing at all");
        }
    }

    void nothingSelectedProducesNothing() {
        const math::TrendRequest request(settings::curveCount());
        QVERIFY(!request.anyWanted());
        const math::TrendResults results =
            math::computeTrends(samples_, request, math::TrendParameters{});
        for (std::size_t i = 0; i < results.size(); ++i) {
            QVERIFY(!results.curve(i).has_value());
        }
    }

    void everyRegisteredCurveCanBeBuilt() {
        // Guards the catalogue against an entry with a null factory, which
        // would otherwise show up only as a missing curve on the chart.
        QCOMPARE(math::registeredCurveCount(), settings::curveCount());
        for (std::size_t i = 0; i < settings::curveCount(); ++i) {
            QVERIFY2(math::createCurveEstimator(i) != nullptr,
                     qPrintable(settings::curveCatalogue()[i].id));
        }
        QVERIFY(math::createCurveEstimator(settings::curveCount()) == nullptr);
    }
};

QTEST_APPLESS_MAIN(TestConcurrency)
#include "test_concurrency.moc"
