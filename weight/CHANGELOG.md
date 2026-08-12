# Changelog

## 0.50.0

### The build, in two places

**Fixed: `ld: cannot open output file .../target/weight: No such file or directory`.**

Not a linker problem. `target/` did not exist.

`CMakeLists.txt` lists `target/` in `ADDITIONAL_CLEAN_FILES`, so `make clean`
removes the directory itself. CMake creates the runtime output directory only
while *generating*, and never again. So the sequence was:

```
make clean          target/ is deleted
make                fails at 57%, at the link step
make clean && make  fails again, identically
cmake ..            recreates target/ as a side effect of the preflight check
make                works
```

which reads as an intermittent linker fault and is a directory that is not
there. Reproduced exactly before being fixed.

`weight_ensure_output_directory()` now attaches a `PRE_BUILD` command to the
`weight` target. Every generator except Visual Studio treats `PRE_BUILD` as
`PRE_LINK`, so the directory is recreated inside the same rule, immediately
before `ld` runs. If the directory is removed by hand the target's output file
is missing too, so the target is out of date, so the rule runs: there is no
ordering in which the linker gets there first. The `deb`, `dmg` and `dist`
targets create `target/packages/` and the staging directory the same way.

**Fixed: `dpkg-deb: error: control directory has bad permissions 777`.**

`mkdir` applies the caller's `umask`. With `umask 000` — which a good number of
shell configurations set — `$STAGE/DEBIAN` came out `0777`, and `dpkg-deb`
requires the control directory to be between `0755` and `0775`. Every form of
`make deb` failed at the last step, after doing all the work.

`packaging/debian/build-deb.sh` now sets `umask 022` and states the mode of
everything it writes: directories `0755`, files `0644`, the binary and the
maintainer scripts `0755`.

Setting the mode is not enough on its own, because on an NTFS or exFAT mount —
the usual arrangement when the source lives on a partition shared with Windows
— `chmod` succeeds and changes nothing. So the script verifies: it creates a
probe directory, sets it to `0755`, reads the mode back, and if the filesystem
did not store it, stages under `$TMPDIR` instead and prints why. The resulting
`.deb` is identical either way, because a package records its own modes.

### Language

**Every `es_*` locale selects Spanish, not only `es_CL`.**

The mapping was `tag.toLower().startsWith("es")`, which is wrong in both
directions: it accepts Estonian written `est_EE` and Esperanto written `eo`,
and it says nothing about which subtag it is looking at. It now extracts the
**primary language subtag** — stripping the codeset (`.UTF-8`), the modifier
(`@valencia`), the script and the region — and compares whole subtags. `es`,
`es_CL`, `es_ES`, `es_MX`, `es_AR`, `es_US`, `es-419`, `es-Latn-MX`,
`es_ES.UTF-8@valencia` and `spa` all select Spanish; `est_EE` and `eo` no
longer do.

**Windows and macOS are asked through their own interfaces.**

New `src/platform/SystemLocale.{hpp,cpp}`:

| Platform | Call |
|---|---|
| Windows | `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, …)`, falling back to `GetUserDefaultLocaleName` |
| macOS | `CFLocaleCopyPreferredLanguages()`, falling back to `CFLocaleCopyCurrent` |
| POSIX | `setlocale(LC_MESSAGES, "")`, asked and then restored |

with `QLocale` demoted to a last resort. `QLocale::system()` reports the
*formatting* locale, and what selects an interface language is the *UI
language* list; on Windows and macOS these genuinely differ.

Both platforms return a **ranked list**, and the list is now walked rather than
truncated to its first entry: a machine ordered `de, es, en` gets Spanish,
because German is skipped for want of a catalogue and Spanish is what that
person asked for next. A neutral tag (`C`, `POSIX`) stops the walk, since it is
an explicit statement rather than a missing one.

`LANGUAGE` is honoured with GNU's own precedence rule, including the exception
that it is ignored when the resolved locale is `C` — which is what keeps
`LC_ALL=C` a reliable way to get untranslated output.

CMake links `CoreFoundation` on macOS rather than relying on Qt to drag it in.

### Interface

**"View chart only" is disabled while there is nothing to draw.**

The button plots the stored history without recording anything, and with an
empty history it produced a chart with no data in it — an empty pair of axes,
which reads as a failure rather than as an answer. `ApplicationController`
gained `storedMeasurementCount()`, and the window asks on every return to the
entry page, because the history changes when a measurement is saved and can
change outside the window entirely: the file is a plain CSV the person is
invited to edit.

The rule lives in `InputPage::setInputsEnabled()` rather than at the call
sites. Four places used to re-enable the page by looping over its widgets, and
every one of them would have switched the button back on. The handler also
re-checks before acting, because a disabled button is a courtesy and not a
guarantee.

The tooltip differs by reason: with a history, what the button does; without
one, why it cannot.

### Tests

- `test_input_page` is new: four cases, including the specific regression of
  re-enabling the page enabling the button.
- `test_localisation` grew from twelve tag rows to thirty, plus the environment
  chain, the `LC_ALL=C` exception and the `WEIGHT_LANG` override.
- `--self-test` checks nine Spanish forms and ten English-or-fallback forms
  rather than one of each, and reports where the detected language came from.
- 12 suites, 45 self-test checks.

### Documentation

- `docs/building.md` is new: from `git clone` to a running binary on each
  platform, including which components to select in the Qt installer on Windows
  and macOS, the vcpkg route, and the two build failures above.
- `docs/curves.md` is rewritten as a complete worked example. A fourth curve
  was written against it, compiled and run through the suite to check the
  instructions are sufficient; two things the previous version omitted came out
  of that — that `TrendParameters` needs an entry, and that `--self-test`
  requires a curve to reproduce a straight line, which the example initially
  failed.
- `docs/packaging.md` opens with the four lines that produce a package, and
  documents the permission problem above.
- `docs/platform.md` gains the locale section.
- The README documents the repository's `README.md` and `LICENSE` symlinks, and
  the language table.

## 0.49.0

### The crash

**Fixed: segmentation fault on entering the chart page.**

`MainWindow` declared `std::unique_ptr<app::RenderEngine> renderEngine_` and
never constructed it. The only reference to the member in the whole project was
its use:

```
src/ui/MainWindow.cpp:190:    renderEngine_->submit(std::move(request));
src/ui/MainWindow.hpp:96:     std::unique_ptr<app::RenderEngine> renderEngine_;
```

So the program started, read the history, showed the entry page — and died the
instant *Continue* or *View chart only* was pressed, dereferencing a null
pointer inside `requestPreview()`. There was no log line between the last
message and the crash because there is no logging on that path.

It was not an overflow and not a double free. It also could not be caught by
`--self-test`, which never instantiates `MainWindow`; the test that would have
caught it is a widget test that drives the button, and there wasn't one.

Three changes, not one:

- the engine is constructed and its `finished` signal connected in the
  constructor, and it is deliberately **not** given `this` as a QObject parent —
  the `unique_ptr` already owns it, and handing one object to two owners is how
  double-frees get written by accident;
- an explicit destructor stops the worker **before** any member is torn down, so
  a render in flight cannot emit against a half-destroyed window;
- `requestPreview()` guards against a missing engine. It is reachable from six
  different signals, and a null engine should be a message, not a fault.

### Robustness found by the sanitisers

The render pipeline was rebuilt under AddressSanitizer and UndefinedBehaviour
Sanitizer and driven over 12 288 combinations: twelve degenerate datasets —
empty, one sample, a duplicated day, negative days, NaN and infinity smuggled
in, zero variance, a 600-day gap, extreme masses — against all 256 combinations
of the visibility checkboxes and four slider positions.

No memory error was found, which is what says the null pointer was the whole of
it rather than the tip of something larger. One correctness defect did surface:

**Fixed: a non-finite sample blanked the chart.** An infinite or NaN day made
the axis scale zero, every mapped coordinate NaN, and QPainter reported
`QPainterPath::arcTo: a parameter is NaN` and drew nothing. Guarded at three
levels, because each is a seam a future caller could arrive through:

- `computeAxisWindow` ignores non-finite samples when choosing the range. NaN
  loses every comparison, so `std::minmax_element` had been returning a
  meaningless element rather than failing;
- `PlotSceneBuilder` refuses a non-finite or degenerate axis range outright, and
  drops non-finite samples before projecting them;
- `PainterPlotRenderer` discards non-drawable points, which makes the renderer
  total: every scene it is handed produces an image.

**Fixed: the single-instance guard could not report the owner's pid while the
owner was busy.** `QLocalServer` delivers `newConnection` through a socket
notifier, which only fires while an event loop is spinning. A second copy
started during a long fit, a modal dialog or a full-resolution export would have
been told "already running" with no process id. The guard now runs on a thread
of its own with its own event loop, and the `QLocalServer` is created, connected
and set listening on that thread — a `QLocalServer` binds its notifier to
whichever thread calls `listen()`.

Found by `test_platform_integration`, which failed on its first run reporting a
pid of 0.

### Packaging

**The `.deb` now reads its own dependencies.** `dpkg-shlibdeps` resolves each
`SONAME` in the built binary to the package that owns it, which is what the
Debian archive itself does. This replaces a hand-written list that was both
stale and release-specific: it named `libqt6concurrent6`, which the binary does
not need at all (Qt Concurrent is header-only in Qt 6), and it hardcoded either
the `t64` names or the non-`t64` names, each wrong on half the targets.

`qt6-qpa-plugins` is added by hand and is the only entry that is: the Qt
platform plugin is `dlopen`'d, appears in no ELF header, and no automatic tool
can see it.

**One build directory, one artefact directory.** `build-deb/` and `build-static/`
are gone. Packaging stages inside whatever build directory you configured, and
every deliverable lands under `target/`:

```
build/package-staging/    scratch
target/weight             the executable
target/Weight.app         the bundle (macOS)
target/packages/          the .deb, the .dmg, the Windows folder and .zip
```

**`make dmg` no longer reconfigures your build tree.** It used to re-run
`cmake -S . -B build` with its own architecture and prefix, silently rewriting a
tree you had configured yourself. It now builds what is there and packages the
result, and reads the architectures back out of the binary with `lipo -archs`
so the file name describes what is actually inside.

**`make dist` added for Windows**: a folder holding `weight.exe` and its Qt
runtime, plus a `.zip`. The architecture in the name comes from the PE header,
not from the host.

### Platform correctness

- **macOS fallback path corrected.** `AppDataLocation` already appends the
  organisation and application names, so the fallback had been
  `~/Library/Application Support/Weight/weight`. `GenericDataLocation` gives the
  intended `~/Library/Application Support/weight`.
- **Windows and Linux gained a last-resort location** (`%LOCALAPPDATA%`,
  `$XDG_DATA_HOME`) so the program can start on an account with no Documents
  folder at all.
- **Linux now consults `DocumentsLocation` as well as `XDG_DOCUMENTS_DIR`**,
  since the two disagree on systems where the variable is not exported.
- **Icons install at their own resolution.** Every hicolor size was previously
  installed from the same 512×512 PNG, leaving the shell to downscale it.

### Pre-build checks

`cmake ..` now verifies compiler version, all five Qt modules, the offscreen
platform plugin, the assets compiled into the binary, and that `target/` is
writable — before compiling anything. Missing compile requirements are fatal and
name the package to install per distribution; missing packaging tools are only
warnings, because you can build and run without ever making a package.

### Tests

`test_platform_integration` added: twelve cases that ask the operating system
directly rather than assuming. Suite total is now 11 executables and the
binary's own 38-assertion `--self-test`.

### Localisation

Every message the program prints now goes through the catalogue. Thread policy,
date reset, history read and written, save errors and the render failures were
still hard-coded English, which produced logs half in one language and half in
the other. `Strings::get` also gained a list form, so a translation may reorder
its placeholders — chained `.arg()` cannot express that.

### Notes

- Negative days are supported and always were: they simply move the start of the
  chart to the left. `computeAxisWindow` clamps nothing.
