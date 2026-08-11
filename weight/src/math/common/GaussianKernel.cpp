#include "math/common/GaussianKernel.hpp"

#include <algorithm>
#include <cmath>

namespace weight::math {
namespace {

/// Smallest sigma that still yields a usable kernel; below it the Gaussian
/// degenerates into a single tap and smoothing becomes the identity.
constexpr double kMinimumSigma = 1e-9;
constexpr double kMinimumRadiusSigmas = 1.0;

}  // namespace

GaussianSmoother::GaussianSmoother(double sigmaSamples, double radiusSigmas) {
    const double sigma = std::max(sigmaSamples, kMinimumSigma);
    const double sigmas = std::max(radiusSigmas, kMinimumRadiusSigmas);

    const double rawRadius = std::ceil(sigma * sigmas);
    radius_ = static_cast<std::size_t>(std::max(1.0, std::min(rawRadius, 1e6)));

    kernel_.resize(2 * radius_ + 1);
    double sum = 0.0;
    for (std::size_t i = 0; i < kernel_.size(); ++i) {
        const double offset = static_cast<double>(i) - static_cast<double>(radius_);
        const double normalised = offset / sigma;
        const double weight = std::exp(-0.5 * normalised * normalised);
        kernel_[i] = weight;
        sum += weight;
    }

    if (!(sum > 0.0)) {
        // Underflow for an extremely small sigma: fall back to the identity.
        std::fill(kernel_.begin(), kernel_.end(), 0.0);
        kernel_[radius_] = 1.0;
        sum = 1.0;
    }
    for (double& weight : kernel_) {
        weight /= sum;
    }
}

std::vector<double> GaussianSmoother::apply(std::span<const double> values) const {
    std::vector<double> result(values.begin(), values.end());
    if (values.size() <= 2) {
        return result;
    }

    const auto n = static_cast<std::ptrdiff_t>(values.size());
    const auto radius = static_cast<std::ptrdiff_t>(radius_);

    for (std::ptrdiff_t i = 0; i < n; ++i) {
        double accumulated = 0.0;
        for (std::ptrdiff_t j = -radius; j <= radius; ++j) {
            // Edge replication: indices outside the series clamp to its ends.
            const std::ptrdiff_t source = std::clamp(i + j, std::ptrdiff_t{0}, n - 1);
            accumulated += kernel_[static_cast<std::size_t>(j + radius)] * values[static_cast<std::size_t>(source)];
        }
        result[static_cast<std::size_t>(i)] = accumulated;
    }
    return result;
}

std::vector<double> gaussianSmooth(std::span<const double> values, double sigmaSamples,
                                   double radiusSigmas) {
    return GaussianSmoother(sigmaSamples, radiusSigmas).apply(values);
}

}  // namespace weight::math
