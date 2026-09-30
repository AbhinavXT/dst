#ifndef PRESENTATIONMODE_H
#define PRESENTATIONMODE_H
// =============================================================================
//  PresentationMode -- F11: full screen, chrome hidden, text one step larger.
//
//  Works on whichever window is active: the main window or any tool window
//  (Live Loco Console, Flasher, ...). For a QMainWindow the menu bar, status
//  bar and toolbars are hidden; F11 again -- or Esc, unless the cursor is in
//  a text field -- puts every one of them back as it was, and the window
//  back in the state it had (normal or maximised).
//
//  While ANY window is presenting, the text is one TextZoom step larger (a
//  boost, not saved: the operator's chosen size is untouched).
//
//  Keyboard shortcuts keep working with the menu bar hidden: a hidden
//  QMenuBar can stop its actions' shortcuts from firing, so while hidden,
//  every action with a shortcut is also added to the window itself, and
//  removed again on the way out.
// =============================================================================
#include <QList>
#include <QObject>
#include <QPointer>
#include <QVector>

class QAction;
class QWidget;

class PresentationMode : public QObject
{
    Q_OBJECT
public:
    static PresentationMode *instance();

    // Enter or leave presentation for this top-level window.
    void toggle(QWidget *window);
    void enter(QWidget *window);
    void leave(QWidget *window);

    bool isPresenting(const QWidget *window) const;
    int  presentingCount() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit PresentationMode(QObject *parent);

    struct Saved {
        QPointer<QWidget>          window;
        Qt::WindowStates           state = Qt::WindowNoState;
        QList<QPointer<QWidget>>   hidden;      // bars we hid (only those that were visible)
        QList<QAction *>           borrowed;    // shortcuts added to the window while menus are hidden
    };

    int  indexOf(const QWidget *window) const;
    void updateBoost();

    QVector<Saved> m_saved;
};

#endif // PRESENTATIONMODE_H
