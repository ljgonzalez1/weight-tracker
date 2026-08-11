#pragma once

#include <optional>
#include <span>
#include <vector>

#include "math/common/TrendEstimator.hpp"

namespace weight::math {

/// Fitted curves indexed by position in the curve catalogue.
struct TrendResults {
    std::vector<std::optional<TrendCurve>> curves;
    std::vector<std::optional<DerivativeCurve>> derivatives;

    [[nodiscard]] const std::optional<TrendCurve>& curve(std::size_t i) const {
        return curves.at(i);
    }
    [[nodiscard]] const std::optional<DerivativeCurve>& derivative(std::size_t i) const {
        return derivatives.at(i);
    }
    [[nodiscard]] std::optional<double> coefficient(std::size_t i) const {
        const auto& fitted = curves.at(i);
        return fitted.has_value() ? std::optional<double>(fitted->coefficientOfDetermination)
                                  : std::nullopt;
    }
    [[nodiscard]] std::size_t size() const noexcept { return curves.size(); }
};

/// Which curves to fit, and whether each needs its rate of change.
struct TrendRequest {
    std::vector<bool> curveWanted;
    std::vector<bool> derivativeWanted;

    explicit TrendRequest(std::size_t count)
        : curveWanted(count, false), derivativeWanted(count, false) {}

    [[nodiscard]] bool anyWanted() const;

    /// Curves that will actually be computed; the number of tasks dispatched.
    [[nodiscard]] std::size_t activeCount() const;
};

/// Fits every requested curve concurrently, one task per curve.
///
/// The shared preprocessing — sort, aggregate duplicate days, build the dense
/// grid — runs once before any task starts, because every curve needs the
/// identical result. Duplicating it would be slower and would risk the curves
/// disagreeing about their own abscissae.
///
/// Each curve is then fitted on its own task, and its derivative is taken
/// there too, since the derivative depends on nothing but that curve. The
/// estimators hold no mutable state and read only the immutable prepared
/// samples, so the tasks share no writable memory and need no locking: each
/// writes to its own slot in a pre-sized vector.
///
/// Curves that are not requested get no task at all, so switching one off
/// removes its cost rather than hiding it.
[[nodiscard]] TrendResults computeTrends(std::span<const SamplePoint> samples,
                                         const TrendRequest& request,
                                         const TrendParameters& parameters);

/// Same computation on the calling thread. The test suite asserts that this
/// and the parallel version produce bit-identical results, which is what shows
/// the concurrency introduced no race and no reordering.
[[nodiscard]] TrendResults computeTrendsSerial(std::span<const SamplePoint> samples,
                                               const TrendRequest& request,
                                               const TrendParameters& parameters);

}  // namespace weight::math
