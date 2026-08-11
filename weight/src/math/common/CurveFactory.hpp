#pragma once

#include <memory>

#include "math/common/TrendEstimator.hpp"

namespace weight::math {

/// Builds the estimator registered at `index` in the curve catalogue.
///
/// The math layer deliberately does not know the catalogue's contents; it only
/// knows that positions exist and that each carries a factory. That inversion
/// is what lets a curve be added by editing one array in `settings/` without
/// this file, the renderer, the UI or the threading layer changing at all.
///
/// Returns nullptr for an index that is out of range, which is what makes a
/// stale config.txt naming a removed curve harmless.
[[nodiscard]] std::unique_ptr<TrendEstimator> createCurveEstimator(std::size_t index);

/// Number of registered curves, forwarded from the catalogue so the math layer
/// does not have to include the settings header.
[[nodiscard]] std::size_t registeredCurveCount();

}  // namespace weight::math
