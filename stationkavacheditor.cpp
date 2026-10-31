#include "stationkavacheditor.h"

#include "stationlayout.h"
#include "uicolors.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <functional>

using StationKavach::Route;
using StationKavach::Row;
using StationKavach::Station;

namespace {

// The SLRP sub-packet (struct, repeat) each profile list is made into, as
// stationkavach.cpp builds it; "class" of a speed row is worked out there.
const char *const kListStruct[] = { "", "StaticSpeedProfile", "GradientProfile", "LCgateProfile", "TurnoutSpeed",
                                    "TrackCondition", "TSR" };
const char *const kListRepeat[] = { "", "speed", "grad", "lc", "to", "tc", "tsr" };
const char *const kListTitle[] = { "Tags", "SSP", "Gradient", "LC gates", "Turnouts", "Track conditions", "TSR" };

enum RouteCol { RName, REntry, RExit, RDir, RSection, RAuth, ROsSpeed, RPointsN, RPointsR, RTrack, RTins,
                RDupDist, RAdjTins, RTin, RLocReset, RNextStn, RLength, RouteCols };

QTableWidget *makeTable(const QStringList &headers, const QString &name, QWidget *parent)
{
    auto *t = new QTableWidget(0, headers.size(), parent);
    t->setObjectName(name);
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    return t;
}

QTableWidgetItem *item(const QString &s) { return new QTableWidgetItem(s); }
QTableWidgetItem *readOnly(const QString &s)
{
    auto *i = new QTableWidgetItem(s);
    i->setFlags(i->flags() & ~Qt::ItemIsEditable);
    return i;
}

QString cell(const QTableWidget *t, int r, int c) { return t->item(r, c) ? t->item(r, c)->text().trimmed() : QString(); }

// A whole number, or false. Empty is 0 when `emptyOk`.
bool number(const QString &s, qint64 *v, bool emptyOk = true)
{
    if (s.isEmpty()) { *v = 0; return emptyOk; }
    bool ok = false;
    *v = s.toLongLong(&ok);
    return ok;
}

QStringList names(const QString &s)
{
    QStringList out;
    for (const QString &x : s.split(QLatin1Char(','))) if (!x.trimmed().isEmpty()) out << x.trimmed();
    return out;
}

QString join(const QStringList &l) { return l.join(QStringLiteral(", ")); }

QPushButton *button(const QString &text, const QString &name, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(name);
    return b;
}

// A table with a row of buttons under it.
QWidget *withButtons(QWidget *table, const QVector<QPushButton *> &buttons, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(table, 1);
    auto *h = new QHBoxLayout;
    for (QPushButton *b : buttons) h->addWidget(b);
    h->addStretch(1);
    v->addLayout(h);
    return w;
}

QList<int> selectedRows(const QTableWidget *t)
{
    QList<int> rows;
    for (const QModelIndex &i : t->selectionModel()->selectedRows()) rows << i.row();
    if (rows.isEmpty() && t->currentRow() >= 0) rows << t->currentRow();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    return rows;
}

}  // namespace

StationKavachEditor::StationKavachEditor(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("stationKavachEditor"));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    m_stack = new QStackedWidget(this);
    lay->addWidget(m_stack);

    // Page 0: no KAVACH data yet.
    auto *empty = new QWidget(m_stack);
    auto *ev = new QVBoxLayout(empty);
    auto *el = new QLabel(tr("No KAVACH data in this layout. With it, Packet Maker ▸ Send log can answer a loco as "
                             "this station would (docs/STATION_KAVACH.md): its signals, and its routes as the rows "
                             "of a Table of Control."), empty);
    el->setWordWrap(true);
    m_start = button(tr("Start KAVACH data from this layout"), QStringLiteral("skeStart"), empty);
    m_start->setToolTip(tr("The layout's station id and its signals (at Red); the routes are added here"));
    ev->addWidget(el);
    auto *eh = new QHBoxLayout;
    eh->addWidget(m_start);
    // Session 460: or straight to the routes the layout gives.
    auto *proposeEmpty = button(tr("Propose routes from the layout…"), QStringLiteral("skeProposeEmpty"), empty);
    proposeEmpty->setToolTip(tr("Each signal's routes to the next signal facing its way, through the points: its tags, "
                                "linking distances, points, track circuits and TINs. Tick the ones to add"));
    eh->addWidget(proposeEmpty);
    connect(proposeEmpty, &QPushButton::clicked, this, [this]() { if (QDialog *d = proposeDialog()) { d->exec(); delete d; } });
    eh->addStretch(1);
    ev->addLayout(eh);
    ev->addStretch(1);
    m_stack->addWidget(empty);
    connect(m_start, &QPushButton::clicked, this, [this]() {
        static const StationLayout::Layout none;
        emit edited(seedFrom(m_seedFrom ? *m_seedFrom : none), tr("start KAVACH data"));
    });

    // Page 1: the editor.
    auto *page = new QWidget(m_stack);
    auto *pv = new QVBoxLayout(page);
    pv->setContentsMargins(0, 0, 0, 0);
    auto *top = new QHBoxLayout;
    m_stationId = new QSpinBox(page);
    m_stationId->setObjectName(QStringLiteral("skeStationId"));
    m_stationId->setRange(0, 65535);
    m_stnVersion = new QSpinBox(page);
    m_stnVersion->setObjectName(QStringLiteral("skeStnVersion"));
    m_stnVersion->setRange(0, 7);
    m_sections = new QLineEdit(page);
    m_sections->setObjectName(QStringLiteral("skeSections"));
    m_sections->setPlaceholderText(tr("Annexure-I: Red 1, Yellow 2, Double Yellow 3, Green 4"));
    m_sections->setToolTip(tr("MA sections by aspect code, where they differ from Annexure-I's, e.g. \"11=3, 10=2\" "
                              "(Green 3, Double Yellow 2). Empty: Red, calling-on and stop board 1; every Yellow 2; "
                              "Double Yellow 3; Green and AG 4"));
    auto *remove = button(tr("Remove KAVACH data"), QStringLiteral("skeRemove"), page);
    top->addWidget(new QLabel(tr("Station id"), page));
    top->addWidget(m_stationId);
    top->addWidget(new QLabel(tr("STN version"), page));
    top->addWidget(m_stnVersion);
    top->addWidget(new QLabel(tr("MA sections by aspect"), page));
    top->addWidget(m_sections, 1);
    top->addWidget(remove);
    pv->addLayout(top);
    connect(remove, &QPushButton::clicked, this, [this]() { emit edited(QJsonObject(), tr("remove KAVACH data")); });
    connect(m_stationId, &QSpinBox::editingFinished, this, &StationKavachEditor::onTopEdited);
    connect(m_stnVersion, &QSpinBox::editingFinished, this, &StationKavachEditor::onTopEdited);
    connect(m_sections, &QLineEdit::editingFinished, this, &StationKavachEditor::onTopEdited);

    // Signals beside the routes, the selected route's tables under both.
    auto *split = new QSplitter(Qt::Vertical, page);
    auto *upper = new QSplitter(Qt::Horizontal, split);
    m_signals = makeTable({ tr("Signal"), tr("Foot tag"), tr("Signal info"), tr("Aspect at start") },
                          QStringLiteral("skeSignals"), upper);
    m_signals->horizontalHeader()->setStretchLastSection(false);
    m_signals->horizontalHeaderItem(2)->setToolTip(tr("CUR_SIG_INFO (17 bits), sent when it is the signal ahead"));
    m_signals->horizontalHeaderItem(3)->setToolTip(tr("The aspect the file starts with; Packet Maker ▸ Station… "
                                                      "changes it while answering"));
    auto *addSig = button(tr("Add signal"), QStringLiteral("skeAddSignal"), upper);
    auto *delSig = button(tr("Delete"), QStringLiteral("skeDeleteSignals"), upper);
    upper->addWidget(withButtons(m_signals, { addSig, delSig }, upper));
    connect(addSig, &QPushButton::clicked, this, &StationKavachEditor::addSignal);
    connect(delSig, &QPushButton::clicked, this, &StationKavachEditor::deleteSignals);
    connect(m_signals, &QTableWidget::cellChanged, this, [this]() { if (!m_filling) onSignalsEdited(); });

    m_routes = makeTable({ tr("Route"), tr("Entry"), tr("Exit"), tr("Direction"), tr("Section"), tr("Authority"),
                           tr("OS speed"), tr("Points normal"), tr("Points reverse"), tr("Track circuits"), tr("TINs"),
                           tr("Dup tag dist"), tr("Adjacent TINs"), tr("TIN"), tr("Loc reset"), tr("Next station"), tr("Length") },
                         QStringLiteral("skeRoutes"), upper);
    const QString list = tr("names separated by commas");
    m_routes->horizontalHeaderItem(RName)->setToolTip(tr("One row of the Table of Control: entry signal → exit signal"));
    m_routes->horizontalHeaderItem(REntry)->setToolTip(tr("The entry signal's name (Signals above)"));
    m_routes->horizontalHeaderItem(RExit)->setToolTip(tr("The exit signal's name: the signal ahead"));
    m_routes->horizontalHeaderItem(ROsSpeed)->setToolTip(tr("AUTHORIZED_SPEED under OS authority (calling-on)"));
    for (int c : { RPointsN, RPointsR, RTrack, RTins })
        m_routes->horizontalHeaderItem(c)->setToolTip(tr("What the route needs (the live state is checked against it): %1").arg(list));
    m_routes->horizontalHeaderItem(RDupDist)->setToolTip(tr("TagLinking DIST_DUP_TAG, m"));
    m_routes->horizontalHeaderItem(RAdjTins)->setToolTip(tr("TagLinking adjacent line TINs: numbers separated by commas"));
    m_routes->horizontalHeaderItem(RLocReset)->setToolTip(tr("Absolute location reset: \"start, direction, correction\" "
                                                             "(start from the entry signal, m); empty for none"));
    m_routes->horizontalHeaderItem(RNextStn)->setToolTip(tr("Session 447: the next station's KAVACH id (APPR_STN_ILC_IBS_ID, 1-65535) when the "
                                                            "loco is handed over to it on this route (NEXT_STN_COMM 1 while the MA "
                                                            "reaches it); empty for none"));
    m_routes->horizontalHeaderItem(RLength)->setToolTip(tr("Entry foot tag to exit foot tag: the tags' linking distances added"));
    auto *addRoute = button(tr("Add route"), QStringLiteral("skeAddRoute"), upper);
    auto *copyRoute = button(tr("Copy route"), QStringLiteral("skeCopyRoute"), upper);
    copyRoute->setToolTip(tr("A copy of the selected route, its tags and profile: a start for a loop or a parallel route"));
    auto *delRoute = button(tr("Delete"), QStringLiteral("skeDeleteRoutes"), upper);
    // Session 460: the routes the layout gives, ticked and added.
    m_propose = button(tr("Propose…"), QStringLiteral("skePropose"), upper);
    m_propose->setToolTip(tr("Routes from the layout: from each signal along its line and over the points to the next "
                             "signal facing its way; tags, linking distances, points, track circuits and TINs. Tick the ones to add"));
    connect(m_propose, &QPushButton::clicked, this, [this]() { if (QDialog *d = proposeDialog()) { d->exec(); delete d; } });
    upper->addWidget(withButtons(m_routes, { addRoute, copyRoute, m_propose, delRoute }, upper));
    upper->setStretchFactor(0, 3);
    upper->setStretchFactor(1, 5);
    split->addWidget(upper);
    connect(addRoute, &QPushButton::clicked, this, [this]() { this->addRoute(false); });
    connect(copyRoute, &QPushButton::clicked, this, [this]() { this->addRoute(true); });
    connect(delRoute, &QPushButton::clicked, this, &StationKavachEditor::deleteRoutes);
    connect(m_routes, &QTableWidget::cellChanged, this, [this]() { if (!m_filling) onRoutesEdited(); });
    connect(m_routes, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (m_filling || row == m_route) return;
        m_route = row;
        fillRoute();
    });

    auto *detail = new QWidget(split);
    auto *dv = new QVBoxLayout(detail);
    dv->setContentsMargins(0, 0, 0, 0);
    m_routeLabel = new QLabel(detail);
    m_routeLabel->setObjectName(QStringLiteral("skeRouteLabel"));
    dv->addWidget(m_routeLabel);
    m_listTabs = new QTabWidget(detail);
    m_listTabs->setUsesScrollButtons(true);
    for (int l = 0; l < ListCount; ++l) {
        m_listCols[l] = listColumns(List(l));
        QStringList headers = m_listCols[l];
        if (l == Tags) headers = QStringList{ tr("From previous (m)"), tr("Tag"), tr("Duplicate dir"), tr("From entry (m)") };
        m_lists[l] = makeTable(headers, QStringLiteral("skeList%1").arg(l), m_listTabs);
        m_lists[l]->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_listTabs->addTab(m_lists[l], tr(kListTitle[l]));
        connect(m_lists[l], &QTableWidget::cellChanged, this, [this, l]() { if (!m_filling) onListEdited(List(l)); });
    }
    m_lists[Tags]->horizontalHeaderItem(0)->setToolTip(tr("The linking distance from the tag before; the first tag "
                                                          "(the entry signal's foot tag) is at 0"));
    m_lists[Tags]->horizontalHeaderItem(2)->setToolTip(tr("TagLinking dup_dir of this tag"));
    // The profile tables' headers: the schema's field, its width and its codes.
    for (int l = Ssp; l < ListCount; ++l) {
        const Schema::StructLayout sl = m_builder.encoder().structLayout(QLatin1String(kListStruct[l]));
        for (const Schema::RepeatInfo &ri : sl.repeats) {
            if (ri.name != QLatin1String(kListRepeat[l])) continue;
            m_lists[l]->setToolTip(tr("%1 %2 rows: up to %3. Distances from the route's entry signal; an empty cell "
                                      "is a field not given").arg(QLatin1String(kListStruct[l]), ri.name).arg(ri.cmax));
            for (const Schema::FieldInfo &f : ri.fields) {
                const int c = m_listCols[l].indexOf(f.name);
                if (c < 0) continue;
                QString tip = tr("%1: %2 bits").arg(f.name).arg(f.bits) + (f.unit.isEmpty() ? QString() : QStringLiteral(", ") + f.unit);
                if (!f.when.isEmpty()) tip += tr("; given when %1").arg(f.when);
                if (!f.enumName.isEmpty()) {
                    QStringList codes;
                    for (const auto &c2 : m_builder.encoder().enumChoices(f.enumName)) codes << c2.second;
                    if (!codes.isEmpty()) tip += QStringLiteral("\n") + codes.mid(0, 24).join(QStringLiteral("\n"))
                                                 + (codes.size() > 24 ? QStringLiteral("\n…") : QString());
                }
                m_lists[l]->horizontalHeaderItem(c)->setToolTip(tip);
            }
        }
    }
    m_lists[Ssp]->horizontalHeaderItem(0)->setToolTip(m_lists[Ssp]->horizontalHeaderItem(0)->toolTip()
                                                      + tr("\nSegment lengths: the rows add up to the route's length"));
    m_lists[Gradient]->horizontalHeaderItem(0)->setToolTip(m_lists[Gradient]->horizontalHeaderItem(0)->toolTip()
                                                           + tr("\nSegment lengths: the rows add up to the route's length"));
    auto *addRow = button(tr("Add row"), QStringLiteral("skeAddRow"), detail);
    auto *delRow = button(tr("Delete rows"), QStringLiteral("skeDeleteRows"), detail);
    // Session 425: the route's tags clicked on the drawing, in order.
    m_pick = button(tr("Pick on drawing"), QStringLiteral("skePick"), detail);
    m_pick->setCheckable(true);
    m_pick->setToolTip(tr("Click the route's tags on the drawing above, in the order the loco reads them: each is added "
                          "after the last, its distance from the layout's tags. Click the last one again to take it off; "
                          "the exit signal's foot tag, or Esc, ends it"));
    // Session 457: a TSR given from one OHE pole to another (the TSR route table's).
    m_poles = button(tr("TSR by OHE poles…"), QStringLiteral("skeTsrPoles"), detail);
    m_poles->setToolTip(tr("A TSR from one OHE pole to another of a TSR route (the station's TSR route table): "
                           "a TSR row on each route it covers, from where the poles lie"));
    dv->addWidget(withButtons(m_listTabs, { addRow, delRow, m_pick, m_poles }, detail), 1);
    connect(m_poles, &QPushButton::clicked, this, [this]() {
        if (m_station.tsrRoutes.isEmpty()) return;
        QDialog d(this);
        d.setWindowTitle(tr("TSR by OHE poles"));
        auto *f = new QFormLayout(&d);
        auto *route = new QComboBox(&d), *from = new QComboBox(&d), *to = new QComboBox(&d);
        for (const StationKavach::TsrRoute &t : m_station.tsrRoutes)
            route->addItem(tr("%1: %2 (%3)").arg(t.id).arg(t.entry, t.routes.join(QStringLiteral(", "))));
        auto fillPoles = [&]() {
            from->clear();
            to->clear();
            for (const StationKavach::OhePole &p : m_station.tsrRoutes.value(route->currentIndex()).poles) {
                const QString label = tr("%1 (%2 m)").arg(p.name).arg(p.dist, 0, 'f', 2);
                from->addItem(label, p.name);
                to->addItem(label, p.name);
            }
            to->setCurrentIndex(to->count() - 1);
        };
        fillPoles();
        connect(route, QOverload<int>::of(&QComboBox::currentIndexChanged), &d, fillPoles);
        auto *speed = new QSpinBox(&d), *id = new QSpinBox(&d), *whistle = new QSpinBox(&d);
        speed->setRange(5, 250);
        speed->setSingleStep(5);
        speed->setValue(30);
        speed->setSuffix(tr(" km/h"));
        id->setRange(0, 255);
        whistle->setRange(0, 3);
        f->addRow(tr("TSR route"), route);
        f->addRow(tr("From pole"), from);
        f->addRow(tr("To pole"), to);
        f->addRow(tr("Speed"), speed);
        f->addRow(tr("TSR id"), id);
        f->addRow(tr("Whistle"), whistle);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        f->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() == QDialog::Accepted)
            addTsrByPoles(route->currentIndex(), from->currentData().toString(), to->currentData().toString(),
                          speed->value(), id->value(), whistle->value());
    });
    connect(m_pick, &QPushButton::toggled, this, [this](bool on) {
        if (on) m_listTabs->setCurrentIndex(Tags);
        emit pickingChanged(on);
    });
    connect(addRow, &QPushButton::clicked, this, &StationKavachEditor::addListRow);
    connect(delRow, &QPushButton::clicked, this, &StationKavachEditor::deleteListRows);
    split->addWidget(detail);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    pv->addWidget(split, 1);
    m_stack->addWidget(page);
    m_stack->setCurrentIndex(0);
}

void StationKavachEditor::setPicking(bool on)
{
    if (m_pick->isChecked() != on) m_pick->setChecked(on);
}

bool StationKavachEditor::isPicking() const { return m_pick->isChecked(); }

bool StationKavachEditor::pickTag(int tag, qint64 loc)
{
    if (m_route < 0 || m_route >= m_station.routes.size()) {
        emit refused(tr("Select a route first: its tags are picked in order"));
        return false;
    }
    Station next = m_station;
    Route &r = next.routes[m_route];
    const int sign = r.pktDir == 2 ? -1 : 1;
    const int e = next.signalIndex(r.exit);
    const int exitFoot = e >= 0 ? next.signalList[e].footTag : -1;
    if (r.tags.isEmpty() || (r.tags.size() == 1 && r.tags[0].tag == 0)) {
        // No entry foot tag yet: the first tag picked is it.
        r.tags = { StationKavach::RouteTag{ 0, tag, 0 } };
        commit(next, tr("pick KAVACH route %1 tag %2").arg(r.name).arg(tag));
        emit said(tr("Route %1 starts at tag %2: pick the next").arg(r.name).arg(tag));
        return true;
    }
    if (r.tags.last().tag == tag) {
        if (r.tags.size() == 1) {
            emit refused(tr("Tag %1 is route %2's entry foot tag: it stays").arg(tag).arg(r.name));
            return false;
        }
        r.tags.removeLast();
        commit(next, tr("unpick KAVACH route %1 tag %2").arg(r.name).arg(tag));
        emit said(tr("Tag %1 taken off route %2 (%3 m)").arg(tag).arg(r.name).arg(r.length()));
        return true;
    }
    for (int i = 0; i < r.tags.size(); ++i)
        if (r.tags[i].tag == tag) {
            emit refused(tr("Tag %1 is already in route %2 (row %3)").arg(tag).arg(r.name).arg(i + 1));
            return false;
        }
    const int last = r.tags.last().tag;
    const QVector<qint64> from = m_tagLocs ? m_tagLocs(last) : QVector<qint64>();
    if (from.isEmpty()) {
        emit refused(tr("Tag %1, the route's last, is not in the layout's tags: type tag %2's distance in the Tags table")
                         .arg(last).arg(tag));
        return false;
    }
    // Of the last tag's places (a number listed more than once), the one nearest the tag clicked.
    qint64 near = from.first();
    for (qint64 x : from) if (std::llabs(loc - x) < std::llabs(loc - near)) near = x;
    const qint64 along = (loc - near) * sign;
    if (along <= 0) {
        emit refused(tr("Tag %1 does not lie on from tag %2 in route %3's direction (%4)")
                         .arg(tag).arg(last).arg(r.name, r.pktDir == 2 ? tr("Reverse: falling locations")
                                                                       : tr("Nominal: rising locations")));
        return false;
    }
    r.tags << StationKavach::RouteTag{ int(along), tag, 0 };
    const QString name = r.name;
    const int length = r.length();
    commit(next, tr("pick KAVACH route %1 tag %2").arg(name).arg(tag));
    if (tag == exitFoot) {
        setPicking(false);
        emit said(tr("Tag %1 is %2's foot tag: route %3 is %4 m, picking done").arg(tag).arg(next.routes[m_route].exit, name).arg(length));
    } else {
        emit said(tr("Tag %1 added to route %2, %3 m on from tag %4 (%5 m)").arg(tag).arg(name).arg(along).arg(last).arg(length));
    }
    return true;
}

QStringList StationKavachEditor::listColumns(List l) const
{
    if (l == Tags) return { QStringLiteral("dist"), QStringLiteral("tag"), QStringLiteral("dup_dir") };
    QStringList out;
    const Schema::StructLayout sl = m_builder.encoder().structLayout(QLatin1String(kListStruct[l]));
    for (const Schema::RepeatInfo &ri : sl.repeats)
        if (ri.name == QLatin1String(kListRepeat[l]))
            for (const Schema::FieldInfo &f : ri.fields)
                // A speed row's class follows from u or a, b, c (stationkavach.cpp).
                if (!f.isPad && !((l == Ssp || l == Tsr) && f.name == QLatin1String("class"))) out << f.name;
    return out;
}

QVector<Row> *StationKavachEditor::listOf(Route &r, List l) const
{
    switch (l) {
    case Ssp: return &r.ssp;
    case Gradient: return &r.gradient;
    case Lc: return &r.lc;
    case Turnouts: return &r.turnouts;
    case TrackConditions: return &r.trackConditions;
    case Tsr: return &r.tsr;
    default: return nullptr;
    }
}

QJsonObject StationKavachEditor::seedFrom(const StationLayout::Layout &l)
{
    Station s;
    s.stationId = l.stations.isEmpty() ? 0 : int(l.stations.first().id);
    for (const StationLayout::Signal &g : l.signalList) {
        if (g.name.trimmed().isEmpty()) continue;
        StationKavach::Signal k;
        k.name = g.name.trimmed();
        QString foot = g.footTag.trimmed();
        if (foot.endsWith(QLatin1Char('D'), Qt::CaseInsensitive)) foot.chop(1);
        k.footTag = foot.toInt();
        k.aspect = 1;
        s.signalList << k;
    }
    return StationKavach::toJson(s);
}

void StationKavachEditor::setKavach(const QJsonObject &kavach)
{
    m_has = !kavach.isEmpty();
    m_station = Station();
    if (m_has) StationKavach::fromJson(kavach, &m_station);   // the window's Checks say what it could not read
    m_stack->setCurrentIndex(m_has ? 1 : 0);
    fill();
}

int StationKavachEditor::currentRoute() const { return m_route; }

void StationKavachEditor::selectRoute(int i)
{
    m_route = i >= 0 && i < m_station.routes.size() ? i : -1;
    m_filling = true;
    if (m_route >= 0) m_routes->selectRow(m_route); else m_routes->clearSelection();
    m_filling = false;
    fillRoute();
}

QComboBox *StationKavachEditor::enumCell(QTableWidget *t, int row, int col, const QString &enumName, qint64 value,
                                         const std::function<void()> &onChange)
{
    auto *c = new QComboBox(t);
    for (const auto &ch : m_builder.encoder().enumChoices(enumName)) c->addItem(ch.second, ch.first);
    if (c->findData(value) < 0) c->addItem(QString::number(value), value);
    c->setCurrentIndex(c->findData(value));
    t->setCellWidget(row, col, c);
    connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, onChange]() {
        if (!m_filling) onChange();
    });
    return c;
}

void StationKavachEditor::fill()
{
    m_filling = true;
    m_stationId->setValue(m_station.stationId);
    m_stnVersion->setValue(m_station.stnVersion);
    QList<int> aspects = m_station.sections.keys();
    std::sort(aspects.begin(), aspects.end());
    QStringList sec;
    for (int a : aspects) sec << QStringLiteral("%1=%2").arg(a).arg(m_station.sections.value(a));
    m_sections->setText(join(sec));

    m_signals->setRowCount(m_station.signalList.size());
    for (int i = 0; i < m_station.signalList.size(); ++i) {
        const StationKavach::Signal &g = m_station.signalList[i];
        m_signals->setItem(i, 0, item(g.name));
        m_signals->setItem(i, 1, item(QString::number(g.footTag)));
        m_signals->setItem(i, 2, item(QString::number(g.sigInfo)));
        enumCell(m_signals, i, 3, QStringLiteral("sigAspect"), g.aspect, [this]() { onSignalsEdited(); });
    }
    m_signals->resizeColumnsToContents();

    m_routes->setRowCount(m_station.routes.size());
    for (int i = 0; i < m_station.routes.size(); ++i) {
        const Route &r = m_station.routes[i];
        auto changed = [this]() { onRoutesEdited(); };
        m_routes->setItem(i, RName, item(r.name));
        m_routes->setItem(i, REntry, item(r.entry));
        m_routes->setItem(i, RExit, item(r.exit));
        enumCell(m_routes, i, RDir, QStringLiteral("pktDir"), r.pktDir, changed);
        enumCell(m_routes, i, RSection, QStringLiteral("secType"), r.sectionType, changed);
        enumCell(m_routes, i, RAuth, QStringLiteral("authType"), r.authority, changed);
        enumCell(m_routes, i, ROsSpeed, QStringLiteral("speed6"), r.osSpeed, changed);
        m_routes->setItem(i, RPointsN, item(join(r.pointsNormal)));
        m_routes->setItem(i, RPointsR, item(join(r.pointsReverse)));
        m_routes->setItem(i, RTrack, item(join(r.trackCircuits)));
        m_routes->setItem(i, RTins, item(join(r.tins)));
        m_routes->setItem(i, RDupDist, item(QString::number(r.distDupTag)));
        QStringList adj;
        for (int t : r.adjTins) adj << QString::number(t);
        m_routes->setItem(i, RAdjTins, item(join(adj)));
        m_routes->setItem(i, RTin, item(QString::number(r.tin)));
        m_routes->setItem(i, RLocReset, item(r.locReset ? QStringLiteral("%1, %2, %3").arg(r.locResetStart)
                                                              .arg(r.adjLocoDir).arg(r.absLocCorrection)
                                                        : QString()));
        m_routes->setItem(i, RNextStn, item(r.nextStn ? QString::number(r.nextStn) : QString()));
        m_routes->setItem(i, RLength, readOnly(tr("%1 m").arg(r.length())));
    }
    m_routes->resizeColumnsToContents();
    if (m_route >= m_station.routes.size()) m_route = m_station.routes.size() - 1;
    if (m_route < 0 && !m_station.routes.isEmpty()) m_route = 0;
    if (m_route >= 0) m_routes->selectRow(m_route);
    m_filling = false;
    fillRoute();
}

void StationKavachEditor::fillRoute()
{
    m_filling = true;
    const bool any = m_route >= 0 && m_route < m_station.routes.size();
    m_listTabs->setEnabled(any);
    m_pick->setEnabled(any);
    m_poles->setEnabled(m_has && !m_station.tsrRoutes.isEmpty());
    m_poles->setVisible(!m_station.tsrRoutes.isEmpty());
    if (!any) setPicking(false);
    if (!any) {
        m_routeLabel->setText(tr("Select a route to edit its tags and track profile."));
        for (QTableWidget *t : m_lists) t->setRowCount(0);
        for (int l = 0; l < ListCount; ++l) m_listTabs->setTabText(l, tr(kListTitle[l]));
        m_filling = false;
        emit routeSelected(-1);
        return;
    }
    const Route &r = m_station.routes[m_route];
    // Session 424: the MA as the drawing shows it, the aspects as the file starts them.
    const StationKavach::Sections sx = StationKavach::sectionsFrom(m_station, StationKavach::State::initial(m_station), m_route);
    QString ma = tr("%1 %2: %3 → MA %4 m").arg(r.exit, StationKavach::aspectName(sx.aspect))
                     .arg(sx.routes.size() == 1 ? tr("1 section") : tr("%1 sections").arg(sx.routes.size())).arg(sx.maEnd);
    if (!sx.broken.isEmpty()) ma += tr(" (%1)").arg(sx.broken);
    else if (!sx.endsShort.isEmpty()) ma += tr(" (%1 wanted; no route on from %2)").arg(sx.wanted)
                                               .arg(m_station.routes[sx.routes.last()].exit);
    m_routeLabel->setText(tr("Route %1: %2 → %3, %4 m. %5").arg(r.name, r.entry, r.exit).arg(r.length()).arg(ma));
    m_routeLabel->setToolTip(tr("The MA from this route as the station would answer with the aspects at start "
                                "(Signals); on the drawing, this route and the sections beyond it"));

    QTableWidget *tags = m_lists[Tags];
    tags->setRowCount(r.tags.size());
    int at = 0;
    for (int i = 0; i < r.tags.size(); ++i) {
        if (i) at += r.tags[i].dist;
        tags->setItem(i, 0, item(QString::number(r.tags[i].dist)));
        tags->setItem(i, 1, item(QString::number(r.tags[i].tag)));
        tags->setItem(i, 2, item(QString::number(r.tags[i].dupDir)));
        tags->setItem(i, 3, readOnly(QString::number(at)));
    }
    m_listTabs->setTabText(Tags, tr("Tags (%1)").arg(r.tags.size()));

    for (int l = Ssp; l < ListCount; ++l) {
        Route copy = r;
        const QVector<Row> rows = *listOf(copy, List(l));
        // Keys a row holds that the schema's columns lack stay, as columns of their own.
        QStringList cols = m_listCols[l];
        for (const Row &x : rows)
            for (const QString &k : x.keys())
                if (!cols.contains(k) && !((l == Ssp || l == Tsr) && k == QLatin1String("class"))) cols << k;
        QTableWidget *t = m_lists[l];
        t->setColumnCount(cols.size());
        for (int c = m_listCols[l].size(); c < cols.size(); ++c) t->setHorizontalHeaderItem(c, new QTableWidgetItem(cols[c]));
        t->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i)
            for (int c = 0; c < cols.size(); ++c)
                t->setItem(i, c, item(rows[i].contains(cols[c]) ? QString::number(rows[i].value(cols[c])) : QString()));
        t->resizeColumnsToContents();
        QString title = tr("%1 (%2)").arg(tr(kListTitle[l])).arg(rows.size());
        if ((l == Ssp || l == Gradient) && !rows.isEmpty()) {
            qint64 sum = 0;
            for (const Row &x : rows) sum += x.value(QStringLiteral("d"));
            if (sum != r.length()) title = tr("%1 (%2, %3 of %4 m)").arg(tr(kListTitle[l])).arg(rows.size()).arg(sum).arg(r.length());
        }
        m_listTabs->setTabText(l, title);
    }
    m_filling = false;
    emit routeSelected(m_route);
}

void StationKavachEditor::commit(const Station &next, const QString &label)
{
    m_station = next;
    emit edited(StationKavach::toJson(next), label);
}

void StationKavachEditor::refuse(const QString &why)
{
    emit refused(why);
    fill();
}

void StationKavachEditor::onTopEdited()
{
    if (m_filling || !m_has) return;
    Station next = m_station;
    next.stationId = m_stationId->value();
    next.stnVersion = m_stnVersion->value();
    next.sections.clear();
    for (const QString &pair : names(m_sections->text())) {
        const QStringList kv = pair.split(QLatin1Char('='));
        qint64 a = 0, n = 0;
        if (kv.size() != 2 || !number(kv[0].trimmed(), &a, false) || !number(kv[1].trimmed(), &n, false) || n < 1)
            return refuse(tr("MA sections by aspect: \"%1\" is not aspect=sections (e.g. 11=3)").arg(pair));
        next.sections.insert(int(a), int(n));
    }
    if (next.stationId == m_station.stationId && next.stnVersion == m_station.stnVersion
        && next.sections == m_station.sections)
        return;
    commit(next, tr("edit KAVACH station"));
}

void StationKavachEditor::onSignalsEdited()
{
    Station next = m_station;
    next.signalList.clear();
    for (int r = 0; r < m_signals->rowCount(); ++r) {
        StationKavach::Signal g;
        g.name = cell(m_signals, r, 0);
        qint64 foot = 0, info = 0;
        if (g.name.isEmpty()) return refuse(tr("signal row %1 has no name").arg(r + 1));
        if (!number(cell(m_signals, r, 1), &foot)) return refuse(tr("signal %1: the foot tag is a number").arg(g.name));
        if (!number(cell(m_signals, r, 2), &info)) return refuse(tr("signal %1: the signal info is a number").arg(g.name));
        for (const StationKavach::Signal &o : next.signalList)
            if (o.name == g.name) return refuse(tr("two signals are called %1").arg(g.name));
        g.footTag = int(foot);
        g.sigInfo = info;
        auto *c = qobject_cast<QComboBox *>(m_signals->cellWidget(r, 3));
        g.aspect = c ? int(c->currentData().toLongLong()) : 1;
        next.signalList << g;
    }
    // A renamed signal: the routes naming it follow.
    for (int i = 0; i < qMin(m_station.signalList.size(), next.signalList.size()); ++i) {
        const QString was = m_station.signalList[i].name, now = next.signalList[i].name;
        if (was == now) continue;
        for (Route &r : next.routes) {
            if (r.entry == was) r.entry = now;
            if (r.exit == was) r.exit = now;
        }
    }
    commit(next, tr("edit KAVACH signals"));
}

void StationKavachEditor::onRoutesEdited()
{
    Station next = m_station;
    for (int r = 0; r < m_routes->rowCount() && r < next.routes.size(); ++r) {
        Route &x = next.routes[r];
        x.name = cell(m_routes, r, RName);
        if (x.name.isEmpty()) return refuse(tr("route row %1 has no name").arg(r + 1));
        for (int o = 0; o < r; ++o)
            if (next.routes[o].name == x.name) return refuse(tr("two routes are called %1").arg(x.name));
        x.entry = cell(m_routes, r, REntry);
        x.exit = cell(m_routes, r, RExit);
        auto combo = [this, r](int col, int def) {
            auto *c = qobject_cast<QComboBox *>(m_routes->cellWidget(r, col));
            return c ? int(c->currentData().toLongLong()) : def;
        };
        x.pktDir = combo(RDir, x.pktDir);
        x.sectionType = combo(RSection, x.sectionType);
        x.authority = combo(RAuth, x.authority);
        x.osSpeed = combo(ROsSpeed, x.osSpeed);
        x.pointsNormal = names(cell(m_routes, r, RPointsN));
        x.pointsReverse = names(cell(m_routes, r, RPointsR));
        x.trackCircuits = names(cell(m_routes, r, RTrack));
        x.tins = names(cell(m_routes, r, RTins));
        qint64 v = 0;
        if (!number(cell(m_routes, r, RDupDist), &v)) return refuse(tr("route %1: the duplicate tag distance is a number").arg(x.name));
        x.distDupTag = int(v);
        x.adjTins.clear();
        for (const QString &t : names(cell(m_routes, r, RAdjTins))) {
            if (!number(t, &v, false)) return refuse(tr("route %1: adjacent TIN \"%2\" is not a number").arg(x.name, t));
            x.adjTins << int(v);
        }
        if (!number(cell(m_routes, r, RTin), &v)) return refuse(tr("route %1: the TIN is a number").arg(x.name));
        x.tin = int(v);
        const QStringList lr = names(cell(m_routes, r, RLocReset));
        x.locReset = !lr.isEmpty();
        if (x.locReset) {
            qint64 a = 0, b = 0, c = 0;
            if (lr.size() != 3 || !number(lr[0], &a, false) || !number(lr[1], &b, false) || !number(lr[2], &c, false))
                return refuse(tr("route %1: the location reset is \"start, direction, correction\", three numbers").arg(x.name));
            x.locResetStart = int(a);
            x.adjLocoDir = int(b);
            x.absLocCorrection = c;
        }
        if (!number(cell(m_routes, r, RNextStn), &v) || v < 0 || v > 65535)
            return refuse(tr("route %1: the next station is a KAVACH id, 1 to 65535 (empty: none)").arg(x.name));
        x.nextStn = int(v);
    }
    commit(next, tr("edit KAVACH routes"));
}

void StationKavachEditor::onListEdited(List l)
{
    if (m_route < 0 || m_route >= m_station.routes.size()) return;
    Station next = m_station;
    Route &r = next.routes[m_route];
    QTableWidget *t = m_lists[l];
    if (l == Tags) {
        QVector<StationKavach::RouteTag> tags;
        for (int i = 0; i < t->rowCount(); ++i) {
            qint64 d = 0, tag = 0, dup = 0;
            if (!number(cell(t, i, 0), &d) || !number(cell(t, i, 1), &tag) || !number(cell(t, i, 2), &dup))
                return refuse(tr("route %1, tag row %2: distance, tag and duplicate dir are numbers").arg(r.name).arg(i + 1));
            tags << StationKavach::RouteTag{ int(d), int(tag), int(dup) };
        }
        if (tags.isEmpty()) return refuse(tr("route %1 needs at least its entry signal's foot tag").arg(r.name));
        r.tags = tags;
        return commit(next, tr("edit KAVACH route %1 tags").arg(r.name));
    }
    QVector<Row> rows;
    for (int i = 0; i < t->rowCount(); ++i) {
        Row x;
        for (int c = 0; c < t->columnCount(); ++c) {
            const QString s = cell(t, i, c);
            if (s.isEmpty()) continue;
            qint64 v = 0;
            const QString col = t->horizontalHeaderItem(c) ? t->horizontalHeaderItem(c)->text() : QString();
            if (!number(s, &v, false))
                return refuse(tr("route %1, %2 row %3: %4 = \"%5\" is not a number")
                                  .arg(r.name, tr(kListTitle[l])).arg(i + 1).arg(col, s));
            x.insert(col, v);
        }
        rows << x;
    }
    *listOf(r, l) = rows;
    commit(next, tr("edit KAVACH route %1 %2").arg(r.name, tr(kListTitle[l]).toLower()));
}

void StationKavachEditor::addSignal()
{
    Station next = m_station;
    QString name;
    for (int n = next.signalList.size() + 1;; ++n) {
        name = tr("S%1").arg(n);
        if (next.signalIndex(name) < 0) break;
    }
    StationKavach::Signal g;
    g.name = name;
    const int at = m_signals->currentRow() >= 0 ? m_signals->currentRow() + 1 : next.signalList.size();
    next.signalList.insert(qBound(0, at, next.signalList.size()), g);
    commit(next, tr("add KAVACH signal"));
}

void StationKavachEditor::deleteSignals()
{
    Station next = m_station;
    const QList<int> rows = selectedRows(m_signals);
    if (rows.isEmpty()) return;
    for (int r : rows) if (r < next.signalList.size()) next.signalList.remove(r);
    commit(next, tr("delete KAVACH signal(s)"));
}

void StationKavachEditor::addRoute(bool copySelected)
{
    Station next = m_station;
    Route r;
    if (copySelected && m_route >= 0 && m_route < next.routes.size()) {
        r = next.routes[m_route];
        r.name = tr("%1 (copy)").arg(r.name);
    } else {
        // From the selected route's exit (the next section on), or the first signal.
        const Route *from = m_route >= 0 && m_route < next.routes.size() ? &next.routes[m_route] : nullptr;
        r.entry = from ? from->exit : next.signalList.isEmpty() ? QString() : next.signalList.first().name;
        if (from) r.pktDir = from->pktDir;
        const int e = next.signalIndex(r.entry);
        r.tags << StationKavach::RouteTag{ 0, e >= 0 ? next.signalList[e].footTag : 0, 0 };
        r.name = r.entry.isEmpty() ? tr("new route") : tr("%1-").arg(r.entry);
    }
    QString name = r.name;
    for (int n = 2;; ++n) {
        bool used = false;
        for (const Route &o : next.routes) used = used || o.name == name;
        if (!used) break;
        name = QStringLiteral("%1 %2").arg(r.name).arg(n);
    }
    r.name = name;
    const int at = m_route >= 0 ? m_route + 1 : next.routes.size();
    next.routes.insert(qBound(0, at, next.routes.size()), r);
    m_route = qBound(0, at, next.routes.size() - 1);
    commit(next, copySelected ? tr("copy KAVACH route") : tr("add KAVACH route"));
}

void StationKavachEditor::deleteRoutes()
{
    Station next = m_station;
    const QList<int> rows = selectedRows(m_routes);
    if (rows.isEmpty()) return;
    for (int r : rows) if (r < next.routes.size()) next.routes.remove(r);
    m_route = qMin(m_route, next.routes.size() - 1);
    commit(next, tr("delete KAVACH route(s)"));
}

bool StationKavachEditor::addTsrByPoles(int tsrRoute, const QString &from, const QString &to, int speedKmh, int tsrId,
                                       int whistle)
{
    if (tsrRoute < 0 || tsrRoute >= m_station.tsrRoutes.size()) {
        emit refused(tr("No TSR route %1 in this station").arg(tsrRoute));
        return false;
    }
    QString why;
    const QVector<StationKavach::TsrPlacement> put =
        StationKavach::tsrByPoles(m_station, m_station.tsrRoutes[tsrRoute], from, to, speedKmh, tsrId, whistle, &why);
    if (put.isEmpty()) {
        emit refused(tr("TSR not added: %1").arg(why));
        return false;
    }
    Station next = m_station;
    QStringList where;
    for (const StationKavach::TsrPlacement &p : put) {
        next.routes[p.route].tsr << p.row;
        where << tr("%1 (%2 m from %3 m)").arg(next.routes[p.route].name).arg(p.row.value(QStringLiteral("len")))
                     .arg(p.row.value(QStringLiteral("dist")));
    }
    m_route = put.first().route;
    commit(next, tr("TSR %1 at %2 km/h, %3 to %4").arg(tsrId).arg(speedKmh).arg(from, to));
    emit said(tr("TSR %1 at %2 km/h from pole %3 to %4: %5").arg(tsrId).arg(speedKmh).arg(from, to)
                  .arg(where.join(QStringLiteral("; "))));
    return true;
}

void StationKavachEditor::addListRow()
{
    if (m_route < 0 || m_route >= m_station.routes.size()) return;
    const List l = List(m_listTabs->currentIndex());
    Station next = m_station;
    Route &r = next.routes[m_route];
    QTableWidget *t = m_lists[l];
    if (l == Tags) {
        const int at = qBound(1, t->currentRow() >= 0 ? t->currentRow() + 1 : r.tags.size(), r.tags.size());
        r.tags.insert(at, StationKavach::RouteTag{ 0, 0, 0 });
        return commit(next, tr("add KAVACH route %1 tag").arg(r.name));
    }
    QVector<Row> &rows = *listOf(r, l);
    const int cur = t->currentRow();
    const int at = cur >= 0 ? cur + 1 : rows.size();
    Row x = cur >= 0 && cur < rows.size() ? rows[cur] : Row();   // a copy of the row selected: change what differs
    // A first row: every field 0, a speed as universal (u) rather than by class (a, b, c).
    const QStringList byClass{ QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") };
    if (x.isEmpty())
        for (const QString &c : m_listCols[l])
            if (!((l == Ssp || l == Tsr) && byClass.contains(c))) x.insert(c, 0);
    rows.insert(qBound(0, at, rows.size()), x);
    commit(next, tr("add KAVACH route %1 %2 row").arg(r.name, tr(kListTitle[l]).toLower()));
}

void StationKavachEditor::deleteListRows()
{
    if (m_route < 0 || m_route >= m_station.routes.size()) return;
    const List l = List(m_listTabs->currentIndex());
    Station next = m_station;
    Route &r = next.routes[m_route];
    const QList<int> rows = selectedRows(m_lists[l]);
    if (rows.isEmpty()) return;
    if (l == Tags) {
        for (int i : rows) if (i < r.tags.size()) r.tags.remove(i);
        if (r.tags.isEmpty()) return refuse(tr("route %1 needs at least its entry signal's foot tag").arg(r.name));
        return commit(next, tr("delete KAVACH route %1 tag(s)").arg(r.name));
    }
    QVector<Row> &list = *listOf(r, l);
    for (int i : rows) if (i < list.size()) list.remove(i);
    commit(next, tr("delete KAVACH route %1 %2 row(s)").arg(r.name, tr(kListTitle[l]).toLower()));
}

// ---- session 460: routes proposed from the layout ---------------------------------

StationRoutes::Result StationKavachEditor::proposals() const
{
    static const StationLayout::Layout none;
    return StationRoutes::propose(m_seedFrom ? *m_seedFrom : none, m_station);
}

bool StationKavachEditor::addProposedRoutes(const QVector<int> &picked)
{
    const StationRoutes::Result r = proposals();
    Station next = m_station;
    if (!m_has) {   // no KAVACH data yet: started from the layout, as Start would
        static const StationLayout::Layout none;
        StationKavach::fromJson(seedFrom(m_seedFrom ? *m_seedFrom : none), &next);
    }
    QVector<int> fresh;
    for (int i : picked)
        if (i >= 0 && i < r.routes.size() && r.routes.at(i).status != StationRoutes::Proposal::Same) fresh << i;
    const int n = StationRoutes::addTo(&next, r, fresh);
    if (!n) { refuse(tr("No route added: none of those picked is new")); return false; }
    m_has = true;
    commit(next, tr("add %n proposed route(s)", nullptr, n));
    emit said(tr("%n route(s) added from the layout: check their points (flank points are not proposed), "
                 "track circuits and profile", nullptr, n));
    return true;
}

QDialog *StationKavachEditor::proposeDialog()
{
    const StationRoutes::Result r = proposals();
    if (r.routes.isEmpty()) {
        refuse(r.notes.isEmpty() ? tr("No routes in this layout: it needs signals on lines, their foot tags placed 1, 2 or 6")
                                 : tr("No routes in this layout: %1").arg(r.notes.join(QStringLiteral("; "))));
        return nullptr;
    }
    auto *d = new QDialog(this);
    d->setObjectName(QStringLiteral("skeProposeDialog"));
    d->setWindowTitle(tr("Propose routes"));
    auto *v = new QVBoxLayout(d);
    int fresh = 0, ticked = 0;
    for (const auto &p : r.routes) {
        fresh += p.status == StationRoutes::Proposal::New;
        ticked += p.status == StationRoutes::Proposal::New && p.against <= StationRoutes::kAgainstMetres;
    }
    auto *head = new QLabel(tr("%1 routes from the layout's signals: %2 new, %3 ticked (the others cross onto a line that runs "
                               "the other way). Ticked ones are added; the tags, linking "
                               "distances, points Normal / Reverse, track circuits and TINs come from the layout. Points "
                               "beside the route (flank) are not proposed: add them to the route.")
                                .arg(r.routes.size()).arg(fresh).arg(ticked), d);
    head->setWordWrap(true);
    v->addWidget(head);
    auto *t = new QTableWidget(r.routes.size(), 7, d);
    t->setObjectName(QStringLiteral("skeProposals"));
    t->setHorizontalHeaderLabels({ tr("Route"), tr("Status"), tr("Tags (m from the tag before)"), tr("Points reverse"),
                                   tr("Points normal"), tr("Track circuits"), tr("TINs") });
    t->verticalHeader()->hide();
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (int i = 0; i < r.routes.size(); ++i) {
        const StationRoutes::Proposal &p = r.routes.at(i);
        auto *name = new QTableWidgetItem(p.route.name);
        name->setFlags(name->flags() | Qt::ItemIsUserCheckable);
        const bool against = p.against > StationRoutes::kAgainstMetres;
        name->setCheckState(p.status == StationRoutes::Proposal::New && !against ? Qt::Checked : Qt::Unchecked);
        if (p.status == StationRoutes::Proposal::Same) name->setFlags(name->flags() & ~Qt::ItemIsEnabled);
        t->setItem(i, 0, name);
        auto *st = new QTableWidgetItem(against ? tr("%1 · %2 m against %3's way").arg(StationRoutes::statusName(p.status)).arg(p.against).arg(p.againstLine)
                                                : StationRoutes::statusName(p.status));
        if (against) {
            st->setForeground(UiColor::muted());
            st->setToolTip(tr("It crosses onto a line whose signals face the other way and runs %1 m on it: not ticked").arg(p.against));
        }
        if (p.status == StationRoutes::Proposal::Differs) {
            st->setForeground(UiColor::warning());
            st->setToolTip(tr("The station's route %1 has the same signals and points but other tags or distances")
                               .arg(m_station.routes.value(p.existing).name));
        }
        t->setItem(i, 1, st);
        QStringList seq;
        for (const StationKavach::RouteTag &x : p.route.tags) seq << (x.dist ? QStringLiteral("%1 (%2)").arg(x.tag).arg(x.dist) : QString::number(x.tag));
        t->setItem(i, 2, new QTableWidgetItem(seq.join(QStringLiteral(", "))));
        t->setItem(i, 3, new QTableWidgetItem(p.route.pointsReverse.join(QStringLiteral(", "))));
        t->setItem(i, 4, new QTableWidgetItem(p.route.pointsNormal.join(QStringLiteral(", "))));
        t->setItem(i, 5, new QTableWidgetItem(p.route.trackCircuits.join(QStringLiteral(", "))));
        t->setItem(i, 6, new QTableWidgetItem(p.route.tins.join(QStringLiteral(", "))));
    }
    t->resizeColumnsToContents();
    for (int c = 2; c < 7; ++c) t->setColumnWidth(c, qMin(t->columnWidth(c), 320));
    v->addWidget(t, 1);
    if (!r.notes.isEmpty()) {
        auto *notes = new QLabel(tr("Not proposed:\n%1").arg(r.notes.join(QLatin1Char('\n'))), d);
        notes->setObjectName(QStringLiteral("skeProposeNotes"));
        notes->setWordWrap(true);
        v->addWidget(notes);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, d);
    QPushButton *add = buttons->addButton(tr("Add ticked routes"), QDialogButtonBox::AcceptRole);
    add->setObjectName(QStringLiteral("skeProposeAdd"));
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::reject);
    connect(add, &QPushButton::clicked, d, [this, d, t]() {
        QVector<int> picked;
        for (int i = 0; i < t->rowCount(); ++i)
            if (t->item(i, 0)->checkState() == Qt::Checked && (t->item(i, 0)->flags() & Qt::ItemIsEnabled)) picked << i;
        if (addProposedRoutes(picked)) d->accept();
    });
    d->resize(1100, 560);
    return d;
}
