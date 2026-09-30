#include "stickymenu.h"

#include <QKeyEvent>
#include <QMouseEvent>

StickyMenu::StickyMenu(const QString &title, QWidget *parent)
    : QMenu(title, parent)
{
}

void StickyMenu::mouseReleaseEvent(QMouseEvent *event)
{
    QAction *action = activeAction();

    // Only intercept checkable, enabled items. Anything else — commands,
    // separators, submenus, disabled entries — behaves exactly as a normal
    // menu, so nothing that used to work changes.
    if (action && action->isEnabled() && action->isCheckable()
        && !action->menu()) {
        action->trigger();          // toggles and emits, without closing
        return;
    }
    QMenu::mouseReleaseEvent(event);
}

void StickyMenu::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter) {
        QAction *action = activeAction();
        if (action && action->isEnabled() && action->isCheckable()
            && !action->menu()) {
            action->trigger();
            return;
        }
    }
    QMenu::keyPressEvent(event);
}
