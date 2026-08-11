#include <QtTest>

#include "data/CalendarUtils.hpp"
#include "plot/AxisWindow.hpp"

using namespace weight;

/// The visible window is derived from the data, so the rules that derive it are
/// worth pinning precisely: an off-by-one here silently crops someone's oldest
/// measurement off the left edge.
class TestAxisWindow : public QObject {
    Q_OBJECT

private:
    settings::Settings settings_ = settings::Settings::defaults();
    QDate today_{2026, 8, 10};

    /// Day-axis coordinate of a calendar date, in the CSV's own units.
    double dayOf(const QDate& date) const {
        return data::CalendarUtils::fractionalDay(QDateTime(date, QTime(0, 0)),
                                                  settings_.plot.geometry.originDate,
                                                  settings_.csv);
    }

private slots:
    void withNoDataTheChartStartsToday() {
        const std::vector<math::SamplePoint> none;
        const plot::AxisWindow window = plot::computeAxisWindow(none, settings_, today_);

        QVERIFY(!window.hasData());
        QCOMPARE(window.xMinimum, dayOf(today_));
        // A full year ahead, since there is no last measurement to extend from.
        QCOMPARE(window.xMaximum, dayOf(today_) + 365.0);
    }

    void withDataTheChartStartsAtTheOldestMeasurement() {
        const std::vector<math::SamplePoint> samples{
            {dayOf(QDate(2026, 5, 20)), 88.0},
            {dayOf(QDate(2026, 4, 1)), 90.0},   // the oldest, given out of order
            {dayOf(QDate(2026, 6, 15)), 86.0},
        };
        const plot::AxisWindow window = plot::computeAxisWindow(samples, settings_, today_);

        QVERIFY(window.hasData());
        QCOMPARE(window.xMinimum, dayOf(QDate(2026, 4, 1)));
        QCOMPARE(window.firstDay, dayOf(QDate(2026, 4, 1)));
        QCOMPARE(window.lastDay, dayOf(QDate(2026, 6, 15)));
    }

    void aShortHistoryGetsAFullYear() {
        // First and last are two weeks apart, so a year from the first is
        // further than three months from the last: the year wins.
        const std::vector<math::SamplePoint> samples{
            {dayOf(QDate(2026, 4, 1)), 90.0},
            {dayOf(QDate(2026, 4, 15)), 89.5},
        };
        const plot::AxisWindow window = plot::computeAxisWindow(samples, settings_, today_);
        QCOMPARE(window.xMaximum, dayOf(QDate(2026, 4, 1)) + 365.0);
    }

    void aLongHistoryGetsThreeMonthsBeyondTheLast() {
        // Two years of data: three months past the last measurement is far
        // beyond a year from the first, so that one wins.
        const std::vector<math::SamplePoint> samples{
            {dayOf(QDate(2024, 1, 1)), 95.0},
            {dayOf(QDate(2026, 1, 1)), 85.0},
        };
        const plot::AxisWindow window = plot::computeAxisWindow(samples, settings_, today_);
        QCOMPARE(window.xMaximum, dayOf(QDate(2026, 1, 1)) + 91.0);
        QVERIFY(window.xMaximum > dayOf(QDate(2024, 1, 1)) + 365.0);
    }

    void theBoundaryPicksWhicheverIsFurther() {
        // Exactly at the crossover: first + 365 equals last + 91 when the
        // record spans 274 days. Either rule gives the same answer, and the
        // maximum must not overshoot.
        const double first = dayOf(QDate(2026, 1, 1));
        const std::vector<math::SamplePoint> samples{{first, 90.0}, {first + 274.0, 85.0}};
        const plot::AxisWindow window = plot::computeAxisWindow(samples, settings_, today_);
        QCOMPARE(window.xMaximum, first + 365.0);
    }

    void aSingleMeasurementIsHandled() {
        const std::vector<math::SamplePoint> samples{{dayOf(QDate(2026, 7, 1)), 90.0}};
        const plot::AxisWindow window = plot::computeAxisWindow(samples, settings_, today_);

        QCOMPARE(window.xMinimum, dayOf(QDate(2026, 7, 1)));
        QCOMPARE(window.xMaximum, dayOf(QDate(2026, 7, 1)) + 365.0);
        QVERIFY(window.span() > 0.0);
    }

    void measurementsBeforeTheEpochStillFit() {
        // Negative day values are legitimate: they are dates before the CSV
        // epoch. The window must start at them, not clamp to zero.
        const double first = dayOf(QDate(2025, 1, 1));
        QVERIFY(first < 0.0);
        const std::vector<math::SamplePoint> samples{{first, 95.0},
                                                     {dayOf(QDate(2025, 3, 1)), 93.0}};
        const plot::AxisWindow window = plot::computeAxisWindow(samples, settings_, today_);
        QCOMPARE(window.xMinimum, first);
    }

    void everyMeasurementLiesInsideTheWindow() {
        const std::vector<math::SamplePoint> samples{
            {dayOf(QDate(2025, 6, 1)), 95.0},
            {dayOf(QDate(2026, 2, 14)), 90.0},
            {dayOf(QDate(2026, 8, 9)), 86.0},
        };
        const plot::AxisWindow window = plot::computeAxisWindow(samples, settings_, today_);
        for (const math::SamplePoint& sample : samples) {
            QVERIFY2(sample.day >= window.xMinimum && sample.day <= window.xMaximum,
                     "a measurement fell outside the visible window");
        }
    }

    void theSpanIsNeverDegenerate() {
        // Guards the coordinate mapping, which divides by the span.
        settings::Settings odd = settings_;
        odd.plot.geometry.horizonFromFirstDays = 0.0;
        odd.plot.geometry.horizonFromLastDays = 0.0;
        const std::vector<math::SamplePoint> samples{{100.0, 90.0}};
        const plot::AxisWindow window = plot::computeAxisWindow(samples, odd, today_);
        QVERIFY(window.span() > 0.0);
    }

    void labelSpacingStaysReadable_data() {
        QTest::addColumn<int>("days");
        QTest::newRow("one year")    << 365;
        QTest::newRow("two years")   << 730;
        QTest::newRow("five years")  << 1825;
        QTest::newRow("ten years")   << 3650;
    }

    void labelSpacingStaysReadable() {
        QFETCH(int, days);
        const double first = dayOf(QDate(2026, 1, 1));
        const std::vector<math::SamplePoint> samples{{first, 90.0},
                                                     {first + days, 85.0}};
        const plot::AxisWindow window = plot::computeAxisWindow(samples, settings_, today_);

        const double labels = window.span() / window.labelStepDays;
        QVERIFY2(labels <= settings_.plot.geometry.maximumXLabels + 1,
                 qPrintable(QStringLiteral("%1 labels over %2 days at a %3-day step")
                                .arg(labels).arg(window.span()).arg(window.labelStepDays)));
        QVERIFY(window.labelStepDays > 0.0);
    }

    void aShortRecordKeepsWeeklyTicks() {
        const std::vector<math::SamplePoint> samples{{dayOf(QDate(2026, 4, 1)), 90.0}};
        const plot::AxisWindow window = plot::computeAxisWindow(samples, settings_, today_);
        QCOMPARE(window.labelStepDays, 7.0);
    }
};

QTEST_APPLESS_MAIN(TestAxisWindow)
#include "test_axis_window.moc"
