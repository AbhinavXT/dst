#ifndef TAGLIBRARY_H
#define TAGLIBRARY_H

// =============================================================================
//  Tag scenario library (session 201) — the RFID Tag Builder's Library….
//  -----------------------------------------------------------------------------
//  tags_sim's KAV_CONFIG routes, ready to open, so nobody starts a route from
//  nothing. Two places, both listed:
//    built in   the copy compiled into DLConsole (:/tag_scenarios, from
//               tag_scenarios/ in the source tree via tag_scenarios.qrc)
//    folder     a folder on disk, <exe_dir>/tag_scenarios unless Settings
//               names another: copy the library there, add to it, or point
//               it at a team's own set
//  Made by scripts/tags_sim_library.py from a KAV_CONFIG folder: each .xlsx
//  route as a .tagroute.xml (tags, signals, direction), each route.xml /
//  Configuration1.xml as it was. Every route of every file is an entry; a
//  Configuration1.xml is many.
// =============================================================================

#include <QDialog>
#include <QString>
#include <QVector>

class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace TagLibrary {

QString builtInRoot();                    // ":/tag_scenarios"

struct Entry {
    QString file;                         // full path (":/…" for built in)
    QString folder;                       // relative folder, "" at the top
    QString fileName;
    int     route = 0;                    // index within the file
    int     routesInFile = 1;
    QString name;                         // route name
    int     tags = 0, signalCount = 0, dir = 0;
};

// Every route under `root`, folders and files in name order. Files that hold
// no route are left out.
QVector<Entry> scan(const QString &root);

}  // namespace TagLibrary

class TagLibraryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TagLibraryDialog(QWidget *parent = nullptr);

    // The picked entry, after Open (or a double-click).
    const TagLibrary::Entry &chosen() const { return m_chosen; }

    QTreeWidget *tree() const { return m_tree; }
    void setFilter(const QString &text);
    int  visibleRoutes() const;
    bool choose(QTreeWidgetItem *item);   // false if it is not a route

private:
    void fill();

    QTreeWidget *m_tree = nullptr;
    QLineEdit *m_filter = nullptr;
    QPushButton *m_open = nullptr;
    QVector<TagLibrary::Entry> m_entries;
    TagLibrary::Entry m_chosen;
};

#endif  // TAGLIBRARY_H
