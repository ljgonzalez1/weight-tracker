#!/bin/sh
# ---------------------------------------------------------------------------
# Builds a .deb package. It installs nothing.
#
# The dependency question
# ----------------------
# This package ships a dynamically linked executable, so it is portable only if
# Depends names exactly the shared libraries the binary really needs, under the
# names the target distribution uses. Writing that list by hand is wrong twice:
#
#   * it goes stale. A Qt module linked next month is not in the list, the
#     package installs cleanly, and the program then fails to start.
#   * the names differ per release. Ubuntu 24.04 and Debian 13 carry the t64
#     suffix from the 64-bit time_t transition (libqt6core6t64); Debian 12 and
#     Ubuntu 22.04 do not. Either list is wrong on half the targets.
#
# So the list is not written by hand. dpkg-shlibdeps reads the ELF headers of
# the binary that was just built, resolves each SONAME to the package that owns
# it on this machine, and emits the Depends line with correct version floors.
# It is the same tool the Debian archive uses on every package it ships.
#
# What dpkg-shlibdeps cannot see
# ------------------------------
# Only libraries in DT_NEEDED. Qt loads its platform plugin (libqxcb.so,
# libqwayland-*.so) with dlopen at run time, so it appears in no ELF header and
# no automatic tool can find it. Without it the program dies at startup with
# "could not load the Qt platform plugin", which looks like an application bug
# and is not. qt6-qpa-plugins is therefore added by hand, and it is the only
# entry that is.
#
# Layout
#   staging -> $BUILD_DIR/package-staging/deb   (inside the build tree)
#   output  -> target/packages/weight_<version>_<arch>.deb
# ---------------------------------------------------------------------------
set -eu

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

VERSION="$(cat VERSION)"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
BUILD_DIR="${WEIGHT_BUILD_DIR:-$root/build}"
TARGET_DIR="${WEIGHT_TARGET_DIR:-$root/target}"
STAGE_ROOT="${WEIGHT_STAGING_DIR:-$BUILD_DIR/package-staging}/deb"
STAGE="$STAGE_ROOT/weight_${VERSION}_${ARCH}"
OUT_DIR="$TARGET_DIR/packages"
PREFIX="/usr"

say()  { printf '\033[1;34m==>\033[0m %s\n' "$1"; }
warn() { printf '\033[1;33m==>\033[0m %s\n' "$1" >&2; }
die()  { printf '\033[1;31m==>\033[0m %s\n' "$1" >&2; exit 1; }

command -v dpkg-deb >/dev/null 2>&1 || die \
    "dpkg-deb not found. Install it with:  sudo apt install dpkg-dev"

# Checking permissions with stat is not enough: a directory can belong to you
# while the files inside belong to root, which is exactly what one earlier
# `sudo make deb` leaves behind. So the check attempts the operation and
# reports the failure, which catches every ownership arrangement.
clear_or_explain() {
    target="$1"
    [ -e "$target" ] || return 0
    if rm -rf "$target" 2>/dev/null && [ ! -e "$target" ]; then
        return 0
    fi
    owner="$(stat -c '%U' "$target" 2>/dev/null || echo 'another user')"
    printf '\033[1;31m==>\033[0m %s\n\n' \
        "Cannot clear $target - it belongs to $owner, not to you." >&2
    printf '    %s\n'   "A previous 'sudo make deb' or 'sudo make install' leaves this" >&2
    printf '    %s\n\n' "behind. Building a package needs no privileges at all." >&2
    printf '        sudo rm -rf %s\n\n' "$target" >&2
    printf '    %s\n' "then run 'make deb' again as yourself." >&2
    exit 1
}

say "Building Weight $VERSION for $ARCH"
cmake --build "$BUILD_DIR" --target weight --parallel "${JOBS:-$(nproc 2>/dev/null || echo 1)}"

clear_or_explain "$STAGE"
mkdir -p "$STAGE/DEBIAN" 2>/dev/null || { clear_or_explain "$STAGE_ROOT"; mkdir -p "$STAGE/DEBIAN"; }

say "Staging into $STAGE"
DESTDIR="$STAGE" cmake --install "$BUILD_DIR" --prefix "$PREFIX" >/dev/null

BINARY="$STAGE$PREFIX/bin/weight"
[ -x "$BINARY" ] || die "The binary did not reach the staging directory."

# --- Runtime dependencies, read out of the binary ---------------------------
#
# dpkg-shlibdeps insists on running from a directory with debian/control beside
# it, because in a normal package build that is where it reads the source name
# from. A throwaway one is created inside the staging tree and never ships.
compute_depends() {
    command -v dpkg-shlibdeps >/dev/null 2>&1 || return 1
    work="$STAGE_ROOT/shlibdeps"
    rm -rf "$work"
    mkdir -p "$work/debian"
    printf 'Source: weight\nPackage: weight\nArchitecture: any\n' > "$work/debian/control"
    : > "$work/debian/substvars"
    # -O prints the field on stdout instead of appending to substvars.
    # --ignore-missing-info keeps a library installed outside dpkg's control
    # (a hand-built Qt under /opt, say) from aborting the package: it is
    # reported and simply not named as a dependency.
    ( cd "$work" && dpkg-shlibdeps -O --ignore-missing-info "$BINARY" 2>"$work/stderr" ) \
        | sed -n 's/^shlibs:Depends=//p'
}

DEPENDS="$(compute_depends || true)"

if [ -n "$DEPENDS" ]; then
    say "Dependencies read from the binary by dpkg-shlibdeps:"
    printf '    %s\n' "$DEPENDS"
    if [ -s "$STAGE_ROOT/shlibdeps/stderr" ]; then
        warn "dpkg-shlibdeps also reported:"
        sed 's/^/    /' "$STAGE_ROOT/shlibdeps/stderr" >&2
    fi
else
    # Fallback for a machine without dpkg-dev. Names are resolved per release
    # rather than hard-coded, for the t64 reason explained at the top.
    warn "dpkg-shlibdeps is unavailable; falling back to a resolved name list."
    warn "Install dpkg-dev for a package that names its real dependencies."
    resolve() {
        if apt-cache show "${1}t64" >/dev/null 2>&1; then echo "${1}t64"; else echo "$1"; fi
    }
    DEPENDS="$(resolve libqt6core6) (>= 6.2.0), $(resolve libqt6gui6) (>= 6.2.0)"
    DEPENDS="$DEPENDS, $(resolve libqt6widgets6) (>= 6.2.0)"
    DEPENDS="$DEPENDS, $(resolve libqt6network6) (>= 6.2.0)"
    DEPENDS="$DEPENDS, $(resolve libqt6concurrent6) (>= 6.2.0), libc6, libstdc++6"
fi

# dlopen'd, therefore invisible to every automatic tool; see the header.
case "$DEPENDS" in
    *qt6-qpa-plugins*) ;;
    *) DEPENDS="$DEPENDS, qt6-qpa-plugins" ;;
esac

INSTALLED_SIZE="$(du -ks "$STAGE$PREFIX" | cut -f1)"

cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: weight
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Depends: $DEPENDS
Installed-Size: $INSTALLED_SIZE
Maintainer: Weight maintainers <weight@example.invalid>
Description: Body-mass logger with nonparametric trend estimation
 Weight records body-mass measurements and draws the trend underneath them
 using three independent nonparametric estimators: an adaptive Gaussian
 spline, a ridge-regularised multiquadric radial basis regression and a
 local linear LOESS fit.
 .
 Charts are rendered natively with Qt, so no plotting stack, Python
 interpreter or external process is involved. Measurements are stored in a
 plain semicolon-separated file that remains readable and editable by hand.
CONTROL

cat > "$STAGE/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t /usr/share/icons/hicolor || true
    fi
fi
exit 0
POSTINST

cat > "$STAGE/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t /usr/share/icons/hicolor || true
    fi
fi
# The workspace folder (by default ~/Documents/weight) is deliberately left in
# place, even on purge. It holds weight-data.csv, the generated images and
# config.txt; those belong to the person, not to the package.
exit 0
POSTRM

chmod 0755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"
rm -rf "$STAGE_ROOT/shlibdeps"

mkdir -p "$OUT_DIR" 2>/dev/null || { clear_or_explain "$OUT_DIR"; mkdir -p "$OUT_DIR"; }
PACKAGE="$OUT_DIR/weight_${VERSION}_${ARCH}.deb"
clear_or_explain "$PACKAGE"
dpkg-deb --build --root-owner-group "$STAGE" "$PACKAGE" >/dev/null

say "Package written: $PACKAGE"
printf '    Depends: %s\n' "$(dpkg-deb -f "$PACKAGE" Depends)"
say "Nothing was installed. To install it:  sudo apt install $PACKAGE"

if command -v lintian >/dev/null 2>&1; then
    lintian --no-tag-display-limit "$PACKAGE" || true
fi
