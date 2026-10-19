# =============================================================================
#  flasher.pri -- the Firmware Flasher (Tools ▸ Firmware Flasher…)
#  ---------------------------------------------------------------------------
#  Included by dlcore.pri (compiled once into the dlcore library) so the
#  programs cannot drift apart on which flasher files they compile.
#
#  flash_engine.*, flashworker.* and board_sim.py are the handoff pack's
#  (the wire protocol lives there); session 296 added the 2.6 updater's
#  HOLD / BOOT / REJECTED to them, behind kflash::Updater::V26. blockmapwidget.* is the
#  handoff's with its colours mapped onto DLConsole's theme. Everything else
#  is the DLConsole integration.
# =============================================================================
INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/flash_engine.cpp \
    $$PWD/flashworker.cpp \
    $$PWD/blockmapwidget.cpp \
    $$PWD/flashercore.cpp \
    $$PWD/flasherstyle.cpp \
    $$PWD/flasherqueuemodel.cpp \
    $$PWD/flasherqueuepage.cpp \
    $$PWD/flasherflashingpage.cpp \
    $$PWD/flashersummarypage.cpp \
    $$PWD/flasherhistorydialog.cpp \
    $$PWD/flasherprofiledialog.cpp \
    $$PWD/flasherwindow.cpp \
    $$PWD/vcclistedit.cpp \
    $$PWD/flasherboarddialog.cpp \
    $$PWD/flasherimagedialog.cpp \
    $$PWD/third_party/monocypher/monocypher.c \
    $$PWD/third_party/monocypher/monocypher-ed25519.c

HEADERS += \
    $$PWD/flash_engine.h \
    $$PWD/flashworker.h \
    $$PWD/blockmapwidget.h \
    $$PWD/flashercore.h \
    $$PWD/flasherstyle.h \
    $$PWD/flasherqueuemodel.h \
    $$PWD/flasherqueuepage.h \
    $$PWD/flasherflashingpage.h \
    $$PWD/flashersummarypage.h \
    $$PWD/flasherhistorydialog.h \
    $$PWD/flasherprofiledialog.h \
    $$PWD/flasherwindow.h \
    $$PWD/vcclistedit.h \
    $$PWD/flasherboarddialog.h \
    $$PWD/flasherimagedialog.h \
    $$PWD/third_party/monocypher/monocypher.h \
    $$PWD/third_party/monocypher/monocypher-ed25519.h

DISTFILES += \
    $$PWD/board_sim.py

# Session 314: Monocypher 4.0.2, unmodified, from the updater repo
# (source/third_party/monocypher): the same Ed25519 check the boards run.

# The engine uses Winsock and timeBeginPeriod() on Windows.
win32: LIBS += -lws2_32 -lwinmm
