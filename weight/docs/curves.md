# Adding a trend curve

A curve is added in **four steps**, and nothing else in the program changes.
Everything under [What you get for free](#what-you-get-for-free) happens on its
own: the checkbox, the colour, the R² entry, the thread, the saved setting.

This page is a complete worked example rather than a summary. The curve built
here — a forward-backward exponential trend — was written against this
checklist, compiled, and run through the whole suite; the output quoted at the
end is its real output. If you follow it and the curve does not appear, the
last section lists the four things that cause that.

| Step | File | What it is |
|---|---|---|
| 1 | `src/math/<your_curve>/YourCurve.{hpp,cpp}` | the estimator |
| 2 | `src/math/common/TrendParameters.hpp` | its numerical constants |
| 3 | `settings/CurveCatalog.cpp` and `settings/Strings.cpp` | registration and names |
| 4 | `CMakeLists.txt` | the folder in the build |

---

## Step 1 — Write the estimator

`src/math/exponential_trend/ExponentialTrendCurve.hpp`:

```cpp
#pragma once

#include "math/common/TrendEstimator.hpp"

namespace weight::math {

/// Curve (D) — "Forward-backward exponential trend".
///
/// Each value is an exponentially weighted average of the record, the weight
/// of a measurement halving every `halfLife` days. The filter runs forwards
/// and then backwards over its own output, which cancels the lag a single
/// pass leaves behind.
class ExponentialTrendCurve final : public TrendEstimator {
public:
    /// Smoothing factor alpha actually used, given the data and the slider.
    /// Public because a test that cannot see alpha cannot check that the
    /// slider moves it in the right direction.
    [[nodiscard]] static double smoothingFactor(const PreparedSamples& prepared,
                                                const TrendParameters& parameters);

protected:
    [[nodiscard]] FitOutcome fit(const PreparedSamples& prepared,
                                 const TrendParameters& parameters) const override;
};

}  // namespace weight::math
```

`fit` is the only thing you must supply. `TrendEstimator::estimate` is a
Template Method that fixes the pipeline around it — preprocess, fit, score,
package — so your curve is preprocessed and scored exactly like the others.
That is what makes the R² figures comparable between curves; a curve doing its
own preprocessing would be scoring itself against different data.

### What you are given

`prepared` is a `PreparedSamples`, and the shared preprocessing has already run:

| Field | What it holds |
|---|---|
| `aggregated.x` | the **distinct** measurement days, ascending |
| `aggregated.meanY` | the mean mass on each of those days |
| `aggregated.counts` | how many measurements that mean came from |
| `rawX`, `rawY` | every sample, sorted, un-aggregated |
| `grid` | the dense evaluation grid, shared by every curve |
| `gridSpacing` | mean distance between grid nodes, in days |
| `span` | last day minus first day |
| `medianSpacing` | the typical gap between measurement days |

Days with several weighings are collapsed to their mean, so a day with six
readings does not outweigh a day with one. The multiplicity survives in
`counts`, so you can still give that day the weight it earns —
`LocalLoessCurve` uses it as a frequency weight, and yours may want to.

### What you must return

`FitOutcome`: **one value per node of `prepared.grid`**, in grid order, plus a
one-line diagnostic for the log. Returning a different number of values is the
commonest way to get a curve that compiles and then draws nothing.

Helpers available, all declared under `math/common/`:

| Helper | Header |
|---|---|
| `interpolateLinearAt(x, xs, ys)` | `NumericArray.hpp` |
| `median(values)` | `NumericArray.hpp` |
| `gaussianSmooth(values, sigmaSamples, radiusSigmas)` | `GaussianKernel.hpp` |
| `kthNearestDistance(centre, nodes, k)` | `GaussianKernel.hpp` |
| `CubicSpline` | `CubicSpline.hpp` |
| the linear solver | `LinearSystem.hpp` |

Now `src/math/exponential_trend/ExponentialTrendCurve.cpp`:

```cpp
#include "math/exponential_trend/ExponentialTrendCurve.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace weight::math {

double ExponentialTrendCurve::smoothingFactor(const PreparedSamples& prepared,
                                              const TrendParameters& parameters) {
    const ExponentialTrendParameters& own = parameters.exponentialD;
    const double smoothness =
        std::max(parameters.common.generalSmoothness, parameters.common.smoothnessEpsilon);

    // The half-life is what the slider controls, in days, because that is the
    // quantity with a meaning a reader can hold: "this curve has forgotten
    // half of what it knew a fortnight ago". Larger s means a longer memory
    // and a smoother curve, which is the monotone direction every estimator
    // here has to agree on.
    const double halfLife = std::clamp(own.halfLifeBaseDays * smoothness,
                                       own.halfLifeMinDays, own.halfLifeMaxDays);

    // alpha is derived from the half-life and the grid step, never stated
    // directly, so the curve has the same shape whether the grid is fine or
    // coarse.
    const double step = std::max(prepared.gridSpacing, parameters.common.positiveEpsilon);
    return std::clamp(1.0 - std::exp(-std::log(2.0) * step / halfLife), 1e-9, 1.0);
}

TrendEstimator::FitOutcome ExponentialTrendCurve::fit(
    const PreparedSamples& prepared, const TrendParameters& parameters) const {
    const std::vector<double>& nodes = prepared.aggregated.x;
    const std::vector<double>& observations = prepared.aggregated.meanY;

    const double alpha = smoothingFactor(prepared, parameters);
    const std::size_t gridSize = prepared.grid.size();

    // Step 1 — put the daily means on the shared grid, so the recursion runs
    // at a constant step and the result is comparable with the other curves.
    std::vector<double> sampled(gridSize, 0.0);
    for (std::size_t g = 0; g < gridSize; ++g) {
        sampled[g] = interpolateLinearAt(prepared.grid[g], nodes, observations);
    }

    // Step 2 — extend both ends by reflecting *through* the endpoint, i.e.
    // 2*x[0] - x[k] rather than x[k].
    //
    // A recursive filter has to start somewhere, and starting it at the first
    // sample leaves a transient that decays over several time constants. With
    // a half-life of a fortnight that transient reaches a month into the
    // record, which is most of a short one. Ordinary mirror padding does not
    // help: it turns a descending record into a V at the boundary, and the
    // filter faithfully reproduces the corner.
    //
    // Reflecting through the endpoint extends a straight line as the same
    // straight line, so a record with a constant slope is filtered with no
    // transient at all — which is exactly what the self-test checks when it
    // asserts every curve reads -0.7 kg/week on a -0.1 kg/day ramp.
    const auto padding = static_cast<std::size_t>(
        std::min<double>(3.0 / alpha, static_cast<double>(gridSize)));
    const std::size_t padded = gridSize + 2 * padding;

    std::vector<double> work(padded, 0.0);
    for (std::size_t i = 0; i < padding; ++i) {
        const std::size_t mirrored = std::min(padding - i, gridSize - 1);
        work[i] = 2.0 * sampled.front() - sampled[mirrored];
        work[padded - 1 - i] = 2.0 * sampled.back() - sampled[gridSize - 1 - mirrored];
    }
    std::copy(sampled.begin(), sampled.end(),
              work.begin() + static_cast<std::ptrdiff_t>(padding));

    // Step 3 — the recursion, forwards and then backwards over the result.
    //
    // One pass alone lags the data by about the half-life, which on a body
    // mass record shows up as a curve that turns a fortnight after the person
    // did. Running the identical filter backwards over the forward result
    // cancels that lag exactly. It also makes the estimator non-causal, which
    // is the right trade here: this draws a record of the past, it does not
    // forecast.
    for (std::size_t i = 1; i < padded; ++i) {
        work[i] = alpha * work[i] + (1.0 - alpha) * work[i - 1];
    }
    for (std::size_t i = padded - 1; i-- > 0;) {
        work[i] = alpha * work[i] + (1.0 - alpha) * work[i + 1];
    }

    FitOutcome outcome;
    outcome.values.assign(work.begin() + static_cast<std::ptrdiff_t>(padding),
                          work.begin() + static_cast<std::ptrdiff_t>(padding + gridSize));

    std::ostringstream summary;
    summary.setf(std::ios::fixed);
    summary.precision(4);
    summary << "alpha=" << alpha << ", half-life="
            << (std::log(2.0) * prepared.gridSpacing / -std::log(1.0 - alpha)) << " days, "
            << padding << " padding nodes";
    outcome.diagnostics = summary.str();
    return outcome;
}

}  // namespace weight::math
```

### The three rules `fit` must obey

**One value per grid node.** `outcome.values.size() == prepared.grid.size()`.

**The smoothness factor is monotone, in the same direction as everyone else.**
`parameters.common.generalSmoothness` arrives from the slider and must keep the
meaning it has everywhere else: **smaller follows the data more closely, larger
is smoother.** That is the promise the slider makes, and one curve that inverts
it makes the control meaningless for all of them at once.

**A straight line must come out as a straight line.** This is the rule that is
easy to miss, and it is checked: `--self-test` feeds every registered curve a
clean −0.1 kg/day ramp over 90 days and requires the rate of change to read
−0.7 kg/week across the middle half of the record, within 0.05.

That check is not decoration. The first draft of the curve above **failed it**,
reading −0.62, because the recursion's start-up transient reached a third of
the way into the record. Nothing looked wrong on screen: the curve was smooth,
plausible, and simply wrong about the rate. The antisymmetric padding in step 2
is what fixed it. **Run the self-test before you believe your curve.**

### Threading

`fit` runs on a worker thread, one task per curve. It must be `const`, hold no
mutable state and touch nothing but its arguments. The estimators are stateless
for exactly this reason, and `test_concurrency` asserts the concurrent and
serial fits are bit-identical for every registered curve — including yours, the
moment you register it.

---

## Step 2 — Declare its constants

Every number that changes the shape of a curve lives in
`src/math/common/TrendParameters.hpp`, never as a literal inside the algorithm.
Add a struct and one field:

```cpp
/// Parameters of curve (D): forward-backward exponential trend.
struct ExponentialTrendParameters {
    /// Half-life at s = 1, in days, then scaled linearly by s and clamped.
    double halfLifeBaseDays = 14.0;
    double halfLifeMinDays = 2.0;
    double halfLifeMaxDays = 90.0;
};

struct TrendParameters {
    TrendCommonParameters common{};
    AdaptiveSplineParameters splineA{};
    MultiquadricRbfParameters rbfB{};
    LocalLinearLoessParameters loessC{};
    ExponentialTrendParameters exponentialD{};   // <-- here
    DerivativeParameters derivative{};
};
```

This is what keeps a curve reproducible and testable: the model can be
re-shaped without touching the algorithm, and a test can hand `fit` a
deliberately extreme parameter set to see what it does.

If a constant of yours has an ordering that must hold — a minimum below a
maximum, a fraction inside [0, 1] — add the clamp in `settings/Settings.cpp`
beside the existing ones, so a hand-edited `config.txt` cannot produce a
nonsensical fit.

---

## Step 3 — Register it, and name it in both languages

### 3a. One entry in `settings/CurveCatalog.cpp`

Add the include at the top of the file, then append to the array:

```cpp
CurveDescriptor{
    /* id                         */ QStringLiteral("exponential_trend"),
    /* letter                     */ QLatin1Char('D'),
    /* shortNameKey               */ QStringLiteral("curve.d.short"),
    /* longNameKey                */ QStringLiteral("curve.d.long"),
    /* colour                     */ Rgb{34, 139, 34},
    /* visibleByDefault           */ false,
    /* derivativeVisibleByDefault */ false,
    /* factory                    */
    [] { return std::unique_ptr<math::TrendEstimator>(
             std::make_unique<math::ExponentialTrendCurve>()); }},
```

The fields are positional, so keep the comments: an entry with the colour and
the visibility flags transposed compiles perfectly and produces an invisible
black curve.

**`id` is permanent.** It is what `config.txt` records, so changing it after a
release silently discards everyone's saved choice for that curve. Positions are
deliberately not used for this — inserting a curve mid-array would otherwise
reassign every saved setting to the wrong curve.

State **one colour only**. `plot::CurveStyle` derives the line, the outer band
and the inner band from it. Do not add a second colour anywhere.

### 3b. Two keys in **both** catalogues of `settings/Strings.cpp`

English:

```cpp
{QStringLiteral("curve.d.short"), QStringLiteral("Exponential trend")},
{QStringLiteral("curve.d.long"),
 QStringLiteral("Exponentially weighted moving average, applied forwards and then "
                "backwards so it does not lag")},
```

Spanish:

```cpp
{QStringLiteral("curve.d.short"), QStringLiteral("Tendencia exponencial")},
{QStringLiteral("curve.d.long"),
 QStringLiteral("Media móvil exponencial, aplicada hacia adelante y luego hacia "
                "atrás para que no vaya con retraso")},
```

Adding them to only one catalogue is a **test failure**, not a silent
half-translation. While this page was being written the Spanish pair was pasted
into the English table by mistake, and the suite said so precisely:

```
FAIL!  : TestLocalisation::bothLanguagesCoverTheSameVocabulary()
         (not translated: curve.d.long, curve.d.short)
```

---

## Step 4 — Add the folder to the build

```cmake
set(WEIGHT_CURVE_DIRS
    adaptive_spline
    multiquadric_rbf
    local_loess
    exponential_trend)     # <-- here
```

Every `.cpp` under `src/math/<name>/` is then globbed into `weight_core`.
Because it is a glob, **re-run `cmake ..`** after adding the folder: a bare
`make` will not notice a directory that did not exist when the build files were
generated.

---

## What you get for free

Nothing below needs any code from you:

- a checkbox for the curve and one for its rate of change, with a colour swatch
  taken from your `Rgb`;
- a tooltip carrying the translated long name;
- an entry in the R² readout, in your colour;
- the line plus its outer and inner bands, derived from the single colour by
  `plot::CurveStyle`;
- a task on the thread pool, and one more worker in the pool's size —
  `Concurrency` is sized from `curveCount()`, not from a constant;
- `curve.exponential_trend` and `derivative.exponential_trend` keys in
  `config.txt`, remembered between runs;
- coverage by `test_concurrency`, which builds every registered curve and
  checks the concurrent and serial fits agree bit for bit;
- coverage by `--self-test`, which constructs it, validates its colour and
  applies the straight-line rule.

No file under `src/ui/`, `src/plot/`, `src/app/` or `src/data/` names any
specific curve, which is why none of them has to be touched.

---

## Verify it

```sh
cd build
cmake ..                      # required: WEIGHT_CURVE_DIRS is globbed
make -j"$(nproc)"
make test                     # 12 suites
../target/weight --self-test  # 45 checks, including the straight-line rule
```

A successful registration looks like this at configure time:

```
-- Weight 0.50.0 | Qt 6.4.2 | GNU | curves=adaptive_spline;multiquadric_rbf;local_loess;exponential_trend
```

and like this in the self-test:

```
Curve catalogue
  ok    curve 'exponential_trend' can be constructed
  ok    curve 'exponential_trend' declares a valid r,g,b

Numerics
  ok    curve 'exponential_trend': rate of change reads -0.7 kg/week
```

Then run the program and watch the log line your `diagnostics` string produces:

```
alpha=0.0062, half-life=7.0000 days, 486 padding nodes
```

That line is the cheapest debugging tool in the project. Put the numbers that
decide your curve's shape into it.

---

## When the curve does not appear

Four causes, in the order they actually happen:

| Symptom | Cause |
|---|---|
| `curves=` at configure time does not list it | the folder is missing from `WEIGHT_CURVE_DIRS`, or `cmake ..` was not re-run after adding it |
| Undefined reference to your constructor | the name in `WEIGHT_CURVE_DIRS` does not match the directory on disk |
| The checkbox reads `curve.d.short` | the key is missing from the catalogue being displayed; the fallback shows the key rather than blanking the label |
| The checkbox appears, the line does not | `fit` returned a different number of values from `prepared.grid.size()` |

---

## Removing a curve

Delete the folder, its catalogue entry and its `WEIGHT_CURVE_DIRS` line. A
`config.txt` naming a curve that no longer exists is ignored rather than being
an error, so nobody's settings file breaks. The string keys can stay: an unused
key costs nothing, and removing it from only one catalogue would fail the test.
