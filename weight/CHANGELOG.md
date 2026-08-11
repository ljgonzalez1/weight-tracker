# Changelog

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
