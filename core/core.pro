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

# Session 116: a type declared `class` in one place and `struct` in another
# is only a warning on clang and GCC, but MSVC mangles the two differently,
# so it is a LINK error on Windows (the first Windows build since patch 88
# stopped on exactly that, for LogEntry). An error here, so a Mac or Linux
# build catches it before Windows does.
clang|*-g++*|*g++: QMAKE_CXXFLAGS += -Werror=mismatched-tags

include(../dlcore.pri)

# Resources live in the programs, not here: a .qrc inside a static library
# registers itself from an object the linker never pulls in.
RESOURCES =
