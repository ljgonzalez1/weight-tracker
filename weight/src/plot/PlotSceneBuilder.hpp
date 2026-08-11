#pragma once

#include <span>

#include <QDate>

#include "settings/Settings.hpp"
#include "math/common/TrendService.hpp"
#include "plot/PlotScene.hpp"

namespace weight::plot {

/// Everything needed to lay out one chart.
struct PlotInput {
    std::span<const math::SamplePoint> samples;
    const math::TrendResults* trends = nullptr;
    const settings::Settings* settings = nullptr;
    const settings::RenderOptions* options = nullptr;

    /// Image size actually being produced. The preview and the exported PNG
    /// share one code path and differ only in these three numbers.
    int widthPixels = 0;
    int heightPixels = 0;
    int dotsPerInch = 0;
};

/// Turns measurements and fitted curves into a drawable PlotScene.
///
/// The builder owns every layout decision: where the axes sit, how data
/// coordinates map to pixels, which ticks appear, how large the fonts are at
/// this resolution, and in what order the layers stack. It performs no
/// numerical fitting of its own and touches no painting API.
///
/// Sizes given in the configuration are typographic points and are converted
/// here using the target DPI, so a preview at 100 DPI and an export at 200 DPI
/// are the same picture at two resolutions rather than two different pictures.
class PlotSceneBuilder {
public:
    [[nodiscard]] static PlotScene build(const PlotInput& input);

    /// Converts a length in typographic points to device pixels.
    [[nodiscard]] static double pointsToPixels(double points, int dotsPerInch) {
        return points * static_cast<double>(dotsPerInch) / 72.0;
    }

    /// Tick positions from `minimum` to `maximum` at the given step, generated
    /// on the multiples of the step rather than by accumulation, so long axes
    /// do not drift.
    [[nodiscard]] static std::vector<double> tickPositions(double minimum, double maximum,
                                                           double step);
};

}  // namespace weight::plot
