#include "math/common/CubicSpline.hpp"

#include <algorithm>
#include <cmath>

namespace weight::math {

NaturalCubicSpline::NaturalCubicSpline(std::span<const double> x, std::span<const double> y)
    : x_(x.begin(), x.end()), y_(y.begin(), y.end()) {
    const std::size_t n = x_.size();
    if (n == 0 || n != y_.size()) {
        return;
    }
    for (std::size_t i = 1; i < n; ++i) {
        if (!(x_[i] > x_[i - 1])) {
            return;  // nodes must be strictly increasing
        }
    }

    second_.assign(n, 0.0);
    valid_ = true;
    if (n <= 2) {
        return;  // a natural spline through <= 2 nodes is the straight line
    }

    const std::size_t interior = n - 2;
    std::vector<double> h(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        h[i] = x_[i + 1] - x_[i];
    }

    // Tridiagonal system: sub-diagonal a, diagonal b, super-diagonal c, rhs d.
    std::vector<double> lower(interior);
    std::vector<double> diagonal(interior);
    std::vector<double> upper(interior);
    std::vector<double> rhs(interior);
    for (std::size_t i = 0; i < interior; ++i) {
        lower[i] = h[i];
        diagonal[i] = 2.0 * (h[i] + h[i + 1]);
        upper[i] = h[i + 1];
        rhs[i] = 6.0 * ((y_[i + 2] - y_[i + 1]) / h[i + 1] - (y_[i + 1] - y_[i]) / h[i]);
    }

    // Thomas algorithm: forward elimination then back substitution. Safe
    // without pivoting because the matrix is strictly diagonally dominant.
    //
    // Note on a behavioural difference from the Python implementation this
    // project replaces: that version indexed the sub-diagonal as a[i-1] during
    // elimination instead of a[i]. On uniformly spaced nodes h is constant and
    // the two coincide, which is why the defect went unnoticed with regular
    // daily weighings; on irregular spacing the solution no longer satisfies
    // its own system (verified residual of ~3.9 on a seven-node example, versus
    // ~1e-15 here). The indexing below is the correct one, and
    // tests/unit/test_cubic_spline.cpp asserts the residual directly so the
    // defect cannot come back. See docs/mathematics.md.
    std::vector<double> cPrime(interior, 0.0);
    std::vector<double> dPrime(interior, 0.0);
    cPrime[0] = (interior > 1) ? upper[0] / diagonal[0] : 0.0;
    dPrime[0] = rhs[0] / diagonal[0];
    for (std::size_t i = 1; i < interior; ++i) {
        const double denominator = diagonal[i] - lower[i] * cPrime[i - 1];
        if (i + 1 < interior) {
            cPrime[i] = upper[i] / denominator;
        }
        dPrime[i] = (rhs[i] - lower[i] * dPrime[i - 1]) / denominator;
    }

    std::vector<double> solution(interior, 0.0);
    solution[interior - 1] = dPrime[interior - 1];
    for (std::size_t i = interior - 1; i-- > 0;) {
        solution[i] = dPrime[i] - cPrime[i] * solution[i + 1];
    }

    for (std::size_t i = 0; i < interior; ++i) {
        second_[i + 1] = solution[i];
    }
}

double NaturalCubicSpline::evaluate(double x) const {
    if (!valid_ || x_.empty()) {
        return 0.0;
    }
    const std::size_t n = x_.size();
    if (n == 1) {
        return y_[0];
    }
    if (n == 2) {
        const double width = x_[1] - x_[0];
        const double t = std::clamp((x - x_[0]) / width, 0.0, 1.0);
        return y_[0] + t * (y_[1] - y_[0]);
    }

    // Segment whose left node is the greatest node not exceeding x.
    const auto upper = std::upper_bound(x_.begin(), x_.end(), x);
    auto index = static_cast<std::size_t>(std::distance(x_.begin(), upper));
    index = (index == 0) ? 0 : index - 1;
    index = std::min(index, n - 2);

    const double left = x_[index];
    const double right = x_[index + 1];
    const double h = right - left;
    const double a = (right - x) / h;
    const double b = (x - left) / h;

    return a * y_[index] + b * y_[index + 1]
           + ((a * a * a - a) * second_[index] + (b * b * b - b) * second_[index + 1]) * (h * h)
                 / 6.0;
}

std::vector<double> NaturalCubicSpline::evaluate(std::span<const double> grid) const {
    std::vector<double> values(grid.size());
    for (std::size_t i = 0; i < grid.size(); ++i) {
        values[i] = evaluate(grid[i]);
    }
    return values;
}

}  // namespace weight::math
