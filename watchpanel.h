#ifndef WATCHPANEL_H
#define WATCHPANEL_H
// =============================================================================
//  WatchPanel — the floating window that holds watches and shows what fired.
//
//  Floating for the same reason the pin board is: a watch is set up before a
//  run and read DURING one, while the operator is at the DMI and the console
//  is behind something else. A panel folded into the side of the log window is
//  a panel nobody is looking at when it fires.
// =============================================================================

#include <QWidget>

#include "watchlist.h"

class NameMap;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QMenu;
class QTimer;
class QueryLineEdit;
class StatusLine;

class WatchPanel : public QWidget
{
    Q_OBJECT

public:
    explicit WatchPanel(QWidget *parent = nullptr);

    WatchList &list() { return m_list; }

    // Offered EVERY entry, not one per batch. See WatchList for why a watch
    // may not sample.
    void observe(const LogEntryPtr &entry, const QString &sourceKey);

    void setNames(const NameMap *names) { m_names = names; }

    void restore();
    void persist() const;
    // Remove pushes its undo here when set (session 79).
    void setUndoLog(class UndoLog *log) { m_undo = log; }
    // Session 82: a watch's actions; the list, for the tests.
    bool setActions(int index, bool beep, bool bookmark, bool everyTime);
    const WatchList &list() const { return m_list; }

    // Session 161: a ready-made watch (watchrules.h) on or off. On adds it
    // as an ordinary watch (once); off removes it, with undo. False when
    // nothing changed or the id is unknown.
    bool setRuleOn(const QString &id, bool on);
    bool ruleOn(const QString &id) const;
    QMenu *readyMadeMenu() const { return m_readyMenu; }

signals:
    // A watch fired. The owner announces it — this panel may be behind
    // another window, which is exactly when a watch matters most.
    void watchFired(const QString &label, const QString &expr,
                    const QString &sourceKey);

    // Take the view to the frame that fired a watch.
    void revealRequested(const LogEntryPtr &entry, const QString &sourceKey);

    // A watch with the bookmark action fired on this frame (session 82).
    void bookmarkRequested(const LogEntryPtr &entry, const QString &sourceKey, const QString &note);

private slots:
    void addWatch();
    void removeSelected();
    void rearmSelected();
    void refresh();

private:
    WatchList      m_list;
    const NameMap *m_names = nullptr;

    QueryLineEdit *m_expr    = nullptr;
    QLineEdit     *m_label   = nullptr;
    QPushButton   *m_addBtn  = nullptr;
    class QToolButton *m_readyBtn = nullptr;
    QMenu         *m_readyMenu = nullptr;
    void           fillReadyMenu();
    int            indexOfExpr(const QString &expr) const;
    QPushButton   *m_delBtn  = nullptr;
    QPushButton   *m_armBtn  = nullptr;
    QTableWidget  *m_table   = nullptr;
    StatusLine    *m_status  = nullptr;
    QTimer        *m_tick    = nullptr;
    class UndoLog *m_undo = nullptr;
};

#endif  // WATCHPANEL_H
