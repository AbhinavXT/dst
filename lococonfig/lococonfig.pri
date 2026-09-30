# =============================================================================
#  lococonfig.pri -- Tools ▸ Loco Configuration… (LOCO_INFO editor / sender)
#  ---------------------------------------------------------------------------
#  Included by dlcore.pri (compiled once into the dlcore library), like
#  flasher.pri. Uses FlasherStyle (flasher/) for its colours, UdpSender for
#  its one transmit, and schema/kavach.xml (already a resource) for the
#  LOCO_INFO layout.
# =============================================================================
INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/lococonfigcore.cpp \
    $$PWD/lococonfigmodel.cpp \
    $$PWD/lococonfighistorydialog.cpp \
    $$PWD/lococonfigwindow.cpp

HEADERS += \
    $$PWD/lococonfigcore.h \
    $$PWD/lococonfigmodel.h \
    $$PWD/lococonfighistorydialog.h \
    $$PWD/lococonfigwindow.h

RESOURCES += \
    $$PWD/lococonfig.qrc

# Adding a LOCO_INFO member: one <field> in schema/kavach.xml, then
#   python3 lococonfig/sync_linfo.py path/to/loco_config_vNN.cpp
DISTFILES += \
    $$PWD/sync_linfo.py \
    $$PWD/ADDING_A_FIELD.md
