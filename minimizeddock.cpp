#include "minimizeddock.h"

#include "uicolors.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QTimer>
#include <QToolButton>
#include <QWindowStateChangeEvent>

namespace {

// A chip's title is cut to this width (with "…"); the full title is in the
// tooltip. Wide enough for "Live Loco Console" and "Loco Configuration".
const int kTitleWidthPx = 200;

}  // namespace

MinimizedDock::MinimizedDock(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 6, 0);
    m_layout->setSpacing(4);
    hide();   // shown only while it holds a chip

    if (qApp != nullptr) {
        qApp->installEventFilter(this);   // removed automatically when this is destroyed
    }
    UiColor::onThemeChange(this, [this]() { restyle(); });
}

bool MinimizedDock::isToolWindow(const QWidget *window)
{
    if (window == nullptr || !window->isWindow()) {
        return false;
    }
    // Exactly Qt::Window: dialogs, popups, tooltips and tool palettes are
    // other window types and keep their normal behaviour.
    if (window->windowType() != Qt::Window) {
        return false;
    }
    // No parent: the main window itself, which minimises to the taskbar.
    return window->parentWidget() != nullptr;
}

bool MinimizedDock::eventFilter(QObject *watched, QEvent *event)
{
    if (!watched->isWidgetType()) {
        return QWidget::eventFilter(watched, event);
    }
    auto *window = static_cast<QWidget *>(watched);

    if (event->type() == QEvent::WindowStateChange && isToolWindow(window)
        && (window->windowState() & Qt::WindowMinimized) && indexOf(window) < 0) {
        // Minimised: take it over. Deferred, because hiding a window from
        // inside its own state-change event confuses the platform window.
        const Qt::WindowStates previous = static_cast<QWindowStateChangeEvent *>(event)->oldState();
        QPointer<QWidget> guarded(window);
        QTimer::singleShot(0, this, [this, guarded, previous]() {
            if (guarded) {
                dock(guarded, previous);
            }
        });
    } else if (event->type() == QEvent::Show && indexOf(window) >= 0 && window->isVisible()) {
        // Brought back some other way (its menu item reopened it): the chip
        // has nothing left to stand for. isVisible() matters: Qt also sends
        // Show to a HIDDEN window whose state changes, without showing it.
        undock(window);
    }
    return QWidget::eventFilter(watched, event);
}

int MinimizedDock::indexOf(const QWidget *window) const
{
    for (int index = 0; index < m_chips.size(); ++index) {
        if (m_chips.at(index).window == window) {
            return index;
        }
    }
    return -1;
}

void MinimizedDock::dock(QWidget *window, Qt::WindowStates previous)
{
    if (!(window->windowState() & Qt::WindowMinimized) || indexOf(window) >= 0) {
        return;   // restored again before we got here
    }

    Chip chip;
    chip.window = window;
    chip.restoreState = previous & ~Qt::WindowMinimized;

    chip.frame = new QFrame(this);
    chip.frame->setObjectName(QStringLiteral("minimizedChip"));
    auto *chipLayout = new QHBoxLayout(chip.frame);
    chipLayout->setContentsMargins(4, 0, 2, 0);
    chipLayout->setSpacing(0);

    chip.button = new QToolButton(chip.frame);
    chip.button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    chip.button->setIcon(window->windowIcon());
    chip.button->setAutoRaise(true);
    chipLayout->addWidget(chip.button);

    auto *closeButton = new QToolButton(chip.frame);
    closeButton->setText(QStringLiteral("✕"));
    closeButton->setAutoRaise(true);
    closeButton->setToolTip(tr("Close this window"));
    chipLayout->addWidget(closeButton);

    QPointer<QWidget> guarded(window);
    connect(chip.button, &QToolButton::clicked, this, [this, guarded]() {
        if (guarded) {
            restore(guarded);
        }
    });
    connect(closeButton, &QToolButton::clicked, this, [this, guarded]() {
        if (guarded) {
            closeChip(indexOf(guarded));
        }
    });
    connect(window, &QWidget::windowTitleChanged, this, [this, guarded]() {
        if (guarded) {
            updateChipText(guarded);
        }
    });
    connect(window, &QObject::destroyed, this, [this]() {
        // The QPointer is already null: drop every chip whose window is gone.
        for (int index = m_chips.size() - 1; index >= 0; --index) {
            if (!m_chips.at(index).window) {
                m_chips.at(index).frame->deleteLater();
                m_chips.removeAt(index);
            }
        }
        setVisible(!m_chips.isEmpty());
    });

    m_chips.append(chip);
    m_layout->addWidget(chip.frame);
    updateChipText(window);
    restyle();
    show();

    // Hide instead of minimised, so Windows draws no nameless stub. Its
    // state is left alone while it is docked -- changing a hidden window's
    // state makes Qt send it stray Show/Hide events -- and set on restore.
    window->hide();
}

void MinimizedDock::undock(QWidget *window)
{
    const int index = indexOf(window);
    if (index < 0) {
        return;
    }
    m_chips.at(index).frame->deleteLater();
    m_chips.removeAt(index);
    setVisible(!m_chips.isEmpty());
}

void MinimizedDock::restore(QWidget *window)
{
    const int index = indexOf(window);
    if (index < 0) {
        return;
    }
    const Qt::WindowStates state = m_chips.at(index).restoreState;
    undock(window);
    window->setWindowState(state);
    window->show();
    window->raise();
    window->activateWindow();
}

void MinimizedDock::updateChipText(QWidget *window)
{
    const int index = indexOf(window);
    if (index < 0) {
        return;
    }
    QToolButton *button = m_chips.at(index).button;
    const QString title = window->windowTitle();
    button->setText(button->fontMetrics().elidedText(title, Qt::ElideRight, kTitleWidthPx));
    button->setToolTip(tr("Restore “%1”").arg(title));
}

void MinimizedDock::restyle()
{
    // A pill in the alternate-row colour with the frame colour around it:
    // distinct from the status text beside it, quiet otherwise.
    const QPalette pal = palette();
    const QString sheet =
        QStringLiteral("QFrame#minimizedChip { background-color:%1; border:1px solid %2; border-radius:10px; }")
            .arg(pal.color(QPalette::AlternateBase).name(), UiColor::frame().name(QColor::HexArgb));
    for (const Chip &chip : m_chips) {
        chip.frame->setStyleSheet(sheet);
    }
}

QStringList MinimizedDock::chipTitles() const
{
    QStringList titles;
    for (const Chip &chip : m_chips) {
        if (chip.window) {
            titles.append(chip.window->windowTitle());
        }
    }
    return titles;
}

void MinimizedDock::restoreChip(int index)
{
    if (index >= 0 && index < m_chips.size() && m_chips.at(index).window) {
        restore(m_chips.at(index).window);
    }
}

void MinimizedDock::closeChip(int index)
{
    if (index < 0 || index >= m_chips.size() || !m_chips.at(index).window) {
        return;
    }
    QPointer<QWidget> window = m_chips.at(index).window;
    // close() can be refused (the Flasher refuses mid-flash, after asking).
    // Refused: the window stays docked. Accepted: the chip goes -- at once
    // if the window lives on hidden, or via destroyed() if it deletes itself.
    if (window->close() && window) {
        undock(window);
    }
}
