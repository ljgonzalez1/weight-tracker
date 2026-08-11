#include "math/common/CurveFactory.hpp"

#include "settings/CurveCatalog.hpp"

namespace weight::math {

std::unique_ptr<TrendEstimator> createCurveEstimator(std::size_t index) {
    const std::span<const settings::CurveDescriptor> catalogue = settings::curveCatalogue();
    if (index >= catalogue.size()) {
        return nullptr;
    }
    const settings::CurveDescriptor& descriptor = catalogue[index];
    return descriptor.factory ? descriptor.factory() : nullptr;
}

std::size_t registeredCurveCount() { return settings::curveCount(); }

}  // namespace weight::math
