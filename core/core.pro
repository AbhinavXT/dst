# =============================================================================
#  dlcore -- the static library holding everything DLConsole's programs share
#  (session 87). The sources are listed in ../dlcore.pri.
# =============================================================================
TEMPLATE = lib
CONFIG  += staticlib c++17
TARGET   = dlcore
# Output next to the Makefile, with no debug/ or release/ subfolder, so the
# programs find it at <build>/core whatever the platform (dlcore_link.pri).
DESTDIR  = $$OUT_PWD

QT      += core gui network xml widgets
DEFINES += QT_NO_DEBUG_OUTPUT QT_DEPRECATED_WARNINGS

include(../dlcore.pri)

# Resources live in the programs, not here: a .qrc inside a static library
# registers itself from an object the linker never pulls in.
RESOURCES =
