# Architecture

## Layers

Dependencies point one way only.

```
        main.cpp                 composition root; the only place that
           │                     knows about every part at once
           ▼
    app/   ApplicationController use cases, no widgets
           RenderEngine          the worker thread
           SelfTest
           │
           ▼
    ui/    MainWindow, InputPage, PreviewPage, DialogFactory
           │
           ▼
    plot/  AxisWindow            data → visible date range
           PlotSceneBuilder      layout → geometry
           CurveStyle            one r,g,b → line + two bands
           PainterPlotRenderer   geometry → pixels
           │
           ▼
    math/  common/               shared numerics + the estimator base
           adaptive_spline/      one folder per curve
           multiquadric_rbf/
           local_loess/
           │
           ▼
    data/  CsvSampleRepository, repair chain, atomic writes
    platform/ Workspace, SingleInstance
           │
           ▼
    settings/  Settings, Strings, CurveCatalog
```

The separation is enforced by the build, not by convention: `weight_core` links
`Qt6::Gui` but **not** `Qt6::Widgets`, so numerical or file-format code cannot
reach a widget — the symbol is not there. The `math` namespace uses no Qt type
at all; diagnostics are `std::string`.

## The settings folder

Every string, number, colour and file name lives under `settings/`:

| | |
|---|---|
| `Settings.{hpp,cpp}` | fifteen documented sections of numbers and names |
| `Strings.{hpp,cpp}` | the English and Spanish catalogues, plus locale detection |
| `CurveCatalog.{hpp,cpp}` | the curve array: id, letter, r/g/b, defaults, factory |

No literal that affects behaviour appears anywhere else. Curve colours are
stated once, as `Rgb{r, g, b}`, and everything else — the line, the two bands,
the legend swatch, the R² text colour — is derived from that one value by
`plot::CurveStyle`. Listing three colours per curve would be three chances to
get a new curve's palette subtly wrong.

## Threading

Three concurrent concerns, and one design decision each.

**Fitting.** `math::computeTrends` runs the shared preprocessing once on the
calling thread, then dispatches one task per selected curve. Each task writes
only to its own slot in a vector sized before any task started, so no two tasks
touch the same memory and no reallocation can invalidate a pointer mid-flight.
The estimators hold no mutable state, so no locking is needed inside the fits.
Curves that are switched off get no task at all.

**Rendering.** `app::RenderEngine` owns a dedicated thread with its own loop —
a pool task would occupy a slot for the whole session. Requests are *coalesced*
rather than queued: the pending slot holds one request and a newer one
overwrites it, because intermediate slider positions carry no information once
the user has moved on. A sequence number lets a result that arrives after a
newer request was made be discarded rather than drawn over a newer chart.
`QImage` is used rather than `QPixmap` because a pixmap may only be touched on
the GUI thread.

**Saving.** `core::SessionConfigStore` writes config.txt on a worker, keeping
only the newest state: four rapid clicks perform one write, and the one that
runs is the one the user ended on.

The pool is sized `max(minimumThreads, curveCount + auxiliaryThreads)` — by
work, not by `hardware_concurrency()`. A two-core machine still has every curve
in flight and lets the kernel schedule them.

`test_concurrency` asserts the concurrent and serial results are bit-identical.
That is the property that matters: concurrency that changes the answer is not an
optimisation, it is a bug.

## Design patterns

**Strategy + Template Method** — `TrendEstimator::estimate` fixes the pipeline
and `fit` is the one virtual step, so every curve is preprocessed and scored
identically and the R² figures stay comparable.

**Factory driven by a catalogue** — `createCurveEstimator(index)` asks the
catalogue for a factory. The math layer never learns the catalogue's contents,
which is what lets a curve be added without touching it.

**Repository** — `SampleRepository` states exactly what the rest of the program
may do with the history: load, save, ensure it exists.

**Chain of Responsibility** — seven ordered CSV repair rules, each testable
alone.

**Scene / painter separation** — layout is asserted geometrically on a
`PlotScene`, with no image and no display server; an SVG or PDF renderer could
be added without touching the layout.

## Two rules that shaped everything

**Nothing is written until the user confirms.** The history and the chart are
both committed by *Save image*. This is why `PendingSession` holds the modified
records in memory rather than the repository writing incrementally.

**Never lose a line.** A row the parser cannot read is preserved verbatim and
written back at the end of the file. Deleting it would be the one unrecoverable
outcome.

## The visible window

`plot::AxisWindow` decides what the horizontal axis covers, as a pure function
of the samples and the current date. Keeping it separate from the scene builder
means the rule can be tested exhaustively without rendering anything, which
matters because an off-by-one there silently crops someone's oldest measurement
off the left edge.

The rule: start at the oldest measurement, or today when there is none; end at
whichever is further away, a year from the first or three months from the last.
Taking the maximum handles both a two-week record and a five-year one without a
special case.

The tick spacing is chosen from a list of multiples of a week, growing until the
label count fits. Multiples of a week rather than arithmetically "nice" numbers,
so every label lands on the same weekday and the reader can count weeks along
the axis.
