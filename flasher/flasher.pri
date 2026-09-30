# =============================================================================
#  flasher.pri -- the Firmware Flasher (Tools ▸ Firmware Flasher…)
#  ---------------------------------------------------------------------------
#  Included by DLConsole.pro, tests/tests.pro and tests/menuaudit.pro so the
#  three builds cannot drift apart on which flasher files they compile.
#
#  flash_engine.*, flashworker.* and board_sim.py are the handoff pack's,
#  unmodified (the wire protocol lives there). blockmapwidget.* is the
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
    $$PWD/flasherwindow.cpp

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
    $$PWD/flasherwindow.h

DISTFILES += \
    $$PWD/board_sim.py

# The engine uses Winsock and timeBeginPeriod() on Windows.
win32: LIBS += -lws2_32 -lwinmm
