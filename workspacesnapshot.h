#ifndef WORKSPACESNAPSHOT_H
#define WORKSPACESNAPSHOT_H

// =============================================================================
//  WorkspaceSnapshot -- the arrangement of the console, as one value
//  (session 79).
//  -----------------------------------------------------------------------------
//  One core, two uses:
//
//    WINDOW LAYOUTS (View ▸ Layouts). A snapshot saved under a name and put
//    back on request: "Bench" with six loco tabs and the Merged view popped
//    out on the second screen, "Review" with two tabs and the docks closed.
//
//    CRASH RECOVERY. Until now the workspace, the pop-outs and the dock
//    layout were written only in closeEvent. A crash, a kill from Task
//    Manager or a power cut lost every tab opened since the last clean exit
//    (the disk log survived; the arrangement did not). The main window now
//    writes a snapshot to session_recovery.json whenever it changes, at most
//    every few seconds, and a flag in the ini says whether the last run
//    closed cleanly. If it did not, the snapshot is written into the ini
//    keys the normal startup restore reads, before that restore runs, so
//    there is one restore path, not two.
//
//  WHAT IS IN IT
//    The main window's geometry and dock state; every tab in bar order with
//    its friendly name and whether it is shown (hidden tabs are sources the
//    operator chose to keep, as in the workspace); the tab in front; which
//    tabs are popped out, and where.
//
//  WHAT IS NOT
//    Tool windows (Loco Console, fault panel, ...) and per-tab filters. A
//    filter restored without the operator asking hides live traffic, which
//    is the reason the workspace never stored them either.
// =============================================================================

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct WorkspaceTab {
    QString key;
    QString name;
    bool    visible = true;
    bool operator==(const WorkspaceTab &o) const
    { return key == o.key && name == o.name && visible == o.visible; }
};

struct WorkspaceSnapshot {
    QByteArray               mainGeometry;
    QByteArray               mainState;
    QVector<WorkspaceTab>    tabs;           // bar order; hidden ones last
    QString                  activeTab;
    QStringList              popouts;        // tab keys, sorted
    QHash<QString, QByteArray> popoutGeometry; // tab key -> saveGeometry()
    QDateTime                savedAt;        // UTC; not part of equality

    bool isEmpty() const { return tabs.isEmpty() && mainGeometry.isEmpty(); }
    int  visibleTabCount() const;
    // "5 tabs (1 hidden), 2 pop-outs"
    QString summary() const;

    QJsonObject toJson() const;
    static WorkspaceSnapshot fromJson(const QJsonObject &object, bool *ok = nullptr);

    // The ini records Settings::workspaceTabs() stores ("key␟name␟1").
    QStringList workspaceRecords() const;
    static QVector<WorkspaceTab> tabsFromRecords(const QStringList &records);

    // Write into the keys MainWindow's startup restore reads: workspace
    // tabs, active tab, open pop-outs and their geometry, main window
    // geometry and state. Used by crash recovery.
    void writeToStartupSettings() const;

    // Same content, ignoring when it was taken.
    bool sameAs(const WorkspaceSnapshot &other) const;
};

// ---- named layouts: window_layouts.json beside dlconsole.ini ----------------
class WindowLayoutStore
{
public:
    explicit WindowLayoutStore(const QString &filePath = QString());   // empty = default path
    static QString defaultPath();

    bool    load();              // a missing file is an empty store, not an error
    bool    save() const;        // atomic (QSaveFile)
    QString lastError() const { return m_lastError; }
    QString filePath()  const { return m_path; }

    QStringList       names() const;       // in the order they were first saved
    bool              contains(const QString &name) const;
    WorkspaceSnapshot snapshot(const QString &name) const;
    int               indexOf(const QString &name) const;

    // Replaces a layout of the same name (keeping its place) or appends.
    void put(const QString &name, const WorkspaceSnapshot &snapshot);
    // Puts a layout back at `index` (undo of remove).
    void insert(int index, const QString &name, const WorkspaceSnapshot &snapshot);
    bool remove(const QString &name);

    // The whole file as JSON, and back: settings export/import.
    QJsonObject toJson() const;
    bool        fromJson(const QJsonObject &object, QString *error = nullptr);

    static constexpr int kMaxNameLength = 40;

private:
    struct Entry { QString name; WorkspaceSnapshot snapshot; };
    QString          m_path;
    QVector<Entry>   m_entries;
    mutable QString  m_lastError;
};

// ---- crash recovery -----------------------------------------------------------
namespace SessionRecovery {

QString defaultPath();       // session_recovery.json beside dlconsole.ini

bool write(const QString &path, const WorkspaceSnapshot &snapshot, QString *error = nullptr);
bool read(const QString &path, WorkspaceSnapshot *snapshot, QString *error = nullptr);
void discard(const QString &path);

// Call once, early in startup. Returns true when the previous run did not
// close cleanly AND left a readable snapshot, which is then written into
// the startup keys (see writeToStartupSettings) and returned in *recovered.
// Marks this run as running either way.
bool adoptAfterCrash(const QString &path, WorkspaceSnapshot *recovered);

// Call from a clean close: marks the run finished and removes the file.
void markCleanExit(const QString &path);

}  // namespace SessionRecovery

#endif // WORKSPACESNAPSHOT_H
