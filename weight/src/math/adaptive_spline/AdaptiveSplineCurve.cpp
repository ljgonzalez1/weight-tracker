#include "math/adaptive_spline/AdaptiveSplineCurve.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

#include "math/common/CubicSpline.hpp"
#include "math/common/GaussianKernel.hpp"

namespace weight::math {
namespace {

/// Largest of a set of candidate scales, never below a positive floor.
///
/// Taking a maximum over "an absolute minimum", "a multiple of the typical
/// spacing" and "a fraction of the total span" is what makes the bandwidth
/// scale-free: it adapts to daily logging over one month and to weekly logging
/// over three years without any per-dataset tuning.
double largestScale(double minimum, double spacing, double spacingFactor, double span,
                    double spanFraction, double floorValue) {
    return std::max({minimum, spacing * spacingFactor, span * spanFraction, floorValue});
}

}  // namespace

double AdaptiveSplineCurve::effectiveSmoothness(const TrendParameters& parameters) {
    const double requested =
        std::max(parameters.common.generalSmoothness, parameters.common.smoothnessEpsilon);
    const double floorValue =
        std::max(parameters.splineA.smoothnessVisualFloor, parameters.common.smoothnessEpsilon);
    return std::max(requested, floorValue);
}

std::vector<double> AdaptiveSplineCurve::pilotDensity(
    const PreparedSamples& prepared, const TrendParameters& parameters) {
    const AdaptiveSplineParameters& spline = parameters.splineA;

    const double pilotSigma = largestScale(spline.bandwidthMinDays, prepared.medianSpacing,
                                           spline.pilotSpacingFactor, prepared.span,
                                           spline.pilotSpanFraction,
                                           parameters.common.positiveEpsilon);

    // Multiplicity is damped by an exponent below one: a day with many
    // weighings should count for more than a day with one, but not linearly so.
    const std::vector<double>& counts = prepared.aggregated.counts;
    std::vector<double> multiplicity(counts.size());
    for (std::size_t j = 0; j < counts.size(); ++j) {
        multiplicity[j] = std::pow(counts[j], spline.duplicateCountExponent);
    }

    const std::vector<double>& nodes = prepared.aggregated.x;
    std::vector<double> density(prepared.grid.size(), 0.0);
    for (std::size_t g = 0; g < prepared.grid.size(); ++g) {
        const double centre = prepared.grid[g];
        double accumulated = 0.0;
        for (std::size_t j = 0; j < nodes.size(); ++j) {
            const double normalised = (centre - nodes[j]) / pilotSigma;
            accumulated += std::exp(-0.5 * normalised * normalised) * multiplicity[j];
        }
        // A strictly positive floor keeps the reciprocal below well defined far
        // away from every measurement.
        density[g] = std::max(accumulated, spline.densityFloor);
    }
    return density;
}

std::vector<double> AdaptiveSplineCurve::bandwidthProfile(
    const PreparedSamples& prepared, const TrendParameters& parameters) {
    const AdaptiveSplineParameters& spline = parameters.splineA;

    const std::vector<double> density = pilotDensity(prepared, parameters);
    const double medianDensity = median(density);

    double baseBandwidth = largestScale(spline.bandwidthMinDays, prepared.medianSpacing,
                                        spline.bandwidthSpacingFactor, prepared.span,
                                        spline.bandwidthSpanFraction,
                                        parameters.common.positiveEpsilon);
    const double smoothness = effectiveSmoothness(parameters);
    baseBandwidth *= smoothness;

    std::vector<double> bandwidth(density.size(), 0.0);
    for (std::size_t g = 0; g < density.size(); ++g) {
        const double ratio = medianDensity / density[g];
        const double raw = baseBandwidth * std::pow(ratio, spline.bandwidthDensityExponent);
        bandwidth[g] = std::clamp(raw, spline.bandwidthMinDays, spline.bandwidthMaxDays);
    }

    // Smooth the profile itself. Sample density changes in steps whenever a
    // measurement day enters or leaves the pilot window, and an unsmoothed h(t)
    // would print those steps onto the final curve as visible kinks.
    if (prepared.grid.size() >= 3 && prepared.gridSpacing > 0.0) {
        const double meanBandwidth =
            std::accumulate(bandwidth.begin(), bandwidth.end(), 0.0)
            / static_cast<double>(bandwidth.size());
        double sigmaSamples = meanBandwidth * spline.profileSigmaFraction
                              * std::sqrt(smoothness) / prepared.gridSpacing;
        sigmaSamples = std::min(std::max(spline.profileMinSigmaSamples, sigmaSamples),
                                spline.profileMaxSigmaSamples);
        bandwidth = gaussianSmooth(bandwidth, sigmaSamples, spline.profileRadiusSigmas);

        for (double& value : bandwidth) {
            value = std::clamp(value, spline.bandwidthMinDays, spline.bandwidthMaxDays);
        }
    }
    return bandwidth;
}

TrendEstimator::FitOutcome AdaptiveSplineCurve::fit(
    const PreparedSamples& prepared, const TrendParameters& parameters) const {
    const AdaptiveSplineParameters& spline = parameters.splineA;
    const std::vector<double>& grid = prepared.grid;

    // Stage 1: interpolating base curve through the daily means.
    const NaturalCubicSpline base(prepared.aggregated.x, prepared.aggregated.meanY);
    std::vector<double> values = base.evaluate(grid);

    // Stage 2: mollification with the adaptive Gaussian kernel.
    const std::vector<double> bandwidth = bandwidthProfile(prepared, parameters);

    if (grid.size() > 2 && prepared.gridSpacing > 0.0) {
        const double radiusSigmas = std::max(spline.mollifierRadiusSigmas, 1.0);
        std::vector<double> mollified(grid.size(), 0.0);

        for (std::size_t i = 0; i < grid.size(); ++i) {
            const double centre = grid[i];
            const double h = std::max(bandwidth[i], parameters.common.positiveEpsilon);
            const double radius = radiusSigmas * h;

            // Only the nodes inside the truncation radius contribute; outside
            // it the kernel is below 1e-4 of its peak.
            const auto first = std::lower_bound(grid.begin(), grid.end(), centre - radius);
            const auto last = std::upper_bound(grid.begin(), grid.end(), centre + radius);
            const auto from = static_cast<std::size_t>(std::distance(grid.begin(), first));
            const auto to = static_cast<std::size_t>(std::distance(grid.begin(), last));

            double weightSum = 0.0;
            double weighted = 0.0;
            for (std::size_t k = from; k < to; ++k) {
                const double normalised = (grid[k] - centre) / h;
                const double weight = std::exp(-0.5 * normalised * normalised);
                weightSum += weight;
                weighted += weight * values[k];
            }
            mollified[i] = (weightSum <= spline.mollifierMinWeightSum)
                               ? values[i]
                               : weighted / weightSum;
        }
        values = std::move(mollified);
    }

    // Stage 3: a light uniform pass that removes the residual ripple left by
    // the varying kernel width, without measurably changing the shape.
    if (grid.size() >= 3 && prepared.gridSpacing > 0.0) {
        const double meanBandwidth =
            std::accumulate(bandwidth.begin(), bandwidth.end(), 0.0)
            / static_cast<double>(bandwidth.size());
        const double smoothness = effectiveSmoothness(parameters);
        double sigmaSamples = meanBandwidth * spline.postSmoothSigmaFraction
                              * std::pow(smoothness, spline.postSmoothSmoothnessExponent)
                              / prepared.gridSpacing;
        sigmaSamples = std::min(std::max(spline.postSmoothMinSigmaSamples, sigmaSamples),
                                spline.postSmoothMaxSigmaSamples);
        values = gaussianSmooth(values, sigmaSamples, spline.postSmoothRadiusSigmas);
    }

    FitOutcome outcome;
    outcome.values = std::move(values);

    const auto [minimum, maximum] = std::minmax_element(bandwidth.begin(), bandwidth.end());
    std::ostringstream summary;
    summary.setf(std::ios::fixed);
    summary.precision(2);
    summary << "h_min=" << *minimum << " days, h_max=" << *maximum << " days";
    outcome.diagnostics = summary.str();
    return outcome;
}

}  // namespace weight::math
