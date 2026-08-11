#pragma once

#include "math/common/TrendEstimator.hpp"

namespace weight::math {

/// Curve (C) — "Local linear LOESS regression".
///
/// Full name
/// ---------
/// Local linear regression (LOESS) with Gaussian weights and a k-nearest
/// neighbour bandwidth.
///
/// What it computes
/// ----------------
/// At every instant t a straight line is fitted to the nearby observations by
/// weighted least squares,
///
///     minimise over (a, b):  sum_i w_i(t) * ( y_i - a - b (x_i - t) )^2
///     w_i(t) = n_i * exp( -((x_i - t)/h(t))^2 / 2 )
///
/// and the value of the trend is the local intercept, y_hat(t) = a, which is
/// the fitted line evaluated at t itself. The multiplicity n_i enters as a
/// frequency weight so that each raw measurement counts once.
///
/// Solving the 2x2 weighted normal equations in closed form, with
/// S_w = sum w_i, S_x = sum w_i d_i, S_xx = sum w_i d_i^2, S_y = sum w_i y_i,
/// S_xy = sum w_i d_i y_i and d_i = x_i - t:
///
///     a = ( S_y S_xx - S_x S_xy ) / ( S_w S_xx - S_x^2 )
///
/// The determinant S_w S_xx - S_x^2 vanishes only when every weight sits on a
/// single abscissa; in that case the fit degenerates gracefully to the weighted
/// mean S_y / S_w rather than dividing by zero.
///
/// Why *linear* and not constant
/// -----------------------------
/// A locally constant fit (the Nadaraya-Watson estimator) is biased wherever
/// the data are asymmetric around t, which is precisely what happens at the
/// two ends of a record and on either side of a gap: the estimate is dragged
/// towards the side that has more neighbours. Fitting a line removes that
/// first-order bias, so the curve stays sensible at the boundaries and
/// continues the local slope across a gap instead of flattening into it.
///
/// The adaptive bandwidth
/// ----------------------
/// h(t) is the distance from t to its q-th nearest measurement day, with
///
///     q = clamp( ceil( spanFraction * s * n ), minimumWindowPoints, n )
///
/// This is a nearest-neighbour, not a fixed-width, bandwidth. Where days are
/// densely recorded h(t) is small and the curve tracks them; inside a long gap
/// h(t) grows automatically until the window reaches data on both sides, so the
/// local line bridges the gap by interpolating between the two neighbourhoods.
/// A fixed bandwidth cannot do both.
///
/// Because q-th-nearest distance is a step function of t, h(t) is smoothed
/// before use; otherwise every change of neighbour would print a kink onto the
/// curve. After smoothing the estimator is smooth to visual accuracy.
///
/// Role of the smoothness factor s
/// -------------------------------
/// s scales the fraction of the data inside the window, so it maps onto the
/// window *size*: smaller s means fewer neighbours and a curve closer to the
/// measurements, exactly as for the other two methods.
///
/// Properties and limitations
/// --------------------------
///   * Not a finite polynomial. Each local fit is linear, but the family of
///     lines varies continuously with t, which is what makes the estimator
///     nonparametric.
///   * Local by construction: an outlier disturbs its own neighbourhood and
///     nothing else, which makes this the most reliable of the three curves
///     when the record contains a suspicious reading. It is not, however, the
///     iteratively reweighted *robust* LOESS; a single gross outlier still
///     bends its neighbourhood.
///   * Complexity O(G log n) for the bandwidth profile and O(G n) for the local
///     fits. The original array formulation materialised a full G-by-n distance
///     matrix; computing the k-th nearest distance directly avoids allocating
///     it, which is where most of the speed-up over the previous implementation
///     comes from.
class LocalLoessCurve final : public TrendEstimator {
public:

    /// Number of neighbours q inside the local window.
    [[nodiscard]] static std::size_t windowSize(const PreparedSamples& prepared,
                                                const TrendParameters& parameters);

    /// Smoothed bandwidth profile h(t) on the grid. Exposed for testing.
    [[nodiscard]] static std::vector<double> bandwidthProfile(const PreparedSamples& prepared,
                                                              const TrendParameters& parameters);

protected:
    [[nodiscard]] FitOutcome fit(const PreparedSamples& prepared,
                                 const TrendParameters& parameters) const override;
};

}  // namespace weight::math
