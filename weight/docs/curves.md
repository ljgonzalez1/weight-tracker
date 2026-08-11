# Adding a curve

Three steps. Nothing else in the program changes.

## 1. Write the estimator

Create `src/math/<your_curve>/YourCurve.{hpp,cpp}`, deriving from
`math::TrendEstimator`:

```cpp
class YourCurve final : public math::TrendEstimator {
protected:
    FitOutcome fit(const PreparedSamples& prepared,
                   const TrendParameters& parameters) const override;
};
```

`fit` is the only thing you have to supply. The base class fixes the pipeline
around it — preprocess, fit, score, package — so your curve is preprocessed and
scored exactly like the others, which is what makes the R² figures comparable.

`prepared` gives you sorted samples, duplicate days collapsed to their mean with
the multiplicity retained, the shared dense grid, the span and the median
spacing. Return one value per grid node, plus a one-line diagnostic for the log.

The smoothness factor arrives as `parameters.common.generalSmoothness` and must
have the same monotone meaning as everywhere else: **smaller follows the data
more closely, larger is smoother**. That is the promise the slider makes, and
the test suite checks it.

## 2. Register it

One entry in the array in `settings/CurveCatalog.cpp`:

```cpp
CurveDescriptor{
    QStringLiteral("your_curve"),      // stable id, used in config.txt
    QLatin1Char('D'),                  // letter for the R² readout
    QStringLiteral("curve.d.short"),   // localisation keys
    QStringLiteral("curve.d.long"),
    Rgb{34, 139, 34},                  // r, g, b — the only colour you state
    false,                             // curve visible by default?
    false,                             // rate of change visible by default?
    [] { return std::unique_ptr<math::TrendEstimator>(
             std::make_unique<math::YourCurve>()); }},
```

Then add `curve.d.short` and `curve.d.long` to **both** catalogues in
`settings/Strings.cpp`. A test fails if you add them to only one.

## 3. Add the folder to the build

```cmake
set(WEIGHT_CURVE_DIRS
    adaptive_spline
    multiquadric_rbf
    local_loess
    your_curve)     # <-- here
```

## What happens automatically

- a checkbox for the curve and one for its rate of change, with a colour swatch
  drawn from your `Rgb`;
- a tooltip carrying the translated long name;
- an entry in the R² readout in your colour;
- the line, plus its outer and inner bands, derived from the single colour by
  `plot::CurveStyle`;
- a task on the thread pool, and one more worker in the pool's size;
- a `curve.your_curve` and `derivative.your_curve` key in config.txt;
- coverage by `test_concurrency`, which builds every registered curve and
  checks the concurrent and serial fits agree bit for bit.

## The stable id matters

`id` is what config.txt records. Once published it must not change, or people's
saved choices stop applying. Positions are deliberately *not* used for this:
inserting a curve in the middle of the array would otherwise reassign everyone's
settings to the wrong curves.

Removing a curve is safe: a config naming a curve that no longer exists is
ignored rather than being an error.
