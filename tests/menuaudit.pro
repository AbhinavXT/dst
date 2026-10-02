# =============================================================================
#  Menu audit
#  ---------------------------------------------------------------------------
#  Builds the real MainWindow and walks its menus, checking that every action
#  a user is meant to reach is actually there and wired.
#
#  This exists because two features shipped unreachable: the UTC toggle had
#  its slot but no menu entry, and the Panels submenu was built before the
#  docks existed so it listed nothing. Neither was caught by the unit suite
#  (which tests below the UI) or the smoke test (which drives traffic, not
#  menus). This closes that gap.
#
#      cd tests && qmake menuaudit.pro && make && ./menuaudit
#  Exit code is the number of failures.
#  Session 87: links the dlcore static library (dlcore_link.pri) instead of
#  compiling the application sources again.
# =============================================================================
TARGET   = menuaudit
# A console program, as dltests is (session 116). As a Windows GUI-subsystem
# exe its exit code was never reported to the CI shell, so a passing audit
# read as a failure there.
CONFIG += console
CONFIG -= app_bundle   # a bare binary on macOS too, where verify.sh runs it
DEFINES += QT_NO_DEBUG_OUTPUT QT_DEPRECATED_WARNINGS
TEMPLATE = app
OBJECTS_DIR = $$OUT_PWD/.obj-$$TARGET
MOC_DIR     = $$OUT_PWD/.moc-$$TARGET
RCC_DIR     = $$OUT_PWD/.rcc-$$TARGET
include($$PWD/../dlcore_link.pri)
INCLUDEPATH += $$PWD

SOURCES += $$PWD/menuaudit_main.cpp

linux: LIBS += -lutil   # openpty() for the serial chip check
