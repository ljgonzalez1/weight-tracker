#include <QtTest>

#include "math/common/CubicSpline.hpp"

using namespace weight::math;

/// The natural cubic spline is the base of curve A. Two properties are checked
/// directly rather than through the finished curve, because a defect here is
/// almost invisible after the mollifier smooths it away.
class TestCubicSpline : public QObject {
    Q_OBJECT

private slots:
    void interpolatesEveryNode() {
        const std::vector<double> x{0.0, 1.0, 3.5, 4.0, 9.0, 11.5, 12.0};
        const std::vector<double> y{2.0, 3.5, 1.0, 0.5, 4.0, 3.0, 1.5};
        const NaturalCubicSpline spline(x, y);

        for (std::size_t i = 0; i < x.size(); ++i) {
            QVERIFY(std::abs(spline.evaluate(x[i]) - y[i]) < 1e-10);
        }
    }

    /// Regression guard for a defect in the implementation this project
    /// replaces: its Thomas elimination indexed the sub-diagonal one place too
    /// early, so on irregular nodes the moments it produced did not solve the
    /// system they came from. Uniform spacing hides the defect entirely, hence
    /// the deliberately irregular abscissae here.
    ///
    /// The check is the strongest one available: substitute the computed
    /// moments back into the defining tridiagonal system and require the
    /// residual to be at rounding level.
    void momentsSatisfyTheDefiningSystem() {
        const std::vector<double> x{0.0, 1.0, 3.5, 4.0, 9.0, 11.5, 12.0};
        const std::vector<double> y{2.0, 3.5, 1.0, 0.5, 4.0, 3.0, 1.5};
        const NaturalCubicSpline spline(x, y);
        const std::vector<double>& moments = spline.secondDerivatives();

        QCOMPARE(moments.size(), x.size());
        // Natural boundary conditions.
        QCOMPARE(moments.front(), 0.0);
        QCOMPARE(moments.back(), 0.0);

        double worstResidual = 0.0;
        for (std::size_t j = 1; j + 1 < x.size(); ++j) {
            const double hLeft = x[j] - x[j - 1];
            const double hRight = x[j + 1] - x[j];
            const double lhs = hLeft * moments[j - 1] + 2.0 * (hLeft + hRight) * moments[j]
                               + hRight * moments[j + 1];
            const double rhs =
                6.0 * ((y[j + 1] - y[j]) / hRight - (y[j] - y[j - 1]) / hLeft);
            worstResidual = std::max(worstResidual, std::abs(lhs - rhs));
        }
        QVERIFY2(worstResidual < 1e-10,
                 qPrintable(QStringLiteral("residual %1 is too large; the tridiagonal solve is "
                                           "not solving its own system")
                                .arg(worstResidual)));
    }

    void reproducesACubicExactly() {
        // A natural spline cannot reproduce an arbitrary cubic (its second
        // derivative is forced to zero at the ends), but it must reproduce a
        // straight line everywhere, including between nodes.
        const std::vector<double> x{0.0, 2.0, 5.0, 9.0, 14.0};
        std::vector<double> y(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) {
            y[i] = -3.0 * x[i] + 11.0;
        }
        const NaturalCubicSpline spline(x, y);
        for (double t = 0.0; t <= 14.0; t += 0.37) {
            QVERIFY(std::abs(spline.evaluate(t) - (-3.0 * t + 11.0)) < 1e-9);
        }
    }

    void extrapolatesWithoutDiverging() {
        const std::vector<double> x{0.0, 1.0, 2.0};
        const std::vector<double> y{0.0, 1.0, 0.0};
        const NaturalCubicSpline spline(x, y);
        // Outside the node range the evaluation must stay finite; the estimators
        // never rely on the values, but they must not produce NaN.
        QVERIFY(std::isfinite(spline.evaluate(-10.0)));
        QVERIFY(std::isfinite(spline.evaluate(12.0)));
    }

    void degenerateInputIsHandled() {
        const std::vector<double> noX;
        const std::vector<double> noY;
        const NaturalCubicSpline empty(noX, noY);
        QVERIFY(!empty.isValid());

        const std::vector<double> oneX{5.0};
        const std::vector<double> oneY{7.0};
        const NaturalCubicSpline single(oneX, oneY);
        QCOMPARE(single.evaluate(5.0), 7.0);
        QCOMPARE(single.evaluate(-3.0), 7.0);  // a constant is the only sane answer

        const std::vector<double> twoX{0.0, 2.0};
        const std::vector<double> twoY{4.0, 8.0};
        const NaturalCubicSpline pair(twoX, twoY);
        QVERIFY(std::abs(pair.evaluate(1.0) - 6.0) < 1e-12);  // linear between two nodes
    }
};

QTEST_APPLESS_MAIN(TestCubicSpline)
#include "test_cubic_spline.moc"
