#!/usr/bin/env python3
"""Build DLConsole's application icons from PNG renders of icons/dlconsole.svg.

    python3 scripts/app_icons.py PNG_DIR

PNG_DIR holds icon_<N>.png renders of the SVG (any SVG renderer; patch 268
used Qt's QSvgRenderer) for N in 16 20 24 32 40 48 64 128 256 512 1024.
Writes:
    myapp_icon.png        the window icon (main.cpp, images.qrc), 256 px
    app/dlconsole.ico     the Windows .exe icon (RC_ICONS in app/app.pro)
    app/dlconsole.icns    the macOS bundle icon (ICON in app/app.pro; needs iconutil)
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
ICNS_SET = [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2),
            (256, 1), (256, 2), (512, 1), (512, 2)]


def png(src, n):
    with open(os.path.join(src, "icon_%d.png" % n), "rb") as f:
        return f.read()


def write_ico(src, out):
    # ICONDIR + one ICONDIRENTRY per size, each image stored as PNG
    # (Windows Vista and later, and rc.exe, accept PNG entries).
    images = [png(src, n) for n in ICO_SIZES]
    data = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for n, img in zip(ICO_SIZES, images):
        dim = 0 if n >= 256 else n          # 0 means 256
        data += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    with open(out, "wb") as f:
        f.write(data + b"".join(images))


def write_icns(src, out):
    if not shutil.which("iconutil"):
        print("iconutil not found (macOS only): %s left unchanged" % out)
        return
    tmp = tempfile.mkdtemp()
    iconset = os.path.join(tmp, "dlconsole.iconset")
    os.mkdir(iconset)
    for n, scale in ICNS_SET:
        name = "icon_%dx%d%s.png" % (n, n, "@2x" if scale == 2 else "")
        with open(os.path.join(iconset, name), "wb") as f:
            f.write(png(src, n * scale))
    subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o", out])
    shutil.rmtree(tmp)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 64
    src = sys.argv[1]
    shutil.copyfile(os.path.join(src, "icon_256.png"), os.path.join(HERE, "myapp_icon.png"))
    write_ico(src, os.path.join(HERE, "app", "dlconsole.ico"))
    write_icns(src, os.path.join(HERE, "app", "dlconsole.icns"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
