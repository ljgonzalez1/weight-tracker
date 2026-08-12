# Building from source

From a clean machine to a running program, per operating system. Every command
is meant to be pasted in order; nothing is left as "install the dependencies".

The repository root holds a `weight/` directory, and **that directory is the
project**. Everything below assumes you have run:

```sh
git clone https://github.com/ljgonzalez1/weight.git
cd weight/weight
```

If `ls` shows `CMakeLists.txt`, `src/` and `assets/`, you are in the right
place. If it shows another `weight/`, go one level deeper.

The build is the same three commands everywhere:

```sh
mkdir build && cd build
cmake ..
make            # or: cmake --build . --parallel
```

What differs per platform is only what has to be installed first.

---

## Requirements, in one table

| | Minimum | Why |
|---|---|---|
| **Qt** | 6.2 | Qt 6 APIs throughout; Qt 5 will not compile |
| **Compiler** | GCC 10, Clang 12, or MSVC 2019 16.11 | C++20: `<span>`, `<concepts>`, designated initialisers |
| **CMake** | 3.21 | `ADDITIONAL_CLEAN_FILES`, the preflight module |
| Qt modules | Core, Gui, Widgets, Network, Concurrent | Network is the single-instance socket; Concurrent is the fitting pool |
| Qt Test | optional | only to build the unit suites |

`cmake ..` checks all of this before compiling a single file and names the
package to install if something is missing. A missing *packaging* tool is a
warning, not an error — you can build and run without ever making a package.

---

## Linux

### Debian, Ubuntu, Linux Mint, Pop!\_OS

```sh
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools
```

Add packaging tools only if you intend to run `make deb`:

```sh
sudo apt install dpkg-dev binutils
```

`qt6-base-dev` brings the headers, the CMake config files, `moc`, `rcc`, `uic`
and Qt Test. `qt6-base-dev-tools` brings the tool binaries themselves. On a
machine with no desktop installed you also want the platform plugins, which the
runtime needs and the build does not:

```sh
sudo apt install qt6-qpa-plugins
```

### Fedora, RHEL 9+, Rocky, AlmaLinux

```sh
sudo dnf install gcc-c++ cmake qt6-qtbase-devel qt6-qtbase-gui
```

### Arch, Manjaro, EndeavourOS

```sh
sudo pacman -S --needed base-devel cmake qt6-base
```

### openSUSE Leap / Tumbleweed

```sh
sudo zypper install gcc-c++ cmake qt6-base-devel qt6-widgets-devel qt6-test-devel
```

`qt6-test-devel` is a separate package here; without it the suites are skipped
and `cmake ..` says so.

### Alpine

```sh
doas apk add build-base cmake qt6-qtbase-dev qt6-qtbase-x11
```

Gui and Widgets live in `qt6-qtbase-x11`, not in `qt6-qtbase`. Installing only
the latter produces a program that builds and then cannot open a window.

### Then

```sh
mkdir build && cd build
cmake ..
make -j"$(nproc)"
make test
./../target/weight
```

---

## macOS

### 1. Command-line tools

```sh
xcode-select --install
```

This is the compiler and the SDK. The full Xcode application is not required.

### 2. Qt — pick one of the two

**Homebrew** (simplest):

```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install cmake qt@6
```

Homebrew's `qt@6` is keg-only, so CMake has to be told where it is:

```sh
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
make -j"$(sysctl -n hw.ncpu)"
```

Homebrew builds Qt for **one architecture only** — the one your Mac runs. The
project defaults to a universal `x86_64;arm64` binary, which such a Qt cannot
satisfy, so on Homebrew you must also say:

```sh
cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)" \
         -DCMAKE_OSX_ARCHITECTURES=arm64        # or x86_64 on an Intel Mac
```

**The official Qt installer** (needed for a universal binary and for
`macdeployqt`):

1. Create a Qt account at <https://login.qt.io/register>.
2. Download the online installer from
   <https://www.qt.io/download-qt-installer-oss> (the open-source build, under
   the LGPL — which is how this project uses Qt).
3. In the component tree, select:
   - **Qt → Qt 6.8.x → macOS** (the desktop kit; it is universal)
   - **Qt → Developer and Designer Tools → CMake** and **Ninja**, unless you
     already have them
4. Install to the default location, `~/Qt`.

Then:

```sh
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.2/macos"
make -j"$(sysctl -n hw.ncpu)"
```

The official kit is universal, so the default `x86_64;arm64` works and one
binary runs on both Intel and Apple silicon.

### 3. Verify

```sh
make test
open ../target/Weight.app
```

---

## Windows

Two routes. **MSVC plus the official Qt installer** is the supported one and is
what `make dist` and `install.ps1` are written against. vcpkg is documented
because it automates the same thing for people who already use it.

### Route A — MSVC 2022 and the official Qt installer

**1. Visual Studio 2022 Build Tools**

Download from <https://visualstudio.microsoft.com/downloads/> (Build Tools are
free and sufficient; the full IDE also works). In the installer select the
workload:

- **Desktop development with C++**

which brings the MSVC v143 toolset, the Windows 11 SDK, CMake and Ninja.

**2. Qt 6**

1. Create a Qt account at <https://login.qt.io/register>.
2. Download the online installer from
   <https://www.qt.io/download-qt-installer-oss>.
3. Choose a **custom installation** and select, in the component tree:
   - **Qt → Qt 6.8.x → MSVC 2022 64-bit** — this is the compiler kit, and it
     must match the compiler you installed in step 1. Do **not** pick the MinGW
     kit if you are building with MSVC; the two produce incompatible binaries.
   - **Qt → Qt 6.8.x → Additional Libraries** — nothing here is needed. The
     project uses only Qt Base.
   - **Qt → Developer and Designer Tools → CMake** and **Ninja** if you did not
     get them from Visual Studio.
4. Install to the default location, `C:\Qt`.

`windeployqt.exe` — which collects the Qt DLLs next to the executable — arrives
with the kit, in `C:\Qt\6.8.2\msvc2022_64\bin`. Nothing extra to install.

**3. Build**

Open **"x64 Native Tools Command Prompt for VS 2022"** from the Start menu.
That shell, and not an ordinary `cmd`, is what puts the compiler on `PATH`.

```bat
cd path\to\weight\weight
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=C:/Qt/6.8.2/msvc2022_64
cmake --build . --config Release --parallel
```

Use forward slashes in `CMAKE_PREFIX_PATH`, and adjust `6.8.2` to the version
you installed. The result is `target\weight.exe` with its Qt DLLs beside it.

**4. Package and install**

```bat
cmake --build . --target dist
```

which produces `target\packages\weight-<version>-windows-x64\` and a `.zip` of
it. Inside the folder, `install.ps1` puts `weight` on your `PATH` and adds
Start-menu and desktop shortcuts, **per user, with no administrator rights**:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
powershell -ExecutionPolicy Bypass -File .\install.ps1 -Uninstall
```

### Route B — vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install qtbase:x64-windows
```

`qtbase` is the only port needed; it is a long build, on the order of an hour.
Then configure with the vcpkg toolchain:

```bat
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
         -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --config Release --parallel
```

Visual Studio 2022 with the **Desktop development with C++** workload is still
required: vcpkg supplies the libraries, not the compiler.

### MinGW

Qt's MinGW kit works, with the matching kit selected in the Qt installer and
`-G "MinGW Makefiles"`. It is not tested here, and `make dist` assumes MSVC
layout, so treat it as unsupported rather than as broken.

---

## After the build, on every platform

```sh
make test                     # the unit suites, headless
../target/weight --self-test  # the binary checking itself
../target/weight              # the program
```

`make test` sets `QT_QPA_PLATFORM=offscreen`, so it works over SSH and inside a
container with no display.

Where things go:

```
build/                     everything disposable, including package staging
target/                    every deliverable
target/weight              the executable  (weight.exe on Windows)
target/Weight.app          the bundle      (macOS)
target/packages/           the .deb, the .dmg, the Windows folder and .zip
```

---

## Configure-time options

| Option | Default | Meaning |
|---|---|---|
| `-DCMAKE_PREFIX_PATH=<path>` | — | where Qt is, when it is not on the system path |
| `-DCMAKE_BUILD_TYPE=Release` | `Release` | `Debug` for symbols and assertions |
| `-DWEIGHT_BUILD_TESTS=OFF` | `ON` | skip the unit suites |
| `-DWEIGHT_TARGET_DIR=<path>` | `<source>/target` | where deliverables go |
| `-DCMAKE_OSX_ARCHITECTURES=arm64` | `x86_64;arm64` | macOS, for a single-architecture Qt |
| `-DCMAKE_INSTALL_PREFIX=<path>` | `/usr/local` | where `make install` writes |

---

## When it goes wrong

### `Could NOT find Qt6 (missing: Qt6_DIR)`

Qt is not installed, or CMake cannot see it. Point at it explicitly:

```sh
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt/6.8.2/gcc_64
```

The path is the directory containing `lib/cmake/Qt6`.

### `ld: cannot open output file .../target/weight: No such file or directory`

The `target/` directory was removed — by `make clean`, or by hand — and, before
release 0.50.0, only `cmake ..` recreated it. Fixed: the directory is now
recreated immediately before the link, in the same rule. If you are on an older
checkout, `cmake ..` once and it will build.

### `dpkg-deb: error: control directory has bad permissions 777`

Your `umask` is `000`, so every directory the packaging script created came out
world-writable, and `dpkg-deb` requires the control directory to be between
`0755` and `0775`. Fixed in 0.50.0: the script sets its own umask and states
every mode explicitly. See [packaging.md](packaging.md) for the NTFS variant of
the same problem.

### `This application failed to start because no Qt platform plugin could be initialized`

The Qt runtime is present but its platform plugin is not. On Linux install
`qt6-qpa-plugins`; on Windows check that `platforms\qwindows.dll` sits beside
the executable, which is `windeployqt`'s job.

### The build succeeds and the window has no icon or the wrong font

The assets are compiled into the binary through `assets/weight.qrc`. Run
`../target/weight --self-test`: the "Embedded assets" section reports whether
the icon and all four font faces are really inside the executable.
