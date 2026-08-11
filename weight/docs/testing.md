# Testing

```sh
make test
```

Ten cases: nine unit suites plus the binary's own self-test. All headless.

| Suite | Covers |
|---|---|
| `test_numeric_array` | linspace, arange, interpolation, gradient, median, aggregation, k-th nearest, R² |
| `test_cubic_spline` | interpolation and **the tridiagonal residual** |
| `test_csv_parsing` | strict parsing, seven repair rules, headers, duplicates, encodings, round-trip |
| `test_localisation` | locale mapping, catalogue parity, substitution |
| `test_session_config` | config.txt round-trip, id-keyed curves, stale files, coalesced writes |
| `test_workspace` | candidate order, adoption before creation, native Documents |
| `test_axis_window` | the visible date range, its boundaries and tick density |
| `test_concurrency` | **concurrent == serial, bit-identical**, thread policy, curve registry |
| `test_validators` | field validators |
| `self_test` | the built binary on this OS and architecture |

## The self-test

Requirement: `make test` must be runnable against the compiled binary alone.
`weight --self-test` does exactly that, and covers what a unit suite cannot —
the *deployed artefact*:

- the icon really is inside the binary (the .qrc compiled into the executable,
  not into a static library where the linker discards it);
- all four Open Runde faces load and the family really is Open Runde;
- locale mapping behaves on this machine;
- every registered curve constructs and declares a valid colour;
- concurrent and serial fits agree bit for bit;
- a −0.1 kg/day trend reads as −0.7 kg/week on the secondary axis;
- a chart renders, is **not a blank canvas**, and writes to a PNG;
- the history and config.txt round-trip;
- the workspace and single-instance guard work on this kernel.

Everything runs in a temporary directory removed on exit. **Nothing touches the
real Documents folder.**

A binary carried to another machine can be checked with one command:

```sh
./weight --self-test
```

## Two tests worth knowing about

**`momentsSatisfyTheDefiningSystem`** substitutes the spline's computed second
derivatives back into the tridiagonal system they came from and requires the
residual to be at rounding level, on deliberately *irregular* nodes. This
catches the class of defect found in the previous Python implementation, whose
solution did not satisfy its own equations. Uniform spacing hides it entirely.

**`parallelAndSerialAgreeExactly`** compares every curve and derivative value
with `QCOMPARE` on the whole vector — bit-identical, not merely close. Any
difference would mean the tasks shared something they should not.

## What the suite does not cover

- no GUI interaction tests: widget construction and validators are covered,
  clicking through the two pages is not;
- no rendered-image comparison; the self-test checks the canvas is not blank
  and the layout is asserted geometrically, but pixels are not diffed;
- no fuzzing of the CSV parser, though it is the component most exposed to
  hostile input;
- Windows and macOS are untested — no machines were available.

---

## The platform suite

`tests/unit/test_platform_integration.cpp` is the one suite whose assertions
change per operating system, and it is therefore the one that has to be re-run
on each target rather than trusted from the last one. It asks the system
directly:

| Case | What it establishes |
|---|---|
| `reportsTheHost` | records kernel, product, CPU and byte order, so a failure report names the machine |
| `documentsLocationResolves` | the platform Documents call returns an absolute path at all |
| `workspaceCandidatesAreOrderedForThisPlatform` | the search order matches what this platform is documented to prefer, with no duplicates, and on macOS that the fallback is `Application Support/weight` rather than `Application Support/Weight/weight` |
| `workspacePrefersAnExistingFolderOverCreatingOne` | creates the *last* candidate and asserts it wins, proving the search really looks before it creates |
| `workspaceFallsBackWhenNoDocumentsFolderExists` | the program can start on a brand-new account |
| `atomicWriteReplacesInPlace` | a replace is a replace |
| `atomicWriteLeavesNoTemporaryBehind` | five writes in a row leave exactly one file; a botched Windows rename sequence accumulates `.tmp` siblings silently |
| `atomicWriteSurvivesAnExistingReadOnlyTarget` | a read-only target produces an error or a legitimate replace, never a truncated file |
| `singleInstanceDetectsItself` | the named socket refuses a second acquisition **and reports the owner's pid** |
| `singleInstanceReleasesItsName` | the name is reusable after the owner is destroyed, which is the whole reason it is not a lock file |
| `threadPoolExceedsTheHardwareCount` | 20 curves + 3 auxiliaries gives 23 workers regardless of cores |
| `separatorsAreNativeInEveryPath` | no doubled or mixed separators anywhere in the candidate list |

Nothing in it writes to a real Documents folder: every path is a
`QTemporaryDir` or a workspace name containing this process's pid, and each case
removes what it made.

`singleInstanceDetectsItself` earned its place immediately. It failed on the
first run, reporting a pid of 0, because the guard's socket notifier only fired
while the main event loop was spinning — so a second copy started while the
owner was busy would have been told "already running" with no pid at all. The
guard now runs on a thread of its own. That is a defect the suite found, not one
it was written to confirm.
