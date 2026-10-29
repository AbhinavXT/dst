#include "minimizeddock.h"

#include "uicolors.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
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
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);   // session 413: never squeezed
    m_more = new QToolButton(this);
    m_more->setObjectName(QStringLiteral("minimizedMore"));
    m_more->setPopupMode(QToolButton::InstantPopup);
    m_more->setAutoRaise(true);
    m_more->setMenu(new QMenu(m_more));
    m_more->hide();
    m_layout->addWidget(m_more);
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
    if (event->type() == QEvent::Resize && window == this->window() && m_budget < 0 && !m_chips.isEmpty()) {
        QTimer::singleShot(0, this, [this]() { relayout(); });   // session 413: the row's room changed
    }

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
        // Session 412: its state was left minimised while it was docked, so
        // it came back as a minimised window that Maximize would not open.
        // It gets the state it had before, as from its chip (deferred: not
        // inside its own Show).
        const Qt::WindowStates state = m_chips.at(indexOf(window)).restoreState;
        undock(window);
        if (window->windowState() & Qt::WindowMinimized) {
            QPointer<QWidget> guarded(window);
            QTimer::singleShot(0, this, [guarded, state]() {
                if (!guarded || !(guarded->windowState() & Qt::WindowMinimized)) return;
                guarded->setWindowState(state);
                guarded->show();
                guarded->raise();
                guarded->activateWindow();
            });
        }
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
    chip.frame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
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
        relayout();
    });

    m_chips.append(chip);
    m_layout->insertWidget(m_layout->indexOf(m_more), chip.frame);
    updateChipText(window);
    restyle();
    show();
    relayout();

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
    m_chips.at(index).frame->hide();
    m_chips.removeAt(index);
    setVisible(!m_chips.isEmpty());
    relayout();
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
    button->setText(button->fontMetrics().elidedText(title, Qt::ElideMiddle, kTitleWidthPx));
    button->setToolTip(tr("Restore “%1”").arg(title));
    relayout();
}

int MinimizedDock::budget() const
{
    if (m_budget >= 0) return m_budget;
    const QWidget *main = window();
    return main && main != this ? qMax(240, main->width() / 2) : 600;
}

// Session 413: chips in order while they fit in the row's width at their own
// size; the rest behind "+N more", a menu that brings each back.
void MinimizedDock::relayout()
{
    const int room = budget();
    const int gap = m_layout->spacing();
    QMenu *menu = m_more->menu();
    menu->clear();
    int used = m_layout->contentsMargins().left() + m_layout->contentsMargins().right();
    int hidden = 0;
    for (int i = 0; i < m_chips.size(); ++i) {
        const Chip &chip = m_chips.at(i);
        const int w = chip.frame->sizeHint().width() + gap;
        const bool last = i == m_chips.size() - 1;
        // Room for the "+N more" button too, unless this is the last chip.
        m_more->setText(tr("+%1 more").arg(m_chips.size() - i));
        const int moreW = last ? 0 : m_more->sizeHint().width() + gap;
        // The first always: a row of none and "+N more" says nothing (session 416).
        const bool fits = hidden == 0 && (i == 0 || used + w + moreW <= room);
        chip.frame->setVisible(fits);
        if (fits) {
            used += w;
            continue;
        }
        ++hidden;
        QPointer<QWidget> guarded(chip.window);
        if (guarded)
            menu->addAction(chip.window->windowIcon(), chip.window->windowTitle(), this, [this, guarded]() {
                if (guarded) restore(guarded);
            });
    }
    m_more->setText(tr("+%1 more").arg(hidden));
    m_more->setToolTip(tr("%n more minimised window(s): pick one to bring it back", nullptr, hidden));
    m_more->setVisible(hidden > 0);
    updateGeometry();
}

void MinimizedDock::setWidthBudget(int px)
{
    m_budget = px;
    relayout();
}

int MinimizedDock::shownChipCount() const
{
    int n = 0;
    for (const Chip &chip : m_chips) n += chip.frame->isVisibleTo(const_cast<MinimizedDock *>(this)) ? 1 : 0;
    return n;
}

QList<QWidget *> MinimizedDock::shownChips() const
{
    QList<QWidget *> out;
    for (const Chip &chip : m_chips)
        if (chip.frame->isVisibleTo(const_cast<MinimizedDock *>(this))) out << chip.frame;
    return out;
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
