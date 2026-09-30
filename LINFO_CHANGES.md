# DLConsole — @linfo (LOCO_INFO) live + replay + CRC

Full project tree. Build as usual:  qmake && make   (or open DLConsole.pro)

Changed vs your upload:
- capturedecoder.cpp    : loco_info_crc recipe in parseLine() -> jamcrc(b,0,n-4)==last4le()
- capturedecoder.h      : recipe documented in header comment
- lococonsolewindow.cpp : CapType::Linfo added to kTabOrder + kLinkOrder (live view)
- schema/kavach.xml     : LINFO comment updated (CRC validated; golden 0xC7C020BC)

Excluded from this zip (unchanged, you already have them):
- replay/*.cap  and  SAVED_LOGS/*.log   (your capture data, ~19 MB)
- *.o objects, the stale DLConsole.app Mac binary, __pycache__  (regenerated on build)

Effect: @linfo now appears in the live console (was replay-only) and the CRC
row reads PASS instead of n/a in both views.
