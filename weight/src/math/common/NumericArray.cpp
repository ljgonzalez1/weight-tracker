#include "math/common/NumericArray.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace weight::math {

std::vector<double> linspace(double start, double stop, std::size_t count) {
    std::vector<double> grid;
    if (count == 0) {
        return grid;
    }
    grid.resize(count);
    if (count == 1) {
        grid[0] = start;
        return grid;
    }
    const double step = (stop - start) / static_cast<double>(count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        grid[i] = start + step * static_cast<double>(i);
    }
    // Pin the endpoint: accumulated rounding must never place the last grid
    // node beyond the data range, which would extrapolate the estimators.
    grid[count - 1] = stop;
    return grid;
}

std::vector<double> arange(double start, double stop, double step) {
    std::vector<double> values;
    if (!(step > 0.0) || !std::isfinite(start) || !std::isfinite(stop)
        || !std::isfinite(step) || stop <= start) {
        return values;
    }
    const double rawCount = std::ceil((stop - start) / step);
    if (!std::isfinite(rawCount) || rawCount <= 0.0) {
        return values;
    }
    const auto count = static_cast<std::size_t>(rawCount);
    values.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        values[i] = start + step * static_cast<double>(i);
    }
    return values;
}

double interpolateLinearAt(double x, std::span<const double> xp, std::span<const double> fp) {
    if (xp.empty() || fp.empty()) {
        return 0.0;
    }
    if (xp.size() == 1) {
        return fp[0];
    }
    if (x <= xp.front()) {
        return fp.front();
    }
    if (x >= xp.back()) {
        return fp.back();
    }

    const auto upper = std::upper_bound(xp.begin(), xp.end(), x);
    const auto index = static_cast<std::size_t>(std::distance(xp.begin(), upper)) - 1;
    const double left = xp[index];
    const double right = xp[index + 1];
    const double width = right - left;
    if (width <= 0.0) {
        return fp[index];
    }
    const double t = (x - left) / width;
    return fp[index] + t * (fp[index + 1] - fp[index]);
}

std::vector<double> interpolateLinear(std::span<const double> x, std::span<const double> xp,
                                      std::span<const double> fp) {
    std::vector<double> result(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        result[i] = interpolateLinearAt(x[i], xp, fp);
    }
    return result;
}

std::vector<double> gradient(std::span<const double> values, std::span<const double> positions) {
    const std::size_t n = values.size();
    std::vector<double> derivative(n, 0.0);
    if (n < 2 || positions.size() != n) {
        return derivative;
    }

    if (n == 2) {
        const double h = positions[1] - positions[0];
        const double slope = (h != 0.0) ? (values[1] - values[0]) / h : 0.0;
        derivative[0] = slope;
        derivative[1] = slope;
        return derivative;
    }

    for (std::size_t i = 1; i + 1 < n; ++i) {
        const double hs = positions[i] - positions[i - 1];
        const double hd = positions[i + 1] - positions[i];
        const double denominator = hs * hd * (hd + hs);
        if (denominator == 0.0) {
            derivative[i] = 0.0;
            continue;
        }
        derivative[i] = (hs * hs * values[i + 1] + (hd * hd - hs * hs) * values[i]
                         - hd * hd * values[i - 1])
                        / denominator;
    }

    // edge_order = 1: one-sided first-order differences at both ends.
    const double firstStep = positions[1] - positions[0];
    derivative[0] = (firstStep != 0.0) ? (values[1] - values[0]) / firstStep : 0.0;
    const double lastStep = positions[n - 1] - positions[n - 2];
    derivative[n - 1] = (lastStep != 0.0) ? (values[n - 1] - values[n - 2]) / lastStep : 0.0;

    return derivative;
}

double median(std::span<const double> values) {
    if (values.empty()) {
        // The median of nothing is undefined. Returning zero would be a silent
        // lie that propagates into a bandwidth or a spacing; NaN forces the
        // caller to notice, and every caller here already checks for it.
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::vector<double> sorted(values.begin(), values.end());
    const std::size_t middle = sorted.size() / 2;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(middle),
                     sorted.end());
    const double upper = sorted[middle];
    if (sorted.size() % 2 == 1) {
        return upper;
    }
    const double lower = *std::max_element(sorted.begin(),
                                           sorted.begin() + static_cast<std::ptrdiff_t>(middle));
    return 0.5 * (lower + upper);
}

double meanSpacing(std::span<const double> values) {
    if (values.size() < 2) {
        return 0.0;
    }
    // The differences telescope, so the mean spacing is exactly the total span
    // divided by the number of intervals. This avoids accumulating rounding
    // error over long series.
    return (values.back() - values.front()) / static_cast<double>(values.size() - 1);
}

double medianSpacing(std::span<const double> values) {
    if (values.size() < 2) {
        return 0.0;
    }
    std::vector<double> differences(values.size() - 1);
    for (std::size_t i = 0; i + 1 < values.size(); ++i) {
        differences[i] = values[i + 1] - values[i];
    }
    return median(differences);
}

double kthNearestDistance(double centre, std::span<const double> sorted, std::size_t k) {
    const std::size_t n = sorted.size();
    if (n == 0) {
        return 0.0;
    }
    const std::size_t index = std::min(k, n - 1);

    // Expand a window outwards from the insertion point of `centre`, taking the
    // closer of the two candidates at each step. Because `sorted` is ascending,
    // the (index + 1)-th value taken this way is the (index)-th nearest.
    auto upper = std::lower_bound(sorted.begin(), sorted.end(), centre);
    auto lower = upper;
    double distance = 0.0;

    for (std::size_t taken = 0; taken <= index; ++taken) {
        const bool hasLower = lower != sorted.begin();
        const bool hasUpper = upper != sorted.end();
        if (!hasLower && !hasUpper) {
            break;
        }
        const double lowerDistance =
            hasLower ? centre - *(lower - 1) : std::numeric_limits<double>::infinity();
        const double upperDistance =
            hasUpper ? *upper - centre : std::numeric_limits<double>::infinity();

        if (lowerDistance <= upperDistance) {
            --lower;
            distance = lowerDistance;
        } else {
            ++upper;
            distance = upperDistance;
        }
    }
    return distance;
}

AggregatedSamples aggregateByX(std::span<const double> x, std::span<const double> y) {
    AggregatedSamples aggregated;
    const std::size_t n = std::min(x.size(), y.size());
    if (n == 0) {
        return aggregated;
    }

    // Grouping works on adjacent equal abscissae, so the input must be ordered.
    // The NumPy routine this replaces sorts unconditionally; sorting only when
    // the data is not already ordered keeps the common path (callers that have
    // just sorted) free of the cost while making the function safe to call with
    // anything.
    if (!std::is_sorted(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(n))) {
        std::vector<std::size_t> order(n);
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(),
                         [x](std::size_t a, std::size_t b) { return x[a] < x[b]; });

        std::vector<double> sortedX(n);
        std::vector<double> sortedY(n);
        for (std::size_t i = 0; i < n; ++i) {
            sortedX[i] = x[order[i]];
            sortedY[i] = y[order[i]];
        }
        return aggregateByX(sortedX, sortedY);
    }

    aggregated.x.reserve(n);
    aggregated.meanY.reserve(n);
    aggregated.counts.reserve(n);

    std::size_t start = 0;
    while (start < n) {
        std::size_t end = start + 1;
        while (end < n && x[end] == x[start]) {
            ++end;
        }
        double sum = 0.0;
        for (std::size_t i = start; i < end; ++i) {
            sum += y[i];
        }
        const auto count = static_cast<double>(end - start);
        aggregated.x.push_back(x[start]);
        aggregated.meanY.push_back(sum / count);
        aggregated.counts.push_back(count);
        start = end;
    }
    return aggregated;
}

double coefficientOfDetermination(std::span<const double> x, std::span<const double> y,
                                  std::span<const double> curveX,
                                  std::span<const double> curveY) {
    if (x.empty() || y.empty() || curveX.empty() || curveY.empty()) {
        return 0.0;
    }
    const double mean = std::accumulate(y.begin(), y.end(), 0.0) / static_cast<double>(y.size());

    double residualSum = 0.0;
    double totalSum = 0.0;
    const std::size_t n = std::min(x.size(), y.size());
    for (std::size_t i = 0; i < n; ++i) {
        const double fitted = interpolateLinearAt(x[i], curveX, curveY);
        const double residual = y[i] - fitted;
        residualSum += residual * residual;
        const double deviation = y[i] - mean;
        totalSum += deviation * deviation;
    }
    if (totalSum <= 0.0) {
        return 1.0;
    }
    return 1.0 - (residualSum / totalSum);
}

bool allFinite(std::span<const double> values) {
    return std::all_of(values.begin(), values.end(),
                       [](double value) { return std::isfinite(value); });
}

}  // namespace weight::math
