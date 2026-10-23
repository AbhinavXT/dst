#include "missionpicker.h"

#include "missionreport.h"
#include "uistyle.h"

#include <QDateTime>
#include <QMenu>

MissionPicker::MissionPicker(QWidget *parent)
    : QToolButton(parent)
{
    setObjectName(QStringLiteral("missionPicker"));
    setPopupMode(QToolButton::InstantPopup);
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    setToolTip(tr("Show one mission: every tab filtered to its span"));
    setMenu(new QMenu(this));
    connect(menu(), &QMenu::aboutToShow, this, &MissionPicker::rebuildMenu);
    setCurrent(-1);
}

void MissionPicker::setCurrent(int index)
{
    m_current = index;
    setText(index < 0 ? tr("All missions") : tr("Mission %1").arg(index));
    setAccessibleName(text());
}

QString MissionPicker::itemText(int index, qint64 fromMs, qint64 toMs, const QString &highestMode)
{
    const QString name = index == 0 ? tr("Before the first start of mission") : tr("Mission %1").arg(index);
    QStringList parts{ name,
                       QStringLiteral("%1–%2").arg(QDateTime::fromMSecsSinceEpoch(fromMs).toString(QStringLiteral("HH:mm")),
                                                   QDateTime::fromMSecsSinceEpoch(toMs).toString(QStringLiteral("HH:mm"))) };
    if (!highestMode.isEmpty()) parts << Missions::modeName(highestMode);
    parts << UiStyle::durationText(toMs - fromMs);
    return parts.join(QStringLiteral(" · "));
}

void MissionPicker::rebuildMenu()
{
    QMenu *m = menu();
    m->clear();
    QAction *all = m->addAction(tr("All missions"));
    all->setCheckable(true);
    all->setChecked(m_current < 0);
    connect(all, &QAction::triggered, this, [this]() { setCurrent(-1); emit allChosen(); });
    m->addSeparator();

    const LogModel *model = m_source ? m_source() : nullptr;
    const QVector<Missions::Mission> missions = model ? Missions::split(model) : QVector<Missions::Mission>();
    if (missions.isEmpty()) {
        m->addAction(tr("(no start of mission in this tab)"))->setEnabled(false);
        return;
    }
    for (const Missions::Mission &ms : missions) {
        QAction *a = m->addAction(itemText(ms.index, ms.fromMs, ms.toMs, ms.highestMode));
        a->setCheckable(true);
        a->setChecked(ms.index == m_current);
        const int idx = ms.index;
        const qint64 from = ms.fromMs, to = ms.toMs;
        connect(a, &QAction::triggered, this, [this, idx, from, to]() {
            setCurrent(idx);
            emit missionChosen(idx, from, to);
        });
    }
}
