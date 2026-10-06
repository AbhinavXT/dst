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
#  GCC only has -Wmismatched-tags from GCC 10; older ones (Qt 5.15's MinGW
#  8.1) reject -Werror=<unknown> outright, so the flag is left off there.
clang {
    QMAKE_CXXFLAGS += -Werror=mismatched-tags
} else:*-g++*|*g++ {
    greaterThan(QMAKE_GCC_MAJOR_VERSION, 9): QMAKE_CXXFLAGS += -Werror=mismatched-tags
}

# Session 116: the sources are UTF-8 (\u25B8 menu paths, \u2014 dashes,
# \u25CF status glyphs, \u26A0 and \u2715 in labels). Without /utf-8, MSVC reads
# them in the Windows code page and every such literal comes out as
# mojibake — on screen, not only in the tests that caught it.
msvc: QMAKE_CXXFLAGS += /utf-8

include(../dlcore.pri)

# Resources live in the programs, not here: a .qrc inside a static library
# registers itself from an object the linker never pulls in.
RESOURCES =
