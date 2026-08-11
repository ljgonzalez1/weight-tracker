#!/bin/sh
# ---------------------------------------------------------------------------
# Builds target/packages/Weight-<version>-<arch>.dmg from the bundle that the
# ordinary build already produced in target/.
#
# Deliberately does NOT reconfigure the caller's build tree. The previous
# version re-ran `cmake -S . -B build` with its own options, silently rewriting
# the architecture and the install prefix of a tree the user had configured
# themselves. This one builds the existing tree and packages what comes out;
# choose the architecture at configure time if you want something other than
# the default.
#
# The bundle is made relocatable with macdeployqt, which copies the Qt
# frameworks inside it and rewrites the load paths. Without that step the
# application launches only on a machine that already has Qt installed in the
# same location, which defeats the purpose of shipping a bundle.
# ---------------------------------------------------------------------------
set -eu

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

VERSION="$(cat VERSION)"
BUILD_DIR="${WEIGHT_BUILD_DIR:-$root/build}"
TARGET_DIR="${WEIGHT_TARGET_DIR:-$root/target}"
STAGE_ROOT="${WEIGHT_STAGING_DIR:-$BUILD_DIR/package-staging}/dmg"
OUT_DIR="$TARGET_DIR/packages"
APP="$TARGET_DIR/Weight.app"

say()  { printf '\033[1;34m==>\033[0m %s\n' "$1"; }
warn() { printf '\033[1;33m==>\033[0m %s\n' "$1" >&2; }
die()  { printf '\033[1;31m==>\033[0m %s\n' "$1" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || die "build-dmg.sh only runs on macOS."
command -v hdiutil >/dev/null 2>&1 || die "hdiutil not found; this is not a macOS system."

say "Building Weight $VERSION"
cmake --build "$BUILD_DIR" --target weight --parallel "${JOBS:-$(sysctl -n hw.ncpu)}"

[ -d "$APP" ] || die "The bundle was not produced at $APP."

# Which architectures actually came out, rather than which were asked for.
ARCHS="$(lipo -archs "$APP/Contents/MacOS/weight" 2>/dev/null | tr ' ' '-' || uname -m)"
say "Bundle architectures: $ARCHS"

# --- Make the bundle relocatable -------------------------------------------
#
# macdeployqt is run on a copy inside the staging tree, never on the bundle in
# target/: it rewrites load paths in place, and a second run over an already
# deployed bundle can corrupt it. target/Weight.app therefore always stays the
# plain build output, and the .dmg gets the deployed copy.
rm -rf "$STAGE_ROOT"
mkdir -p "$STAGE_ROOT/image"
cp -R "$APP" "$STAGE_ROOT/image/"
DEPLOYED="$STAGE_ROOT/image/Weight.app"

MACDEPLOYQT="$(command -v macdeployqt || true)"
if [ -z "$MACDEPLOYQT" ] && [ -n "${QT_PREFIX:-}" ]; then
    MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
fi
if [ -x "$MACDEPLOYQT" ]; then
    say "Bundling the Qt frameworks with macdeployqt"
    "$MACDEPLOYQT" "$DEPLOYED" -always-overwrite
else
    warn "macdeployqt was not found. The .dmg will contain a bundle that needs Qt"
    warn "installed on the target machine. Set QT_PREFIX to your Qt installation"
    warn "for a self-contained bundle."
fi

# --- Optional signing -------------------------------------------------------
# Unsigned bundles remain usable; Gatekeeper asks the user to confirm on first
# launch. Signing is applied only when an identity is supplied.
if [ -n "${CODESIGN_IDENTITY:-}" ]; then
    say "Signing with: $CODESIGN_IDENTITY"
    codesign --force --deep --options runtime --sign "$CODESIGN_IDENTITY" "$DEPLOYED"
    codesign --verify --deep --strict --verbose=2 "$DEPLOYED"
else
    say "No CODESIGN_IDENTITY set; the bundle will be unsigned."
fi

# --- Disk image -------------------------------------------------------------
ln -s /Applications "$STAGE_ROOT/image/Applications"   # the drag-to-install layout

mkdir -p "$OUT_DIR"
DMG="$OUT_DIR/Weight-${VERSION}-${ARCHS}.dmg"
rm -f "$DMG"

say "Creating $DMG"
hdiutil create -volname "Weight $VERSION" \
    -srcfolder "$STAGE_ROOT/image" \
    -ov -format UDZO "$DMG" >/dev/null

say "Disk image written: $DMG"
say "Nothing was installed. Open it and drag Weight to Applications."
