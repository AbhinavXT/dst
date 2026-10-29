#ifndef ROWCLICKS_H
#define ROWCLICKS_H
// =============================================================================
//  RowClicks (session 414) — what a click on a log row does, as the operator
//  set it in Settings ▸ Row clicks.
//  -----------------------------------------------------------------------------
//  Five gestures: double-click, triple-click, Ctrl+double-click,
//  Shift+double-click, middle-click. Each runs one of the row menu's actions
//  (by its id: "workbench", "packetMaker", "bookmark", ...) or nothing.
//
//  One event filter on a table's viewport tells the gestures apart and hands
//  the row and the action's id to the window that owns the table, which runs
//  it through its own row menu: the same action, with the same checks, as
//  picking it from the menu. Used by the main window's log tabs (popped out
//  too) and the .dlr viewer's.
//
//  A plain double-click waits for the double-click interval before it acts
//  when triple-click runs something, so a triple-click does not also run the
//  double-click's action. With triple-click on Nothing, it acts at once.
// =============================================================================

#include <QModelIndex>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>

class QTableView;
class QTimer;

namespace RowClicks {

enum Gesture { Double, Triple, CtrlDouble, ShiftDouble, Middle, GestureCount };

QString gestureKey(Gesture g);     // "double", "triple", "ctrl_double", ...
QString gestureLabel(Gesture g);   // "Double-click", ...

struct Action {
    QString id;      // "" = nothing
    QString label;   // as the row menu names it
};
// Every action a gesture can run, "Nothing" first, in the row menu's order.
QVector<Action> actions();
QString actionLabel(const QString &id);

// The action set for a gesture (Settings, row_clicks/<gesture>); "" = none.
QString binding(Gesture g);
void setBinding(Gesture g, const QString &id);

}  // namespace RowClicks

class RowClickFilter : public QObject
{
    Q_OBJECT
public:
    // run(view, proxy index of the row, action id): the owner's row menu.
    using Runner = std::function<void(QTableView *, const QModelIndex &, const QString &)>;

    // Filters the view's viewport; owned by the view.
    static RowClickFilter *install(QTableView *view, Runner run);

    // What the next gesture would run (tests).
    static RowClicks::Gesture gestureOf(int button, Qt::KeyboardModifiers mods, bool triple);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    RowClickFilter(QTableView *view, Runner run);
    bool fire(RowClicks::Gesture g, const QModelIndex &index);

    QPointer<QTableView> m_view;
    Runner m_run;
    QTimer *m_wait = nullptr;          // a double-click, waiting to see if a third comes
    QPersistentModelIndex m_waiting;
    QPoint m_doubleAt;
    qint64 m_doubleMs = 0;
    QPersistentModelIndex m_middle;    // the row a middle press went down on
};

#endif // ROWCLICKS_H
