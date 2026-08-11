#include "math/local_loess/LocalLoessCurve.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "math/common/GaussianKernel.hpp"

namespace weight::math {

std::size_t LocalLoessCurve::windowSize(const PreparedSamples& prepared,
                                                  const TrendParameters& parameters) {
    const LocalLinearLoessParameters& loess = parameters.loessC;
    const double smoothness =
        std::max(parameters.common.generalSmoothness, parameters.common.smoothnessEpsilon);
    const std::size_t n = prepared.uniqueCount();

    const double requested = std::clamp(loess.spanFractionBase * smoothness,
                                        loess.spanFractionMin, loess.spanFractionMax);
    const auto proposed = static_cast<std::size_t>(
        std::ceil(requested * static_cast<double>(n)));

    std::size_t window = std::max(loess.minimumWindowPoints, proposed);
    window = std::max<std::size_t>(window, 2);
    return std::min(window, n);
}

std::vector<double> LocalLoessCurve::bandwidthProfile(
    const PreparedSamples& prepared, const TrendParameters& parameters) {
    const LocalLinearLoessParameters& loess = parameters.loessC;
    const std::vector<double>& nodes = prepared.aggregated.x;

    const std::size_t window = windowSize(prepared, parameters);
    const std::size_t neighbour = std::min(window - 1, nodes.size() - 1);

    std::vector<double> bandwidth(prepared.grid.size(), 0.0);
    for (std::size_t g = 0; g < prepared.grid.size(); ++g) {
        const double distance = kthNearestDistance(prepared.grid[g], nodes, neighbour);
        bandwidth[g] = std::max(distance, loess.bandwidthMinDays);
    }

    // The k-th nearest distance jumps whenever the window swaps a neighbour;
    // smoothing the profile removes those steps so no kink reaches the curve.
    if (prepared.grid.size() >= 3 && prepared.gridSpacing > 0.0) {
        const double sigmaSamples =
            std::max(loess.bandwidthProfileMinSigmaSamples,
                     (prepared.span * loess.bandwidthSmoothFraction) / prepared.gridSpacing);
        bandwidth = gaussianSmooth(bandwidth, sigmaSamples, loess.bandwidthProfileRadiusSigmas);
        for (double& value : bandwidth) {
            value = std::max(value, loess.bandwidthMinDays);
        }
    }
    return bandwidth;
}

TrendEstimator::FitOutcome LocalLoessCurve::fit(
    const PreparedSamples& prepared, const TrendParameters& parameters) const {
    const LocalLinearLoessParameters& loess = parameters.loessC;

    const std::vector<double>& nodes = prepared.aggregated.x;
    const std::vector<double>& observations = prepared.aggregated.meanY;
    const std::vector<double>& multiplicity = prepared.aggregated.counts;

    const std::size_t window = windowSize(prepared, parameters);
    const std::vector<double> bandwidth = bandwidthProfile(prepared, parameters);

    std::vector<double> values(prepared.grid.size(), 0.0);
    for (std::size_t g = 0; g < prepared.grid.size(); ++g) {
        const double centre = prepared.grid[g];
        const double h = std::max(bandwidth[g], parameters.common.positiveEpsilon);

        double sumWeight = 0.0;
        double sumWeightedOffset = 0.0;
        double sumWeightedOffsetSquared = 0.0;
        double sumWeightedValue = 0.0;
        double sumWeightedCross = 0.0;

        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const double offset = nodes[i] - centre;
            const double normalised = offset / h;
            const double weight = std::exp(-0.5 * normalised * normalised) * multiplicity[i];

            sumWeight += weight;
            sumWeightedOffset += weight * offset;
            sumWeightedOffsetSquared += weight * offset * offset;
            sumWeightedValue += weight * observations[i];
            sumWeightedCross += weight * offset * observations[i];
        }

        if (!(sumWeight > 0.0)) {
            // Numerically no neighbour is in reach; interpolating the daily
            // means keeps the curve continuous instead of leaving a hole.
            values[g] = interpolateLinearAt(centre, nodes, observations);
            continue;
        }

        const double determinant =
            sumWeight * sumWeightedOffsetSquared - sumWeightedOffset * sumWeightedOffset;
        const double scale =
            std::max(sumWeight * sumWeightedOffsetSquared, loess.degeneracyTolerance);

        if (std::abs(determinant) <= loess.degeneracyTolerance * scale) {
            // Every weight concentrated on one abscissa: the slope is not
            // identifiable, so fall back to the locally weighted mean.
            values[g] = sumWeightedValue / sumWeight;
        } else {
            values[g] = (sumWeightedValue * sumWeightedOffsetSquared
                         - sumWeightedOffset * sumWeightedCross)
                        / determinant;
        }
    }

    FitOutcome outcome;
    outcome.values = std::move(values);

    std::ostringstream summary;
    summary.setf(std::ios::fixed);
    summary.precision(2);
    summary << "q=" << window << '/' << nodes.size() << " neighbours, h_median="
            << median(bandwidth) << " days";
    outcome.diagnostics = summary.str();
    return outcome;
}

}  // namespace weight::math
