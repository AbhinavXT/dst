#include "workspacesnapshot.h"

#include "settings.h"
#include "tabpopoutwindow.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>

namespace {

const QChar kSep(0x1F);   // the workspace record separator; see saveWorkspace()
const char  kLayoutsFormat[]  = "dlconsole-layouts";
const char  kRecoveryFormat[] = "dlconsole-session-recovery";
const int   kFormatVersion    = 1;

QString besideIni(const char *name)
{
    return QDir(QFileInfo(Settings::iniPath()).absolutePath()).filePath(QLatin1String(name));
}

QString b64(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toBase64());
}

QByteArray unb64(const QJsonValue &value)
{
    return QByteArray::fromBase64(value.toString().toLatin1());
}

bool writeAtomically(const QString &path, const QJsonObject &object, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
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

bool readObject(const QString &path, QJsonObject *object, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("not valid JSON: %1").arg(parseError.errorString());
        return false;
    }
    *object = doc.object();
    return true;
}

}  // namespace

// =============================================================================
//  WorkspaceSnapshot
// =============================================================================

int WorkspaceSnapshot::visibleTabCount() const
{
    int n = 0;
    for (const WorkspaceTab &t : tabs) {
        if (t.visible) ++n;
    }
    return n;
}

QString WorkspaceSnapshot::summary() const
{
    const int shown  = visibleTabCount();
    const int hidden = tabs.size() - shown;
    QString text = (shown == 1) ? QStringLiteral("1 tab") : QStringLiteral("%1 tabs").arg(shown);
    if (hidden > 0) {
        text += QStringLiteral(" (%1 hidden)").arg(hidden);
    }
    if (!popouts.isEmpty()) {
        text += (popouts.size() == 1) ? QStringLiteral(", 1 pop-out")
                                      : QStringLiteral(", %1 pop-outs").arg(popouts.size());
    }
    return text;
}

QJsonObject WorkspaceSnapshot::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("main_geometry")] = b64(mainGeometry);
    o[QStringLiteral("main_state")]    = b64(mainState);
    QJsonArray tabArray;
    for (const WorkspaceTab &t : tabs) {
        QJsonObject to;
        to[QStringLiteral("key")]     = t.key;
        to[QStringLiteral("name")]    = t.name;
        to[QStringLiteral("visible")] = t.visible;
        tabArray.append(to);
    }
    o[QStringLiteral("tabs")]       = tabArray;
    o[QStringLiteral("active_tab")] = activeTab;
    QJsonArray pops;
    for (const QString &key : popouts) {
        QJsonObject po;
        po[QStringLiteral("key")]      = key;
        po[QStringLiteral("geometry")] = b64(popoutGeometry.value(key));
        pops.append(po);
    }
    o[QStringLiteral("popouts")] = pops;
    if (savedAt.isValid()) {
        o[QStringLiteral("saved")] = savedAt.toUTC().toString(Qt::ISODate);
    }
    return o;
}

WorkspaceSnapshot WorkspaceSnapshot::fromJson(const QJsonObject &o, bool *ok)
{
    WorkspaceSnapshot s;
    bool good = o.contains(QStringLiteral("tabs")) && o.value(QStringLiteral("tabs")).isArray();
    s.mainGeometry = unb64(o.value(QStringLiteral("main_geometry")));
    s.mainState    = unb64(o.value(QStringLiteral("main_state")));
    for (const QJsonValue &v : o.value(QStringLiteral("tabs")).toArray()) {
        const QJsonObject to = v.toObject();
        WorkspaceTab t;
        t.key     = to.value(QStringLiteral("key")).toString();
        t.name    = to.value(QStringLiteral("name")).toString();
        t.visible = to.value(QStringLiteral("visible")).toBool(true);
        // A key is what a tab is; a nameless one is fine (the dispatcher
        // names it), a keyless one is nothing.
        if (!t.key.isEmpty() && !t.key.contains(kSep)) {
            s.tabs.append(t);
        } else {
            good = false;
        }
    }
    s.activeTab = o.value(QStringLiteral("active_tab")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("popouts")).toArray()) {
        const QJsonObject po = v.toObject();
        const QString key = po.value(QStringLiteral("key")).toString();
        if (key.isEmpty() || s.popouts.contains(key)) continue;
        s.popouts.append(key);
        const QByteArray geometry = unb64(po.value(QStringLiteral("geometry")));
        if (!geometry.isEmpty()) {
            s.popoutGeometry.insert(key, geometry);
        }
    }
    s.popouts.sort();
    s.savedAt = QDateTime::fromString(o.value(QStringLiteral("saved")).toString(), Qt::ISODate);
    if (ok) *ok = good;
    return s;
}

QStringList WorkspaceSnapshot::workspaceRecords() const
{
    QStringList records;
    for (const WorkspaceTab &t : tabs) {
        records << QStringList{ t.key, t.name, t.visible ? QStringLiteral("1") : QStringLiteral("0") }.join(kSep);
    }
    return records;
}

QVector<WorkspaceTab> WorkspaceSnapshot::tabsFromRecords(const QStringList &records)
{
    QVector<WorkspaceTab> out;
    for (const QString &rec : records) {
        const QStringList parts = rec.split(kSep);
        if (parts.size() < 3 || parts.at(0).isEmpty()) continue;
        out.append({ parts.at(0), parts.at(1), parts.at(2) == QLatin1String("1") });
    }
    return out;
}

void WorkspaceSnapshot::writeToStartupSettings() const
{
    Settings::setWorkspaceTabs(workspaceRecords());
    Settings::setWorkspaceActiveTab(activeTab);
    if (!mainGeometry.isEmpty()) Settings::setMainWindowGeometry(mainGeometry);
    if (!mainState.isEmpty())    Settings::setMainWindowState(mainState);
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ui/popouts"), popouts);
    for (auto it = popoutGeometry.constBegin(); it != popoutGeometry.constEnd(); ++it) {
        settings.setValue(QStringLiteral("windows/%1_geometry").arg(TabPopoutWindow::geometryKey(it.key())),
                          it.value());
    }
    settings.sync();
}

bool WorkspaceSnapshot::sameAs(const WorkspaceSnapshot &other) const
{
    return mainGeometry == other.mainGeometry && mainState == other.mainState
        && tabs == other.tabs && activeTab == other.activeTab
        && popouts == other.popouts && popoutGeometry == other.popoutGeometry;
}

// =============================================================================
//  WindowLayoutStore
// =============================================================================

WindowLayoutStore::WindowLayoutStore(const QString &filePath)
    : m_path(filePath.isEmpty() ? defaultPath() : filePath)
{
}

QString WindowLayoutStore::defaultPath()
{
    return besideIni("window_layouts.json");
}

bool WindowLayoutStore::load()
{
    m_entries.clear();
    if (!QFileInfo::exists(m_path)) {
        return true;
    }
    QJsonObject object;
    if (!readObject(m_path, &object, &m_lastError)) {
        return false;
    }
    return fromJson(object, &m_lastError);
}

bool WindowLayoutStore::save() const
{
    return writeAtomically(m_path, toJson(), &m_lastError);
}

QJsonObject WindowLayoutStore::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("format")]  = QLatin1String(kLayoutsFormat);
    o[QStringLiteral("version")] = kFormatVersion;
    QJsonArray layouts;
    for (const Entry &e : m_entries) {
        QJsonObject lo;
        lo[QStringLiteral("name")]     = e.name;
        lo[QStringLiteral("snapshot")] = e.snapshot.toJson();
        layouts.append(lo);
    }
    o[QStringLiteral("layouts")] = layouts;
    return o;
}

bool WindowLayoutStore::fromJson(const QJsonObject &o, QString *error)
{
    if (o.value(QStringLiteral("format")).toString() != QLatin1String(kLayoutsFormat)) {
        if (error) *error = QStringLiteral("not a DLConsole layouts file");
        return false;
    }
    if (o.value(QStringLiteral("version")).toInt() > kFormatVersion) {
        if (error) *error = QStringLiteral("written by a newer DLConsole (version %1)")
                                .arg(o.value(QStringLiteral("version")).toInt());
        return false;
    }
    QVector<Entry> entries;
    for (const QJsonValue &v : o.value(QStringLiteral("layouts")).toArray()) {
        const QJsonObject lo = v.toObject();
        const QString name = lo.value(QStringLiteral("name")).toString().trimmed();
        bool ok = false;
        const WorkspaceSnapshot snap = WorkspaceSnapshot::fromJson(lo.value(QStringLiteral("snapshot")).toObject(), &ok);
        if (name.isEmpty() || !ok) {
            if (error) *error = QStringLiteral("a layout entry is damaged (\"%1\")").arg(name);
            return false;
        }
        bool dup = false;
        for (const Entry &e : entries) dup = dup || e.name == name;
        if (!dup) entries.append({ name, snap });
    }
    m_entries = entries;
    return true;
}

QStringList WindowLayoutStore::names() const
{
    QStringList out;
    for (const Entry &e : m_entries) out << e.name;
    return out;
}

int WindowLayoutStore::indexOf(const QString &name) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).name == name) return i;
    }
    return -1;
}

bool WindowLayoutStore::contains(const QString &name) const
{
    return indexOf(name) >= 0;
}

WorkspaceSnapshot WindowLayoutStore::snapshot(const QString &name) const
{
    const int i = indexOf(name);
    return i >= 0 ? m_entries.at(i).snapshot : WorkspaceSnapshot();
}

void WindowLayoutStore::put(const QString &name, const WorkspaceSnapshot &snapshot)
{
    const QString clean = name.trimmed().left(kMaxNameLength);
    if (clean.isEmpty()) return;
    const int i = indexOf(clean);
    if (i >= 0) {
        m_entries[i].snapshot = snapshot;
    } else {
        m_entries.append({ clean, snapshot });
    }
}

void WindowLayoutStore::insert(int index, const QString &name, const WorkspaceSnapshot &snapshot)
{
    if (name.trimmed().isEmpty() || contains(name)) return;
    index = qBound(0, index, m_entries.size());
    m_entries.insert(index, { name, snapshot });
}

bool WindowLayoutStore::remove(const QString &name)
{
    const int i = indexOf(name);
    if (i < 0) return false;
    m_entries.removeAt(i);
    return true;
}

// =============================================================================
//  SessionRecovery
// =============================================================================

QString SessionRecovery::defaultPath()
{
    return besideIni("session_recovery.json");
}

bool SessionRecovery::write(const QString &path, const WorkspaceSnapshot &snapshot, QString *error)
{
    QJsonObject o;
    o[QStringLiteral("format")]   = QLatin1String(kRecoveryFormat);
    o[QStringLiteral("version")]  = kFormatVersion;
    o[QStringLiteral("snapshot")] = snapshot.toJson();
    return writeAtomically(path, o, error);
}

bool SessionRecovery::read(const QString &path, WorkspaceSnapshot *snapshot, QString *error)
{
    QJsonObject o;
    if (!readObject(path, &o, error)) return false;
    if (o.value(QStringLiteral("format")).toString() != QLatin1String(kRecoveryFormat)) {
        if (error) *error = QStringLiteral("not a session recovery file");
        return false;
    }
    bool ok = false;
    const WorkspaceSnapshot s = WorkspaceSnapshot::fromJson(o.value(QStringLiteral("snapshot")).toObject(), &ok);
    if (!ok) {
        if (error) *error = QStringLiteral("the snapshot in it is damaged");
        return false;
    }
    if (snapshot) *snapshot = s;
    return true;
}

void SessionRecovery::discard(const QString &path)
{
    QFile::remove(path);
}

bool SessionRecovery::adoptAfterCrash(const QString &path, WorkspaceSnapshot *recovered)
{
    const bool crashed = Settings::sessionRunning();
    Settings::setSessionRunning(true);
    if (!crashed) {
        // A file left from a clean run (a copy restored from a backup, say)
        // is older than what closeEvent wrote. Not used.
        discard(path);
        return false;
    }
    WorkspaceSnapshot snap;
    if (!read(path, &snap) || snap.isEmpty()) {
        return false;
    }
    snap.writeToStartupSettings();
    if (recovered) *recovered = snap;
    return true;
}

void SessionRecovery::markCleanExit(const QString &path)
{
    discard(path);
    Settings::setSessionRunning(false);
}
