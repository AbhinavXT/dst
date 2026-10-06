# Session 85/86: the serial terminal is OPTIONAL at build time.
#
# With Qt's Serial Port module present (the normal case) it is built in and
# DL_HAVE_SERIAL is defined. Without it -- or with `qmake CONFIG+=no_serial`
# -- DLConsole still builds and runs on Ethernet alone; Tools > Serial Port
# Terminal is then present but disabled, and says why.
!no_serial:qtHaveModule(serialport) {
    QT      += serialport
    DEFINES += DL_HAVE_SERIAL
    CONFIG  += dl_serial
    # Session 156: where each COM port is plugged in (SetupAPI, the device
    # tree). In the library's programs too: a static library carries no links.
    win32: LIBS += -lsetupapi -lcfgmgr32 -ladvapi32
    # The library compiles them; programs linking it only need the module
    # and the define (dlcore_link.pri sets dl_link_only).
    !dl_link_only {
        SOURCES += $$PWD/seriallink.cpp $$PWD/serialconsolewindow.cpp $$PWD/serialmanager.cpp $$PWD/serialfilesender.cpp $$PWD/serialautobaud.cpp $$PWD/serialportscan.cpp
        HEADERS += $$PWD/seriallink.h   $$PWD/serialconsolewindow.h $$PWD/serialmanager.h $$PWD/serialfilesender.h $$PWD/serialautobaud.h $$PWD/serialportscan.h
    }
} else:!dl_link_only {
    message("DLConsole: building without the serial terminal (Qt Serial Port module not used)")
}
