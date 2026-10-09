#include "soswindow.h"

#include "dmitimetravel.h"
#include "sosrelay.h"
#include "sosstrip.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace {

QString hms(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); }
QString metres(double m) { return QStringLiteral("%1 m").arg(m, 0, 'f', 0); }

const QStringList &sourceHeaders()
{
    // The checks right after the distances: they are what the table is for.
    static const QStringList h = {
        QStringLiteral("Source"), QStringLiteral("Threat"), QStringLiteral("Closest"),
        QStringLiteral("Gap"), QStringLiteral("SoS dist."), QStringLiteral("Coll. dist."),
        // the checks, in SosLog::checks() order
        QStringLiteral("Same TIN"), QStringLiteral("SoS rng"), QStringLiteral("Coll. rng"),
        QStringLiteral("Adjacent"), QStringLiteral("Adjusted"), QStringLiteral("Stn chk"),
        QStringLiteral("Man. proc"), QStringLiteral("Unus. proc"), QStringLiteral("Passed"),
        QStringLiteral("Position"), QStringLiteral("Dir"), QStringLiteral("TIN"),
        QStringLiteral("ARP status"), QStringLiteral("Age") };
    return h;
}
const QStringList &sourceHeaderTips()
{
    static const QStringList t = {
        QString(), QString(), QStringLiteral("Closest of its kind among the tracked locos (SOS_IsClosestActiveThreat)"),
        QStringLiteral("Distance from the own loco's front to the source's reported position"),
        QStringLiteral("sos_distance: the distance the SoS target is at (0 = removed)"),
        QStringLiteral("collision_distance"),
        QStringLiteral("Same TIN as the own loco"), QStringLiteral("Within sos_trigger_distance"),
        QStringLiteral("Within collision_trigger_distance"), QStringLiteral("IsAdjacentLineInfringing, and where the answer came from"),
        QStringLiteral("SOSWithAdjustment changed the position or direction the ARP gave"),
        QStringLiteral("The station-section check for collisions (CheckLocoCollisionConditions_PerSource)"),
        QStringLiteral("IsManualSOSToBeProcessed, re-run at the snapshot"),
        QStringLiteral("IsUnusualStopOfOtherLocoToBeProcessed, re-run at the snapshot"),
        QStringLiteral("IsSOSTargetDistanceRemove: the own loco is past the source"),
        QStringLiteral("The source's position after adjustment (the ARP's own on hover)"), QString(), QString(),
        QStringLiteral("EMERGENCY_STATUS of the source's last ARP"), QStringLiteral("Since its last ARP") };
    return t;
}
constexpr int kFirstCheck = 6;
constexpr int kPosition   = 15;

QTableWidgetItem *cell(const QString &text, const QString &tip = QString())
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (!tip.isEmpty()) it->setToolTip(tip);
    return it;
}

int eventIndexAtOrBefore(const SosLog::Timeline &t, qint64 ms)
{
    int lo = 0, hi = t.events.size();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (t.events.at(mid).epochMs <= ms) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

QColor eventColor(int code)
{
    switch (code) {
    case SosLog::EvBrakeApplied:
    case SosLog::EvThreatStart:
    case SosLog::EvStnSosStart:     return UiColor::error();
    case SosLog::EvBrakeSkipped:
    case SosLog::EvSrcTimeout:
    case SosLog::EvSrcEvicted:
    case SosLog::EvSrcDroppedFull:
    case SosLog::EvTableReset:
    case SosLog::EvSelfUnusualAlarm:
    case SosLog::EvDestLocoSos:     return UiColor::warning();
    case SosLog::EvThreatEnd:
    case SosLog::EvStnSosEnd:
    case SosLog::EvTargetRemoved:   return UiColor::ok();
    default:                        return QColor();
    }
}

}  // namespace

SosWindow::SosWindow(MessageDispatcher *dispatcher, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_dispatcher(dispatcher)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("SoS"));
    WindowGeometry::makeResizableWindow(this);
    resize(1100, 680);

    m_picker = new QComboBox(this);
    m_picker->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_picker->setToolTip(tr("The tab whose @sos log is read (tabs without one are listed too, and say so)"));
    m_follow = new QCheckBox(tr("Follow the log"), this);
    m_follow->setToolTip(tr("Show the snapshot at or before the row picked in any tab or replay window"));
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Loco log:"), this));
    top->addWidget(m_picker);
    top->addStretch(1);
    top->addWidget(m_follow);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_prevEvent = new QPushButton(QStringLiteral("◀ ") + tr("Decision"), this);
    m_nextEvent = new QPushButton(tr("Decision") + QStringLiteral(" ▶"), this);
    m_prevEvent->setToolTip(tr("To the previous SoS decision (@sosev)"));
    m_nextEvent->setToolTip(tr("To the next SoS decision (@sosev)"));
    auto *time = new QHBoxLayout;
    time->addWidget(m_prevEvent);
    time->addWidget(m_slider, 1);
    time->addWidget(m_nextEvent);
    m_readout = new QLabel(this);
    m_readout->setWordWrap(true);

    m_sources = new QTableWidget(this);
    m_sources->setColumnCount(sourceHeaders().size());
    m_sources->setHorizontalHeaderLabels(sourceHeaders());
    for (int c = 0; c < sourceHeaderTips().size(); ++c)
        if (!sourceHeaderTips().at(c).isEmpty()) m_sources->horizontalHeaderItem(c)->setToolTip(sourceHeaderTips().at(c));
    m_sources->verticalHeader()->hide();
    m_sources->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sources->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sources->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_sources->setToolTip(tr("The loco's SoS table at this moment: one row per tracked loco, then the stations"));

    m_events = new QTableWidget(this);
    m_events->setColumnCount(2);
    m_events->setHorizontalHeaderLabels({ tr("Time"), tr("Decision") });
    m_events->verticalHeader()->hide();
    m_events->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_events->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_events->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_events->horizontalHeader()->setStretchLastSection(true);
    m_events->setToolTip(tr("Every SoS decision the loco logged. Double-click: show that moment in the log"));

    // Session 185: the strip beside what the loco is reacting to, the table under them.
    m_strip = new SosStrip(this);
    m_decision = new QLabel(this);
    m_decision->setWordWrap(true);
    m_decision->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_decision->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_decision->setContentsMargins(6, 4, 6, 4);
    auto *decisionScroll = new QScrollArea(this);
    decisionScroll->setWidget(m_decision);
    decisionScroll->setWidgetResizable(true);
    decisionScroll->setMinimumWidth(260);
    decisionScroll->setToolTip(tr("What the loco is reacting to at this moment, from @sos"));
    auto *upper = new QSplitter(Qt::Horizontal, this);
    upper->addWidget(m_strip);
    upper->addWidget(decisionScroll);
    upper->setStretchFactor(0, 3);
    upper->setStretchFactor(1, 1);
    upper->setChildrenCollapsible(false);

    m_split = new QSplitter(Qt::Vertical, this);
    m_split->addWidget(upper);
    m_split->addWidget(m_sources);
    // Session 187: the decisions, and what another loco sent and this one did with it.
    m_relay = new SosRelayPanel(m_dispatcher, this);
    m_lowerTabs = new QTabWidget(this);
    m_lowerTabs->addTab(m_events, tr("Decisions"));
    m_lowerTabs->addTab(m_relay, tr("Two logs"));
    m_lowerTabs->setTabToolTip(1, tr("What another loco sent (its log's broadcast emergency status), when this loco heard it, and what it decided"));
    m_split->addWidget(m_lowerTabs);
    m_split->setStretchFactor(0, 3);
    m_split->setStretchFactor(1, 2);
    m_split->setStretchFactor(2, 2);
    m_split->setChildrenCollapsible(false);

    m_status = new StatusLine;

    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addLayout(time);
    root->addWidget(m_readout);
    root->addWidget(m_split, 1);
    root->addWidget(m_status);

    connect(m_picker, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        setSource(m_picker->currentData().toString());
    });
    connect(m_slider, &QSlider::valueChanged, this, &SosWindow::setCurrentSnapshot);
    connect(m_prevEvent, &QPushButton::clicked, this, [this]() { stepEvent(-1); });
    connect(m_nextEvent, &QPushButton::clicked, this, [this]() { stepEvent(+1); });
    connect(m_follow, &QCheckBox::toggled, this, &SosWindow::setFollowLog);
    connect(m_relay, &SosRelayPanel::jumpRequested, this, [this](const QString &key, qint64 ms) {
        if (key == m_key) showMoment(ms);
        emit jumpRequested(key, ms);
    });
    connect(m_events, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0 || row >= m_t.events.size()) return;
        const qint64 ms = m_t.events.at(row).epochMs;
        showMoment(ms);
        emit jumpRequested(m_key, ms);
    });
    connect(DmiTimeTravel::instance(), &DmiTimeTravel::momentChanged, this, [this](const DmiMoment &m) {
        if (!m_follow->isChecked() || !m.valid) return;
        if (!m.preferredKey.isEmpty() && m.preferredKey != m_key && m_dispatcher
            && SosLog::hasSos(m_dispatcher->modelForKey(m.preferredKey))) {
            const int idx = m_picker->findData(m.preferredKey);
            if (idx >= 0) m_picker->setCurrentIndex(idx);
        }
        showMoment(m.atMs);
    });
    if (m_dispatcher)
        connect(m_dispatcher, &MessageDispatcher::tabRequested, this, [this](QString, QString) { refreshPicker(); });

    refreshPicker();
    // The first tab that carries @sos, else the first tab.
    QString pick;
    for (int i = 0; i < m_picker->count() && pick.isEmpty(); ++i) {
        const QString k = m_picker->itemData(i).toString();
        if (m_dispatcher && SosLog::hasSos(m_dispatcher->modelForKey(k))) pick = k;
    }
    if (pick.isEmpty() && m_picker->count() > 0) pick = m_picker->itemData(0).toString();
    setSource(pick);
}

void SosWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    // Centre the current decision once the list has its real height.
    QTimer::singleShot(0, this, [this]() { markEvents(); });
}

SosWindow::~SosWindow()
{
    DmiTimeTravel::instance()->unfollow(this);
}

void SosWindow::refreshPicker()
{
    if (!m_dispatcher) return;
    QStringList keys = m_dispatcher->knownKeys();
    keys.sort();
    m_picker->blockSignals(true);
    m_picker->clear();
    for (const QString &k : keys) {
        const QString friendly = m_dispatcher->friendlyNameFor(k);
        m_picker->addItem(friendly == k ? k : QStringLiteral("%1   (%2)").arg(friendly, k), k);
    }
    const int idx = m_picker->findData(m_key);
    if (idx >= 0) m_picker->setCurrentIndex(idx);
    m_picker->blockSignals(false);
}

void SosWindow::setSource(const QString &key)
{
    m_key = key;
    const int idx = m_picker->findData(key);
    if (idx >= 0 && idx != m_picker->currentIndex()) {
        m_picker->blockSignals(true);
        m_picker->setCurrentIndex(idx);
        m_picker->blockSignals(false);
    }
    const QString name = m_dispatcher ? m_dispatcher->friendlyNameFor(key) : key;
    setWindowTitle(key.isEmpty() ? tr("SoS") : tr("SoS — %1").arg(name.isEmpty() ? key : name));
    rebuild();
}

void SosWindow::rebuild()
{
    const LogModel *model = (m_dispatcher && !m_key.isEmpty()) ? m_dispatcher->modelForKey(m_key) : nullptr;
    m_t = SosLog::extract(model);
    m_strip->setConfig(SosStrip::configFromLog(model));
    m_relay->setReceiver(m_key, m_t);
    m_slider->blockSignals(true);
    m_slider->setRange(0, qMax(0, m_t.snaps.size() - 1));
    m_slider->setValue(qMax(0, m_t.snaps.size() - 1));
    m_slider->blockSignals(false);
    m_slider->setEnabled(m_t.snaps.size() > 1);
    m_prevEvent->setEnabled(!m_t.events.isEmpty());
    m_nextEvent->setEnabled(!m_t.events.isEmpty());
    fillEvents();

    if (m_t.isEmpty()) {
        m_index = -1;
        fillSources();
        fillDecision();
        m_readout->setText(tr("No @sos in this tab."));
        m_status->warn(tr("This log has no SoS lines (@sos / @sossrc / @sosev). LKAVACH logs them only "
                          "with the SoS logging added (SOS_handoff, README 01)."));
        return;
    }

    QStringList parts;
    auto count = [](int n, const QString &one, const QString &many) {
        return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
    };
    parts << count(m_t.snaps.size(), tr("snapshot"), tr("snapshots"))
          << (m_t.eventsDerived ? count(m_t.events.size(), tr("change"), tr("changes"))
                                : count(m_t.events.size(), tr("decision"), tr("decisions")))
          << count(SosLog::spells(m_t).size(), tr("threat"), tr("threats"));
    if (m_t.minimal())
        parts << tr("minimal layout: threats to the second, no reasons or checks");
    if (m_t.otherVersion > 0) parts << tr("%1 of another layout version, not read").arg(m_t.otherVersion);
    if (m_t.orphanSources > 0) parts << tr("%1 source lines without their snapshot").arg(m_t.orphanSources);
    if (m_t.otherVersion > 0 || m_t.orphanSources > 0) m_status->warn(parts.join(QStringLiteral(" · ")));
    else m_status->state(parts.join(QStringLiteral(" · ")));

    setCurrentSnapshot(m_t.snaps.isEmpty() ? -1 : m_t.snaps.size() - 1);
}

void SosWindow::setCurrentSnapshot(int index)
{
    if (index >= m_t.snaps.size()) index = m_t.snaps.size() - 1;
    m_index = index;
    if (index >= 0 && m_slider->value() != index) {
        m_slider->blockSignals(true);
        m_slider->setValue(index);
        m_slider->blockSignals(false);
    }
    if (index >= 0) {
        const SosLog::Snapshot &s = m_t.snaps.at(index);
        static const char *const kWhy[] = { "every second", "after an ARP", "after an AEP", "after DEST_LOCO_SOS" };
        const QString why = (s.why >= 0 && s.why < 4) ? tr(kWhy[s.why]) : QString::number(s.why);
        m_readout->setText(tr("%1 · snapshot %2 of %3 (%4) · own loco at %5, %6, %7, TIN %8")
                               .arg(hms(s.epochMs)).arg(index + 1).arg(m_t.snaps.size()).arg(why)
                               .arg(metres(s.ownLocM), SosLog::dirName(s.ownDir),
                                    QStringLiteral("sensor speed %1").arg(s.ownSpeed, 0, 'f', 1))
                               .arg(s.ownTin));
    }
    fillSources();
    markEvents();
    fillDecision();
}

void SosWindow::fillDecision()
{
    if (m_index < 0 || m_index >= m_t.snaps.size()) {
        m_strip->setSnapshot(nullptr);
        m_decision->setText(tr("No snapshot."));
        return;
    }
    const SosLog::Snapshot &s = m_t.snaps.at(m_index);
    m_strip->setSnapshot(&s);
    QString html = QStringLiteral("<b>%1</b>").arg(tr("What the loco is reacting to").toHtmlEscaped());
    for (const QString &line : SosLog::decisionLines(s)) {
        const bool observed = line.startsWith(QLatin1String("Observed:"));
        html += QStringLiteral("<p style=\"margin:4px 0\">%1%2%3</p>")
                    .arg(observed ? QStringLiteral("<span style=\"color:%1\">").arg(UiColor::warning().name()) : QString(),
                         line.toHtmlEscaped(), observed ? QStringLiteral("</span>") : QString());
    }
    m_decision->setText(html);
}

void SosWindow::showMoment(qint64 ms)
{
    const int i = m_t.snapAtOrBefore(ms);
    setCurrentSnapshot(i >= 0 ? i : (m_t.snaps.isEmpty() ? -1 : 0));
}

void SosWindow::setFollowLog(bool on)
{
    if (m_follow->isChecked() != on) { m_follow->setChecked(on); return; }
    if (on) DmiTimeTravel::instance()->follow(this);
    else DmiTimeTravel::instance()->unfollow(this);
}

int SosWindow::sourceColumn(const QString &header) const
{
    return sourceHeaders().indexOf(header);
}

void SosWindow::fillSources()
{
    m_sources->setRowCount(0);
    if (m_index < 0 || m_index >= m_t.snaps.size()) return;
    const SosLog::Snapshot &s = m_t.snaps.at(m_index);

    // The target's row is bold, and says so.
    for (const SosLog::Source &src : s.sources) {
        const int r = m_sources->rowCount();
        m_sources->insertRow(r);
        const bool target = !s.aggStationWon && s.aggThreat != SosLog::ThreatNone && src.locoId == s.collisionLoco;
        QString name = tr("Loco %1").arg(src.locoId);
        if (target) name += QStringLiteral("  ◎ ") + tr("target");
        auto *nameItem = cell(name, tr("Slot %1. Last ARP frame %2, last tag %3, approaching station %4")
                                        .arg(src.slot).arg(src.frameNo).arg(src.lastTag).arg(src.approachingStation));
        m_sources->setItem(r, 0, nameItem);
        auto *threat = cell(SosLog::threatBitsText(src.threats));
        if (src.threats & (SosLog::BitHeadOn | SosLog::BitRearEnd)) threat->setForeground(UiColor::error());
        else if (src.threats) threat->setForeground(UiColor::warning());
        m_sources->setItem(r, 1, threat);
        // A check mark when it is the closest for every threat it has; else which.
        const QString closest = !src.closest ? (src.threats ? QStringLiteral("\u2013") : QString())
                              : (src.closest == src.threats ? QStringLiteral("\u2713") : SosLog::threatBitsText(src.closest));
        auto *cl = cell(closest, tr("Closest of its kind among the tracked locos (SOS_IsClosestActiveThreat): %1")
                                     .arg(SosLog::threatBitsText(src.closest)));
        cl->setTextAlignment(Qt::AlignCenter);
        m_sources->setItem(r, 2, cl);
        const double gap = std::abs(s.ownLocM - double(src.locM));
        m_sources->setItem(r, 3, cell(metres(gap), tr("The firmware's own distance field reads %1").arg(metres(src.distM))));
        m_sources->setItem(r, 4, cell(src.has(SosLog::BitManual | SosLog::BitUnusual | SosLog::BitParted) || src.sosDistM != 0.0
                                          ? metres(src.sosDistM) : QString(),
                                      src.has(SosLog::BitManual | SosLog::BitUnusual | SosLog::BitParted) && src.sosDistM == 0.0
                                          ? tr("0 m with the threat still set: the target distance was removed (source passed)")
                                          : QString()));
        m_sources->setItem(r, 5, cell(src.collisionDistM != 0.0 ? metres(src.collisionDistM) : QString()));
        m_sources->setItem(r, kPosition, cell(metres(src.locM),
                                      tr("As the ARP said: %1 m %2").arg(src.rawLocM).arg(SosLog::dirName(src.rawDir))));
        m_sources->setItem(r, kPosition + 1, cell(SosLog::dirName(src.dir)));
        m_sources->setItem(r, kPosition + 2, cell(QString::number(src.tin), tr("Length %1 m").arg(src.lengthM)));
        auto *sts = cell(src.emergency < 0 ? QStringLiteral("\u2013") : SosLog::emergencyName(src.emergency),
                         src.emergency < 0 ? tr("Not in the minimal layout (README 03)")
                                           : tr("EMERGENCY_STATUS of the loco's last ARP, as received"));
        if (src.emergency) sts->setForeground(UiColor::warning());
        m_sources->setItem(r, kPosition + 3, sts);
        m_sources->setItem(r, kPosition + 4, cell(QStringLiteral("%1 s").arg(src.ageMs / 1000.0, 0, 'f', 1)));
        if (s.version == 2) {   // session 192: the minimal layout runs no checks
            for (int c = kFirstCheck; c < kPosition; ++c)
                m_sources->setItem(r, c, cell(QString(), tr("Not in the minimal layout (README 03)")));
        }
        const QVector<SosLog::Check> cs = s.version == 2 ? QVector<SosLog::Check>() : SosLog::checks(src);
        for (int i = 0; i < cs.size(); ++i) {
            const SosLog::Check &c = cs.at(i);
            auto *it = cell(c.warn ? QStringLiteral("!") : (c.on ? QStringLiteral("✓") : QStringLiteral("–")),
                            c.label + QStringLiteral(": ") + (c.on ? tr("yes") : tr("no")) + QStringLiteral("\n") + c.tip);
            it->setTextAlignment(Qt::AlignCenter);
            if (c.warn) it->setForeground(UiColor::warning());
            else if (!c.on) it->setForeground(UiColor::muted());
            m_sources->setItem(r, kFirstCheck + i, it);
        }
        if (target) {
            QFont f = nameItem->font();
            f.setBold(true);
            for (int c = 0; c < m_sources->columnCount(); ++c)
                if (QTableWidgetItem *it = m_sources->item(r, c)) it->setFont(f);
        }
    }

    for (const SosLog::Station &st : s.stations) {
        if (!st.inUse) continue;
        const int r = m_sources->rowCount();
        m_sources->insertRow(r);
        const bool target = s.aggStationWon && st.id == s.aggStation;
        QString name = tr("Station %1").arg(st.id);
        if (target) name += QStringLiteral("  ◎ ") + tr("target");
        m_sources->setItem(r, 0, cell(name, tr("Station slot: additional emergency packets (AEP)")));
        auto *threat = cell(st.addEmSos() ? tr("general SoS") : tr("none"));
        if (st.addEmSos()) threat->setForeground(UiColor::warning());
        m_sources->setItem(r, 1, threat);
        auto *scl = cell(st.addEmSos() ? (st.closest() ? QStringLiteral("\u2713") : QStringLiteral("\u2013")) : QString());
        scl->setTextAlignment(Qt::AlignCenter);
        m_sources->setItem(r, 2, scl);
        m_sources->setItem(r, 3, cell(metres(std::abs(s.ownLocM - double(st.absLocM)))));
        m_sources->setItem(r, 4, cell(st.addEmSos() ? metres(st.sosDistM) : QString()));
        m_sources->setItem(r, kPosition, cell(metres(st.absLocM)));
        m_sources->setItem(r, kPosition + 3, cell(tr("gen_sos_call %1").arg(st.genSosCall() ? 1 : 0)));
        m_sources->setItem(r, kPosition + 4, cell(QStringLiteral("%1 s").arg(st.ageMs / 1000.0, 0, 'f', 1)));
        if (target) {
            QFont f = m_sources->item(r, 0)->font();
            f.setBold(true);
            for (int c = 0; c < m_sources->columnCount(); ++c)
                if (QTableWidgetItem *it = m_sources->item(r, c)) it->setFont(f);
        }
    }
}

void SosWindow::fillEvents()
{
    m_events->setRowCount(m_t.events.size());
    for (int i = 0; i < m_t.events.size(); ++i) {
        const SosLog::Event &e = m_t.events.at(i);
        m_events->setItem(i, 0, cell(hms(e.epochMs)));
        auto *text = cell(SosLog::eventText(e));
        text->setData(Qt::UserRole, eventColor(e.code));
        m_events->setItem(i, 1, text);
    }
    markEvents();
}

void SosWindow::markEvents()
{
    const qint64 ms = (m_index >= 0 && m_index < m_t.snaps.size()) ? m_t.snaps.at(m_index).epochMs : 0;
    const int last = m_index >= 0 ? eventIndexAtOrBefore(m_t, ms) : -1;
    for (int i = 0; i < m_events->rowCount(); ++i) {
        QTableWidgetItem *t = m_events->item(i, 0), *d = m_events->item(i, 1);
        if (!t || !d) continue;
        const bool past = i <= last;
        const QColor c = d->data(Qt::UserRole).value<QColor>();
        t->setForeground(past ? palette().color(QPalette::Text) : UiColor::muted());
        d->setForeground(past ? (c.isValid() ? c : palette().color(QPalette::Text)) : UiColor::muted());
    }
    if (last >= 0) {
        m_events->blockSignals(true);
        m_events->setCurrentCell(last, 1);
        m_events->blockSignals(false);
        m_events->scrollToItem(m_events->item(last, 1), QAbstractItemView::PositionAtCenter);
    } else {
        m_events->clearSelection();
    }
}

void SosWindow::stepEvent(int direction)
{
    if (m_t.events.isEmpty()) return;
    const qint64 ms = (m_index >= 0 && m_index < m_t.snaps.size()) ? m_t.snaps.at(m_index).epochMs : 0;
    const int at = eventIndexAtOrBefore(m_t, ms);
    int target = -1;
    if (direction > 0) {
        for (int i = qMax(0, at); i < m_t.events.size(); ++i)
            if (m_t.events.at(i).epochMs > ms) { target = i; break; }
    } else {
        for (int i = at; i >= 0; --i)
            if (m_t.events.at(i).epochMs < ms) { target = i; break; }
    }
    if (target < 0) return;
    showMoment(m_t.events.at(target).epochMs);
}
