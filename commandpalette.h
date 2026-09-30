#ifndef COMMANDPALETTE_H
#define COMMANDPALETTE_H

// =============================================================================
//  CommandPalette
//  -----------------------------------------------------------------------------
//  Ctrl+P: type a few letters, hit Enter, run the command.
//
//  WHY
//    There are around thirty-five actions spread across six menus, and nine
//    separate top-level windows among them. Finding "Plot field over time"
//    means knowing it lives under Tools rather than View — a distinction
//    that is obvious to whoever wrote the menus and arbitrary to everybody
//    else. Menus are fine for browsing and poor for recall.
//
//  IT HARVESTS THE REAL ACTIONS
//    The command list is built by walking the menu bar, not by maintaining
//    a parallel table. A hand-written list would be wrong within two
//    features — someone adds a menu item, forgets the palette, and the
//    palette silently becomes a subset that is worse than useless because
//    absence from it starts to mean nothing.
//
//    Walking the menus also means every command arrives with its shortcut
//    and its menu path already attached, so the palette doubles as the
//    shortcut reference the application otherwise lacks.
//
//  MATCHING IS SUBSEQUENCE, NOT SUBSTRING
//    "pfot" finds "Plot field over time". Substring matching would require
//    remembering the exact wording, which is the problem being solved.
// =============================================================================

#include <QDialog>
#include <QVector>

class QAction;
class QLineEdit;
class QListWidget;
class QMenuBar;

struct Command {
    QAction *action = nullptr;
    QString  path;        // "Tools › Plot field over time"
    QString  shortcut;
};

// Collect every enabled, non-separator action reachable from the menu bar,
// with its full path. Submenus are followed; menus themselves are not
// offered, only leaves.
QVector<Command> harvestCommands(QMenuBar *bar);

// Case-insensitive subsequence match, with a score: lower is better.
// Returns false when `needle` is not a subsequence of `haystack`.
bool fuzzyMatch(const QString &haystack, const QString &needle, int *score);

class CommandPalette : public QDialog
{
    Q_OBJECT

public:
    CommandPalette(const QVector<Command> &commands, QWidget *parent = nullptr);

protected:
    // Up/Down steer the result list while focus stays in the edit.
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void refilter(const QString &text);
    void runCurrent();

private:
    QVector<Command> m_all;
    QVector<int>     m_shown;     // indices into m_all

    QLineEdit   *m_edit = nullptr;
    QListWidget *m_list = nullptr;
};

#endif // COMMANDPALETTE_H
