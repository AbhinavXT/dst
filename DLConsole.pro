QT       += core gui network xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

DEFINES += QT_NO_DEBUG_OUTPUT
DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    capturedecoder.cpp \
    crypto/aes128.cpp \
    crypto/kavachmac.cpp \
    schema/schemaencoder.cpp \
    packetbuilder.cpp \
    udpsender.cpp \
    packetmakerdialog.cpp \
    packetvariation.cpp \
    packetpreset.cpp \
    packetsequencedialog.cpp \
    subpacketwindow.cpp \
    dlrplayer.cpp \
    dlrplayerdialog.cpp \
    messageheader.cpp \
    sessionkeygen.cpp \
    sessionkeydialog.cpp \
    sessionkeystore.cpp \
    schema/schemadecoder.cpp \
    colorrules.cpp \
    comparewindow.cpp \
    constants.cpp \
    exportdialog.cpp \
    exporter.cpp \
    filterbar.cpp \
    findbar.cpp \
    gototimestampdialog.cpp \
    lococonsolewindow.cpp \
    tablefindbar.cpp \
    minimizeddock.cpp \
    textzoom.cpp \
    presentationmode.cpp \
    tabtags.cpp \
    tabpopoutwindow.cpp \
    livefields.cpp \
    bignumberpanel.cpp \
    statuspins.cpp \
    replaywindow.cpp \
    decodeworkbench.cpp \
    fieldsweep.cpp \
    fieldsweepdialog.cpp \
    framediff.cpp \
    framediffwindow.cpp \
    faultpanelwindow.cpp \
    brakingcurves.cpp \
    brakingcurveplot.cpp \
    brakingpanel.cpp \
    fieldindexdialog.cpp \
    logmodel.cpp \
    logtableview.cpp \
    logtimedelegate.cpp \
    locoidentity.cpp \
    rejectrules.cpp \
    logwriter.cpp \
    main.cpp \
    mainwindow.cpp \
    mainwindowsession.cpp \
    messagedispatcher.cpp \
    undolog.cpp \
    cabpanel.cpp \
    dmipanel.cpp \
    dmitimetravel.cpp \
    speeddistance.cpp \
    clockskewalarm.cpp \
    runreport.cpp \
    runreportwindow.cpp \
    workspacesnapshot.cpp \
    settingsbundle.cpp \
    namemap.cpp \
    rawbytespanel.cpp \
    bookmarks.cpp \
    archivesearch.cpp \
    commandpalette.cpp \
    archivesearchwindow.cpp \
    fieldcatalog.cpp \
    fieldindex.cpp \
    fieldinspector.cpp \
    fieldplot.cpp \
    logentry.cpp \
    keyblock.cpp \
    logquery.cpp \
    queryhistory.cpp \
    querylineedit.cpp \
    windowgeometry.cpp \
    markerscrollbar.cpp \
    mergedwindow.cpp \
    notificationcenter.cpp \
    timelineribbon.cpp \
    roundtrip.cpp \
    roundtripwindow.cpp \
    emptystate.cpp \
    watchpanel.cpp \
    watchlist.cpp \
    pinboard.cpp \
    pinpanel.cpp \
    frameclock.cpp \
    framenumberwatch.cpp \
    savedata.cpp \
    statusline.cpp \
    uicolors.cpp \
    uistyle.cpp \
    searchwindow.cpp \
    sessionreader.cpp \
    testassertions.cpp \
    stickymenu.cpp \
    sessionwindow.cpp \
    settingsdialog.cpp \
    udpcommunication.cpp

HEADERS += \
    Constants.h \
    capturedecoder.h \
    crypto/aes128.h \
    crypto/kavachmac.h \
    schema/schemaencoder.h \
    packetbuilder.h \
    udpsender.h \
    packetmakerdialog.h \
    packetvariation.h \
    packetpreset.h \
    packetsequencedialog.h \
    subpacketwindow.h \
    dlrplayer.h \
    dlrplayerdialog.h \
    messageheader.h \
    sessionkeygen.h \
    sessionkeydialog.h \
    sessionkeystore.h \
    schema/schemadecoder.h \
    IRS.h \
    Structures.h \
    colorrules.h \
    comparewindow.h \
    exportdialog.h \
    exporter.h \
    filterbar.h \
    findbar.h \
    gototimestampdialog.h \
    lococonsolewindow.h \
    tablefindbar.h \
    minimizeddock.h \
    textzoom.h \
    presentationmode.h \
    tabtags.h \
    tabpopoutwindow.h \
    livefields.h \
    bignumberpanel.h \
    statuspins.h \
    replaywindow.h \
    decodeworkbench.h \
    fieldsweep.h \
    fieldsweepdialog.h \
    framediff.h \
    framediffwindow.h \
    faultpanelwindow.h \
    brakingcurves.h \
    brakingcurveplot.h \
    brakingpanel.h \
    logentry.h \
    fieldindexdialog.h \
    logmodel.h \
    logtableview.h \
    logtimedelegate.h \
    locoidentity.h \
    rejectrules.h \
    logwriter.h \
    mainwindow.h \
    messagedispatcher.h \
    undolog.h \
    cabpanel.h \
    dmipanel.h \
    dmitimetravel.h \
    speeddistance.h \
    clockskewalarm.h \
    runreport.h \
    runreportwindow.h \
    workspacesnapshot.h \
    settingsbundle.h \
    namemap.h \
    rawbytespanel.h \
    bookmarks.h \
    archivesearch.h \
    commandpalette.h \
    archivesearchwindow.h \
    fieldcatalog.h \
    fieldindex.h \
    fieldinspector.h \
    fieldplot.h \
    keyblock.h \
    logquery.h \
    queryhistory.h \
    querylineedit.h \
    windowgeometry.h \
    markerscrollbar.h \
    mergedwindow.h \
    notificationcenter.h \
    timelineribbon.h \
    roundtrip.h \
    roundtripwindow.h \
    emptystate.h \
    watchpanel.h \
    watchlist.h \
    pinboard.h \
    pinpanel.h \
    frameclock.h \
    framenumberwatch.h \
    savedata.h \
    statusline.h \
    sendguard.h \
    uicolors.h \
    uistyle.h \
    searchwindow.h \
    sessionfile.h \
    sessionreader.h \
    testassertions.h \
    stickymenu.h \
    sessionwindow.h \
    settings.h \
    settingsdialog.h \
    theme.h \
    udpcommunication.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx:        target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    images.qrc

# --- Firmware Flasher (Tools ▸ Firmware Flasher…) ------------------------
include(flasher/flasher.pri)

# --- Loco Configuration (Tools ▸ Loco Configuration…) --------------------
include(lococonfig/lococonfig.pri)

# --- schema engine (data-driven decoder) ---------------------------------
# schema/schemadecoder.cpp includes "capturedecoder.h" from the project root,
# so make both the root and schema/ visible to the compiler.
INCLUDEPATH += $$PWD $$PWD/schema

# Non-code schema files: listed here so they show in the Qt Creator project
# tree (under "Other files") without being compiled.
DISTFILES += \
    schema/kavach.xml \
    schema/engine.py \
    schema/SCHEMA_GUIDE.md \
    schema/INTEGRATION.md

include($$PWD/serial.pri)
