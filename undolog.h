#ifndef UNDOLOG_H
#define UNDOLOG_H

// =============================================================================
//  UndoLog -- one step back from a destructive click (session 79).
//  -----------------------------------------------------------------------------
//  WHY NOT QUndoStack
//    QUndoStack wants every change to be a command that can be redone, and
//    wants to own the change as it happens. What this program needs is
//    narrower: a handful of clicks throw something away (Clear this tab,
//    Delete configuration, Remove all bookmarks, an import that replaces
//    settings) and the operator should be able to take the last one back.
//    Each of those sites already works; wrapping them as commands would
//    rewrite them. Here the site does exactly what it did, and pushes a
//    closure that puts back what it removed.
//
//  ONE PER WINDOW
//    Each window has its own log, and Ctrl+Z in a window undoes that
//    window's last step. One app-wide log would let Ctrl+Z in the Loco
//    Configuration bring back a tab cleared in the main window ten minutes
//    earlier, which nobody pressing it in that window means.
//
//  WHAT IT HOLDS
//    A cleared tab is the rows it had. Those are shared pointers the model
//    let go of, so holding them is holding the memory: the log keeps at most
//    kMaxSteps steps AND at most kMaxHeldRows rows across them, dropping the
//    oldest step first. A dropped step is gone; the Undo item always names
//    the step it will undo.
//
//  WHEN THE THING IS GONE
//    An undo closure returns false when what it would restore no longer
//    exists (the tab's model was deleted, the window closed). The step is
//    still consumed, and the caller says so rather than pretending.
// =============================================================================

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QAction;
class QWidget;

class UndoLog : public QObject
{
    Q_OBJECT

public:
    static constexpr int    kMaxSteps    = 20;
    static constexpr qint64 kMaxHeldRows = 1000000;

    explicit UndoLog(QObject *parent = nullptr);

    // `label` completes "Undo …": "Clear tab L1_V1", "Delete configuration
    // \"Loco 7\"". `heldRows` is how many log rows the closure keeps alive
    // (0 for anything that is not rows).
    void push(const QString &label, std::function<bool()> undo, qint64 heldRows = 0);

    bool    canUndo()   const { return !m_steps.isEmpty(); }
    QString nextLabel() const;
    int     depth()     const { return m_steps.size(); }
    qint64  heldRows()  const;

    // Undo the newest step. Returns what the closure returned (false also
    // when there was nothing to undo); `label` gets the step's label.
    bool undo(QString *label = nullptr);

    void clear();

    // An "Undo …" action for a window's menu: Ctrl+Z, scoped to that
    // window, its text and enabled state kept in step with the log.
    // Triggering it emits undone(label, ok) after running the step.
    QAction *createAction(QWidget *window);

signals:
    void changed();
    void pushed(const QString &label);
    void undone(const QString &label, bool restored);

private:
    struct Step {
        QString               label;
        std::function<bool()> undo;
        qint64                heldRows = 0;
    };
    void trim();

    QVector<Step> m_steps;   // oldest first
};

#endif // UNDOLOG_H
