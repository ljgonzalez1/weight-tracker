# Packaging

Every target stages inside the build tree and writes its result to
`target/packages/`. None of them installs anything, and none of them needs
privileges.

```
build/package-staging/     scratch, disposable with the rest of build/
target/packages/           the artefacts
```

## The whole thing in four lines

```sh
cd weight/weight            # the project directory, not the repository root
mkdir build && cd build
cmake ..
make package                # deb on Linux, dmg on macOS, folder+zip on Windows
```

`make package` is an alias for whichever of `deb`, `dmg` and `dist` applies to
the machine you are on. Each is also available by name, and each **rebuilds
what it needs first** — you do not have to run `make` beforehand.

| Command | Platform | Result |
|---|---|---|
| `make deb` | Linux | `target/packages/weight_<version>_<arch>.deb` |
| `make dmg` | macOS | `target/packages/Weight-<version>-<archs>.dmg` |
| `make dist` | Windows | `target/packages/weight-<version>-windows-<arch>/` and `.zip` |
| `make package` | any | whichever of the three applies |

Nothing is installed by any of them. Installing is a separate, explicit step:

```sh
sudo apt install ../target/packages/weight_0.50.0_amd64.deb   # Linux
sudo make install                                              # from source, any platform
sudo make uninstall                                            # removes exactly that
```

`make deb` needs no privileges. If you ever ran it under `sudo`, the leftovers
are root-owned; the next run detects that and prints the exact `rm` command
instead of failing with "permission denied".

---

## Linux — `make deb`

```sh
cd build && cmake .. && make deb
sudo apt install ../target/packages/weight_0.50.0_amd64.deb
```

`apt install` rather than `dpkg -i`, because apt resolves the dependencies the
package declares; `dpkg -i` installs it and then leaves it unconfigured if
something is missing.

### How the dependencies are decided

This package ships a **dynamically linked** executable, so it is portable only
if `Depends:` names exactly the shared libraries the binary really needs, under
the names the target distribution uses. A hand-written list is wrong twice over:

- **It goes stale.** A Qt module linked next month is not in the list. The
  package installs cleanly and the program then fails to start.
- **The names differ per release.** Ubuntu 24.04 and Debian 13 carry the `t64`
  suffix from the 64-bit `time_t` transition — `libqt6core6t64` — while Debian
  12 and Ubuntu 22.04 do not. Either list is wrong on half the targets.

So the list is not written by hand. **`dpkg-shlibdeps`** reads the ELF headers
of the binary that was just built, resolves each `SONAME` to the package that
owns it on this machine, and emits the field with correct version floors. It is
the same tool the Debian archive runs on every package it ships.

On Ubuntu 24.04 that produces:

```
Depends: libc6 (>= 2.34), libgcc-s1 (>= 3.0), libqt6core6t64 (>= 6.4.0),
         libqt6gui6t64 (>= 6.4.0), libqt6network6t64 (>= 6.1.2),
         libqt6widgets6t64 (>= 6.1.2), libstdc++6 (>= 13), qt6-qpa-plugins
```

Note what is **not** there: `libqt6concurrent6`. The hand-written list used to
include it, and it was wrong — Qt Concurrent is header-only in Qt 6, so the
binary has no such `DT_NEEDED` entry. That is one dependency the package used to
demand and did not need, found by reading the binary instead of guessing.

Build the package on the **oldest** distribution you intend to support. The
version floors come from what is installed here, so a package built against
Qt 6.7 will refuse to install on a machine with Qt 6.4.

### The one entry added by hand

`qt6-qpa-plugins`.

`dpkg-shlibdeps` sees only libraries in `DT_NEEDED`. Qt loads its **platform
plugin** — `libqxcb.so`, `libqwayland-*.so` — with `dlopen` at run time, so it
appears in no ELF header and no automatic tool can find it. Without it the
program dies at startup with

```
This application failed to start because no Qt platform plugin could be
initialized.
```

which looks like a bug in the application and is not. It is therefore added
explicitly, and it is the only entry that is.

### If `dpkg-shlibdeps` is missing

The script falls back to a resolved name list — still resolving `t64` per
release rather than hardcoding it — and says so. Install `dpkg-dev` for a
package that names its real dependencies.

### What the package contains

```
/usr/bin/weight
/usr/share/applications/weight.desktop
/usr/share/icons/hicolor/{16,24,32,48,64,96,128,256,512}x*/apps/weight.png
/usr/share/icons/hicolor/scalable/apps/weight.svg
/usr/share/doc/weight/{README.md,LICENSE,OpenRunde-LICENSE.txt}
```

`postinst` and `postrm` refresh the desktop database and the icon cache, which
is what makes the launcher entry appear immediately rather than after the next
login.

**Removal never deletes data.** Neither `remove` nor `purge` touches the
workspace folder. `weight-data.csv`, the images and `config.txt` belong to the
person, not to the package.

### Permissions, and the two ways they go wrong

`dpkg-deb` refuses to build a package whose control directory is group- or
world-writable:

```
dpkg-deb: error: control directory has bad permissions 777 (must be >=0755 and <=0775)
```

Both causes are environmental, neither is your mistake, and the script now
handles both.

**A permissive umask.** `mkdir` applies the caller's `umask`, so with
`umask 000` — which some desktop sessions and many shell configurations set —
every staged directory comes out `0777`. The script therefore sets its own
`umask 022` and states the mode of everything it writes explicitly: directories
`0755`, files `0644`, the binary and the maintainer scripts `0755`. Ownership
is forced to `root:root` by `--root-owner-group`, so the package does not carry
your user id either.

**A filesystem that has no permissions to set.** If the build tree is on an
NTFS or exFAT partition — the usual arrangement when the source lives on a
partition shared with Windows — `chmod` succeeds and changes nothing, because
the mount fixes the mode for every file. Setting a umask cannot fix that.

So the script verifies rather than assumes: it creates a probe directory, sets
it to `0755`, reads the mode back, and if the filesystem did not store it,
stages the package under `$TMPDIR` instead and says why:

```
==> .../build/package-staging/deb is on a filesystem that does not store POSIX
    permissions (usually NTFS or exFAT). dpkg-deb requires the control
    directory to be 0755, so the package is being staged under ${TMPDIR:-/tmp}.
```

The resulting `.deb` is identical either way: a package records its own modes,
so where it was assembled does not survive into it. If `$TMPDIR` is also on
such a filesystem, the script stops and tells you to point `TMPDIR` somewhere
that is not.

---

## macOS — `make dmg`

```sh
cd build && cmake .. && make dmg
```

Produces `target/packages/Weight-<version>-<archs>.dmg` containing `Weight.app`
and a symlink to `/Applications`, which is the drag-to-install layout everyone
recognises.

The script **does not reconfigure your build tree**. An earlier version re-ran
`cmake -S . -B build` with its own options and silently rewrote the architecture
and install prefix of a tree you had configured yourself. Choose the
architecture at configure time instead:

```sh
cmake .. -DCMAKE_OSX_ARCHITECTURES=arm64        # or "x86_64;arm64"
```

The default is `x86_64;arm64`, which needs a **universal Qt**. The official Qt
binaries are universal; most Homebrew builds are not. The architectures written
into the file name are read back from the built binary with `lipo -archs`, so
the name always describes what is actually inside.

`macdeployqt` copies the Qt frameworks into the bundle and rewrites the load
paths. Without it the app launches only on a machine that already has Qt
installed in the same place. If `macdeployqt` is not on `PATH`, set `QT_PREFIX`;
the script warns rather than silently shipping a bundle that only works on the
machine that built it.

It runs on a **copy** inside the staging tree, never on `target/Weight.app`: it
rewrites load paths in place, and a second run over an already-deployed bundle
can corrupt it. `target/Weight.app` therefore always stays the plain build
output.

Signing is applied only when `CODESIGN_IDENTITY` is set. Unsigned bundles remain
usable; Gatekeeper asks the user to confirm on first launch.

### Regenerating the .icns

```sh
mkdir weight.iconset
for s in 16 32 128 256 512; do
    cp assets/icons/program-icon-$s.png       weight.iconset/icon_${s}x${s}.png
    cp assets/icons/program-icon-$((s*2)).png weight.iconset/icon_${s}x${s}@2x.png
done
iconutil -c icns weight.iconset -o assets/icons/weight.icns
```

---

## Windows — `make dist`

```powershell
cd build; cmake ..; cmake --build . --target dist
```

Produces both a folder and a `.zip` of it:

```
target\packages\weight-<version>-windows-<arch>\
    weight.exe
    Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6Network.dll
    platforms\qwindows.dll
    styles\, imageformats\, iconengines\
    install.ps1
    doc\
target\packages\weight-<version>-windows-<arch>.zip
```

The architecture in the name is read from the **PE header** of the built
executable, not from the host, so a cross-compiled arm64 build on an amd64
machine is not mislabelled.

The folder is produced by running the same `cmake --install` rules the machine
install uses, so it cannot drift from what `make install` gives you.
`WEIGHT_SKIP_USER_INSTALL` keeps that step from also installing the program onto
the machine doing the packaging.

`install.ps1` inside the folder adds the `weight` command to your PATH, a
Start-menu entry and a desktop shortcut. It is **per-user**: no administrator
rights, and uninstalling is `install.ps1 -Uninstall`.

### Regenerating the .ico

```powershell
magick assets/icons/program-icon-16.png  assets/icons/program-icon-32.png `
       assets/icons/program-icon-48.png  assets/icons/program-icon-64.png `
       assets/icons/program-icon-128.png assets/icons/program-icon-256.png `
       assets/icons/program-icon.ico
```

The `.ico` is optional: CMake links `packaging/windows/weight.rc` only when the
file exists, so a build without it succeeds and the executable simply carries
the default Windows icon.

### The honest caveat about DLLs

A single `.exe` with no DLLs beside it requires a **statically linked Qt**,
which the official Qt binaries are not. What you get instead is `weight.exe`
plus the handful of Qt DLLs `windeployqt` collects, in one folder that can be
copied anywhere and run. Shipping a folder that works is better than shipping a
single file that only works on the machine that built it.

If `platforms\qwindows.dll` is missing from the staged folder, `windeployqt` did
not run — it lives in the Qt `bin` directory. The program will otherwise fail at
startup with the platform-plugin error, for the same `dlopen` reason described
under Linux.

---

## Not provided

There is no `make rpm`, `make pkgbuild` or `make apk`. Offering targets that
have never been executed is worse than not offering them. `DESTDIR` works
correctly, which is what those tools actually need:

```sh
DESTDIR=/tmp/stage cmake --install build --prefix /usr
```

Each distribution's own packaging guide takes it from there.
