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
TARGET = dltests

W = $$PWD/..
INCLUDEPATH += $$PWD $$W $$W/schema

SOURCES += \
    main.cpp \
    test_archivesearch.cpp \
    test_authkeys.cpp \
    test_cbcmac.cpp \
    test_packetbuilder.cpp \
    test_bufferload.cpp \
    test_dlrplayer.cpp \
    test_packetvary.cpp \
    test_multidest.cpp \
    test_framenumwatch.cpp \
    test_subpacketwindow.cpp \
    test_messageheader.cpp \
    test_headerextras.cpp \
    test_sessionkeygen.cpp \
    test_fieldsweep.cpp \
    test_framediff.cpp \
    test_roundtrip.cpp \
    test_uicolors.cpp \
    test_contrastaudit.cpp \
    test_colorblind.cpp \
    test_fieldplotzoom.cpp \
    test_flasherroute.cpp \
    test_session81.cpp \
    test_session82.cpp \
    test_session83.cpp \
    test_session84.cpp \
    test_undolog.cpp \
    test_workspacesnapshot.cpp \
    test_settingsbundle.cpp \
    test_emptystate.cpp \
    test_workspace.cpp \
    test_problemnav.cpp \
    test_sessiontools.cpp \
    test_comparetools.cpp \
    test_statusline.cpp \
    test_windowframes.cpp \
    test_sendguard.cpp \
    test_keypickers.cpp \
    test_keysnapshots.cpp \
    test_sessionkeyloco.cpp \
    test_sessionkeystore.cpp \
    test_assertions.cpp \
    test_bookmarks.cpp \
    test_fieldmutation.cpp \
    test_clipboard.cpp \
    test_commandpalette.cpp \
    test_density.cpp \
    test_faultreport.cpp \
    test_fieldcatalog.cpp \
    test_fieldindex.cpp \
    test_fieldplot.cpp \
    test_fieldquery.cpp \
    test_fieldspan.cpp \
    test_crcheader.cpp \
    test_speedanalog.cpp \
    test_arprecv.cpp \
    test_turnoutwidth.cpp \
    test_locowillignore.cpp \
    test_frameclock.cpp \
    test_rejectrules.cpp \
    test_pinboard.cpp \
    test_watchlist.cpp \
    test_slrpcarry.cpp \
    test_comparefind.cpp \
    test_statusconventions.cpp \
    test_tabmetrics.cpp \
    test_findhold.cpp \
    test_findextras.cpp \
    test_findlive.cpp \
    test_findmodes.cpp \
    test_openbuffer.cpp \
    test_keyblock.cpp \
    test_layout.cpp \
    test_locohealth.cpp \
    test_logquery.cpp \
    test_locoidentity.cpp \
    test_logtimedelegate.cpp \
    test_pipeline.cpp \
    test_queryhistory.cpp \
    test_querycomplete.cpp \
    test_namemap_csv.cpp \
    test_notifications.cpp \
    test_qtcompat.cpp \
    test_quota.cpp \
    test_random.cpp \
    test_ruleattribution.cpp \
    test_schemareload.cpp \
    test_savedlr.cpp \
    test_sessionfile.cpp \
    test_severityglyph.cpp \
    test_sourcelist.cpp \
    test_timeline.cpp \
    test_timezone.cpp \
    test_windowgeometry.cpp \
    test_flasher.cpp \
    test_lococonfig.cpp \
    test_tablefindbar.cpp \
    test_minimizeddock.cpp \
    test_uizoom.cpp \
    test_tabtags.cpp \
    test_livedata.cpp \
    $$W/archivesearch.cpp \
    $$W/bookmarks.cpp \
    $$W/commandpalette.cpp \
    $$W/capturedecoder.cpp \
    $$W/crypto/aes128.cpp \
    $$W/crypto/kavachmac.cpp \
    $$W/schema/schemaencoder.cpp \
    $$W/packetbuilder.cpp \
    $$W/udpsender.cpp \
    $$W/tablefindbar.cpp \
    $$W/minimizeddock.cpp \
    $$W/textzoom.cpp \
    $$W/presentationmode.cpp \
    $$W/tabtags.cpp \
    $$W/tabpopoutwindow.cpp \
    $$W/livefields.cpp \
    $$W/lococonsolewindow.cpp \
    $$W/replaywindow.cpp \
    $$W/bignumberpanel.cpp \
    $$W/statuspins.cpp \
    $$W/subpacketwindow.cpp \
    $$W/packetmakerdialog.cpp \
    $$W/decodeworkbench.cpp \
    $$W/fieldsweep.cpp \
    $$W/framediff.cpp \
    $$W/framediffwindow.cpp \
    $$W/roundtrip.cpp \
    $$W/sessionwindow.cpp \
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
    $$W/fieldindexdialog.cpp \
    $$W/logmodel.cpp \
    $$W/logtableview.cpp \
    $$W/logtimedelegate.cpp \
    $$W/locoidentity.cpp \
    $$W/rejectrules.cpp \
    $$W/keyblock.cpp \
    $$W/logquery.cpp \
    $$W/queryhistory.cpp \
    $$W/windowgeometry.cpp \
    $$W/querylineedit.cpp \
    $$W/logwriter.cpp \
    $$W/markerscrollbar.cpp \
    $$W/messagedispatcher.cpp \
    $$W/undolog.cpp \
    $$W/cabpanel.cpp \
    $$W/dmipanel.cpp \
    $$W/dmitimetravel.cpp \
    $$W/searchwindow.cpp \
    $$W/speeddistance.cpp \
    $$W/clockskewalarm.cpp \
    $$W/runreport.cpp \
    $$W/runreportwindow.cpp \
    $$W/brakingcurves.cpp \
    $$W/workspacesnapshot.cpp \
    $$W/settingsbundle.cpp \
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
    $$W/fieldindexdialog.h \
    $$W/logmodel.h \
    $$W/logtimedelegate.h \
    $$W/locoidentity.h \
    $$W/rejectrules.h \
    $$W/keyblock.h \
    $$W/logquery.h \
    $$W/queryhistory.h \
    $$W/windowgeometry.h \
    $$W/querylineedit.h \
    $$W/logwriter.h \
    $$W/markerscrollbar.h \
    $$W/messagedispatcher.h \
    $$W/undolog.h \
    $$W/cabpanel.h \
    $$W/dmipanel.h \
    $$W/dmitimetravel.h \
    $$W/searchwindow.h \
    $$W/speeddistance.h \
    $$W/clockskewalarm.h \
    $$W/runreport.h \
    $$W/runreportwindow.h \
    $$W/brakingcurves.h \
    $$W/workspacesnapshot.h \
    $$W/settingsbundle.h \
    $$W/namemap.h \
    $$W/notificationcenter.h \
    $$W/sessionfile.h \
    $$W/savedata.h \
    $$W/sessionreader.h \
    $$W/dlrplayer.h \
    $$W/udpsender.h \
    $$W/tablefindbar.h \
    $$W/minimizeddock.h \
    $$W/textzoom.h \
    $$W/presentationmode.h \
    $$W/tabtags.h \
    $$W/tabpopoutwindow.h \
    $$W/livefields.h \
    $$W/lococonsolewindow.h \
    $$W/replaywindow.h \
    $$W/bignumberpanel.h \
    $$W/statuspins.h \
    $$W/subpacketwindow.h \
    $$W/packetmakerdialog.h \
    $$W/packetvariation.h \
    $$W/packetpreset.h \
    $$W/testassertions.h \
    $$W/timelineribbon.h \
    $$W/udpcommunication.h \
    $$W/schema/schemadecoder.h

# The Firmware Flasher: its logic, its pages and the window itself (the
# flasherrun suite drives a real FlasherWindow against an in-process fake board).
include($$W/flasher/flasher.pri)
include($$W/lococonfig/lococonfig.pri)

DEFINES += DL_SRC_DIR=\\\"$$W\\\"

RESOURCES += $$W/images.qrc

include($$PWD/../serial.pri)
dl_serial {
    SOURCES += test_session85.cpp
    linux: LIBS += -lutil   # openpty() for the serial end-to-end test
}
