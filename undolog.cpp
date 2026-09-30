#include "undolog.h"

#include <QAction>
#include <QKeySequence>
#include <QWidget>

UndoLog::UndoLog(QObject *parent)
    : QObject(parent)
{
}

void UndoLog::push(const QString &label, std::function<bool()> undo, qint64 heldRows)
{
    if (!undo) {
        return;
    }
    Step step;
    step.label    = label;
    step.undo     = std::move(undo);
    step.heldRows = qMax<qint64>(0, heldRows);
    m_steps.append(step);
    trim();
    emit changed();
    emit pushed(label);
}

QString UndoLog::nextLabel() const
{
    if (m_steps.isEmpty()) {
        return QString();
    }
    return m_steps.last().label;
}

qint64 UndoLog::heldRows() const
{
    qint64 total = 0;
    for (const Step &step : m_steps) {
        total += step.heldRows;
    }
    return total;
}

bool UndoLog::undo(QString *label)
{
    if (m_steps.isEmpty()) {
        if (label) {
            label->clear();
        }
        return false;
    }
    // Taken off before it runs: a closure that itself pushes (it should
    // not, but) must not find its own step still on top.
    const Step step = m_steps.takeLast();
    if (label) {
        *label = step.label;
    }
    const bool restored = step.undo();
    emit changed();
    return restored;
}

void UndoLog::clear()
{
    if (m_steps.isEmpty()) {
        return;
    }
    m_steps.clear();
    emit changed();
}

void UndoLog::trim()
{
    // Oldest first. The newest step always survives, however big: refusing
    // to undo the thing just done because it was large would be the worst
    // possible moment to run out of room.
    while (m_steps.size() > kMaxSteps) {
        m_steps.removeFirst();
    }
    while (m_steps.size() > 1 && heldRows() > kMaxHeldRows) {
        m_steps.removeFirst();
    }
}

QAction *UndoLog::createAction(QWidget *window)
{
    auto *action = new QAction(tr("&Undo"), window);
    action->setShortcut(QKeySequence::Undo);
    // Window scope: a line edit with focus still gets its own Ctrl+Z,
    // because QLineEdit claims the key through ShortcutOverride first.
    action->setShortcutContext(Qt::WindowShortcut);
    auto refresh = [this, action]() {
        if (canUndo()) {
            action->setText(tr("&Undo %1").arg(nextLabel()));
            action->setEnabled(true);
        } else {
            action->setText(tr("&Undo"));
            action->setEnabled(false);
        }
    };
    refresh();
    connect(this, &UndoLog::changed, action, refresh);
    connect(action, &QAction::triggered, this, [this]() {
        QString label;
        const bool restored = undo(&label);
        if (!label.isEmpty()) {
            emit undone(label, restored);
        }
    });
    return action;
}
