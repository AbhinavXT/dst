#ifndef SHORTCUTSDIALOG_H
#define SHORTCUTSDIALOG_H

// =============================================================================
//  ShortcutsDialog -- Help ▸ Keyboard Shortcuts (F1), session 290
//  ---------------------------------------------------------------------------
//  Every keyboard shortcut of a window, with what it does and where it sits
//  in the menus, and a search box. Built from the window's real actions
//  each time it opens, like the command palette (commandpalette.h), so it
//  cannot fall behind the menus. Unlike the palette it lists disabled
//  actions too: what a key does is worth knowing before it can be pressed.
// =============================================================================

#include <QDialog>
#include <QVector>

class QLineEdit;
class QMenuBar;
class QTableWidget;

struct ShortcutRow {
    QString keys;       // native text, e.g. "Ctrl+Shift+L" / "⇧⌘L"
    QString portable;   // always "Ctrl+Shift+L": what the search also matches (a Mac shows ⌘)
    QString command;    // "Live Loco Console…"
    QString where;      // "Tools › Live › Live Loco Console…"; "(window)" when not in a menu
    bool    enabled = true;
};

// Every action with a shortcut reachable from `bar`, in menu order, then
// any on `window` itself that no menu holds. Submenus are followed
// (aboutToShow first, for menus built on show).
QVector<ShortcutRow> harvestShortcuts(QMenuBar *bar, QWidget *window = nullptr);

class ShortcutsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortcutsDialog(const QVector<ShortcutRow> &rows, QWidget *parent = nullptr);

    // The search box's text; rows matching none of keys, command, where are hidden.
    void setFilter(const QString &text);
    int  visibleRows() const;
    QTableWidget *table() const { return m_table; }

private:
    QLineEdit    *m_search = nullptr;
    QTableWidget *m_table  = nullptr;
};

#endif  // SHORTCUTSDIALOG_H
