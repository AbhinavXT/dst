#include "rowclicks.h"

#include "settings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QTableView>
#include <QTimer>

namespace RowClicks {

QString gestureKey(Gesture g)
{
    switch (g) {
    case Double:      return QStringLiteral("double");
    case Triple:      return QStringLiteral("triple");
    case CtrlDouble:  return QStringLiteral("ctrl_double");
    case ShiftDouble: return QStringLiteral("shift_double");
    case Middle:      return QStringLiteral("middle");
    case GestureCount: break;
    }
    return QString();
}

QString gestureLabel(Gesture g)
{
    switch (g) {
    case Double:      return QCoreApplication::translate("RowClicks", "Double-click");
    case Triple:      return QCoreApplication::translate("RowClicks", "Triple-click");
    case CtrlDouble:  return QCoreApplication::translate("RowClicks", "Ctrl + double-click");
    case ShiftDouble: return QCoreApplication::translate("RowClicks", "Shift + double-click");
    case Middle:      return QCoreApplication::translate("RowClicks", "Middle-click");
    case GestureCount: break;
    }
    return QString();
}

QVector<Action> actions()
{
    auto t = [](const char *s) { return QCoreApplication::translate("RowClicks", s); };
    return QVector<Action>{
        { QString(),                        t("Nothing") },
        { QStringLiteral("workbench"),      t("Open in Decode Workbench") },
        { QStringLiteral("packetMaker"),    t("Open in Packet Maker") },
        { QStringLiteral("bookmark"),       t("Add / remove bookmark") },
        { QStringLiteral("note"),           t("Add / edit note…") },
        { QStringLiteral("dmi"),            t("Show on DMI (an @dmi row)") },
        { QStringLiteral("dmiClip"),        t("DMI clip around here…") },
        { QStringLiteral("radioChart"),     t("Radio chart around here…") },
        { QStringLiteral("diff"),           t("Diff the selected rows") },
        { QStringLiteral("copy"),           t("Copy message") },
        { QStringLiteral("copyRow"),        t("Copy row (tab-separated)") },
        { QStringLiteral("copyHeader"),     t("Copy rows with header") },
        { QStringLiteral("why"),            t("Why is this row coloured?") },
        { QStringLiteral("filterSource"),   t("Filter to this source") },
        { QStringLiteral("incidentReport"), t("Incident report around here… (.dlr viewer)") },
    };
}

QString actionLabel(const QString &id)
{
    for (const Action &a : actions())
        if (a.id == id) return a.label;
    return id;
}

QString binding(Gesture g)
{
    return Settings::rowClickAction(gestureKey(g));
}

void setBinding(Gesture g, const QString &id)
{
    Settings::setRowClickAction(gestureKey(g), id);
}

}  // namespace RowClicks

RowClickFilter *RowClickFilter::install(QTableView *view, Runner run)
{
    auto *f = new RowClickFilter(view, std::move(run));
    view->viewport()->installEventFilter(f);
    return f;
}

RowClickFilter::RowClickFilter(QTableView *view, Runner run)
    : QObject(view), m_view(view), m_run(std::move(run))
{
    m_wait = new QTimer(this);
    m_wait->setSingleShot(true);
    connect(m_wait, &QTimer::timeout, this, [this]() {
        const QModelIndex at = m_waiting;
        m_waiting = QPersistentModelIndex();
        m_doubleMs = 0;   // the double-click has acted: a press now starts afresh
        fire(RowClicks::Double, at);
    });
}

RowClicks::Gesture RowClickFilter::gestureOf(int button, Qt::KeyboardModifiers mods, bool triple)
{
    if (button == Qt::MiddleButton) return RowClicks::Middle;
    if (mods & Qt::ControlModifier) return RowClicks::CtrlDouble;
    if (mods & Qt::ShiftModifier) return RowClicks::ShiftDouble;
    return triple ? RowClicks::Triple : RowClicks::Double;
}

bool RowClickFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_view || watched != m_view->viewport()) return QObject::eventFilter(watched, event);
    const QEvent::Type type = event->type();
    if (type != QEvent::MouseButtonDblClick && type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease)
        return QObject::eventFilter(watched, event);

    auto *me = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint pos = me->position().toPoint();
#else
    const QPoint pos = me->pos();
#endif
    const QModelIndex index = m_view->indexAt(pos);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (me->button() == Qt::MiddleButton) {
        if (type == QEvent::MouseButtonPress) {
            m_middle = index;
            return index.isValid() && !RowClicks::binding(RowClicks::Middle).isEmpty();
        }
        if (type == QEvent::MouseButtonRelease) {
            const bool same = index.isValid() && m_middle.isValid() && index.row() == m_middle.row();
            m_middle = QPersistentModelIndex();
            return same && fire(RowClicks::Middle, index);
        }
        return false;
    }
    if (me->button() != Qt::LeftButton) return false;

    // A third press soon after a plain double-click, where it was: triple.
    if (type == QEvent::MouseButtonPress && m_doubleMs > 0) {
        const bool soon = now - m_doubleMs <= QApplication::doubleClickInterval();
        const bool near = (pos - m_doubleAt).manhattanLength() <= 2 * QApplication::startDragDistance();
        m_doubleMs = 0;
        if (soon && near && me->modifiers() == Qt::NoModifier && index.isValid()) {
            m_wait->stop();
            m_waiting = QPersistentModelIndex();
            return fire(RowClicks::Triple, index);
        }
        if (m_wait->isActive()) {   // something else clicked: the double-click stands
            m_wait->stop();
            fire(RowClicks::Double, m_waiting);
            m_waiting = QPersistentModelIndex();
        }
        return false;
    }
    if (type != QEvent::MouseButtonDblClick || !index.isValid()) return false;

    const RowClicks::Gesture g = gestureOf(Qt::LeftButton, me->modifiers(), false);
    if (g != RowClicks::Double) return fire(g, index);
    m_doubleAt = pos;
    m_doubleMs = now;
    if (RowClicks::binding(RowClicks::Double).isEmpty()) return false;
    if (!RowClicks::binding(RowClicks::Triple).isEmpty()) {
        m_waiting = index;
        m_wait->start(QApplication::doubleClickInterval());
        return true;
    }
    return fire(RowClicks::Double, index);
}

bool RowClickFilter::fire(RowClicks::Gesture g, const QModelIndex &index)
{
    const QString id = RowClicks::binding(g);
    if (id.isEmpty() || !index.isValid() || !m_view) return false;
    // The row clicked is the one the action is for: current, and selected
    // unless it is already part of the selection (a span stays a span).
    QItemSelectionModel *sm = m_view->selectionModel();
    if (sm) {
        if (sm->isSelected(index))
            sm->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        else
            sm->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    // After the mouse event: the action may open a dialog of its own.
    QPointer<QTableView> view(m_view);
    const QPersistentModelIndex at(index);
    Runner run = m_run;
    QTimer::singleShot(0, this, [view, at, id, run]() {
        if (view && at.isValid()) run(view, at, id);
    });
    return true;
}
