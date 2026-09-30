# =============================================================================
#  The DLConsole program itself (session 87): main.cpp and the library.
#  Built by the top-level DLConsole.pro after core/.
# =============================================================================
TEMPLATE = app
TARGET   = DLConsole
DEFINES += QT_NO_DEBUG_OUTPUT QT_DEPRECATED_WARNINGS

include(../dlcore_link.pri)

SOURCES += ../main.cpp

qnx:        target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
