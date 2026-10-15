#include "dialogfocus.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QHash>
#include <QPointer>
#include <QTimer>

namespace {

// The second try, for a platform that activates the main window only after
// the dialog has gone (macOS's panels).
constexpr int kLateRetryMs = 150;

void giveBack(const QPointer<QWidget> &target, bool onlyIfLost)
{
    if (!target || !target->isVisible() || target->isMinimized()) return;
    if (QApplication::activeModalWidget() != nullptr) return;
    if (onlyIfLost && QApplication::activeWindow() == target) return;
    target->raise();
    target->activateWindow();
}

class Filter : public QObject
{
public:
    using QObject::QObject;

    bool eventFilter(QObject *obj, QEvent *event) override
    {
        const QEvent::Type t = event->type();
        // Spontaneous ones are the window system's: a dialog minimised or
        // restored, not opened or closed.
        if ((t != QEvent::Show && t != QEvent::Hide) || event->spontaneous()) return false;
        auto *dialog = qobject_cast<QDialog *>(obj);
        if (dialog == nullptr || !dialog->isWindow()) return false;
        if (t == QEvent::Show) {
            m_returnTo.insert(dialog, DialogFocus::returnTarget(dialog));
            return false;
        }
        const QPointer<QWidget> target = m_returnTo.take(dialog);
        if (!target) return false;
        QTimer::singleShot(0, target, [target]() { giveBack(target, false); });
        QTimer::singleShot(kLateRetryMs, target, [target]() { giveBack(target, true); });
        return false;
    }

private:
    QHash<const QObject *, QPointer<QWidget>> m_returnTo;
};

}  // namespace

namespace DialogFocus {

QWidget *returnTarget(const QWidget *dialog)
{
    if (dialog == nullptr) return nullptr;
    QWidget *target = dialog->parentWidget() ? dialog->parentWidget()->window() : QApplication::activeWindow();
    return target == dialog ? nullptr : target;
}

void install()
{
    static Filter *filter = nullptr;
    if (filter != nullptr || qApp == nullptr) return;
    filter = new Filter(qApp);
    qApp->installEventFilter(filter);
}

}  // namespace DialogFocus
