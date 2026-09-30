# =============================================================================
#  DLConsole test suite
#  ---------------------------------------------------------------------------
#  Builds one console binary that runs every suite:
#
#      cd tests && qmake && make && ./dltests
#      ./dltests logquery          # one suite
#
#  Exit code is the number of failed checks, so CI needs no output parsing.
#
#  The application sources are compiled in directly rather than linked from
#  a library: DLConsole.pro produces an app, not a lib, and splitting it
#  would be a bigger change than the tests justify. main.cpp and the window
#  classes are deliberately excluded — everything here is testable without
#  a GUI.
# =============================================================================

QT += core network gui widgets xml testlib
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = findbench

W = $$PWD/..
INCLUDEPATH += $$PWD $$W $$W/schema

SOURCES += \
    $$W/archivesearch.cpp \
    $$W/bookmarks.cpp \
    $$W/commandpalette.cpp \
    $$W/capturedecoder.cpp \
    $$W/crypto/aes128.cpp \
    $$W/crypto/kavachmac.cpp \
    $$W/schema/schemaencoder.cpp \
    $$W/packetbuilder.cpp \
    $$W/udpsender.cpp \
    $$W/subpacketwindow.cpp \
    $$W/packetmakerdialog.cpp \
    $$W/decodeworkbench.cpp \
    $$W/fieldsweep.cpp \
    $$W/framediff.cpp \
    $$W/framediffwindow.cpp \
    $$W/roundtrip.cpp \
    $$W/sessionwindow.cpp \
    $$W/dmitimetravel.cpp \
    $$W/comparewindow.cpp \
    $$W/fieldinspector.cpp \
    $$W/filterbar.cpp \
    $$W/rawbytespanel.cpp \
    $$W/fieldsweepdialog.cpp \
    $$W/packetsequencedialog.cpp \
    $$W/uicolors.cpp \
    $$W/statusline.cpp \
    $$W/watchpanel.cpp \
    $$W/watchlist.cpp \
    $$W/pinboard.cpp \
    $$W/pinpanel.cpp \
    $$W/frameclock.cpp \
    $$W/framenumberwatch.cpp \
    $$W/gototimestampdialog.cpp \
    $$W/emptystate.cpp \
    $$W/uistyle.cpp \
    $$W/roundtripwindow.cpp \
    $$W/messageheader.cpp \
    $$W/sessionkeygen.cpp \
    $$W/sessionkeystore.cpp \
    $$W/colorrules.cpp \
    $$W/faultpanelwindow.cpp \
    $$W/fieldcatalog.cpp \
    $$W/fieldindex.cpp \
    $$W/fieldplot.cpp \
    $$W/findbar.cpp \
    $$W/logentry.cpp \
    $$W/logmodel.cpp \
    $$W/logtableview.cpp \
    $$W/keyblock.cpp \
    $$W/logquery.cpp \
    $$W/queryhistory.cpp \
    $$W/windowgeometry.cpp \
    $$W/querylineedit.cpp \
    $$W/logwriter.cpp \
    $$W/markerscrollbar.cpp \
    $$W/messagedispatcher.cpp \
    $$W/namemap.cpp \
    $$W/notificationcenter.cpp \
    $$W/savedata.cpp \
    $$W/sessionreader.cpp \
    $$W/dlrplayer.cpp \
    $$W/packetvariation.cpp \
    $$W/packetpreset.cpp \
    $$W/testassertions.cpp \
    $$W/timelineribbon.cpp \
    $$W/udpcommunication.cpp \
    $$W/schema/schemadecoder.cpp

HEADERS += \
    testutil.h \
    $$W/archivesearch.h \
    $$W/bookmarks.h \
    $$W/commandpalette.h \
    $$W/capturedecoder.h \
    $$W/sessionkeystore.h \
    $$W/decodeworkbench.h \
    $$W/framediffwindow.h \
    $$W/roundtrip.h \
    $$W/sessionwindow.h \
    $$W/comparewindow.h \
    $$W/fieldinspector.h \
    $$W/filterbar.h \
    $$W/rawbytespanel.h \
    $$W/fieldsweepdialog.h \
    $$W/packetsequencedialog.h \
    $$W/uicolors.h \
    $$W/statusline.h \
    $$W/watchpanel.h \
    $$W/watchlist.h \
    $$W/pinboard.h \
    $$W/pinpanel.h \
    $$W/frameclock.h \
    $$W/framenumberwatch.h \
    $$W/gototimestampdialog.h \
    $$W/emptystate.h \
    $$W/sendguard.h \
    $$W/uistyle.h \
    $$W/roundtripwindow.h \
    $$W/colorrules.h \
    $$W/faultpanelwindow.h \
    $$W/fieldcatalog.h \
    $$W/fieldindex.h \
    $$W/fieldplot.h \
    $$W/findbar.h \
    $$W/logentry.h \
    $$W/logmodel.h \
    $$W/keyblock.h \
    $$W/logquery.h \
    $$W/queryhistory.h \
    $$W/windowgeometry.h \
    $$W/querylineedit.h \
    $$W/logwriter.h \
    $$W/markerscrollbar.h \
    $$W/messagedispatcher.h \
    $$W/namemap.h \
    $$W/notificationcenter.h \
    $$W/sessionfile.h \
    $$W/savedata.h \
    $$W/sessionreader.h \
    $$W/dlrplayer.h \
    $$W/udpsender.h \
    $$W/subpacketwindow.h \
    $$W/packetmakerdialog.h \
    $$W/packetvariation.h \
    $$W/packetpreset.h \
    $$W/testassertions.h \
    $$W/timelineribbon.h \
    $$W/udpcommunication.h \
    $$W/schema/schemadecoder.h

DEFINES += DL_SRC_DIR=\\\"$$W\\\"

RESOURCES += $$W/images.qrc

SOURCES += $$PWD/bench_main.cpp
