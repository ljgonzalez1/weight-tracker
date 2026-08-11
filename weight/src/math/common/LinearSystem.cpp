#include "math/common/LinearSystem.hpp"

#include <cmath>
#include <limits>

namespace weight::math {

double DenseMatrix::trace() const noexcept {
    double sum = 0.0;
    for (std::size_t i = 0; i < order_; ++i) {
        sum += (*this)(i, i);
    }
    return sum;
}

void DenseMatrix::addToDiagonal(double value) noexcept {
    for (std::size_t i = 0; i < order_; ++i) {
        (*this)(i, i) += value;
    }
}

std::optional<std::vector<double>> solveSymmetricPositiveDefinite(const DenseMatrix& matrix,
                                                                  std::span<const double> rhs) {
    const std::size_t n = matrix.order();
    if (n == 0 || rhs.size() != n) {
        return std::nullopt;
    }

    // Lower triangular Cholesky factor, computed in place in `lower`.
    std::vector<double> lower(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double sum = matrix(i, j);
            for (std::size_t k = 0; k < j; ++k) {
                sum -= lower[i * n + k] * lower[j * n + k];
            }
            if (i == j) {
                if (!(sum > 0.0) || !std::isfinite(sum)) {
                    return std::nullopt;  // not positive definite
                }
                lower[i * n + j] = std::sqrt(sum);
            } else {
                const double pivot = lower[j * n + j];
                if (pivot == 0.0) {
                    return std::nullopt;
                }
                lower[i * n + j] = sum / pivot;
            }
        }
    }

    // Forward substitution L y = b.
    std::vector<double> y(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        double sum = rhs[i];
        for (std::size_t k = 0; k < i; ++k) {
            sum -= lower[i * n + k] * y[k];
        }
        y[i] = sum / lower[i * n + i];
    }

    // Back substitution L^T x = y.
    std::vector<double> x(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double sum = y[i];
        for (std::size_t k = i + 1; k < n; ++k) {
            sum -= lower[k * n + i] * x[k];
        }
        x[i] = sum / lower[i * n + i];
    }

    for (const double value : x) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
    }
    return x;
}

std::optional<std::vector<double>> solveGeneral(DenseMatrix matrix, std::vector<double> rhs) {
    const std::size_t n = matrix.order();
    if (n == 0 || rhs.size() != n) {
        return std::nullopt;
    }

    for (std::size_t column = 0; column < n; ++column) {
        // Partial pivoting: move the row with the largest magnitude pivot up.
        std::size_t pivotRow = column;
        double pivotMagnitude = std::abs(matrix(column, column));
        for (std::size_t row = column + 1; row < n; ++row) {
            const double magnitude = std::abs(matrix(row, column));
            if (magnitude > pivotMagnitude) {
                pivotMagnitude = magnitude;
                pivotRow = row;
            }
        }
        if (!(pivotMagnitude > 0.0)) {
            return std::nullopt;  // singular
        }
        if (pivotRow != column) {
            for (std::size_t k = 0; k < n; ++k) {
                std::swap(matrix(column, k), matrix(pivotRow, k));
            }
            std::swap(rhs[column], rhs[pivotRow]);
        }

        const double pivot = matrix(column, column);
        for (std::size_t row = column + 1; row < n; ++row) {
            const double factor = matrix(row, column) / pivot;
            if (factor == 0.0) {
                continue;
            }
            for (std::size_t k = column; k < n; ++k) {
                matrix(row, k) -= factor * matrix(column, k);
            }
            rhs[row] -= factor * rhs[column];
        }
    }

    std::vector<double> x(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double sum = rhs[i];
        for (std::size_t k = i + 1; k < n; ++k) {
            sum -= matrix(i, k) * x[k];
        }
        const double diagonal = matrix(i, i);
        if (diagonal == 0.0) {
            return std::nullopt;
        }
        x[i] = sum / diagonal;
    }

    for (const double value : x) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
    }
    return x;
}

}  // namespace weight::math
