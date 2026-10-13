#!/usr/bin/env bash
# =============================================================================
#  make_appimage.sh -- DLConsole as ONE file for Linux (session 269)
#  ---------------------------------------------------------------------------
#  An AppImage: the release DLConsole and its Qt libraries and plugins in a
#  single file. Mark it executable and start it; nothing is installed. Runs
#  on Linux distributions at least as new as the one it was built on
#  (CI: AlmaLinux 8 = RHEL 8, glibc 2.28; RHEL 9, Ubuntu 20.04+ and newer).
#
#      packaging/linux/make_appimage.sh build/DLConsole dist/DLConsole-x86_64.AppImage
#
#  Uses linuxdeploy and its Qt plugin, fixed releases, checked against their
#  SHA-256. QMAKE picks the Qt (default: qmake on PATH), which must be the Qt
#  the program was built with. Needs internet access for the two tools.
# =============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
[ $# -eq 2 ] || { sed -n '2,15p' "$0"; exit 64; }
BIN="$(realpath "$1")"
OUT="$(realpath -m "$2")"
WORK="$(realpath -m "${WORK:-package-work-linux}")"

LD_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
LD_SHA=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
QT_URL=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage
QT_SHA=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724

rm -rf "$WORK" && mkdir -p "$WORK" "$(dirname "$OUT")"
cd "$WORK"
fetch() {   # fetch FILE URL SHA256
  curl -fsSL -o "$1" "$2"
  echo "$3  $1" | sha256sum -c --quiet -
  chmod +x "$1"
}
fetch linuxdeploy-x86_64.AppImage           "$LD_URL" "$LD_SHA"
fetch linuxdeploy-plugin-qt-x86_64.AppImage "$QT_URL" "$QT_SHA"

# The icon's name must match Icon= in the .desktop file.
cp "$REPO/myapp_icon.png" dlconsole.png

export APPIMAGE_EXTRACT_AND_RUN=1      # no FUSE needed (CI runners have none)
export QMAKE="${QMAKE:-qmake}"
export LDAI_OUTPUT="$OUT"              # the AppImage plugin's output file
./linuxdeploy-x86_64.AppImage --appdir AppDir \
    --executable "$BIN" \
    --desktop-file "$HERE/dlconsole.desktop" \
    --icon-file dlconsole.png \
    --plugin qt --output appimage

[ -f "$OUT" ] || { echo "no AppImage at $OUT"; exit 1; }
chmod +x "$OUT"
echo "$OUT: $(du -h "$OUT" | cut -f1)"
