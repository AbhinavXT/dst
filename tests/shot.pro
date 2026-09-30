# =============================================================================
#  Screenshot harness: builds the real MainWindow offscreen and saves images
#  of it (tests/screenshot_main.cpp). Not part of the gate.
#
#      qmake tests/shot.pro && make && ./shot
#
#  Session 87: links the dlcore static library (dlcore_link.pri). Until now
#  it compiled its own copy of the sources, and that list had fallen behind
#  the app's by 66 files, so it no longer linked.
# =============================================================================
TARGET   = shot
DEFINES += QT_NO_DEBUG_OUTPUT QT_DEPRECATED_WARNINGS
TEMPLATE = app
OBJECTS_DIR = $$OUT_PWD/.obj-$$TARGET
MOC_DIR     = $$OUT_PWD/.moc-$$TARGET
RCC_DIR     = $$OUT_PWD/.rcc-$$TARGET
include($$PWD/../dlcore_link.pri)
INCLUDEPATH += $$PWD

SOURCES += $$PWD/screenshot_main.cpp
