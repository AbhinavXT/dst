#ifndef SETTINGSBUNDLE_H
#define SETTINGSBUNDLE_H

// =============================================================================
//  SettingsBundle -- File ▸ Export settings… / Import settings… (session 79).
//  -----------------------------------------------------------------------------
//  One JSON file carrying the parts of a DLConsole setup that take effort to
//  build and are worth moving to another PC or giving to a colleague:
//
//    appearance        theme (and the last light/dark pair), colour-blind-
//                      safe status colours, text size
//    tags              the colour tag and label on each tab
//    pins              status-bar pins, Loco Console big-number tiles, the
//                      Pinned fields panel
//    layouts           named window layouts (window_layouts.json)
//    flasher_profiles  Firmware Flasher profiles (flasher_profiles.json)
//    loco_configs      Loco Configuration configurations (loco_configs.json)
//    serial            serial port profiles with their macros, the default
//                      macro row, and each port's / adapter's settings
//                      (session 115)
//
//  Deliberately NOT carried: network port, disk-log paths and quotas,
//  window positions outside layouts, session keys, history logs. Those
//  belong to the machine or to the record, not to the person's setup.
//
//  IMPORT REPLACES A SECTION WHOLE
//    Merging tab tags or flasher profiles key by key would leave a mix
//    nobody chose and no way to say which half came from where. A ticked
//    section is replaced as a unit, and the main window records what it
//    replaced so Edit ▸ Undo puts it back (capture() before apply()).
//
//  REFUSE, DON'T GUESS
//    A file section is checked before anything is written: the layouts
//    through WindowLayoutStore, the flasher profiles through the Flasher's
//    own ProfileStore loader, the loco configurations for their format and
//    a non-empty list. One bad section stops the whole import with nothing
//    changed.
// =============================================================================

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace SettingsBundle {

enum class Section { Appearance, Tags, Pins, Layouts, FlasherProfiles, LocoConfigs, Serial };

QList<Section> allSections();
QString id(Section s);            // the key in the file: "appearance", ...
QString label(Section s);         // "Theme and appearance", ...
bool    fromId(const QString &id, Section *s);

struct Bundle {
    QJsonObject sections;         // id -> section object
    QDateTime   exported;         // UTC
    QString     exportedBy;       // "DLConsole 1.0"

    bool           has(Section s) const { return sections.contains(id(s)); }
    QList<Section> present() const;
    // One line for the chooser: "Theme: nord, text size 110 %".
    QString summary(Section s) const;
    QJsonObject toJson() const;
};

// What the given sections hold right now. `dataDir` is where the JSON
// stores live; `iniPath` the INI. Both default to the real ones.
Bundle capture(const QList<Section> &sections,
               const QString &dataDir = QString(), const QString &iniPath = QString());

bool writeFile(const QString &path, const Bundle &bundle, QString *error = nullptr);
bool readFile(const QString &path, Bundle *bundle, QString *error = nullptr);
bool fromJson(const QJsonObject &object, Bundle *bundle, QString *error = nullptr);

// Check every requested section, then replace them. Nothing is written
// unless all of them pass. A section the bundle does not have is an error.
bool apply(const Bundle &bundle, const QList<Section> &sections,
           const QString &dataDir = QString(), const QString &iniPath = QString(),
           QString *error = nullptr);

// Undo of an import: put back what capture() took before it. A file
// section that was absent from the capture (no such file then) is deleted.
bool restore(const Bundle &backup, const QList<Section> &sections,
             const QString &dataDir = QString(), const QString &iniPath = QString(),
             QString *error = nullptr);

// Which stores a section's file lives in, for "close the X window first".
QString fileNameFor(Section s);   // empty for INI-only sections

}  // namespace SettingsBundle

#endif // SETTINGSBUNDLE_H
