#pragma once

#include "math/common/TrendEstimator.hpp"

namespace weight::math {

/// Curve (B) — "Multiquadric RBF regression".
///
/// Full name
/// ---------
/// Ridge regression on a multiquadric radial basis, phi(r) = sqrt(r^2 + eps^2).
///
/// The model
/// ---------
/// The trend is written as a constant plus a linear combination of radial basis
/// functions centred on the days that carry data:
///
///     f(t) = y_bar + sum_j c_j * phi(t - x_j),   phi(r) = sqrt(r^2 + eps^2)
///
/// y_bar is the multiplicity-weighted mean of the observations. Centring on it
/// rather than on zero matters: as the penalty grows the coefficients shrink
/// towards zero and the curve therefore tends to the average of the data, which
/// is the sensible limiting behaviour, instead of collapsing to the x axis.
///
/// Fitting
/// -------
/// The coefficients solve a weighted, Tikhonov-regularised least squares
/// problem. With Phi the design matrix Phi_ij = phi(x_i - x_j), W the diagonal
/// matrix of measurement multiplicities and lambda the ridge penalty:
///
///     ( Phi^T W Phi + lambda I ) c = Phi^T W ( y - y_bar )
///
/// The normal-equation matrix is symmetric and, for lambda > 0, positive
/// definite, so it is solved by Cholesky factorisation; a pivoted LU solve is
/// kept as a fallback for the case where rounding costs it definiteness.
///
/// Choice of the two hyper-parameters
/// ----------------------------------
///     eps    = max(shapeMinDays, medianSpacing * shapeSpacingFactor) * s
///     lambda = max(ridgeMinimum, ridgeBase * s^2) * mean(diag(Phi^T W Phi))
///
/// Scaling lambda by the mean diagonal of the Gram matrix makes the penalty
/// dimensionless with respect to the data: the same ridgeBase behaves the same
/// way whether the history spans two months or two years. Both quantities grow
/// with s, so a single slider makes the curve smoother in two reinforcing ways
/// at once — a wider kernel and a stronger penalty.
///
/// Why the multiquadric
/// --------------------
/// phi is C-infinity everywhere, so the fitted curve is infinitely smooth, and
/// for large r it behaves like |r|. That near-linear tail lets the basis
/// represent piecewise-linear trends cheaply, which is exactly what produces a
/// sensible straight bridge across a long gap in the record rather than the
/// oscillation a compactly supported or Gaussian basis would show there.
///
/// This is a *global* estimator: one linear system couples every observation,
/// unlike the purely local LOESS fit. It therefore propagates information
/// across gaps, which is its main advantage and also the source of its main
/// risk — a single extreme outlier influences the whole curve slightly rather
/// than one neighbourhood strongly.
///
/// Statistical reading
/// -------------------
/// Ridge regression is the maximum a posteriori estimate under a Gaussian prior
/// on the coefficients: lambda encodes how strongly one believes in advance
/// that the trend is gentle. The multiplicity weights make the fit equivalent
/// to using each raw measurement once, rather than each measurement day once.
///
/// Complexity and limitations
/// --------------------------
///   * O(n k) to assemble the design matrix, O(k^2 n) for the Gram matrix and
///     O(k^3 / 6) for the Cholesky solve, with k the number of centres.
///   * Centres are thinned by uniform subsampling above `maximumCentres` (800)
///     to keep the cubic term affordable. Thinning slightly reduces the
///     achievable resolution on very long histories.
///   * The Gram matrix of a multiquadric basis is notoriously ill-conditioned
///     for small eps; the ridge term is what keeps the solve well posed, so
///     lowering `ridgeBase` towards zero is not safe.
///   * Should the solution still come out non-finite, the estimator falls back
///     to linear interpolation of the daily means and says so in its
///     diagnostics rather than returning silent nonsense.
class MultiquadricRbfCurve final : public TrendEstimator {
public:

    /// Basis centres actually used, after thinning. Exposed for testing.
    [[nodiscard]] static std::vector<double> selectCentres(const PreparedSamples& prepared,
                                                           const TrendParameters& parameters);

protected:
    [[nodiscard]] FitOutcome fit(const PreparedSamples& prepared,
                                 const TrendParameters& parameters) const override;
};

}  // namespace weight::math
