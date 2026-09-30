# =============================================================================
#  The DLConsole program itself (session 87): main.cpp and the library.
#  Built by the top-level DLConsole.pro after core/.
# =============================================================================
TEMPLATE = app
TARGET   = DLConsole
DEFINES += QT_NO_DEBUG_OUTPUT QT_DEPRECATED_WARNINGS

include(../dlcore_link.pri)

SOURCES += ../main.cpp

# The program goes in the MAIN build folder (<build>/DLConsole.exe), not in
# <build>/app/release/, so it sits where it always did. Qt Creator's Run,
# windeployqt and any script find it there. Override with
#   qmake DLCONSOLE_BIN_DIR=/some/where
isEmpty(DLCONSOLE_BIN_DIR): DLCONSOLE_BIN_DIR = $$clean_path($$OUT_PWD/..)
DESTDIR = $$DLCONSOLE_BIN_DIR

qnx:        target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
