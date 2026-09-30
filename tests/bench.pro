# =============================================================================
#  Find benchmark (tests/bench_main.cpp): times search over a large log.
#  Not part of the gate.
#
#  Session 87: links the dlcore static library (dlcore_link.pri) instead of
#  compiling the application sources again.
# =============================================================================
QT += testlib
CONFIG += console
CONFIG -= app_bundle
TARGET   = findbench
DEFINES += DL_SRC_DIR=\\\"$$clean_path($$PWD/..)\\\"
TEMPLATE = app
OBJECTS_DIR = $$OUT_PWD/.obj-$$TARGET
MOC_DIR     = $$OUT_PWD/.moc-$$TARGET
RCC_DIR     = $$OUT_PWD/.rcc-$$TARGET
include($$PWD/../dlcore_link.pri)
INCLUDEPATH += $$PWD

SOURCES += $$PWD/bench_main.cpp
