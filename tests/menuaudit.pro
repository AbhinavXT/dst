# =============================================================================
#  Menu audit
#  ---------------------------------------------------------------------------
#  Builds the real MainWindow and walks its menus, checking that every action
#  a user is meant to reach is actually there and wired.
#
#  This exists because two features shipped unreachable: the UTC toggle had
#  its slot but no menu entry, and the Panels submenu was built before the
#  docks existed so it listed nothing. Neither was caught by the unit suite
#  (which tests below the UI) or the smoke test (which drives traffic, not
#  menus). This closes that gap.
#
#      cd tests && qmake menuaudit.pro && make && ./menuaudit
#  Exit code is the number of failures.
# =============================================================================

QT       += core gui network xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

DEFINES += QT_NO_DEBUG_OUTPUT
DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    $$PWD/../capturedecoder.cpp \
    $$PWD/../schema/schemadecoder.cpp \
    $$PWD/../colorrules.cpp \
    $$PWD/../comparewindow.cpp \
    $$PWD/../constants.cpp \
    $$PWD/../exportdialog.cpp \
    $$PWD/../exporter.cpp \
    $$PWD/../filterbar.cpp \
    $$PWD/../findbar.cpp \
    $$PWD/../gototimestampdialog.cpp \
    $$PWD/../lococonsolewindow.cpp \
    $$PWD/../tablefindbar.cpp \
    $$PWD/../minimizeddock.cpp \
    $$PWD/../textzoom.cpp \
    $$PWD/../presentationmode.cpp \
    $$PWD/../tabtags.cpp \
    $$PWD/../tabpopoutwindow.cpp \
    $$PWD/../livefields.cpp \
    $$PWD/../bignumberpanel.cpp \
    $$PWD/../statuspins.cpp \
    $$PWD/../replaywindow.cpp \
    $$PWD/../decodeworkbench.cpp \
    $$PWD/../faultpanelwindow.cpp \
    $$PWD/../fieldindexdialog.cpp \
    $$PWD/../logmodel.cpp \
    $$PWD/../logtableview.cpp \
    $$PWD/../logtimedelegate.cpp \
    $$PWD/../locoidentity.cpp \
    $$PWD/../rejectrules.cpp \
    $$PWD/../logwriter.cpp \
    $$PWD/menuaudit_main.cpp \
    $$PWD/../mainwindow.cpp \
    $$PWD/../mainwindowsession.cpp \
    $$PWD/../messagedispatcher.cpp \
    $$PWD/../undolog.cpp \
    $$PWD/../cabpanel.cpp \
    $$PWD/../dmipanel.cpp \
    $$PWD/../dmitimetravel.cpp \
    $$PWD/../speeddistance.cpp \
    $$PWD/../clockskewalarm.cpp \
    $$PWD/../runreport.cpp \
    $$PWD/../runreportwindow.cpp \
    $$PWD/../workspacesnapshot.cpp \
    $$PWD/../settingsbundle.cpp \
    $$PWD/../namemap.cpp \
    $$PWD/../rawbytespanel.cpp \
    $$PWD/../bookmarks.cpp \
    $$PWD/../archivesearch.cpp \
    $$PWD/../commandpalette.cpp \
    $$PWD/../archivesearchwindow.cpp \
    $$PWD/../fieldinspector.cpp \
    $$PWD/../fieldcatalog.cpp \
    $$PWD/../fieldplot.cpp \
    $$PWD/../fieldindex.cpp \
    $$PWD/../keyblock.cpp \
    $$PWD/../packetmakerdialog.cpp \
    $$PWD/../packetsequencedialog.cpp \
    $$PWD/../sessionkeydialog.cpp \
    $$PWD/../sessionkeystore.cpp \
    $$PWD/../testassertions.cpp \
    $$PWD/../dlrplayerdialog.cpp \
    $$PWD/../dlrplayer.cpp \
    $$PWD/../subpacketwindow.cpp \
    $$PWD/../packetbuilder.cpp \
    $$PWD/../framediff.cpp \
    $$PWD/../framediffwindow.cpp \
    $$PWD/../fieldsweep.cpp \
    $$PWD/../fieldsweepdialog.cpp \
    $$PWD/../roundtrip.cpp \
    $$PWD/../uicolors.cpp \
    $$PWD/../statusline.cpp \
    $$PWD/../watchpanel.cpp \
    $$PWD/../watchlist.cpp \
    $$PWD/../pinboard.cpp \
    $$PWD/../pinpanel.cpp \
    $$PWD/../frameclock.cpp \
    $$PWD/../framenumberwatch.cpp \
    $$PWD/../emptystate.cpp \
    $$PWD/../uistyle.cpp \
    $$PWD/../roundtripwindow.cpp \
    $$PWD/../packetvariation.cpp \
    $$PWD/../packetpreset.cpp \
    $$PWD/../udpsender.cpp \
    $$PWD/../messageheader.cpp \
    $$PWD/../sessionkeygen.cpp \
    $$PWD/../crypto/aes128.cpp \
    $$PWD/../crypto/kavachmac.cpp \
    $$PWD/../schema/schemaencoder.cpp \
    $$PWD/../brakingcurves.cpp \
    $$PWD/../brakingcurveplot.cpp \
    $$PWD/../brakingpanel.cpp \
    $$PWD/../logentry.cpp \
    $$PWD/../logquery.cpp \
    $$PWD/../queryhistory.cpp \
    $$PWD/../querylineedit.cpp \
    $$PWD/../windowgeometry.cpp \
    $$PWD/../markerscrollbar.cpp \
    $$PWD/../mergedwindow.cpp \
    $$PWD/../notificationcenter.cpp \
    $$PWD/../timelineribbon.cpp \
    $$PWD/../savedata.cpp \
    $$PWD/../searchwindow.cpp \
    $$PWD/../sessionreader.cpp \
    $$PWD/../stickymenu.cpp \
    $$PWD/../sessionwindow.cpp \
    $$PWD/../settingsdialog.cpp \
    $$PWD/../udpcommunication.cpp

HEADERS += \
    $$PWD/../fieldcatalog.h \
    $$PWD/../fieldindex.h \
    $$PWD/../keyblock.h \
    $$PWD/../packetmakerdialog.h \
    $$PWD/../packetsequencedialog.h \
    $$PWD/../sessionkeydialog.h \
    $$PWD/../sessionkeystore.h \
    $$PWD/../testassertions.h \
    $$PWD/../dlrplayerdialog.h \
    $$PWD/../dlrplayer.h \
    $$PWD/../subpacketwindow.h \
    $$PWD/../packetbuilder.h \
    $$PWD/../framediff.h \
    $$PWD/../framediffwindow.h \
    $$PWD/../fieldsweep.h \
    $$PWD/../fieldsweepdialog.h \
    $$PWD/../roundtrip.h \
    $$PWD/../uicolors.h \
    $$PWD/../statusline.h \
    $$PWD/../watchpanel.h \
    $$PWD/../watchlist.h \
    $$PWD/../pinboard.h \
    $$PWD/../pinpanel.h \
    $$PWD/../frameclock.h \
    $$PWD/../framenumberwatch.h \
    $$PWD/../emptystate.h \
    $$PWD/../uistyle.h \
    $$PWD/../roundtripwindow.h \
    $$PWD/../packetvariation.h \
    $$PWD/../packetpreset.h \
    $$PWD/../udpsender.h \
    $$PWD/../messageheader.h \
    $$PWD/../sessionkeygen.h \
    $$PWD/../crypto/aes128.h \
    $$PWD/../crypto/kavachmac.h \
    $$PWD/../schema/schemaencoder.h \
    $$PWD/../brakingcurves.h \
    $$PWD/../brakingcurveplot.h \
    $$PWD/../brakingpanel.h \
    $$PWD/../Constants.h \
    $$PWD/../capturedecoder.h \
    $$PWD/../schema/schemadecoder.h \
    $$PWD/../IRS.h \
    $$PWD/../Structures.h \
    $$PWD/../colorrules.h \
    $$PWD/../comparewindow.h \
    $$PWD/../exportdialog.h \
    $$PWD/../exporter.h \
    $$PWD/../filterbar.h \
    $$PWD/../findbar.h \
    $$PWD/../gototimestampdialog.h \
    $$PWD/../lococonsolewindow.h \
    $$PWD/../tablefindbar.h \
    $$PWD/../minimizeddock.h \
    $$PWD/../textzoom.h \
    $$PWD/../presentationmode.h \
    $$PWD/../tabtags.h \
    $$PWD/../tabpopoutwindow.h \
    $$PWD/../livefields.h \
    $$PWD/../bignumberpanel.h \
    $$PWD/../statuspins.h \
    $$PWD/../replaywindow.h \
    $$PWD/../decodeworkbench.h \
    $$PWD/../faultpanelwindow.h \
    $$PWD/../logentry.h \
    $$PWD/../fieldindexdialog.h \
    $$PWD/../logmodel.h \
    $$PWD/../logtimedelegate.h \
    $$PWD/../logwriter.h \
    $$PWD/../mainwindow.h \
    $$PWD/../messagedispatcher.h \
    $$PWD/../undolog.h \
    $$PWD/../cabpanel.h \
    $$PWD/../dmipanel.h \
    $$PWD/../dmitimetravel.h \
    $$PWD/../speeddistance.h \
    $$PWD/../clockskewalarm.h \
    $$PWD/../runreport.h \
    $$PWD/../runreportwindow.h \
    $$PWD/../workspacesnapshot.h \
    $$PWD/../settingsbundle.h \
    $$PWD/../namemap.h \
    $$PWD/../rawbytespanel.h \
    $$PWD/../bookmarks.h \
    $$PWD/../archivesearch.h \
    $$PWD/../commandpalette.h \
    $$PWD/../archivesearchwindow.h \
    $$PWD/../fieldinspector.h \
    $$PWD/../fieldplot.h \
    $$PWD/../logquery.h \
    $$PWD/../queryhistory.h \
    $$PWD/../querylineedit.h \
    $$PWD/../windowgeometry.h \
    $$PWD/../markerscrollbar.h \
    $$PWD/../mergedwindow.h \
    $$PWD/../notificationcenter.h \
    $$PWD/../timelineribbon.h \
    $$PWD/../savedata.h \
    $$PWD/../searchwindow.h \
    $$PWD/../sessionfile.h \
    $$PWD/../sessionreader.h \
    $$PWD/../stickymenu.h \
    $$PWD/../sessionwindow.h \
    $$PWD/../settings.h \
    $$PWD/../settingsdialog.h \
    $$PWD/../theme.h \
    $$PWD/../udpcommunication.h

FORMS += \
    $$PWD/../mainwindow.ui

# Default rules for deployment.
qnx:        target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    $$PWD/../images.qrc

# --- Firmware Flasher (MainWindow opens it from Tools) ----------------------
include($$PWD/../flasher/flasher.pri)
include($$PWD/../lococonfig/lococonfig.pri)

# --- schema engine (data-driven decoder) ---------------------------------
# schema/schemadecoder.cpp includes "capturedecoder.h" from the project root,
# so make both the root and schema/ visible to the compiler.
INCLUDEPATH += $$PWD/.. $$PWD/../schema

# Non-code schema files: listed here so they show in the Qt Creator project
# tree (under "Other files") without being compiled.
DISTFILES += \
    schema/kavach.xml \
    schema/engine.py \
    schema/SCHEMA_GUIDE.md \
    schema/INTEGRATION.md

include($$PWD/../serial.pri)
