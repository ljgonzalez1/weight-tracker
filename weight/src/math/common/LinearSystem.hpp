#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace weight::math {

/// Dense square matrix in row-major order, sized once at construction.
/// Deliberately minimal: the only linear algebra this program performs is the
/// regularised normal-equation solve of the radial basis fit, so pulling in a
/// full matrix library would add a heavy dependency for a single call site.
class DenseMatrix {
public:
    DenseMatrix() = default;
    explicit DenseMatrix(std::size_t order) : order_(order), data_(order * order, 0.0) {}

    [[nodiscard]] std::size_t order() const noexcept { return order_; }

    double& operator()(std::size_t row, std::size_t column) noexcept {
        return data_[row * order_ + column];
    }
    const double& operator()(std::size_t row, std::size_t column) const noexcept {
        return data_[row * order_ + column];
    }

    [[nodiscard]] double trace() const noexcept;

    /// Adds `value` to every diagonal entry (Tikhonov regularisation).
    void addToDiagonal(double value) noexcept;

private:
    std::size_t order_ = 0;
    std::vector<double> data_;
};

/// Solves A x = b for a symmetric positive definite A using a Cholesky
/// factorisation A = L L^T.
///
/// Cholesky is chosen over a general LU because the matrix here is a Gram
/// matrix plus a positive multiple of the identity, hence symmetric and
/// positive definite by construction. It needs half the operations of LU
/// (n^3/6 versus n^3/3), requires no pivoting to stay numerically stable, and
/// its failure to complete is itself a useful signal that the regularisation
/// was too weak for the conditioning of the problem.
///
/// Returns std::nullopt when a non-positive pivot is encountered, which under
/// exact arithmetic cannot happen and in practice means the matrix has lost
/// positive definiteness to rounding.
[[nodiscard]] std::optional<std::vector<double>> solveSymmetricPositiveDefinite(
    const DenseMatrix& matrix, std::span<const double> rhs);

/// Solves A x = b by Gaussian elimination with partial pivoting.
/// Used as the fallback when the Cholesky factorisation does not complete;
/// partial pivoting keeps the growth factor bounded in practice and handles
/// matrices that are merely symmetric rather than positive definite.
///
/// Returns std::nullopt when the matrix is numerically singular.
[[nodiscard]] std::optional<std::vector<double>> solveGeneral(DenseMatrix matrix,
                                                              std::vector<double> rhs);

}  // namespace weight::math
