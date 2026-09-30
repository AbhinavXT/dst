#include "settingsbundle.h"

#include "settings.h"
#include "workspacesnapshot.h"
#include "flasher/flashercore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>

namespace SettingsBundle {
namespace {

const char kFormat[]  = "dlconsole-settings";
const int  kVersion   = 1;

QString resolvedIni(const QString &iniPath)
{
    return iniPath.isEmpty() ? Settings::iniPath() : iniPath;
}

QString resolvedDir(const QString &dataDir, const QString &iniPath)
{
    return dataDir.isEmpty() ? QFileInfo(resolvedIni(iniPath)).absolutePath() : dataDir;
}

// Exact keys each INI section owns, plus a group prefix for the one that
// has a key per tab.
QStringList iniKeys(Section s)
{
    switch (s) {
    case Section::Appearance:
        return { QStringLiteral("ui/theme"), QStringLiteral("ui/lastDarkTheme"),
                 QStringLiteral("ui/lastLightTheme"), QStringLiteral("ui/colorBlindSafe"),
                 QStringLiteral("ui/textZoom") };
    case Section::Pins:
        return { QStringLiteral("ui/statusPins"), QStringLiteral("lococonsole/bigNumbers"),
                 QStringLiteral("ui/pinned_fields") };
    default:
        return {};
    }
}

QString iniPrefix(Section s)
{
    return s == Section::Tags ? QStringLiteral("ui/tabTags/") : QString();
}

bool isIniSection(Section s)
{
    return s == Section::Appearance || s == Section::Tags || s == Section::Pins;
}

QStringList keysOf(Section s, QSettings &settings)
{
    QStringList keys = iniKeys(s);
    const QString prefix = iniPrefix(s);
    if (!prefix.isEmpty()) {
        for (const QString &k : settings.allKeys()) {
            if (k.startsWith(prefix)) keys << k;
        }
    }
    return keys;
}

// INI values come back as strings, string lists, or invalid (an empty list
// is written as @Invalid()). Those are the three JSON shapes used.
QJsonValue toJsonValue(const QVariant &v)
{
    if (!v.isValid()) {
        return QJsonValue(QJsonValue::Null);
    }
    if (v.type() == QVariant::StringList || v.type() == QVariant::List) {
        return QJsonArray::fromStringList(v.toStringList());
    }
    return QJsonValue(v.toString());
}

QVariant fromJsonValue(const QJsonValue &j)
{
    if (j.isNull()) {
        return QVariant(QStringList());
    }
    if (j.isArray()) {
        QStringList list;
        for (const QJsonValue &item : j.toArray()) list << item.toString();
        return QVariant(list);
    }
    if (j.isBool()) {
        return QVariant(j.toBool());
    }
    if (j.isDouble()) {
        return QVariant(j.toInt());
    }
    return QVariant(j.toString());
}

bool readJsonFile(const QString &path, QJsonObject *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = pe.errorString();
        return false;
    }
    *out = doc.object();
    return true;
}

bool writeJsonFile(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

// ---- checks, one per file section -----------------------------------------
bool checkFileSection(Section s, const QJsonObject &content, QString *why)
{
    if (s == Section::Layouts) {
        WindowLayoutStore store(QStringLiteral("unused"));
        return store.fromJson(content, why);
    }
    if (s == Section::FlasherProfiles) {
        const QJsonArray profiles = content.value(QStringLiteral("profiles")).toArray();
        if (profiles.isEmpty()) {
            if (why) *why = QStringLiteral("it holds no profiles");
            return false;
        }
        // Through the Flasher's own loader: what it would refuse, this refuses.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("flasher_profiles.json"));
        if (!dir.isValid() || !writeJsonFile(path, content, why)) return false;
        Flasher::ProfileStore store(path);
        if (!store.load()) {
            if (why) *why = store.lastError();
            return false;
        }
        return true;
    }
    if (s == Section::LocoConfigs) {
        if (content.value(QStringLiteral("format")).toInt() != 1) {
            if (why) *why = QStringLiteral("unknown loco_configs format %1")
                                .arg(content.value(QStringLiteral("format")).toInt());
            return false;
        }
        if (content.value(QStringLiteral("configs")).toArray().isEmpty()) {
            if (why) *why = QStringLiteral("it holds no configurations");
            return false;
        }
        return true;
    }
    return true;
}

QStringList namesIn(const QJsonArray &array, const char *field)
{
    QStringList names;
    for (const QJsonValue &v : array) names << v.toObject().value(QLatin1String(field)).toString();
    return names;
}

QString countAndNames(int n, const QString &one, const QString &many, const QStringList &names)
{
    QString text = (n == 1) ? one : many.arg(n);
    if (!names.isEmpty()) {
        QStringList shown = names.mid(0, 4);
        text += QStringLiteral(": ") + shown.join(QStringLiteral(", "));
        if (names.size() > 4) text += QStringLiteral(", …");
    }
    return text;
}

}  // namespace

// =============================================================================

QList<Section> allSections()
{
    return { Section::Appearance, Section::Tags, Section::Pins,
             Section::Layouts, Section::FlasherProfiles, Section::LocoConfigs };
}

QString id(Section s)
{
    switch (s) {
    case Section::Appearance:      return QStringLiteral("appearance");
    case Section::Tags:            return QStringLiteral("tags");
    case Section::Pins:            return QStringLiteral("pins");
    case Section::Layouts:         return QStringLiteral("layouts");
    case Section::FlasherProfiles: return QStringLiteral("flasher_profiles");
    case Section::LocoConfigs:     return QStringLiteral("loco_configs");
    }
    return QString();
}

QString label(Section s)
{
    switch (s) {
    case Section::Appearance:      return QObject::tr("Theme and appearance");
    case Section::Tags:            return QObject::tr("Tab colour tags");
    case Section::Pins:            return QObject::tr("Pins and big numbers");
    case Section::Layouts:         return QObject::tr("Window layouts");
    case Section::FlasherProfiles: return QObject::tr("Firmware Flasher profiles");
    case Section::LocoConfigs:     return QObject::tr("Loco configurations");
    }
    return QString();
}

bool fromId(const QString &text, Section *s)
{
    for (Section candidate : allSections()) {
        if (id(candidate) == text) {
            if (s) *s = candidate;
            return true;
        }
    }
    return false;
}

QString fileNameFor(Section s)
{
    switch (s) {
    case Section::Layouts:         return QStringLiteral("window_layouts.json");
    case Section::FlasherProfiles: return QStringLiteral("flasher_profiles.json");
    case Section::LocoConfigs:     return QStringLiteral("loco_configs.json");
    default:                       return QString();
    }
}

QList<Section> Bundle::present() const
{
    QList<Section> out;
    for (Section s : allSections()) {
        if (has(s)) out << s;
    }
    return out;
}

QString Bundle::summary(Section s) const
{
    const QJsonObject section = sections.value(id(s)).toObject();
    const QJsonObject ini  = section.value(QStringLiteral("ini")).toObject();
    const QJsonObject file = section.value(QStringLiteral("file")).toObject();
    auto listSize = [&ini](const char *key) {
        const QJsonValue v = ini.value(QLatin1String(key));
        if (v.isArray()) return v.toArray().size();
        return v.toString().isEmpty() ? 0 : 1;
    };
    switch (s) {
    case Section::Appearance: {
        QStringList parts;
        if (ini.contains(QStringLiteral("ui/theme")))
            parts << QObject::tr("theme %1").arg(ini.value(QStringLiteral("ui/theme")).toString());
        if (ini.contains(QStringLiteral("ui/textZoom")))
            parts << QObject::tr("text %1 %").arg(ini.value(QStringLiteral("ui/textZoom")).toVariant().toString());
        const QString cb = ini.value(QStringLiteral("ui/colorBlindSafe")).toVariant().toString();
        if (cb == QLatin1String("true"))
            parts << QObject::tr("colour-blind-safe colours");
        return parts.isEmpty() ? QObject::tr("defaults") : parts.join(QStringLiteral(", "));
    }
    case Section::Tags: {
        const int n = ini.size();
        return n == 1 ? QObject::tr("1 tagged tab") : QObject::tr("%1 tagged tabs").arg(n);
    }
    case Section::Pins:
        return QObject::tr("%1 status-bar, %2 big numbers, %3 pinned fields")
            .arg(listSize("ui/statusPins"))
            .arg(ini.contains(QStringLiteral("lococonsole/bigNumbers"))
                     ? QString::number(listSize("lococonsole/bigNumbers")) : QObject::tr("default"))
            .arg(listSize("ui/pinned_fields"));
    case Section::Layouts: {
        const QJsonArray a = file.value(QStringLiteral("layouts")).toArray();
        return countAndNames(a.size(), QObject::tr("1 layout"), QObject::tr("%1 layouts"), namesIn(a, "name"));
    }
    case Section::FlasherProfiles: {
        const QJsonArray a = file.value(QStringLiteral("profiles")).toArray();
        return countAndNames(a.size(), QObject::tr("1 profile"), QObject::tr("%1 profiles"), namesIn(a, "name"));
    }
    case Section::LocoConfigs: {
        const QJsonArray a = file.value(QStringLiteral("configs")).toArray();
        return countAndNames(a.size(), QObject::tr("1 configuration"), QObject::tr("%1 configurations"), namesIn(a, "name"));
    }
    }
    return QString();
}

QJsonObject Bundle::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("format")]      = QLatin1String(kFormat);
    o[QStringLiteral("version")]     = kVersion;
    o[QStringLiteral("exported")]    = exported.toUTC().toString(Qt::ISODate);
    o[QStringLiteral("exported_by")] = exportedBy;
    o[QStringLiteral("sections")]    = sections;
    return o;
}

Bundle capture(const QList<Section> &wanted, const QString &dataDir, const QString &iniPath)
{
    Bundle bundle;
    bundle.exported   = QDateTime::currentDateTimeUtc();
    bundle.exportedBy = QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(),
                                                     QCoreApplication::applicationVersion()).trimmed();
    QSettings settings(resolvedIni(iniPath), QSettings::IniFormat);
    const QDir dir(resolvedDir(dataDir, iniPath));

    for (Section s : wanted) {
        QJsonObject section;
        if (isIniSection(s)) {
            QJsonObject ini;
            for (const QString &key : keysOf(s, settings)) {
                if (settings.contains(key)) {
                    ini[key] = toJsonValue(settings.value(key));
                }
            }
            section[QStringLiteral("ini")] = ini;
        } else {
            const QString path = dir.filePath(fileNameFor(s));
            QJsonObject content;
            if (QFileInfo::exists(path) && readJsonFile(path, &content, nullptr)) {
                section[QStringLiteral("file")] = content;
            } else {
                // Nothing set up on this machine: no section, rather than an
                // empty one that would wipe the target on import.
                continue;
            }
        }
        bundle.sections[id(s)] = section;
    }
    return bundle;
}

bool fromJson(const QJsonObject &o, Bundle *bundle, QString *error)
{
    if (o.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)) {
        if (error) *error = QObject::tr("This is not a DLConsole settings file.");
        return false;
    }
    if (o.value(QStringLiteral("version")).toInt() > kVersion) {
        if (error) *error = QObject::tr("This file was written by a newer DLConsole (format %1).")
                                .arg(o.value(QStringLiteral("version")).toInt());
        return false;
    }
    Bundle b;
    b.exported   = QDateTime::fromString(o.value(QStringLiteral("exported")).toString(), Qt::ISODate);
    b.exportedBy = o.value(QStringLiteral("exported_by")).toString();
    const QJsonObject sections = o.value(QStringLiteral("sections")).toObject();
    for (auto it = sections.constBegin(); it != sections.constEnd(); ++it) {
        Section s;
        if (fromId(it.key(), &s)) {          // unknown sections: from a newer build, ignored
            b.sections[it.key()] = it.value().toObject();
        }
    }
    if (bundle) *bundle = b;
    return true;
}

bool writeFile(const QString &path, const Bundle &bundle, QString *error)
{
    return writeJsonFile(path, bundle.toJson(), error);
}

bool readFile(const QString &path, Bundle *bundle, QString *error)
{
    QJsonObject o;
    QString why;
    if (!readJsonFile(path, &o, &why)) {
        if (error) *error = QObject::tr("Could not read %1: %2").arg(path, why);
        return false;
    }
    return fromJson(o, bundle, error);
}

bool apply(const Bundle &bundle, const QList<Section> &wanted,
           const QString &dataDir, const QString &iniPath, QString *error)
{
    // ---- check everything first ------------------------------------------
    for (Section s : wanted) {
        if (!bundle.has(s)) {
            if (error) *error = QObject::tr("The file has no \"%1\" section.").arg(label(s));
            return false;
        }
        if (!isIniSection(s)) {
            const QJsonObject content = bundle.sections.value(id(s)).toObject()
                                            .value(QStringLiteral("file")).toObject();
            QString why;
            if (!checkFileSection(s, content, &why)) {
                if (error) *error = QObject::tr("%1: %2. Nothing was imported.").arg(label(s), why);
                return false;
            }
        }
    }

    // ---- then write ------------------------------------------------------
    QSettings settings(resolvedIni(iniPath), QSettings::IniFormat);
    const QDir dir(resolvedDir(dataDir, iniPath));
    for (Section s : wanted) {
        const QJsonObject section = bundle.sections.value(id(s)).toObject();
        if (isIniSection(s)) {
            for (const QString &key : keysOf(s, settings)) {
                settings.remove(key);
            }
            const QJsonObject ini = section.value(QStringLiteral("ini")).toObject();
            const QStringList owned = iniKeys(s);
            const QString prefix = iniPrefix(s);
            for (auto it = ini.constBegin(); it != ini.constEnd(); ++it) {
                // Only keys this section owns: a hand-edited file cannot use
                // "tags" to set the UDP port.
                const bool mine = owned.contains(it.key())
                               || (!prefix.isEmpty() && it.key().startsWith(prefix));
                if (mine) {
                    settings.setValue(it.key(), fromJsonValue(it.value()));
                }
            }
        } else {
            const QString path = dir.filePath(fileNameFor(s));
            QString why;
            if (!writeJsonFile(path, section.value(QStringLiteral("file")).toObject(), &why)) {
                if (error) *error = QObject::tr("%1: could not write %2: %3").arg(label(s), path, why);
                return false;
            }
        }
    }
    settings.sync();
    return true;
}

bool restore(const Bundle &backup, const QList<Section> &sections,
             const QString &dataDir, const QString &iniPath, QString *error)
{
    QList<Section> present;
    const QDir dir(resolvedDir(dataDir, iniPath));
    for (Section s : sections) {
        if (backup.has(s)) {
            present << s;
        } else if (!isIniSection(s)) {
            // There was no file before the import: there is none after undo.
            QFile::remove(dir.filePath(fileNameFor(s)));
        }
    }
    return present.isEmpty() || apply(backup, present, dataDir, iniPath, error);
}

}  // namespace SettingsBundle
