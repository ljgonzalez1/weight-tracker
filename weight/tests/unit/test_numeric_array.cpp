#include <QtTest>

#include "math/common/NumericArray.hpp"

using namespace weight::math;

/// The array primitives are reimplementations of NumPy routines the previous
/// version relied on. Their exact edge-case behaviour is what makes the curves
/// reproducible, so each one is pinned here rather than assumed.
class TestNumericArray : public QObject {
    Q_OBJECT

private slots:
    void linspaceIncludesBothEndpoints() {
        const std::vector<double> values = linspace(2.0, 3.0, 5);
        QCOMPARE(values.size(), std::size_t(5));
        QCOMPARE(values.front(), 2.0);
        // The endpoint must be exact, not the result of accumulating a step.
        QCOMPARE(values.back(), 3.0);
        QVERIFY(std::abs(values[2] - 2.5) < 1e-15);
    }

    void linspaceHandlesDegenerateCounts() {
        QVERIFY(linspace(0.0, 1.0, 0).empty());
        const std::vector<double> single = linspace(4.0, 9.0, 1);
        QCOMPARE(single.size(), std::size_t(1));
        QCOMPARE(single.front(), 4.0);
    }

    void arangeStopsBeforeTheLimit() {
        const std::vector<double> values = arange(0.0, 1.0, 0.25);
        QCOMPARE(values.size(), std::size_t(4));  // 0, 0.25, 0.5, 0.75
        QCOMPARE(values.front(), 0.0);
        QVERIFY(values.back() < 1.0);
    }

    void arangeRejectsNonPositiveStep() {
        QVERIFY(arange(0.0, 1.0, 0.0).empty());
        QVERIFY(arange(0.0, 1.0, -0.5).empty());
    }

    void interpolationClampsOutsideTheRange() {
        const std::vector<double> x{0.0, 1.0, 2.0};
        const std::vector<double> y{10.0, 20.0, 40.0};

        QCOMPARE(interpolateLinearAt(-5.0, x, y), 10.0);  // clamped to the left
        QCOMPARE(interpolateLinearAt(9.0, x, y), 40.0);   // clamped to the right
        QCOMPARE(interpolateLinearAt(0.5, x, y), 15.0);
        QCOMPARE(interpolateLinearAt(1.5, x, y), 30.0);
        QCOMPARE(interpolateLinearAt(1.0, x, y), 20.0);   // exactly on a node
    }

    void gradientMatchesNumpyEdgeOrderOne() {
        // Central differences inside, one-sided at the two ends, which is what
        // numpy.gradient does with edge_order=1.
        const std::vector<double> x{0.0, 1.0, 2.0, 4.0};
        const std::vector<double> y{0.0, 1.0, 4.0, 16.0};
        const std::vector<double> slope = gradient(y, x);

        QCOMPARE(slope.size(), std::size_t(4));
        QVERIFY(std::abs(slope.front() - 1.0) < 1e-12);            // (1-0)/(1-0)
        QVERIFY(std::abs(slope.back() - 6.0) < 1e-12);             // (16-4)/(4-2)
        QVERIFY(std::abs(slope[1] - 2.0) < 1e-12);                 // (4-0)/(2-0)
    }

    void gradientOfAStraightLineIsConstant() {
        const std::vector<double> x = linspace(0.0, 10.0, 51);
        std::vector<double> y(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            y[i] = 3.0 * x[i] - 7.0;
        }
        for (const double slope : gradient(y, x)) {
            QVERIFY(std::abs(slope - 3.0) < 1e-10);
        }
    }

    void medianHandlesBothParities() {
        // std::span has no implicit conversion from a braced list, so the
        // sequences are named; that is also closer to how callers use it.
        const std::vector<double> odd{3.0, 1.0, 2.0};
        const std::vector<double> even{4.0, 1.0, 3.0, 2.0};
        const std::vector<double> empty;

        QCOMPARE(median(odd), 2.0);
        QCOMPARE(median(even), 2.5);
        QVERIFY(std::isnan(median(empty)));
    }

    void aggregationAveragesRepeatedDays() {
        const std::vector<double> x{2.0, 1.0, 2.0, 1.0, 3.0};
        const std::vector<double> y{20.0, 10.0, 30.0, 12.0, 5.0};
        const AggregatedSamples aggregated = aggregateByX(x, y);

        QCOMPARE(aggregated.x.size(), std::size_t(3));
        QCOMPARE(aggregated.x[0], 1.0);
        QCOMPARE(aggregated.x[1], 2.0);
        QCOMPARE(aggregated.x[2], 3.0);
        QCOMPARE(aggregated.meanY[0], 11.0);  // (10 + 12) / 2
        QCOMPARE(aggregated.meanY[1], 25.0);  // (20 + 30) / 2
        QCOMPARE(aggregated.meanY[2], 5.0);
        // Multiplicity is retained so estimators can still weight by it.
        QCOMPARE(aggregated.counts[0], 2.0);
        QCOMPARE(aggregated.counts[2], 1.0);
    }

    void kthNearestDistanceMatchesABruteForceScan() {
        const std::vector<double> nodes{0.0, 1.0, 4.0, 9.0, 16.0};
        for (const double probe : {-3.0, 0.5, 5.0, 12.0, 40.0}) {
            for (std::size_t k = 0; k < nodes.size(); ++k) {
                std::vector<double> distances;
                for (const double node : nodes) {
                    distances.push_back(std::abs(node - probe));
                }
                std::sort(distances.begin(), distances.end());
                QVERIFY(std::abs(kthNearestDistance(probe, nodes, k) - distances[k]) < 1e-12);
            }
        }
    }

    void coefficientOfDeterminationIsOneForAnExactFit() {
        const std::vector<double> x{0.0, 1.0, 2.0, 3.0};
        const std::vector<double> y{1.0, 3.0, 5.0, 7.0};
        // The curve passes exactly through every sample.
        const double r2 = coefficientOfDetermination(x, y, x, y);
        QVERIFY(std::abs(r2 - 1.0) < 1e-12);
    }

    void coefficientOfDeterminationIsZeroForTheMeanPredictor() {
        const std::vector<double> x{0.0, 1.0, 2.0, 3.0};
        const std::vector<double> y{1.0, 3.0, 5.0, 7.0};
        const std::vector<double> flat(x.size(), 4.0);  // the mean of y
        QVERIFY(std::abs(coefficientOfDetermination(x, y, x, flat)) < 1e-12);
    }
};

QTEST_APPLESS_MAIN(TestNumericArray)
#include "test_numeric_array.moc"
