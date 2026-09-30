# =============================================================================
#  DLConsole test suite
#  ---------------------------------------------------------------------------
#  Builds one console binary that runs every suite:
#
#      ./verify.sh                  # the whole gate, the usual way
#      ./dltests logquery           # one suite (run from tests/)
#
#  Exit code is the number of failed checks, so CI needs no output parsing.
#
#  Session 87: the application sources are no longer compiled in here; they
#  come from the dlcore static library (dlcore.pri / dlcore_link.pri), built
#  once in <build>/core and shared with the app and the menu audit.
# =============================================================================

QT += testlib
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = dltests
OBJECTS_DIR = $$OUT_PWD/.obj-$$TARGET
MOC_DIR     = $$OUT_PWD/.moc-$$TARGET
RCC_DIR     = $$OUT_PWD/.rcc-$$TARGET

include($$PWD/../dlcore_link.pri)
INCLUDEPATH += $$PWD
DEFINES += DL_SRC_DIR=\\\"$$clean_path($$PWD/..)\\\"

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
    test_session89.cpp \
    test_session90.cpp \
    test_session92.cpp \
    test_session94.cpp \
    test_session95.cpp \
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
    test_livedata.cpp

dl_serial {
    SOURCES += test_session85.cpp
    linux: LIBS += -lutil   # openpty() for the serial end-to-end test
}
