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
    test_session96.cpp \
    test_session97.cpp \
    test_session98.cpp \
    test_session99.cpp \
    test_session100.cpp \
    test_session112.cpp \
    test_session114.cpp \
    test_session117.cpp \
    test_session120.cpp \
    test_session121.cpp \
    test_session123.cpp \
    test_session124.cpp \
    test_session128.cpp \
    test_session129.cpp \
    test_session130.cpp \
    test_session131.cpp \
    test_session132.cpp \
    test_session133.cpp \
    test_session134.cpp \
    test_session135.cpp \
    test_session136.cpp \
    test_session137.cpp \
    test_session138.cpp \
    test_session139.cpp \
    test_session140.cpp \
    test_session141.cpp \
    test_session142.cpp \
    test_session143.cpp \
    test_session144.cpp \
    test_session145.cpp \
    test_session146.cpp \
    test_session147.cpp \
    test_session148.cpp \
    test_session149.cpp \
    test_session150.cpp \
    test_session151.cpp \
    test_session152.cpp \
    test_session153.cpp \
    test_session154.cpp \
    test_session155.cpp \
    test_session157.cpp \
    test_session158.cpp \
    test_session159.cpp \
    test_session160.cpp \
    test_session161.cpp \
    test_session164.cpp \
    test_session165.cpp \
    test_session166.cpp \
    test_session167.cpp \
    test_session168.cpp \
    test_session169.cpp \
    test_session170.cpp \
    test_session171.cpp \
    test_session172.cpp \
    test_session173.cpp \
    test_session174.cpp \
    test_session175.cpp \
    test_session176.cpp \
    test_session177.cpp \
    test_session178.cpp \
    test_session179.cpp \
    test_session180.cpp \
    test_session181.cpp \
    test_session184.cpp \
    test_session185.cpp \
    test_session186.cpp \
    test_session187.cpp \
    test_session189.cpp \
    test_session190.cpp \
    test_session191.cpp \
    test_session192.cpp \
    test_session193.cpp \
    test_session194.cpp \
    test_session195.cpp \
    test_session196.cpp \
    test_session197.cpp \
    test_session198.cpp \
    test_session199.cpp \
    test_session200.cpp \
    test_session201.cpp \
    test_session202.cpp \
    test_session203.cpp \
    test_session204.cpp \
    test_session205.cpp \
    test_session206.cpp \
    test_session207.cpp \
    test_session208.cpp \
    test_session209.cpp \
    test_session210.cpp \
    test_session211.cpp \
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

# The serial terminal's suites, built only where Qt Serial Port is: a Qt
# without the module builds the app without the terminal, and these then
# could not compile (session 130: Qt 6.12 from the online installer, with
# the module not ticked). 101-111, 115 and 122 had been added outside.
dl_serial {
    SOURCES += test_session85.cpp \
               test_session101.cpp \
               test_session102.cpp \
               test_session103.cpp \
               test_session104.cpp \
               test_session105.cpp \
               test_session106.cpp \
               test_session107.cpp \
               test_session108.cpp \
               test_session109.cpp \
               test_session110.cpp \
               test_session111.cpp \
               test_session115.cpp \
               test_session122.cpp \
               test_session156.cpp
    linux: LIBS += -lutil   # openpty() for the serial end-to-end test
}
