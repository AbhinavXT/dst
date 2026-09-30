#ifndef TABPOPOUTWINDOW_H
#define TABPOPOUTWINDOW_H
// =============================================================================
//  TabPopoutWindow -- a live copy of one log tab in its own window.
//
//  Replaces DetachConsoleDialog. Same idea -- the window shares the tab's
//  LogModel, so the two can never disagree and nothing is duplicated; the
//  tab stays in the main window -- but now:
//    - a proper tool window (owned by the main window), so it closes with
//      it and minimises to a named chip in the status bar;
//    - rows follow the Row density setting and the text size, and the
//      hidden columns and column widths match the tabs;
//    - the tab's colour tag and label show on the window (icon, title,
//      header), and follow any change;
//    - "Follow latest" keeps the newest row in view; "Show in main window"
//      brings the main window forward on this tab;
//    - its place and size are remembered per loco, and MainWindow reopens
//      it on the next start once that loco is heard from.
//  Opened from the tab's right-click menu, the Detach button, or by
//  dragging the tab out of the tab bar.
// =============================================================================
#include <QMainWindow>

class LogModel;
class MessageDispatcher;
class QCheckBox;
class QLabel;
class QTableView;
class LogEntry;

class TabPopoutWindow : public QMainWindow
{
    Q_OBJECT
public:
    // `tags`: MainWindow's (session 96); nullptr = a private one.
    TabPopoutWindow(const QString &key, const QString &friendlyName, LogModel *model,
                    MessageDispatcher *dispatcher, QWidget *parent, class TabTags *tags = nullptr);

    QString key() const { return m_key; }
    QTableView *view() const { return m_view; }

    void setFollowLatest(bool on);
    bool followLatest() const;

    // Where its geometry is remembered.
    static QString geometryKey(const QString &key);

signals:
    void showInMainRequested(const QString &key);
    // The operator closed it (not: the application is shutting down).
    void closedByUser(const QString &key);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onEntryAppended(QString tabKey, QSharedPointer<LogEntry> entry);

private:
    void refreshTag();

    QString     m_key;
    QString     m_friendlyName;
    QTableView *m_view = nullptr;
    QLabel     *m_dot = nullptr;
    QLabel     *m_title = nullptr;
    QCheckBox  *m_follow = nullptr;
    class TabTags *m_tags = nullptr;       // not owned (unless made here)
};

#endif // TABPOPOUTWINDOW_H
