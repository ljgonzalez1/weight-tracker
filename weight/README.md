# Weight

Records body mass and draws the trend underneath it.

Charts are rendered natively with Qt: no Python, no plotting library, no
subprocess. Curves are fitted concurrently and drawn on a worker thread, so the
window stays responsive while the smoothness slider moves.

| | Curve | Method |
|---|---|---|
| **A** | red | adaptive Gaussian spline |
| **B** | black | multiquadric RBF ridge regression |
| **C** | blue | local linear LOESS |

Each can also be shown as its rate of change in kilograms per week. Adding a
fourth curve is a three-step change — see [docs/curves.md](docs/curves.md).

---

## Building

```sh
mkdir build && cd build
cmake ..

make            # or `make all` — builds target/weight
make test       # unit suites plus the binary's own self-test
make deb        # Linux:   target/packages/weight_<version>_<arch>.deb
make dmg        # macOS:   target/packages/Weight-<version>-<arch>.dmg
make dist       # Windows: target/packages/weight-<version>-windows-<arch>/ and .zip
make package    # whichever of the three applies here
make clean      # removes the build tree, target/ and the packaging staging area

sudo make install     # installs the command `weight`, the icon and the menu entry
sudo make uninstall   # removes exactly that; never touches your data
```

### Where things go

```
build/                     everything disposable, including package staging
target/                    every deliverable
target/weight              the executable  (weight.exe on Windows)
target/Weight.app          the bundle      (macOS)
target/packages/           the .deb, the .dmg, the Windows folder and .zip
```

Two rules, and both of them matter:

**One build directory.** There is no `build-deb/` and no `build-static/`. You
choose the build directory, and every intermediate — including the tree the
packaging scripts stage into — lives inside it. `rm -rf build` really does
remove all of it, and no packaging target can leave a root-owned directory in
your source tree.

**One artefact directory.** Deliverables never sit inside the build tree, so
wiping the build tree cannot destroy one and you never have to hunt through a
generator-specific subdirectory to find the binary.

`make deb` needs no privileges and installs nothing. If you ever ran it with
`sudo`, the leftovers it created are root-owned; the next run detects that and
prints the exact `rm` command rather than failing with "permission denied".

### Pre-build checks

`cmake ..` verifies the toolchain before compiling anything, and prints what it
found:

```
-- Pre-build checks
--   [ ok ] compiler: GNU 13.3.0
--   [ ok ] Qt 6.4.2 with Core, Gui, Widgets, Network, Concurrent
--   [ ok ] offscreen platform plugin (tests can run without a display)
--   [ ok ] assets: 4 font faces, the icon set and the resource manifest
--   [ ok ] artefact directory is writable: /home/you/weight/target
--   [ ok ] dpkg-deb and dpkg-shlibdeps found; `make deb` is available
```

A missing compile-time requirement is a **fatal error** naming the package to
install, per distribution. A missing *packaging* tool is only a **warning**,
because you can build and run the program perfectly well without ever making a
package. The checks live in `cmake/WeightPreflight.cmake`; adding one is a
function and a call.

---

## Dependencies

Listed separately by purpose. **Running** needs far less than **building**, and
and packaging needs a little more again.

Minimum: **Qt 6.2**, a **C++20** compiler (GCC 11+, Clang 14+, MSVC 2022),
**CMake 3.21**.

### Runtime — dynamic build

| Distribution | Packages |
|---|---|
| **Debian 12, Ubuntu 22.04, Mint 21** | `libqt6core6 libqt6gui6 libqt6widgets6 libqt6network6 libqt6concurrent6 qt6-qpa-plugins` |
| **Debian 13+, Ubuntu 24.04+, Mint 22+** | `libqt6core6t64 libqt6gui6t64 libqt6widgets6t64 libqt6network6t64 libqt6concurrent6t64 qt6-qpa-plugins` |
| **Arch, Manjaro** | `qt6-base` |
| **Fedora, RHEL 9+, Rocky, Alma** | `qt6-qtbase qt6-qtbase-gui` |
| **openSUSE** | `libQt6Core6 libQt6Gui6 libQt6Widgets6 libQt6Network6 libQt6Concurrent6` |
| **Alpine** | `qt6-qtbase qt6-qtbase-x11` |
| **Windows** | the Qt DLLs collected by `windeployqt`, shipped beside the .exe |
| **macOS 11+** | nothing when built as a bundle with `macdeployqt` |

> **The `t64` suffix.** Debian 13 and Ubuntu 24.04 renamed library packages
> during the 64-bit `time_t` transition. `make deb` resolves the correct names
> from the build machine rather than hard-coding either set.

> **Alpine.** Gui and Widgets live in `qt6-qtbase-x11`, not `qt6-qtbase`.
> Installing only the latter gets a program that cannot open a window.

### Build

```sh
# Debian, Ubuntu, Mint
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools

# Arch, Manjaro
sudo pacman -S --needed base-devel cmake qt6-base

# Fedora, RHEL, Rocky, Alma
sudo dnf install gcc-c++ cmake qt6-qtbase-devel qt6-qtbase-gui

# openSUSE
sudo zypper install gcc-c++ cmake qt6-base-devel qt6-widgets-devel

# Alpine
sudo apk add build-base cmake qt6-qtbase-dev qt6-qtbase-x11

# macOS
xcode-select --install && brew install cmake qt@6

# Windows (MSVC 2022 + the official Qt installer, or vcpkg)
```

### Testing

The Qt Test module, which is bundled with the development package on most
distributions (`qt6-test-devel` on openSUSE). The suite runs headless:
`QT_QPA_PLATFORM=offscreen` is set by the test registration, so `make test`
works over SSH and inside a container.

### Packaging

```sh
# Debian, Ubuntu, Mint — dpkg-shlibdeps reads the binary's real dependencies
sudo apt install dpkg-dev binutils

# macOS — macdeployqt ships with Qt; hdiutil and iconutil ship with the system
# Windows — windeployqt ships with Qt, in its bin directory
```

`make deb` computes `Depends:` from the ELF headers of the binary it just built
rather than from a list somebody typed. That is what makes the package portable
across releases that renamed their Qt libraries; see
[docs/packaging.md](docs/packaging.md).

---

## Platform notes

The whole of the platform-specific surface is four questions, and they live in
four files. [docs/platform.md](docs/platform.md) explains each call and why it
was the one chosen; the short version:

**Linux** — `make` links against the system Qt. `make deb` wraps it with
dependencies read out of the binary. The `.desktop` entry and the icon theme are
what make the taskbar icon appear **under Wayland**: a Wayland shell ignores the
window icon entirely and matches the surface to a desktop entry by name.

**macOS** — the build targets `x86_64;arm64` by default, so one binary runs on
Intel and Apple silicon. That needs a universal Qt; if yours is
single-architecture, configure with `cmake .. -DCMAKE_OSX_ARCHITECTURES=arm64`.
`sudo make install` puts `Weight.app` in `/Applications` and symlinks `weight`
onto your PATH, so the app and the command are the same binary.

**Windows** — `make` produces `weight.exe`, and `windeployqt` places the Qt DLLs
and the platform plugin beside it. `make install` copies the set into
`%LOCALAPPDATA%\Programs\Weight`, puts it on your PATH so `weight` works from a
terminal, and adds Start-menu and desktop shortcuts. **No administrator rights.**
Documents is located with `SHGetKnownFolderPath(FOLDERID_Documents)` — through
`QStandardPaths`, which makes exactly that call — so a redirected or
OneDrive-backed Documents folder is honoured instead of assuming
`C:\Users\<name>\Documents`.

> **One honest caveat.** A single `.exe` with no DLLs at all is only possible
> with a **statically linked Qt**, and static linking was removed from this
> project at your request. What you get instead is `weight.exe` plus the handful
> of Qt DLLs `windeployqt` collects, all in one folder that can be copied
> anywhere and run. If a genuinely DLL-free executable matters more than
> dropping static builds, that decision would need revisiting — the two cannot
> both hold.

### Licensing

Qt is used under the **LGPL v3**. Dynamic linking — which is all this project
does now — carries no obligation beyond attribution, because the user can
substitute their own Qt.

Open Runde is used under the SIL Open Font License 1.1; a copy travels with the
source at `assets/fonts/OpenRunde-LICENSE.txt` and is installed alongside the
program.

---

## What the chart covers

The horizontal axis is derived from your data, not fixed:

- **No valid measurements** — the chart starts **today**. There is nothing to
  look back at.
- **At least one measurement** — the chart starts at the **oldest** one, so
  nothing is cropped off the left edge.
- The right edge is whichever is **further away**: one year from the first
  measurement, or three months from the last. The first keeps a young record
  from being drawn on a comically short axis; the second keeps a long record
  from ending exactly at today, which would leave no room to see where the
  trend is heading.

| Your data | Axis |
|---|---|
| empty file | today → today + 1 year |
| two weeks | first reading → first + 1 year |
| eight months | first reading → first + 1 year |
| two years | first reading → last + 3 months |

Tick spacing adapts to the resulting span — weekly for a year, wider for a long
record — so the labels never collapse into a grey band.

---

## Where your data lives

A folder named **`weight`**, searched for before it is created:

**Linux**
1. `$XDG_DOCUMENTS_DIR/weight`
2. `~/Documents/weight`
3. `~/documents/weight`
4. `~/Documentos/weight`
5. `~/documentos/weight`
6. `~/.local/share/weight`

**Windows**
1. the Documents known folder, resolved through the platform API, so a
   redirected or roamed Documents folder is honoured
2. `%USERPROFILE%\Documents\weight`

**macOS**
1. `~/Documents/weight`
2. `~/Documentos/weight`
3. `~/Library/Application Support/weight`

If one of these already exists it is adopted; only when none does is the first
suitable one created. Override with `weight --workspace /some/path`.

```
weight/
├── weight-data.csv    the history
├── config.txt         what you last had switched on
└── images/            generated charts
```

`config.txt` is rewritten the moment you tick a box or move the slider, on a
worker thread so the click stays instant. Curve visibility is keyed by the
curve's stable id, so inserting a curve never reassigns your saved choices.

**`make uninstall` never removes any of this.** It reads the manifest CMake
wrote at install time and deletes exactly what was installed.

---

## Language

English by default. Spanish when the system both supports and uses it.

The chain, highest priority first: `WEIGHT_LANG`, `LC_ALL`, `LC_MESSAGES`,
`LANG`, then Qt's view of the platform UI language (which is what Windows and
macOS actually use). A value of `C` or `POSIX` means *no locale configured* and
selects English. Anything beginning `es` selects Spanish; everything else falls
back to English.

```sh
WEIGHT_LANG=es_CL weight     # force Spanish
WEIGHT_LANG=C weight         # force English
```

---

## Concurrency

Three things run at once: the window, the fitting and rendering, and saving.

The thread pool is sized from **work, not cores** —
`max(minimumThreads, curveCount + auxiliaryThreads)` — so a two-core machine
still dispatches every curve simultaneously and lets the operating system
schedule them. Curves that are switched off get no task at all.

Renders are **coalesced**: dragging the slider produces requests faster than
they can be served, so only the newest survives, and a result belonging to a
superseded request is discarded rather than drawn over a newer one.

`test_concurrency` asserts the concurrent and serial fits are **bit-identical**,
which is what shows the concurrency introduced no race.

---

## One instance at a time

Enforced with a **named local socket**, not a lock file. A lock file records an
intention: kill the process and it survives, blocking every later run until
someone deletes it by hand. A socket is owned by the kernel — on Windows a named
pipe vanishes with the process, and on Unix a stale socket file refuses
connections immediately, which is exactly the signal that the owner is gone.

A second copy shows one window naming the running process id in small type, then
exits with status 5.

---

## Command line

```
weight [options] [workspace]

  --workspace <path>         folder holding weight-data.csv, images/, config.txt
  --origin-date <yyyy-MM-dd> calendar date mapped to day 0.0
  --smoothness <value>       initial smoothness
  --x-min --x-max            day-axis bounds
  --y-min --y-max            mass-axis bounds, kg
  --width-px --height-px     exported image size
  --dpi                      exported image resolution
  --self-test                run the built-in checks and exit
  --quiet                    only report errors
  --help --version
```

Exit statuses: `0` saved, `2` bad usage, `3` unusable workspace, `4` cancelled,
`5` already running.

---

## Compatibility

"Tested" means built and exercised there.

| Platform | Architecture | Status |
|---|---|---|
| Ubuntu 24.04, Qt 6.4, GCC 13 | x86_64 | **Tested** — builds, 9/9 suites, 38/38 self-test, .deb built and installed |
| Debian, Ubuntu, Mint, Fedora, Arch, openSUSE, Alpine | x86_64, aarch64 | Expected |
| macOS 11+ | x86_64, arm64 | **Untested** — no machine available |
| Windows 10/11 | amd64, arm64 | **Untested** — no machine available |

Not supported: Qt 5 (Qt 6 APIs throughout), 32-bit targets (untested).

---

## Documentation

| | |
|---|---|
| [docs/curves.md](docs/curves.md) | adding a curve |
| [docs/architecture.md](docs/architecture.md) | layers, patterns, threading |
| [docs/mathematics.md](docs/mathematics.md) | the estimators, and one corrected defect |
| [docs/platform.md](docs/platform.md) | the three operating systems, and which call answers each question |
| [docs/packaging.md](docs/packaging.md) | packages, install layout, icons |
| [docs/testing.md](docs/testing.md) | what the suite covers |
| [CHANGELOG.md](CHANGELOG.md) | what changed in this release, and why |

---

## Licence

MIT — see [LICENSE](LICENSE). Qt under the LGPL v3; Open Runde under the SIL OFL 1.1.
