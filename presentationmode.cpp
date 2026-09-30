#include "presentationmode.h"

#include "textzoom.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

namespace {

// Every action under a menu bar that has a shortcut, submenus included.
void collectShortcutActions(const QList<QAction *> &actions, QList<QAction *> *out)
{
    for (QAction *action : actions) {
        if (action->menu() != nullptr) {
            collectShortcutActions(action->menu()->actions(), out);
        } else if (!action->shortcut().isEmpty()) {
            out->append(action);
        }
    }
}

// Esc is left to a field the operator is typing in (the find bar's Esc
// closes the find bar, not presentation mode).
bool isTextInput(const QWidget *widget)
{
    if (widget == nullptr) {
        return false;
    }
    if (qobject_cast<const QLineEdit *>(widget) || qobject_cast<const QTextEdit *>(widget)
        || qobject_cast<const QPlainTextEdit *>(widget) || qobject_cast<const QAbstractSpinBox *>(widget)) {
        return true;
    }
    const auto *combo = qobject_cast<const QComboBox *>(widget);
    return combo != nullptr && combo->isEditable();
}

}  // namespace

PresentationMode::PresentationMode(QObject *parent)
    : QObject(parent)
{
    if (qApp != nullptr) {
        qApp->installEventFilter(this);
    }
}

PresentationMode *PresentationMode::instance()
{
    static PresentationMode *mode = new PresentationMode(qApp);
    return mode;
}

int PresentationMode::indexOf(const QWidget *window) const
{
    for (int index = 0; index < m_saved.size(); ++index) {
        if (m_saved.at(index).window == window) {
            return index;
        }
    }
    return -1;
}

bool PresentationMode::isPresenting(const QWidget *window) const
{
    return indexOf(window) >= 0;
}

int PresentationMode::presentingCount() const
{
    int count = 0;
    for (const Saved &saved : m_saved) {
        if (saved.window) {
            ++count;
        }
    }
    return count;
}

void PresentationMode::toggle(QWidget *window)
{
    if (window == nullptr) {
        return;
    }
    window = window->window();
    if (isPresenting(window)) {
        leave(window);
    } else {
        enter(window);
    }
}

void PresentationMode::enter(QWidget *window)
{
    if (window == nullptr || isPresenting(window)) {
        return;
    }
    Saved saved;
    saved.window = window;
    saved.state = window->windowState();

    if (auto *mainWindow = qobject_cast<QMainWindow *>(window)) {
        QWidget *menu = mainWindow->menuWidget();
        if (menu != nullptr && menu->isVisible()) {
            if (auto *menuBar = qobject_cast<QMenuBar *>(menu)) {
                QList<QAction *> shortcuts;
                collectShortcutActions(menuBar->actions(), &shortcuts);
                for (QAction *action : shortcuts) {
                    if (!window->actions().contains(action)) {
                        window->addAction(action);
                        saved.borrowed.append(action);
                    }
                }
            }
            menu->hide();
            saved.hidden.append(menu);
        }
        // findChild, not statusBar(): statusBar() would CREATE one.
        if (auto *status = mainWindow->findChild<QStatusBar *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (status->isVisible()) {
                status->hide();
                saved.hidden.append(status);
            }
        }
        for (QToolBar *bar : mainWindow->findChildren<QToolBar *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (bar->isVisible()) {
                bar->hide();
                saved.hidden.append(bar);
            }
        }
    }

    // A widget's destroyed() fires before QPointers to it are cleared, so
    // match the entry by address, not by a null pointer.
    QObject *address = window;
    connect(window, &QObject::destroyed, this, [this, address]() {
        for (int index = m_saved.size() - 1; index >= 0; --index) {
            if (m_saved.at(index).window.data() == address || !m_saved.at(index).window) {
                m_saved.removeAt(index);
            }
        }
        updateBoost();
    });
    m_saved.append(saved);
    window->showFullScreen();
    updateBoost();
}

void PresentationMode::leave(QWidget *window)
{
    const int index = indexOf(window);
    if (index < 0) {
        return;
    }
    const Saved saved = m_saved.takeAt(index);
    for (const QPointer<QWidget> &bar : saved.hidden) {
        if (bar) {
            bar->show();
        }
    }
    for (QAction *action : saved.borrowed) {
        window->removeAction(action);
    }
    // Back to exactly the state it had: normal or maximised.
    window->setWindowState(saved.state & ~Qt::WindowFullScreen);
    window->show();
    updateBoost();
}

void PresentationMode::updateBoost()
{
    // Forget windows destroyed while presenting.
    for (int index = m_saved.size() - 1; index >= 0; --index) {
        if (!m_saved.at(index).window) {
            m_saved.removeAt(index);
        }
    }
    if (m_saved.isEmpty()) {
        TextZoom::setBoost(0);
    } else {
        TextZoom::setBoost(1);
    }
}

bool PresentationMode::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && !m_saved.isEmpty()) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
            QWidget *active = QApplication::activeWindow();
            if (active != nullptr && isPresenting(active) && !isTextInput(QApplication::focusWidget())) {
                leave(active);
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}
