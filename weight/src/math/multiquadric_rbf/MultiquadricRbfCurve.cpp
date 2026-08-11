#include "math/multiquadric_rbf/MultiquadricRbfCurve.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>

#include "math/common/LinearSystem.hpp"

namespace weight::math {
namespace {

/// phi(r) = sqrt(r^2 + eps^2), evaluated from the signed distance.
///
/// Computing it from the squared distance directly avoids the cancellation
/// that abs(r) followed by squaring would introduce for very small r.
inline double multiquadric(double distance, double shapeSquared) {
    return std::sqrt(distance * distance + shapeSquared);
}

}  // namespace

std::vector<double> MultiquadricRbfCurve::selectCentres(const PreparedSamples& prepared,
                                                            const TrendParameters& parameters) {
    const std::vector<double>& nodes = prepared.aggregated.x;
    const std::size_t maximum = std::max<std::size_t>(parameters.rbfB.maximumCentres, 2);
    if (nodes.size() <= maximum) {
        return nodes;
    }

    // Uniform thinning in index space: keeps the centres spread over the whole
    // history rather than clustering them where measurements happen to be dense.
    const std::vector<double> positions =
        linspace(0.0, static_cast<double>(nodes.size() - 1), maximum);

    std::vector<double> centres;
    centres.reserve(maximum);
    std::size_t previous = std::numeric_limits<std::size_t>::max();
    for (const double position : positions) {
        const auto index = static_cast<std::size_t>(std::llround(position));
        if (index != previous && index < nodes.size()) {
            centres.push_back(nodes[index]);
            previous = index;
        }
    }
    return centres;
}

TrendEstimator::FitOutcome MultiquadricRbfCurve::fit(
    const PreparedSamples& prepared, const TrendParameters& parameters) const {
    const MultiquadricRbfParameters& rbf = parameters.rbfB;

    const std::vector<double>& nodes = prepared.aggregated.x;
    const std::vector<double>& observations = prepared.aggregated.meanY;
    const std::vector<double>& weights = prepared.aggregated.counts;

    const double smoothness =
        std::max(parameters.common.generalSmoothness, parameters.common.smoothnessEpsilon);
    const std::vector<double> centres = selectCentres(prepared, parameters);

    const std::size_t n = nodes.size();
    const std::size_t k = centres.size();

    const double shape =
        std::max(rbf.shapeMinDays, prepared.medianSpacing * rbf.shapeSpacingFactor) * smoothness;
    const double shapeSquared = shape * shape;

    // Weighted mean of the observations; the model is fitted to the residual
    // around it so that strong regularisation pulls the curve to the average.
    const double weightTotal = std::accumulate(weights.begin(), weights.end(), 0.0);
    double weightedSum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        weightedSum += weights[i] * observations[i];
    }
    const double mean = (weightTotal > 0.0) ? weightedSum / weightTotal : 0.0;

    // Design matrix Phi (n x k), stored row-major.
    std::vector<double> design(n * k, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < k; ++j) {
            design[i * k + j] = multiquadric(nodes[i] - centres[j], shapeSquared);
        }
    }

    // Normal equations: gram = Phi^T W Phi, rhs = Phi^T W (y - y_bar).
    // The Gram matrix is symmetric, so only the lower triangle is accumulated
    // and then mirrored, halving the work.
    DenseMatrix gram(k);
    std::vector<double> rhs(k, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double weight = weights[i];
        const double residual = observations[i] - mean;
        const double* row = &design[i * k];
        for (std::size_t a = 0; a < k; ++a) {
            const double weightedBasis = weight * row[a];
            rhs[a] += weightedBasis * residual;
            for (std::size_t b = 0; b <= a; ++b) {
                gram(a, b) += weightedBasis * row[b];
            }
        }
    }
    for (std::size_t a = 0; a < k; ++a) {
        for (std::size_t b = 0; b < a; ++b) {
            gram(b, a) = gram(a, b);
        }
    }

    // Ridge penalty scaled by the mean diagonal, which makes it invariant to
    // the magnitude of the basis values and hence to the units of the data.
    double diagonalScale = gram.trace() / static_cast<double>(std::max<std::size_t>(k, 1));
    if (!std::isfinite(diagonalScale) || diagonalScale <= 0.0) {
        diagonalScale = 1.0;
    }
    const double penalty =
        std::max(rbf.ridgeMinimum, rbf.ridgeBase * smoothness * smoothness) * diagonalScale;
    gram.addToDiagonal(penalty);

    std::optional<std::vector<double>> coefficients =
        solveSymmetricPositiveDefinite(gram, rhs);
    bool usedFallbackSolver = false;
    if (!coefficients.has_value()) {
        coefficients = solveGeneral(gram, rhs);
        usedFallbackSolver = true;
    }

    FitOutcome outcome;
    std::ostringstream summary;
    summary.setf(std::ios::fixed);
    summary.precision(2);

    if (!coefficients.has_value()) {
        // Both solvers refused the system. Linear interpolation of the daily
        // means is a poor trend but an honest one, and it never misleads.
        outcome.values = interpolateLinear(prepared.grid, nodes, observations);
        outcome.diagnostics = "singular system; fell back to linear interpolation";
        return outcome;
    }

    std::vector<double> values(prepared.grid.size(), 0.0);
    for (std::size_t g = 0; g < prepared.grid.size(); ++g) {
        const double t = prepared.grid[g];
        double accumulated = 0.0;
        for (std::size_t j = 0; j < k; ++j) {
            accumulated += (*coefficients)[j] * multiquadric(t - centres[j], shapeSquared);
        }
        values[g] = mean + accumulated;
    }

    if (!allFinite(values)) {
        outcome.values = interpolateLinear(prepared.grid, nodes, observations);
        outcome.diagnostics = "non-finite result; fell back to linear interpolation";
        return outcome;
    }

    summary << "eps=" << shape << " days, lambda=" << std::scientific << penalty
            << std::fixed << ", centres=" << k;
    if (usedFallbackSolver) {
        summary << " (pivoted LU fallback)";
    }
    outcome.values = std::move(values);
    outcome.diagnostics = summary.str();
    return outcome;
}

}  // namespace weight::math
