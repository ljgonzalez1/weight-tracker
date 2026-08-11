# The mathematics

Three estimators are available, all *nonparametric*: none of them assumes the
trend has any particular shape. That matters for body mass, which does not
follow a line, an exponential or any other closed form — it responds to whatever
happened that month.

All three accept the same smoothness factor `s` with the same meaning: **smaller
follows the measurements more closely, larger is smoother**. That equivalence is
deliberate, so one slider governs all three.

---

## Shared preprocessing

Every estimator receives the same prepared data, which is what makes the three
curves and their derivatives directly comparable.

1. **Sort** by day.
2. **Aggregate repeated days** to their mean. Six weighings on one morning must
   not outvote a day with one. The multiplicity is kept so estimators can still
   give that day proportionate weight.
3. **Build a dense grid**, 16 nodes per day, clamped to between 1400 and 4000
   nodes. Fine enough for a smooth derivative, bounded so a multi-year history
   does not grow without limit.

Fitting fewer than three samples, or samples spanning fewer than two distinct
days, returns no curve at all. A trend through a single day is not defined, and
inventing one would be worse than showing nothing.

---

## Curve A — adaptive Gaussian spline

**Natural cubic spline, then convolution with a Gaussian kernel of locally
varying width.**

    ŷ = M_{h(t)}( S(y) )

`S` interpolates the daily means with a natural cubic spline: a C² curve passing
exactly through every day, with no smoothing bias of its own. `M_h` then
convolves it with a Gaussian:

    ŷ(t) = Σ_u K((u−t)/h(t)) · S(y)(u)  ⁄  Σ_u K((u−t)/h(t)),   K(z) = e^{−z²/2}

In the sense of Friedrichs this is a **mollifier**: convolving with a smooth,
positive, unit-mass kernel turns a merely C² curve into an essentially C^∞ one.
Dividing by the weight sum makes the operator reproduce constants exactly, so a
flat stretch stays flat and the ends are not dragged.

### The adaptive bandwidth

`h(t)` is driven by how densely measurements sit near `t`:

    ρ(t) = Σⱼ nⱼ^p · exp(−((t−xⱼ)/σ_pilot)²/2)
    h(t) = h_base · (median(ρ)/ρ(t))^q,  clamped to [2.5, 32] days

`nⱼ` is the number of readings on day `xⱼ`, damped by `p = 0.35` so twelve
weighings do not behave like twelve independent days. The exponent `q = 0.60`
sets how strongly the kernel reacts: `q = 0` disables adaptation, `q = 1` makes
the width inversely proportional to density.

The effect is what a reader expects: where the record is dense the kernel is
narrow and the curve tracks it; across a three-week gap the kernel widens and
the curve bridges smoothly rather than inventing structure inside the gap.

`h(t)` is itself Gaussian-smoothed before use, because sample density changes in
steps whenever a day enters or leaves the pilot window, and an unsmoothed
profile would print those steps onto the curve as visible kinks.

`s` multiplies `h_base`, after a visual floor of 0.45 — below that the kernel is
narrower than the spline's own curvature scale and the rendered curve shows
corners rather than looking smooth.

### Properties and limits

- Not a polynomial. Piecewise-polynomial before convolution, not polynomial at
  all after it.
- Reproduces constants exactly; no drift at the boundaries.
- **Not robust.** The base curve interpolates, so an outlier still bends the
  result locally; the mollifier attenuates it but does not reject it. When you
  suspect a bad reading, curve C is the better reference.
- Cost: O(G·W) for the mollifier plus O(G·n) for the pilot density.

---

## Curve B — multiquadric RBF ridge regression

**A constant plus a weighted sum of radial basis functions, fitted by
regularised least squares.**

    f(t) = ȳ + Σⱼ cⱼ · φ(t − xⱼ),    φ(r) = √(r² + ε²)

`ȳ` is the multiplicity-weighted mean. Centring on it rather than zero matters:
as the penalty grows the coefficients shrink toward zero, so the curve tends to
the *average of the data* rather than collapsing onto the x axis.

The coefficients solve

    (Φᵀ W Φ + λI) c = Φᵀ W (y − ȳ)

with `W` the diagonal matrix of multiplicities. The matrix is symmetric and, for
`λ > 0`, positive definite, so it is solved by Cholesky factorisation with a
pivoted LU fallback for the case where rounding costs it definiteness.

### The two hyper-parameters

    ε = max(1.5, median spacing × 1.10) × s
    λ = max(10⁻⁸, 5×10⁻³ · s²) × mean(diag(Φᵀ W Φ))

Scaling `λ` by the mean Gram diagonal makes the penalty dimensionless with
respect to the data, so the same constant behaves identically whether the record
spans two months or two years. Both quantities grow with `s`, so the slider
makes the curve smoother in two reinforcing ways at once.

### Why the multiquadric

`φ` is C^∞ everywhere, and for large `r` it behaves like `|r|`. That near-linear
tail lets the basis represent piecewise-linear trends cheaply, which is exactly
what produces a sensible straight bridge across a long gap instead of the
oscillation a Gaussian or compactly supported basis would show there.

Statistically, ridge regression is the maximum a posteriori estimate under a
Gaussian prior on the coefficients: `λ` encodes how strongly one believes in
advance that the trend is gentle.

### Properties and limits

- **Global.** One linear system couples every observation. That is the strength
  (information propagates across gaps) and the weakness (an extreme outlier
  perturbs the whole curve slightly rather than one neighbourhood strongly).
- Centres are thinned to at most 800 by uniform subsampling, keeping the cubic
  solve affordable at some cost in resolution on very long histories.
- The Gram matrix of a multiquadric basis is notoriously ill-conditioned for
  small `ε`. **The ridge term is what keeps the solve well posed**, so lowering
  `rbf/ridgeBase` toward zero is not safe.
- If the solution still comes out non-finite, the estimator falls back to linear
  interpolation of the daily means and says so in its diagnostics rather than
  returning silent nonsense.
- Cost: O(nk) to assemble, O(k²n) for the Gram matrix, O(k³/6) to solve.

---

## Curve C — local linear LOESS

**A straight line fitted by weighted least squares at every instant.**

    minimise over (a,b):  Σᵢ wᵢ(t) · (yᵢ − a − b(xᵢ − t))²
    wᵢ(t) = nᵢ · exp(−((xᵢ−t)/h(t))²/2)

The trend value is the local intercept `a`, the fitted line evaluated at `t`
itself. Solving the 2×2 normal equations in closed form, with `dᵢ = xᵢ − t`:

    a = (S_y·S_xx − S_x·S_xy) / (S_w·S_xx − S_x²)

The determinant vanishes only when every weight sits on a single abscissa; there
the fit degenerates gracefully to the weighted mean `S_y/S_w`.

### Why linear rather than constant

A locally *constant* fit — the Nadaraya–Watson estimator — is biased wherever the
data are asymmetric around `t`, which is exactly what happens at the two ends of
a record and on either side of a gap: the estimate is dragged toward the side
with more neighbours. Fitting a line removes that first-order bias, so the curve
stays sensible at the boundaries and continues the local slope across a gap
instead of flattening into it.

### The nearest-neighbour bandwidth

`h(t)` is the distance from `t` to its `q`-th nearest measurement day:

    q = clamp(⌈0.28 · s · n⌉, 4, n)

This is a *nearest-neighbour* rather than a fixed-width bandwidth. Where days
are dense `h` is small and the curve tracks them; inside a long gap `h` grows
automatically until the window reaches data on both sides. A fixed bandwidth
cannot do both. Because the `q`-th nearest distance is a step function of `t`,
the profile is smoothed before use.

### Properties and limits

- **Local by construction.** An outlier disturbs its own neighbourhood and
  nothing else, which makes this the most reliable of the three when a reading
  is suspect.
- It is *not* the iteratively reweighted robust LOESS; a single gross outlier
  still bends its neighbourhood.
- Cost: O(G log n) for the bandwidth profile, O(G·n) for the local fits. The
  original array formulation materialised a full G×n distance matrix; computing
  the k-th nearest distance directly avoids that allocation, which is where most
  of the speed-up comes from.

---

## Rate of change

The dense curve is resampled every half day, then differentiated with central
differences (one-sided at the ends) and multiplied by 7 to give **kilograms per
week**.

Resampling first is deliberate. Differentiating the very fine grid directly
would amplify the small numerical ripple left by the smoothing operators,
whereas the coarser step acts as a mild low-pass filter. All three curves are
smooth by construction, so central differences are accurate and stable here.

---

## Goodness of fit

R² is computed against the **raw** measurements, not the daily means:

    R² = 1 − Σ(yᵢ − ŷ(xᵢ))² / Σ(yᵢ − ȳ)²

so it answers the question a reader actually asks: how well does this curve
describe *my measurements*. All three curves are scored identically, which is
what makes the three numbers comparable.

---

## A defect corrected from the previous implementation

The Python version this project replaces contained an off-by-one error in the
Thomas algorithm that solves for the spline's second derivatives. It indexed the
sub-diagonal one position too early:

```python
denom = b[i] - a[i - 1] * c_prime[i - 1]   # previous version
denom = b[i] - a[i]     * c_prime[i - 1]   # correct
```

For interior row `i` the sub-diagonal coefficient is `h_i`, which is `a[i]`, not
`a[i-1]`.

**On uniformly spaced nodes `h` is constant and the two coincide**, which is why
the defect went unnoticed: with regular daily weighings the spacing is nearly
uniform. On irregular spacing it does not.

This was verified rather than assumed. Solving the same seven-node system
densely with `numpy.linalg.solve` and substituting each result back:

| Solution | Residual of its own system |
|---|---|
| Previous version | **3.95** |
| Weight | **1.8 × 10⁻¹⁵** |

The previous solution does not satisfy the equations it came from. The test
`tests/unit/test_cubic_spline.cpp::momentsSatisfyTheDefiningSystem` asserts this
residual directly on deliberately irregular nodes, so the defect cannot return.

### Consequence for existing users

Curve A will differ slightly from the old output when measurement days are
irregularly spaced — by about 0.013 kg on the validation dataset, which is well
below the resolution of a bathroom scale and generally invisible on the chart.
Curves B and C are unaffected.

This is the **only** intentional behavioural difference between the two
implementations.

---

## Numerical validation

The C++ estimators were cross-checked against the original NumPy code on a
synthetic 121-sample history with irregular spacing, duplicate days and a
five-week gap, at three smoothness settings.

With only the spline fix applied to the reference, **all nine curve × smoothness
combinations agree to about 1 × 10⁻¹³** — machine precision for double
arithmetic. That is the evidence that the migration preserved behaviour rather
than merely producing something that looks similar.

A compact extract of that reference is checked in as
`tests/data/golden_curves.txt` (25 probe points plus R² per combination) and
asserted on every `make test`.
