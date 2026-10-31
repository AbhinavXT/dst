#include "stationkavachpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QTableWidget *table(const QStringList &headers, const QString &name, QWidget *parent)
{
    auto *t = new QTableWidget(0, headers.size(), parent);
    t->setObjectName(name);
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

void text(QTableWidget *t, int row, int col, const QString &s) { t->setItem(row, col, new QTableWidgetItem(s)); }

}  // namespace

StationKavachPanel::StationKavachPanel(const QVector<QPair<qint64, QString>> &aspects, QWidget *parent)
    : QDialog(parent), m_aspects(aspects)
{
    setWindowTitle(tr("Station"));
    setObjectName(QStringLiteral("stationKavachPanel"));
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("The station's live state: the next LSRP is answered with it. The aspects are "
                                 "set here as the interlocking would show them; what does not agree with a "
                                 "route is said under Answers, not corrected. A track circuit a loco's "
                                 "train covers (by its last ARP or LSRP) is occupied by it."), this));
    auto *split = new QSplitter(Qt::Vertical, this);
    auto *tabs = new QTabWidget(split);
    m_signals = table({ tr("Signal"), tr("Foot tag"), tr("Aspect") }, QStringLiteral("skSignals"), tabs);
    m_points = table({ tr("Point"), tr("Lies") }, QStringLiteral("skPoints"), tabs);
    m_occupied = table({ tr("Track circuit / TIN"), tr("Occupied"), tr("By a loco") }, QStringLiteral("skOccupied"), tabs);
    m_routes = table({ tr("Route"), tr("Section"), tr("Length"), tr("Authority") }, QStringLiteral("skRoutes"), tabs);
    m_tsr = table({ tr("Route"), tr("TSR id"), tr("On") }, QStringLiteral("skTsr"), tabs);
    tabs->addTab(m_signals, tr("Signals"));
    tabs->addTab(m_points, tr("Points"));
    tabs->addTab(m_occupied, tr("Occupied"));
    tabs->addTab(m_routes, tr("Routes"));
    tabs->addTab(m_tsr, tr("TSR"));
    m_answers = new QPlainTextEdit(split);
    m_answers->setObjectName(QStringLiteral("skAnswers"));
    m_answers->setReadOnly(true);
    m_answers->setPlaceholderText(tr("Answers: the last SLRP sent to each loco, and why."));
    split->addWidget(tabs);
    split->addWidget(m_answers);
    lay->addWidget(split, 1);
    m_drawing = new QPushButton(tr("Show on the drawing"), this);
    m_drawing->setObjectName(QStringLiteral("skDrawing"));
    m_drawing->setToolTip(tr("The station's layout in Station Layout, live: the signals at these aspects, each\n"
                             "loco where its last LSRP put it, and the MA it was last sent."));
    connect(m_drawing, &QPushButton::clicked, this, &StationKavachPanel::drawingRequested);
    lay->addWidget(m_drawing, 0, Qt::AlignRight);
    resize(640, 520);
}

void StationKavachPanel::setStation(const StationKavach::Station &s, StationKavach::State *st)
{
    m_station = s;
    m_state = st;
    m_lastAnswer.clear();
    m_answers->clear();
    rebuild();
}

void StationKavachPanel::rebuild()
{
    if (!m_state) return;
    StationKavach::State *st = m_state;

    m_signals->setRowCount(m_station.signalList.size());
    for (int i = 0; i < m_station.signalList.size(); ++i) {
        const StationKavach::Signal &g = m_station.signalList[i];
        text(m_signals, i, 0, g.name);
        text(m_signals, i, 1, QString::number(g.footTag));
        auto *c = new QComboBox(m_signals);
        const int now = st->aspectOf(m_station, g.name);
        for (const auto &a : m_aspects) c->addItem(a.second, a.first);
        if (c->findData(now) < 0) c->addItem(QString::number(now), now);
        c->setCurrentIndex(c->findData(now));
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, st, c, name = g.name]() {
            st->aspect.insert(name, int(c->currentData().toLongLong()));
            emit stateChanged();
        });
        m_signals->setCellWidget(i, 2, c);
    }

    const QStringList points = m_station.pointNames();
    m_points->setRowCount(points.size());
    for (int i = 0; i < points.size(); ++i) {
        text(m_points, i, 0, points[i]);
        auto *c = new QComboBox(m_points);
        c->addItems({ tr("Normal"), tr("Reverse") });
        c->setCurrentIndex(st->reverse.contains(points[i]) ? 1 : 0);
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, st, name = points[i]](int ix) {
            if (ix == 1) st->reverse.insert(name); else st->reverse.remove(name);
            emit stateChanged();
        });
        m_points->setCellWidget(i, 1, c);
    }

    // Session 441: and every circuit a loco occupies (where its ARP / LSRP puts it).
    QStringList occ = m_station.trackCircuitNames() + m_station.tinNames();
    QStringList byLoco = st->occupiedBy.keys();
    std::sort(byLoco.begin(), byLoco.end());
    for (const QString &c : byLoco) if (!occ.contains(c)) occ << c;
    m_occupied->setRowCount(occ.size());
    for (int i = 0; i < occ.size(); ++i) {
        text(m_occupied, i, 0, occ[i]);
        auto *b = new QCheckBox(m_occupied);
        b->setChecked(st->occupied.contains(occ[i]));
        connect(b, &QCheckBox::toggled, this, [this, st, name = occ[i]](bool on) {
            if (on) st->occupied.insert(name); else st->occupied.remove(name);
            emit stateChanged();
        });
        m_occupied->setCellWidget(i, 1, b);
        text(m_occupied, i, 2, st->occupiedBy.contains(occ[i]) ? tr("loco %1").arg(st->occupiedBy.value(occ[i])) : QString());
    }

    m_routes->setRowCount(m_station.routes.size());
    int tsrRows = 0;
    for (const StationKavach::Route &r : m_station.routes) tsrRows += r.tsr.size();
    m_tsr->setRowCount(tsrRows);
    for (int i = 0, t = 0; i < m_station.routes.size(); ++i) {
        const StationKavach::Route &r = m_station.routes[i];
        static const char *const kSec[] = { "Station", "Absolute Block", "Autoblock", "Reserved" };
        text(m_routes, i, 0, r.name);
        text(m_routes, i, 1, tr(kSec[r.sectionType & 3]));
        text(m_routes, i, 2, tr("%1 m").arg(r.length()));
        auto *c = new QComboBox(m_routes);
        c->addItem(tr("OS"), 1);
        c->addItem(tr("FS"), 2);
        c->addItem(tr("SR"), 3);
        const int auth = st->authority.value(r.name, r.authority);
        c->setCurrentIndex(std::max(0, c->findData(auth)));
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, st, c, name = r.name]() {
            st->authority.insert(name, c->currentData().toInt());
            emit stateChanged();
        });
        m_routes->setCellWidget(i, 3, c);
        for (const StationKavach::Row &x : r.tsr) {
            const QString key = QStringLiteral("%1/%2").arg(r.name).arg(x.value(QStringLiteral("id")));
            text(m_tsr, t, 0, r.name);
            text(m_tsr, t, 1, QString::number(x.value(QStringLiteral("id"))));
            auto *b = new QCheckBox(m_tsr);
            b->setChecked(!st->tsrOff.contains(key));
            connect(b, &QCheckBox::toggled, this, [this, st, key](bool on) {
                if (on) st->tsrOff.remove(key); else st->tsrOff.insert(key);
                emit stateChanged();
            });
            m_tsr->setCellWidget(t++, 2, b);
        }
    }
}

void StationKavachPanel::showAnswer(int locoId, const QString &answerText, const QStringList &checks)
{
    QString s = tr("Loco %1: %2").arg(locoId).arg(answerText);
    for (const QString &c : checks) s += QStringLiteral("\n    ") + tr("does not agree: %1").arg(c);
    m_lastAnswer.insert(locoId, s);
    QList<int> ids = m_lastAnswer.keys();
    std::sort(ids.begin(), ids.end());
    QStringList all;
    for (int id : ids) all << m_lastAnswer.value(id);
    m_answers->setPlainText(all.join(QStringLiteral("\n\n")));
}
