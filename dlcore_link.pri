# =============================================================================
#  dlcore_link.pri -- include this to link the shared library (session 87)
#  ---------------------------------------------------------------------------
#  For the app, the unit tests, the menu audit, the screenshot harness and
#  the find benchmark. Gives the headers, the Qt modules, the serial switch
#  (the same test the library was built with), the resources, and the link
#  to <build>/core/libdlcore.a (dlcore.lib with MSVC).
#
#  The library is expected in a `core` folder NEXT TO this program's build
#  folder -- which is where the top-level DLConsole.pro (subdirs) and
#  verify.sh both put it. Elsewhere: qmake DLCORE_DIR=/path/to/core
# =============================================================================
isEmpty(DLCORE_DIR): DLCORE_DIR = $$clean_path($$OUT_PWD/../core)

INCLUDEPATH += $$PWD $$PWD/schema $$PWD/flasher $$PWD/lococonfig
DEPENDPATH  += $$PWD $$PWD/schema $$PWD/flasher $$PWD/lococonfig
QT      += core gui network xml widgets
CONFIG  += c++17

CONFIG += dl_link_only
include($$PWD/serial.pri)

RESOURCES += $$PWD/images.qrc $$PWD/lococonfig/lococonfig.qrc

LIBS += -L$$DLCORE_DIR -ldlcore
msvc: PRE_TARGETDEPS += $$DLCORE_DIR/dlcore.lib
else: PRE_TARGETDEPS += $$DLCORE_DIR/libdlcore.a

# The flasher engine uses Winsock and timeBeginPeriod() on Windows.
win32: LIBS += -lws2_32 -lwinmm
