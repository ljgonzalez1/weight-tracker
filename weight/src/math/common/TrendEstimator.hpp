#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "math/common/NumericArray.hpp"
#include "math/common/TrendParameters.hpp"

namespace weight::math {

/// One recorded measurement: a fractional day and a mass in kilograms.
struct SamplePoint {
    double day = 0.0;
    double mass = 0.0;
};

/// A fitted trend, evaluated on the dense grid shared by every estimator.
struct TrendCurve {
    std::vector<double> x;
    std::vector<double> y;

    /// Coefficient of determination against the raw (un-aggregated) samples.
    double coefficientOfDetermination = 0.0;

    /// One-line summary of the fit for the log: bandwidths, penalties, window
    /// sizes. Free-form text, never parsed.
    std::string diagnostics;
};

/// Numerical derivative of a trend, expressed in kilograms per week.
struct DerivativeCurve {
    std::vector<double> x;
    std::vector<double> y;
};

/// Result of the preprocessing every estimator shares.
///
/// Repeated measurements on the same day are collapsed to their mean so that a
/// day with six weighings does not outweigh a day with one; the multiplicity
/// survives in `aggregated.counts` so estimators can still give that day the
/// weight it deserves. The dense grid is common to all three methods, which is
/// what makes their curves and their derivatives directly comparable.
struct PreparedSamples {
    std::vector<double> rawX;  ///< All samples, sorted by day.
    std::vector<double> rawY;

    AggregatedSamples aggregated;  ///< Unique days, mean mass, multiplicity.

    std::vector<double> grid;  ///< Uniform evaluation grid over the data range.
    double gridSpacing = 0.0;  ///< Mean distance between consecutive grid nodes.

    double span = 0.0;           ///< Last day minus first day.
    double medianSpacing = 0.0;  ///< Typical gap between measurement days.

    [[nodiscard]] std::size_t uniqueCount() const noexcept { return aggregated.x.size(); }
};

/// Runs the shared preprocessing. Returns std::nullopt when there is not
/// enough data to attempt any fit: fewer samples than required, fewer than two
/// distinct days, or a degenerate (non-finite, zero-width) day range.
[[nodiscard]] std::optional<PreparedSamples> prepareSamples(std::span<const SamplePoint> samples,
                                                            const TrendParameters& parameters);

/// Abstract nonparametric trend estimator.
///
/// The three concrete estimators are interchangeable Strategies: they accept
/// the same samples, honour the same smoothness factor with the same monotone
/// meaning (smaller means closer to the data), and return a curve on the same
/// grid. Callers select one by catalogue index and never branch on the concrete
/// type.
///
/// `estimate` is a Template Method: it fixes the pipeline (preprocess, fit,
/// score, package) so that every estimator is preprocessed identically and
/// scored identically, and subclasses supply only the step that actually
/// differs between them.
class TrendEstimator {
public:
    TrendEstimator() = default;
    virtual ~TrendEstimator() = default;

    TrendEstimator(const TrendEstimator&) = delete;
    TrendEstimator& operator=(const TrendEstimator&) = delete;
    TrendEstimator(TrendEstimator&&) = delete;
    TrendEstimator& operator=(TrendEstimator&&) = delete;

    /// Fits the trend. Returns std::nullopt when the data cannot support a fit.
    [[nodiscard]] std::optional<TrendCurve> estimate(std::span<const SamplePoint> samples,
                                                     const TrendParameters& parameters) const;

    /// Fits from already preprocessed data, so that several estimators can
    /// share one preprocessing pass.
    [[nodiscard]] TrendCurve estimatePrepared(const PreparedSamples& prepared,
                                              const TrendParameters& parameters) const;

protected:
    /// Values of the fitted curve at every node of `prepared.grid`, plus a
    /// diagnostics line. This is the only step that differs between methods.
    struct FitOutcome {
        std::vector<double> values;
        std::string diagnostics;
    };

    [[nodiscard]] virtual FitOutcome fit(const PreparedSamples& prepared,
                                         const TrendParameters& parameters) const = 0;
};

/// Numerical derivative of any trend curve, in kilograms per week.
///
/// The dense curve is resampled on a coarser uniform grid (default: one node
/// every half day) before differentiating. Resampling first is deliberate:
/// differentiating the very fine grid directly would amplify the small
/// numerical ripple left by the smoothing operators, whereas the coarser step
/// acts as a mild low-pass filter. Because all three curves are smooth by
/// construction, central differences are accurate and stable here.
///
/// The daily slope is multiplied by `daysPerDisplayedPeriod` so the reader
/// sees a rate in kilograms per week, which is the unit people actually reason
/// about when tracking body mass.
///
/// Returns std::nullopt when the curve has fewer than two nodes or a
/// degenerate abscissa range.
[[nodiscard]] std::optional<DerivativeCurve> differentiate(const TrendCurve& curve,
                                                           const DerivativeParameters& parameters);

}  // namespace weight::math
