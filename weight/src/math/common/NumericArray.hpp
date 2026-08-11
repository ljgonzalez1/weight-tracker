#pragma once

#include <cstddef>
#include <span>
#include <vector>

/// Small, dependency-free numeric helpers.
///
/// The previous implementation of this program expressed its estimators in
/// NumPy. The functions below reproduce the exact semantics of the NumPy calls
/// that were used, including their edge-case behaviour, so that the fitted
/// curves are numerically the same rather than merely similar. Each function
/// documents the NumPy call it corresponds to.
namespace weight::math {

/// Samples grouped by their (unique, ascending) x coordinate.
struct AggregatedSamples {
    std::vector<double> x;       ///< Strictly increasing.
    std::vector<double> meanY;   ///< Arithmetic mean of the y values at each x.
    std::vector<double> counts;  ///< How many raw observations fell on each x.
};

/// Equivalent of `numpy.linspace(start, stop, count)`: `count` points spanning
/// the closed interval. The last point is set exactly to `stop` so that no
/// rounding error can push the grid past the data range.
[[nodiscard]] std::vector<double> linspace(double start, double stop, std::size_t count);

/// Equivalent of `numpy.arange(start, stop, step)` for positive `step`: the
/// number of elements is ceil((stop - start) / step) and `stop` is excluded.
[[nodiscard]] std::vector<double> arange(double start, double stop, double step);

/// Equivalent of `numpy.interp`: piecewise linear interpolation of the samples
/// (xp, fp) evaluated at x. Values outside [xp.front(), xp.back()] are clamped
/// to the corresponding endpoint rather than extrapolated.
///
/// `xp` must be non-empty and non-decreasing. Complexity O(n log m) using a
/// binary search per query, which dominates only for very large query sets.
[[nodiscard]] std::vector<double> interpolateLinear(std::span<const double> x,
                                                    std::span<const double> xp,
                                                    std::span<const double> fp);

/// Scalar form of interpolateLinear.
[[nodiscard]] double interpolateLinearAt(double x, std::span<const double> xp,
                                         std::span<const double> fp);

/// Equivalent of `numpy.gradient(values, positions)` with the default
/// `edge_order = 1`: second-order accurate central differences in the interior
/// (correct for a non-uniform grid) and one-sided first-order differences at
/// the two ends.
///
/// Formula used in the interior, with hs = x[i] - x[i-1] and hd = x[i+1] - x[i]:
///
///     f'(x_i) = (hs^2*f[i+1] + (hd^2 - hs^2)*f[i] - hd^2*f[i-1]) / (hs*hd*(hd + hs))
///
/// which reduces to the familiar (f[i+1] - f[i-1]) / (2h) on a uniform grid.
[[nodiscard]] std::vector<double> gradient(std::span<const double> values,
                                           std::span<const double> positions);

/// Equivalent of `numpy.median`: for an even number of elements the mean of
/// the two central values. Operates on a copy, so the input is untouched.
[[nodiscard]] double median(std::span<const double> values);

/// Mean of the differences between consecutive elements. Equivalent to
/// `numpy.mean(numpy.diff(values))`; returns 0 for fewer than two elements.
[[nodiscard]] double meanSpacing(std::span<const double> values);

/// Median of the differences between consecutive elements, i.e.
/// `numpy.median(numpy.diff(values))`.
[[nodiscard]] double medianSpacing(std::span<const double> values);

/// Distance to the k-th nearest element of `sorted` from `centre`, where k is
/// zero-based. Equivalent to `numpy.partition(|centre - values|, k)[k]` but in
/// O(n) without materialising the distance matrix that the array formulation
/// required, which is what makes the LOESS estimator cheap here.
[[nodiscard]] double kthNearestDistance(double centre, std::span<const double> sorted,
                                        std::size_t k);

/// Groups observations that share exactly the same x value, averaging their y
/// and recording the multiplicity. Equivalent to the combination of
/// `numpy.unique(..., return_inverse=True, return_counts=True)` and
/// `numpy.add.at` used previously.
///
/// Averaging duplicates prevents a day with many weighings from dominating a
/// fit, while `counts` preserves the information so the estimators can still
/// weight that day appropriately.
///
/// `x` must be sorted ascending; `x` and `y` must have the same length.
[[nodiscard]] AggregatedSamples aggregateByX(std::span<const double> x,
                                             std::span<const double> y);

/// Coefficient of determination of a fitted curve against the raw samples.
///
///     R^2 = 1 - SS_res / SS_tot,  SS_res = sum (y_i - f(x_i))^2,
///                                 SS_tot = sum (y_i - mean(y))^2
///
/// The fit is sampled at the observation abscissae by linear interpolation of
/// the dense curve. When every observation carries the same value SS_tot is
/// zero, the ratio is undefined, and 1.0 is returned by convention: a constant
/// model reproduces constant data exactly.
[[nodiscard]] double coefficientOfDetermination(std::span<const double> x,
                                                std::span<const double> y,
                                                std::span<const double> curveX,
                                                std::span<const double> curveY);

/// True when every element is finite (neither NaN nor infinite).
[[nodiscard]] bool allFinite(std::span<const double> values);

}  // namespace weight::math
