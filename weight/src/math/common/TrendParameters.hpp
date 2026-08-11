#pragma once

#include <cstddef>

namespace weight::math {

/// Identifies one of the three nonparametric trend estimators.
///
/// The letters A/B/C are part of the user-visible vocabulary (legend entries,
/// R-squared labels, tooltips), so they are kept as a stable, ordered enum
/// rather than being derived from the class names.
// The set of curves is no longer fixed here. It lives in
// settings/CurveCatalog.cpp, and every array in the program is sized from
// settings::curveCount(), so adding a curve does not touch this file.

// ---------------------------------------------------------------------------
// Numerical parameters
//
// Every constant that influences a fitted curve lives here. Nothing in the
// estimators is hard-coded: changing a field of this struct is the only way to
// alter the shape of a curve, which keeps the models reproducible and makes
// them testable without touching the algorithms.
//
// The physical unit of the independent variable is the *day*, so every
// bandwidth-like quantity below is expressed in days.
// ---------------------------------------------------------------------------

/// Parameters shared by the three estimators.
struct TrendCommonParameters {
    /// Master smoothness factor `s`, driven by the GUI slider.
    ///   s < 1  -> the curve tracks the samples closely
    ///   s = 1  -> baseline behaviour
    ///   s > 1  -> smoother, flatter, less faithful to individual samples
    /// Each estimator maps `s` onto its own bandwidth or regularisation level,
    /// always with the same monotone meaning.
    double generalSmoothness = 0.50;

    /// Minimum number of raw samples before a fit is attempted at all.
    std::size_t minimumPoints = 3;

    /// Dense evaluation grid shared by all curves. A high resolution keeps the
    /// numerical derivative (rate of change) smooth; the cost is negligible for
    /// the data volumes this application handles.
    std::size_t denseGridMinPoints = 1400;
    std::size_t denseGridMaxPoints = 4000;
    std::size_t denseGridPointsPerDay = 16;

    /// Lower clamp applied to `s` before it is used as a divisor or exponent.
    /// Prevents division by zero without visibly changing any legitimate value.
    double smoothnessEpsilon = 1e-6;

    /// Generic guard for quantities that must stay strictly positive
    /// (bandwidths, kernel widths, grid spacings).
    double positiveEpsilon = 1e-9;
};

/// Parameters of curve (A): natural cubic spline mollified by a Gaussian kernel
/// whose bandwidth varies continuously with the local sample density.
struct AdaptiveSplineParameters {
    /// Visual floor applied to `s`. Very small smoothness values make the
    /// mollifier narrower than the spline's own curvature scale, which produces
    /// visible corners; the floor keeps the rendered curve smooth.
    double smoothnessVisualFloor = 0.45;

    /// Exponent applied to the multiplicity of repeated x values when building
    /// the pilot density. A value below 1 dampens the influence of days with
    /// many measurements so that they do not dominate the bandwidth profile.
    double duplicateCountExponent = 0.35;

    /// Hard bounds of the adaptive bandwidth h(t), in days.
    double bandwidthMinDays = 2.5;
    double bandwidthMaxDays = 32.0;

    /// The base bandwidth is the largest of: the minimum above, the median
    /// sample spacing times this factor, and the data span times the fraction
    /// below. Taking a maximum makes the estimator scale-free: it adapts to
    /// dense daily logging and to sparse monthly logging alike.
    double bandwidthSpacingFactor = 2.40;
    double bandwidthSpanFraction = 0.12;

    /// Exponent of the density ratio in h(t) = h_base * (median_density/density)^p.
    /// p = 0 disables adaptivity; p = 1 makes the bandwidth inversely
    /// proportional to density. The default sits between the two.
    double bandwidthDensityExponent = 0.60;

    /// Width of the pilot Gaussian used to estimate the local sample density,
    /// expressed the same way as the base bandwidth above.
    double pilotSpacingFactor = 1.60;
    double pilotSpanFraction = 0.05;

    /// The raw h(t) profile is itself smoothed so that the mollifier width
    /// changes gradually; otherwise abrupt density changes create kinks.
    double profileSigmaFraction = 0.22;
    double profileMinSigmaSamples = 1.2;
    double profileMaxSigmaSamples = 32.0;
    double profileRadiusSigmas = 3.0;

    /// Truncation radius of the adaptive Gaussian mollifier, in units of h(t).
    /// Four sigmas retain more than 99.99 % of the kernel mass.
    double mollifierRadiusSigmas = 4.0;

    /// If the accumulated kernel weight falls below this value the sample is
    /// copied unchanged instead of dividing by a near-zero denominator.
    double mollifierMinWeightSum = 1e-12;

    /// Final light Gaussian pass applied to the mollified curve. Its width is
    /// `mean(h) * sigmaFraction * s^exponent`, clamped to the sample bounds.
    double postSmoothSigmaFraction = 0.10;
    double postSmoothSmoothnessExponent = 0.85;
    double postSmoothMinSigmaSamples = 0.75;
    double postSmoothMaxSigmaSamples = 18.0;
    double postSmoothRadiusSigmas = 3.0;

    /// Floor applied to the estimated density to avoid dividing by zero far
    /// away from every sample.
    double densityFloor = 1e-12;
};

/// Parameters of curve (B): ridge regression on a multiquadric radial basis.
struct MultiquadricRbfParameters {
    /// Shape parameter epsilon of phi(r) = sqrt(r^2 + eps^2), expressed as a
    /// multiple of the median sample spacing and then scaled by `s`.
    double shapeSpacingFactor = 1.10;
    double shapeMinDays = 1.5;

    /// Ridge penalty lambda, scaled by s^2 and by the mean diagonal of the
    /// Gram matrix so that it is invariant to the units of the data.
    double ridgeBase = 5e-3;
    double ridgeMinimum = 1e-8;

    /// Upper bound on the number of basis centres. Beyond this the centres are
    /// thinned by uniform subsampling, which keeps the O(k^3) solve affordable.
    std::size_t maximumCentres = 800;
};

/// Parameters of curve (C): local linear regression with a k-nearest bandwidth.
struct LocalLinearLoessParameters {
    /// Fraction of the samples inside the local window at s = 1, then scaled
    /// linearly by `s` and clamped to the bounds below.
    double spanFractionBase = 0.28;
    double spanFractionMin = 0.06;
    double spanFractionMax = 1.00;

    /// Absolute minimum number of neighbours in a window. Two points are the
    /// theoretical minimum for a line; four gives the fit some redundancy.
    std::size_t minimumWindowPoints = 4;

    /// Lower clamp of the bandwidth, in days.
    double bandwidthMinDays = 1.5;

    /// The k-nearest bandwidth is a step function of t; smoothing it over this
    /// fraction of the data span removes the resulting kinks.
    double bandwidthSmoothFraction = 0.04;
    double bandwidthProfileRadiusSigmas = 3.0;
    double bandwidthProfileMinSigmaSamples = 1.0;

    /// Relative tolerance below which the 2x2 weighted normal-equation system
    /// is considered singular and the fit degrades to a weighted mean.
    double degeneracyTolerance = 1e-12;
};

/// Parameters of the numerical derivative ("rate of change") of any curve.
struct DerivativeParameters {
    /// Resampling step of the dense curve before differentiating, in days.
    double sampleStepDays = 0.5;

    /// Conversion from the natural slope (kg/day) to the displayed unit.
    double daysPerDisplayedPeriod = 7.0;
};

/// Complete numerical configuration of the trend layer.
struct TrendParameters {
    TrendCommonParameters common{};
    AdaptiveSplineParameters splineA{};
    MultiquadricRbfParameters rbfB{};
    LocalLinearLoessParameters loessC{};
    DerivativeParameters derivative{};
};

}  // namespace weight::math
