#include "stationkavachpanel.h"
#include "stationinterlock.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSpinBox>
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
    auto *intro = new QLabel(tr("The station's live state: the next LSRP is answered with it. The aspects are "
                                "set here as the interlocking would show them; what does not agree with a "
                                "route is said under Answers, not corrected. A track circuit a loco's "
                                "train covers (by its last ARP or LSRP) is occupied by it."), this);
    intro->setWordWrap(true);   // session 442: the panel keeps a sensible width
    lay->addWidget(intro);
    // Session 442: the station's ways of working, a tab of their own (Options).
    auto *optsPage = new QWidget(this);
    auto *opts = new QVBoxLayout(optsPage);
    m_interlocking = new QCheckBox(tr("Interlocking: the station works its signals from the track circuits"), this);
    m_interlocking->setObjectName(QStringLiteral("skInterlocking"));
    m_interlocking->setToolTip(tr("A route set locks its points; a train in it puts its signal back to Red; the route is\n"
                                  "released when its track circuits clear. Off: the aspects are the operator's alone."));
    connect(m_interlocking, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_state || m_state->interlocking == on) return;
        m_state->interlocking = on;
        emit stateChanged();
    });
    opts->addWidget(m_interlocking);
    // Session 445: SoS from the station.
    auto *sosRow = new QHBoxLayout;
    m_general = new QCheckBox(tr("Station General SoS"), this);
    m_general->setObjectName(QStringLiteral("skGeneralSos"));
    m_general->setToolTip(tr("Every loco answered gets DEST_LOCO_SOS 8 (Station General SoS) until unticked"));
    connect(m_general, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_state) return;
        if (on) m_state->sos.insert(0, 8); else m_state->sos.remove(0);
        showSos();
        emit stateChanged();
    });
    m_sosLoco = new QSpinBox(this);
    m_sosLoco->setObjectName(QStringLiteral("skSosLoco"));
    m_sosLoco->setRange(1, 1048575);   // SOURCE_LOCO_ID: 20 bits
    m_sosLoco->setPrefix(tr("loco "));
    m_sosKind = new QComboBox(this);
    m_sosKind->setObjectName(QStringLiteral("skSosKind"));
    m_sosKind->addItem(tr("Foreign RFID (1)"), 1);
    m_sosKind->addItem(tr("Head-on (6)"), 6);
    m_sosKind->addItem(tr("Shunt violation (7)"), 7);
    m_sosKind->addItem(tr("Station General (8)"), 8);
    auto *sendSos = new QPushButton(tr("Send SoS"), this);
    sendSos->setObjectName(QStringLiteral("skSosSend"));
    sendSos->setToolTip(tr("That loco's answers carry this DEST_LOCO_SOS until cleared"));
    auto *clearSos = new QPushButton(tr("Clear"), this);
    clearSos->setObjectName(QStringLiteral("skSosClear"));
    connect(sendSos, &QPushButton::clicked, this, [this]() {
        if (!m_state) return;
        m_state->sos.insert(m_sosLoco->value(), m_sosKind->currentData().toInt());
        showSos();
        emit stateChanged();
    });
    connect(clearSos, &QPushButton::clicked, this, [this]() {
        if (!m_state) return;
        m_state->sos.remove(m_sosLoco->value());
        showSos();
        emit stateChanged();
    });
    m_sosNow = new QLabel(this);
    m_sosNow->setObjectName(QStringLiteral("skSosNow"));
    sosRow->addWidget(m_general);
    sosRow->addSpacing(12);
    sosRow->addWidget(m_sosLoco);
    sosRow->addWidget(m_sosKind);
    sosRow->addWidget(sendSos);
    sosRow->addWidget(clearSos);
    sosRow->addWidget(m_sosNow, 1);
    lay->addLayout(sosRow);
    auto *split = new QSplitter(Qt::Vertical, this);
    auto *tabs = new QTabWidget(split);
    m_signals = table({ tr("Signal"), tr("Foot tag"), tr("Aspect") }, QStringLiteral("skSignals"), tabs);
    m_points = table({ tr("Point"), tr("Lies") }, QStringLiteral("skPoints"), tabs);
    m_occupied = table({ tr("Track circuit / TIN"), tr("Occupied"), tr("By a loco") }, QStringLiteral("skOccupied"), tabs);
    m_routes = table({ tr("Route"), tr("Section"), tr("Length"), tr("Authority"), tr("Set") }, QStringLiteral("skRoutes"), tabs);
    m_tsr = table({ tr("Route"), tr("TSR id"), tr("On") }, QStringLiteral("skTsr"), tabs);
    tabs->addTab(m_signals, tr("Signals"));
    tabs->addTab(m_points, tr("Points"));
    tabs->addTab(m_occupied, tr("Occupied"));
    tabs->addTab(m_routes, tr("Routes"));
    tabs->addTab(m_tsr, tr("TSR"));
    opts->addStretch(1);
    tabs->addTab(optsPage, tr("Options"));
    m_answers = new QPlainTextEdit(split);
    m_answers->setObjectName(QStringLiteral("skAnswers"));
    m_answers->setReadOnly(true);
    m_answers->setPlaceholderText(tr("Answers: the last SLRP sent to each loco, and why."));
    m_events = new QPlainTextEdit(split);
    m_events->setObjectName(QStringLiteral("skEvents"));
    m_events->setReadOnly(true);
    m_events->setMaximumBlockCount(200);
    m_events->setPlaceholderText(tr("Interlocking: signals put back, routes released, as the locos move."));
    split->addWidget(tabs);
    split->addWidget(m_answers);
    split->addWidget(m_events);
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
    {
        const QSignalBlocker block(m_interlocking);
        m_interlocking->setChecked(st->interlocking);
        const QSignalBlocker blockGeneral(m_general);
        m_general->setChecked(st->sos.value(0) == 8);
    }
    showSos();

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
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, st, c, name = points[i]](int ix) {
            // Session 442: a point a set route holds is not thrown.
            const QString why = st->interlocking ? StationInterlock::pointLocked(m_station, *st, name) : QString();
            if (!why.isEmpty()) {
                const QSignalBlocker block(c);
                c->setCurrentIndex(st->reverse.contains(name) ? 1 : 0);
                emit refused(tr("Not thrown: %1").arg(why));
                return;
            }
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
        QString set;   // session 442
        if (st->routes.value(r.entry, -1) == i) set = st->entered.contains(r.entry) ? tr("set, a train in it") : tr("set");
        text(m_routes, i, 4, set);
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

void StationKavachPanel::showSos()
{
    if (!m_state) return;
    QList<int> ids = m_state->sos.keys();
    std::sort(ids.begin(), ids.end());
    QStringList parts;
    for (int id : ids)
        if (id) parts << tr("loco %1: %2").arg(id).arg(m_state->sos.value(id));
    m_sosNow->setText(parts.isEmpty() ? QString() : tr("SoS sent: %1").arg(parts.join(QStringLiteral(", "))));
}

void StationKavachPanel::showInterlocking(const QStringList &events)
{
    for (const QString &e : events) m_events->appendPlainText(e);
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
