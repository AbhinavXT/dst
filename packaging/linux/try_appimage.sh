#!/bin/sh
# =============================================================================
#  try_appimage.sh APPIMAGE -- start the AppImage on a fresh Linux (session 269)
#  ---------------------------------------------------------------------------
#  Run inside a fresh container (CI: AlmaLinux 8 and 9 for RHEL 8 and 9,
#  Ubuntu 24.04). Installs only what any Linux desktop has and an AppImage
#  leaves to its host (X, GL, fonts, harfbuzz: GTK 3 brings them), checks
#  that NO Qt is installed, and starts DLConsole on a virtual display.
#  Pass = still running after 15 s.
# =============================================================================
set -u
if command -v dnf >/dev/null 2>&1; then
    dnf install -y -q xorg-x11-server-Xvfb gtk3 mesa-libGL mesa-libEGL >/dev/null || exit 1
else
    apt-get update -qq &&
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq xvfb libgtk-3-0t64 libgl1 libegl1 >/dev/null || exit 1
fi
if ls /usr/lib64 /usr/lib/x86_64-linux-gnu 2>/dev/null | grep -q '^libQt'; then
    echo "Qt is installed here: this would not prove the AppImage carries its own"; exit 1
fi
Xvfb :99 >/dev/null 2>&1 &
sleep 2
DISPLAY=:99 APPIMAGE_EXTRACT_AND_RUN=1 timeout 15 "$1"
rc=$?
echo "$(. /etc/os-release; echo "$PRETTY_NAME"): DLConsole exit $rc (124 = alive at timeout)"
[ "$rc" -eq 124 ]
