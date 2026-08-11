#include "settings/CurveCatalog.hpp"

#include "math/adaptive_spline/AdaptiveSplineCurve.hpp"
#include "math/local_loess/LocalLoessCurve.hpp"
#include "math/multiquadric_rbf/MultiquadricRbfCurve.hpp"

namespace weight::settings {
namespace {

// ===========================================================================
// THE CURVE ARRAY
//
// This is the whole registry. To add a curve, append one entry here and add
// its folder to WEIGHT_CURVE_DIRS in CMakeLists.txt. Nothing else in the
// program needs to change: the UI builds its checkboxes from this list, the
// renderer derives every colour from it, the settings store keys off the ids,
// and the thread pool sizes itself from its length.
//
// Colours are the ones the previous releases used, restated as explicit
// r, g, b components.
// ===========================================================================
const std::vector<CurveDescriptor>& catalogue() {
    static const std::vector<CurveDescriptor> entries = {
        CurveDescriptor{
            /* id                         */ QStringLiteral("adaptive_spline"),
            /* letter                     */ QLatin1Char('A'),
            /* shortNameKey               */ QStringLiteral("curve.a.short"),
            /* longNameKey                */ QStringLiteral("curve.a.long"),
            /* colour  (was #d62728)      */ Rgb{214, 39, 40},
            /* visibleByDefault           */ false,
            /* derivativeVisibleByDefault */ false,
            /* factory                    */
            [] { return std::unique_ptr<math::TrendEstimator>(
                     std::make_unique<math::AdaptiveSplineCurve>()); }},

        CurveDescriptor{
            QStringLiteral("multiquadric_rbf"),
            QLatin1Char('B'),
            QStringLiteral("curve.b.short"),
            QStringLiteral("curve.b.long"),
            /* was #1a1a1a */ Rgb{26, 26, 26},
            /* the default curve */ true,
            false,
            [] { return std::unique_ptr<math::TrendEstimator>(
                     std::make_unique<math::MultiquadricRbfCurve>()); }},

        CurveDescriptor{
            QStringLiteral("local_loess"),
            QLatin1Char('C'),
            QStringLiteral("curve.c.short"),
            QStringLiteral("curve.c.long"),
            /* was #1f77b4 */ Rgb{31, 119, 180},
            false,
            false,
            [] { return std::unique_ptr<math::TrendEstimator>(
                     std::make_unique<math::LocalLoessCurve>()); }},
    };
    return entries;
}

}  // namespace

std::span<const CurveDescriptor> curveCatalogue() { return catalogue(); }

std::size_t curveCount() { return catalogue().size(); }

const CurveDescriptor* findCurve(const QString& id) {
    for (const CurveDescriptor& entry : catalogue()) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

std::optional<std::size_t> curveIndex(const QString& id) {
    const std::vector<CurveDescriptor>& entries = catalogue();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

}  // namespace weight::settings
