#ifndef RFIDWORKSPACEWINDOW_H
#define RFIDWORKSPACEWINDOW_H
// =============================================================================
//  RfidWorkspaceWindow (session 417) — Tools ▸ RFID Workspace…: the Station
//  Layout, a tag editor and the Loco Simulator in one window, in step.
//  -----------------------------------------------------------------------------
//  Left, the station's drawing and tables (a StationLayoutWindow) above the
//  Loco Simulator; right, the selected tag's fields (the Tag Builder's tag
//  editor, session 401).
//
//  IN STEP
//    - A tag picked in the drawing or the Tags table: its fields in the
//      editor, and its row in the simulator's Tags table when the route has it.
//    - A tag picked in the simulator's Tags table: selected in the layout,
//      its fields in the editor.
//    - Apply in the editor: the tag into the layout (one Undo step there);
//      a line the simulator runs from the layout (Simulator ▸ Line … in the
//      layout) follows every edit, as it does with the windows apart (411).
//
//  THE SIMULATOR is the main window's own (there is one: its serial ports,
//  its recorder): it is moved into the workspace while the workspace is
//  open and handed back, as its own window again, when it closes. Tools ▸
//  Simulators ▸ Loco Simulator… raises the workspace meanwhile.
//
//  Each pane scrolls when the window is smaller than it wants, so the three
//  fit a laptop; the splitters share the room.
// =============================================================================

#include <QPointer>
#include <QWidget>

#include <functional>

class LocoSimWindow;
class QLabel;
class QScrollArea;
class QSplitter;
class QVBoxLayout;
class StationLayoutWindow;
class TagEditDialog;

class RfidWorkspaceWindow : public QWidget
{
    Q_OBJECT
public:
    explicit RfidWorkspaceWindow(QWidget *parent = nullptr);
    ~RfidWorkspaceWindow() override;

    StationLayoutWindow *layoutWindow() const { return m_layout; }
    // The simulator to hold: `take` gives MainWindow's, made if need be.
    void setSimulatorSource(const std::function<LocoSimWindow *()> &take) { m_takeSim = take; }
    LocoSimWindow *simulator() const;
    bool holdsSimulator() const { return !m_sim.isNull(); }
    LocoSimWindow *takeSimulator();     // into the bottom pane (once)
    void giveBackSimulator();           // its own window again, hidden

    // The editor: the tag it shows ("" none), and Apply (tests).
    QString editedTag() const { return m_editedTag; }
    TagEditDialog *editor() const;
    bool applyEditor();
    void showTag(const QString &name);  // as a pick in the layout does

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void onLayoutTag(const QString &name);
    void onSimTag(const QString &name);
    void rebuildEditor();
    void refreshTitle();

    StationLayoutWindow *m_layout = nullptr;
    QPointer<LocoSimWindow> m_sim;
    Qt::WindowFlags m_simFlags;
    QScrollArea *m_simScroll = nullptr;
    QLabel *m_simPlaceholder = nullptr;
    QSplitter *m_left = nullptr;
    QWidget *m_editorHost = nullptr;
    QVBoxLayout *m_editorLayout = nullptr;
    QLabel *m_editorHead = nullptr;
    QPointer<TagEditDialog> m_editor;
    QString m_editedTag;
    bool m_syncing = false;
    std::function<LocoSimWindow *()> m_takeSim;
};

#endif // RFIDWORKSPACEWINDOW_H
