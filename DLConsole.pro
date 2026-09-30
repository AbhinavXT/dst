# =============================================================================
#  DLConsole -- top-level project (session 87)
#  ---------------------------------------------------------------------------
#  Open this in Qt Creator, or `qmake && make`, exactly as before. It now
#  builds two things in order:
#
#    core/  the static library `dlcore`: every shared source (dlcore.pri)
#    app/   the DLConsole program: main.cpp linked against it
#
#  The program lands in the main build folder: <build>/DLConsole.exe
#  (app/app.pro sets DESTDIR), as it did before the split.
#
#  The unit tests, the menu audit and the other harnesses (tests/*.pro) link
#  the same library, so the shared sources are compiled once, not once per
#  program. `qmake CONFIG+=with_tests` adds the tests and the menu audit here.
#  verify.sh builds them all.
# =============================================================================
TEMPLATE = subdirs

SUBDIRS += core app
core.file = core/core.pro
app.file  = app/app.pro
app.depends = core

with_tests {
    SUBDIRS += dltests menuaudit
    dltests.file   = tests/tests.pro
    dltests.makefile = Makefile.dltests
    dltests.depends = core
    menuaudit.file  = tests/menuaudit.pro
    menuaudit.makefile = Makefile.menuaudit
    menuaudit.depends = core
}

DISTFILES += \
    dlcore.pri \
    dlcore_link.pri \
    serial.pri \
    schema/kavach.xml \
    schema/engine.py \
    schema/SCHEMA_GUIDE.md \
    schema/INTEGRATION.md
