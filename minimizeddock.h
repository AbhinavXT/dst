#ifndef MINIMIZEDDOCK_H
#define MINIMIZEDDOCK_H
// =============================================================================
//  MinimizedDock
//  -----------------------------------------------------------------------------
//  Named chips, in DLConsole's status bar, for tool windows that have been
//  minimised: "Live Loco Console", "Firmware Flasher", ... Click a chip to
//  bring its window back; its ✕ closes the window.
//
//  THE PROBLEM IT SOLVES
//    Tool windows are owned by the main window (so they close with it and
//    stay above it). Minimised, Windows draws an owned window as a small
//    caption stub -- icon, restore, close, and no room for the title. With
//    two minimised there was no telling which was which.
//
//  HOW
//    One application-wide event filter. When a tool window is minimised, it
//    is hidden instead (so Windows draws no stub) and a chip with its title
//    and icon is added here. The chip remembers whether the window was
//    maximised, and restores it that way. If the window is brought back
//    some other way -- its menu item reopens it, say -- its chip goes; if
//    it is destroyed, its chip goes; if its title changes, its chip follows.
//
//    "Tool window": a real window (Qt::Window) with a parent. The main
//    window, dialogs, message boxes, menus and tooltips are not affected.
//    Every tool window, current or future, is covered without asking.
// =============================================================================
#include <QHash>
#include <QPointer>
#include <QWidget>

class QHBoxLayout;
class QToolButton;

class MinimizedDock : public QWidget
{
    Q_OBJECT
public:
    explicit MinimizedDock(QWidget *parent = nullptr);

    // Is this a tool window whose minimising the dock takes over?
    static bool isToolWindow(const QWidget *window);

    // ---- for the tests ------------------------------------------------------
    int         chipCount() const { return m_chips.size(); }
    QStringList chipTitles() const;
    void        restoreChip(int index);   // as a click on the chip
    void        closeChip(int index);     // as a click on its ✕

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Chip {
        QPointer<QWidget> window;
        QWidget          *frame = nullptr;
        QToolButton      *button = nullptr;
        Qt::WindowStates  restoreState = Qt::WindowNoState;
    };

    void dock(QWidget *window, Qt::WindowStates previous);
    void undock(QWidget *window);          // remove the chip only
    void restore(QWidget *window);         // remove the chip and show the window
    void updateChipText(QWidget *window);
    void restyle();
    int  indexOf(const QWidget *window) const;

    QHBoxLayout  *m_layout = nullptr;
    QVector<Chip> m_chips;
};

#endif // MINIMIZEDDOCK_H
