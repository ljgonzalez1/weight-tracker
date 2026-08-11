#pragma once

#include <span>
#include <vector>

namespace weight::math {

/// Natural cubic spline interpolant of a set of strictly increasing nodes.
///
/// Method
/// ------
/// Given nodes (x_0, y_0) ... (x_{n-1}, y_{n-1}) with x strictly increasing,
/// the interpolant S(x) is the unique piecewise cubic that
///   * passes through every node,
///   * is twice continuously differentiable (C^2) across the interior nodes,
///   * has zero second derivative at both ends (the *natural* condition).
///
/// Writing M_i = S''(x_i) and h_i = x_{i+1} - x_i, the C^2 continuity
/// conditions give a tridiagonal system in the interior second derivatives:
///
///     h_{i-1} M_{i-1} + 2 (h_{i-1} + h_i) M_i + h_i M_{i+1}
///         = 6 [ (y_{i+1} - y_i)/h_i - (y_i - y_{i-1})/h_{i-1} ]
///
/// with M_0 = M_{n-1} = 0. On a segment the interpolant is then
///
///     S(x) = a y_i + b y_{i+1} + [ (a^3 - a) M_i + (b^3 - b) M_{i+1} ] h_i^2 / 6
///     a = (x_{i+1} - x)/h_i,   b = (x - x_i)/h_i = 1 - a
///
/// Numerical properties
/// --------------------
/// The system is symmetric, tridiagonal and strictly diagonally dominant
/// (2(h_{i-1} + h_i) > h_{i-1} + h_i), so the Thomas algorithm is stable
/// without pivoting. Cost is O(n) in time and memory.
///
/// Why this method
/// ---------------
/// It provides a smooth, parameter-free base curve through the daily averages
/// before any smoothing is applied. Being an interpolant it introduces no bias
/// of its own; all of the actual smoothing is delegated to the adaptive
/// Gaussian mollifier that follows it, which keeps the two concerns separate
/// and independently testable.
///
/// Limitation
/// ----------
/// As an interpolant it reproduces measurement noise exactly, and between
/// widely separated nodes it can overshoot. Both are acceptable here because
/// the spline is never displayed on its own.
class NaturalCubicSpline {
public:
    /// Builds the spline. Requires x.size() == y.size() and strictly
    /// increasing x; use `isValid()` to check whether construction succeeded.
    NaturalCubicSpline(std::span<const double> x, std::span<const double> y);

    [[nodiscard]] bool isValid() const noexcept { return valid_; }

    /// Evaluates the spline at a single abscissa. Outside the node range the
    /// nearest end segment is used, which keeps the value continuous.
    [[nodiscard]] double evaluate(double x) const;

    /// Evaluates the spline on an arbitrary grid.
    [[nodiscard]] std::vector<double> evaluate(std::span<const double> grid) const;

    /// Second derivatives at the nodes, exposed for testing.
    [[nodiscard]] const std::vector<double>& secondDerivatives() const noexcept {
        return second_;
    }

private:
    std::vector<double> x_;
    std::vector<double> y_;
    std::vector<double> second_;
    bool valid_ = false;
};

}  // namespace weight::math
