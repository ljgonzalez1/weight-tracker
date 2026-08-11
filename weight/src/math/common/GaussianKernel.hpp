#pragma once

#include <span>
#include <vector>

namespace weight::math {

/// Discrete Gaussian smoothing on a uniformly sampled series.
///
/// Method
/// ------
/// A normalised Gaussian kernel is built on the integer sample lattice,
///
///     k[j] = exp(-j^2 / (2 sigma^2)) / Z,   j = -R .. R,   Z = sum_j exp(...)
///
/// with sigma expressed *in samples* rather than in days, and the truncation
/// radius R = ceil(sigma * radiusSigmas). Truncating at four sigmas discards
/// less than 0.01 % of the kernel mass; the explicit renormalisation by Z
/// makes the operator exactly mass preserving regardless of R, so a constant
/// series is reproduced exactly and no bias is introduced.
///
/// The series is extended by edge replication before convolution. That is the
/// standard choice for a signal that is not periodic and has no meaningful
/// behaviour outside its support: it keeps the smoothed value at the boundary
/// close to the observed one instead of pulling it towards zero.
///
/// Complexity O(n * R). For the grid sizes used here (a few thousand nodes)
/// this is far cheaper than an FFT-based convolution and avoids its numerical
/// noise floor.
///
/// The kernel is symmetric, so convolution and correlation coincide; there is
/// no phase shift and the smoothed curve stays aligned with the data.
class GaussianSmoother {
public:
    /// Builds the discrete kernel. `sigmaSamples` is clamped to a tiny
    /// positive value and `radiusSigmas` to at least 1, so the kernel always
    /// has at least three taps and the operation is always well defined.
    GaussianSmoother(double sigmaSamples, double radiusSigmas);

    [[nodiscard]] const std::vector<double>& kernel() const noexcept { return kernel_; }
    [[nodiscard]] std::size_t radius() const noexcept { return radius_; }

    /// Applies the smoother. Series shorter than three samples are returned
    /// unchanged: there is nothing meaningful to smooth.
    [[nodiscard]] std::vector<double> apply(std::span<const double> values) const;

private:
    std::vector<double> kernel_;
    std::size_t radius_ = 0;
};

/// Convenience wrapper equivalent to `GaussianSmoother(sigma, radius).apply(v)`.
[[nodiscard]] std::vector<double> gaussianSmooth(std::span<const double> values,
                                                 double sigmaSamples, double radiusSigmas);

}  // namespace weight::math
