#pragma once

#include "math/common/TrendEstimator.hpp"

namespace weight::math {

/// Curve (A) — "Adaptive Gaussian spline".
///
/// Full name
/// ---------
/// Natural cubic spline mollified by a Gaussian kernel of adaptive bandwidth.
///
/// What it computes
/// ----------------
/// The estimator is the composition of two operators applied to the daily
/// averages y_j observed on days x_j:
///
///     y_hat = M_{h(t)} ( S(y) )
///
///   S      interpolation by a natural cubic spline through the daily means.
///          The result is a C^2 curve passing exactly through every day.
///   M_h    convolution with a Gaussian kernel whose width h(t) varies
///          continuously along the time axis. In the sense of Friedrichs this
///          is a *mollifier*: convolving with a smooth, positive, unit-mass
///          kernel turns a merely C^2 curve into an essentially C-infinity one.
///
/// Formally, on the dense grid,
///
///     y_hat(t) = [ sum_u K((u - t)/h(t)) S(y)(u) ] / [ sum_u K((u - t)/h(t)) ]
///     K(z) = exp(-z^2 / 2)
///
/// The normalisation by the weight sum makes the operator reproduce constants
/// exactly, so a flat history stays flat and no bias is introduced at the ends.
///
/// The adaptive bandwidth
/// ----------------------
/// h(t) is driven by the local sample density, estimated with a pilot Gaussian
/// kernel of fixed width:
///
///     rho(t) = sum_j n_j^p * exp( -((t - x_j)/sigma_pilot)^2 / 2 )
///     h(t)   = h_base * ( median(rho) / rho(t) ) ^ q,  clamped to [h_min, h_max]
///
/// where n_j is the number of measurements taken on day x_j and p
/// (`duplicateCountExponent`, 0.35) damps their influence so that a day with
/// twelve weighings does not behave like twelve independent days. The exponent
/// q (`bandwidthDensityExponent`, 0.60) sets how strongly the bandwidth reacts:
/// q = 0 disables adaptation, q = 1 makes h inversely proportional to density.
///
/// The consequence is the behaviour a reader expects: where measurements are
/// dense the kernel is narrow and the curve follows them closely; across a
/// three-week gap the kernel widens and the curve bridges the gap smoothly
/// instead of inventing structure inside it.
///
/// h(t) is itself Gaussian-smoothed before use, because an abrupt change in
/// density would otherwise produce a visible kink where the bandwidth jumps.
///
/// Role of the smoothness factor s
/// -------------------------------
/// s multiplies h_base, so it scales the whole bandwidth profile: smaller s
/// means less mollification and a curve closer to the measurements. A visual
/// floor (`smoothnessVisualFloor`, 0.45) is applied first, because below it the
/// kernel becomes narrower than the spline's own curvature scale and the
/// rendered curve shows corners rather than looking smooth.
///
/// Why this method was chosen
/// --------------------------
/// It separates interpolation from smoothing. The spline contributes a
/// parameter-free base curve with no bias of its own, and every smoothing
/// decision lives in one interpretable quantity, the bandwidth h(t). That makes
/// the estimator easy to reason about and easy to test: the two stages can be
/// checked independently.
///
/// Properties and limitations
/// --------------------------
///   * Not a finite polynomial. A spline is piecewise polynomial, and after
///     Gaussian convolution the representation stops being polynomial at all.
///   * Smooth to visual accuracy (approximately C-infinity after mollification).
///   * Reproduces constants exactly; no systematic drift at the boundaries
///     beyond the edge replication used by the smoother.
///   * Because the base curve interpolates, an isolated outlier still bends the
///     result locally; it is attenuated by the mollifier, not rejected. This
///     estimator is not robust in the statistical sense, and the LOESS curve is
///     the better reference when outliers are suspected.
///   * Complexity O(G * W) where G is the grid size and W the widest kernel in
///     samples, plus O(G * n) for the pilot density.
class AdaptiveSplineCurve final : public TrendEstimator {
public:

    /// Continuous bandwidth profile h(t) evaluated on the grid. Exposed so the
    /// adaptation can be tested directly rather than only through the curve.
    [[nodiscard]] static std::vector<double> bandwidthProfile(const PreparedSamples& prepared,
                                                              const TrendParameters& parameters);

    /// Pilot density estimate rho(t) on the grid, also exposed for testing.
    [[nodiscard]] static std::vector<double> pilotDensity(const PreparedSamples& prepared,
                                                          const TrendParameters& parameters);

    /// Smoothness actually used, after the visual floor is applied.
    [[nodiscard]] static double effectiveSmoothness(const TrendParameters& parameters);

protected:
    [[nodiscard]] FitOutcome fit(const PreparedSamples& prepared,
                                 const TrendParameters& parameters) const override;
};

}  // namespace weight::math
