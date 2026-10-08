#include "lococonsolewindow.h"
#include "undolog.h"
#include "fieldplot.h"
#include "cabpanel.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "uistyle.h"
#include "uicolors.h"
#include "windowgeometry.h"
#include "messagedispatcher.h"
#include "replaywindow.h"

#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QPushButton>
#include <QTextStream>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QShortcut>

#include "tablefindbar.h"
#include "tabtags.h"
#include "bignumberpanel.h"
#include "livefields.h"
#include "statuspins.h"
#include "settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QMenu>
#include <QSettings>
#include <QVBoxLayout>
#include <QWidget>

namespace {
const int    kRefreshMs = 250;
const qint64 kStaleMs   = 4000;
const qint64 kDeadMs    = 10000;

// Recordings land in a "replay" folder next to SAVED_LOGS/LOGS (created on
// demand, relative to the working dir like SaveData's SAVED_LOGS). Each file
// is "<stem>_<loco>_<ctrl>_<ddMMyyyy_HHmmss>.cap".
const char *kReplaySubdir = "replay";
const char *kCapStem      = "loco";

// Tab order: the four radio frames first (as requested), then DMI, then the
// NMS types, RFID, the digital IO frames, and finally the controller/logger
// self-status frames (ccsys/dlsys). All of these are decode-capable
// (describe() handles them) and shown in the replay window, so they belong
// here too.
const CapType kTabOrder[] = {
    CapType::SLRP, CapType::AAP, CapType::ARP, CapType::ArpRecv, CapType::LSRP,
    CapType::Dmi,
    CapType::NmsHlth, CapType::NmsFault, CapType::NmsRssi,
    CapType::Rfid,
    CapType::Dip1, CapType::Dip2, CapType::Dop1, CapType::Dop2,
    CapType::CcSys, CapType::DlSys, CapType::Aep, CapType::Linfo,
    CapType::AuthKeys,                      // key-set lifecycle (from LCU)
    CapType::Random,                        // session nonce pair
    CapType::UBA,                           // target + braking curve
    CapType::Speed,                         // tachometer pulses
    CapType::AnalogTop, CapType::AnalogBottom   // analog (pressure) inputs
};
const int kTabOrderCount = int(sizeof(kTabOrder) / sizeof(kTabOrder[0]));

// Link-overview row order (also includes anything else that may arrive).
const CapType kLinkOrder[] = {
    CapType::SLRP, CapType::AAP, CapType::ARP, CapType::ArpRecv, CapType::LSRP,
    CapType::NmsHlth, CapType::NmsFault, CapType::NmsRssi,
    CapType::Dlt, CapType::Dmi, CapType::Biu, CapType::Brk,
    CapType::Rfid,
    CapType::Dip1, CapType::Dip2, CapType::Dop1, CapType::Dop2,
    CapType::CcSys, CapType::DlSys, CapType::Aep, CapType::Linfo,
    CapType::AuthKeys, CapType::Random, CapType::UBA,
    CapType::Speed, CapType::AnalogTop, CapType::AnalogBottom
};
const int kLinkOrderCount = int(sizeof(kLinkOrder) / sizeof(kLinkOrder[0]));

// These were the dark-theme values, used in both themes — green on a white
// background at 2.4:1. They now follow the palette.
QColor okColor()   { return UiColor::ok(); }
QColor warnColor() { return UiColor::warning(); }
QColor failColor() { return UiColor::error(); }
QColor dimColor()  { return UiColor::muted(); }

const char *dirText(CapDir d) {
    switch (d) { case CapDir::In: return "RX"; case CapDir::Out: return "TX"; default: return "--"; }
}
} // namespace

LocoConsoleWindow::LocoConsoleWindow(MessageDispatcher *dispatcher, QWidget *parent, TabTags *tags,
                                     const SessionKeyStore *keys)
    : QMainWindow(parent)
    , m_dispatcher(dispatcher)
{
    m_tags = tags ? tags : new TabTags(this);   // session 96
    m_keys = keys;
    setWindowTitle(tr("Live Loco Console"));
    WindowGeometry::makeResizableWindow(this);
    setWindowFlag(Qt::Window);
    resize(840, 580);
    // Default above; a remembered size/position wins over it.
    WindowGeometry::restore(this, QStringLiteral("locoConsole"));

    buildUi();

    if (m_dispatcher) {
        connect(m_dispatcher, &MessageDispatcher::entryAppended,
                this,         &LocoConsoleWindow::onEntryAppended);
    }

    m_refresh = new QTimer(this);
    m_refresh->setInterval(kRefreshMs);
    connect(m_refresh, &QTimer::timeout, this, &LocoConsoleWindow::onRefreshTick);
    m_refresh->start();
}

LocoConsoleWindow::~LocoConsoleWindow()
{
    // Flush/close any recording files if the window is destroyed mid-record.
    for (RecordSink &s : m_recSinks) {
        if (s.stream) { s.stream->flush(); delete s.stream; s.stream = nullptr; }
        if (s.file)   { s.file->close();  delete s.file;   s.file   = nullptr; }
    }
    m_recSinks.clear();
}

void LocoConsoleWindow::addTypeTab(CapType t)
{
    QTableWidget *tbl = new QTableWidget(0, 2, m_tabs);
    tbl->setHorizontalHeaderLabels({tr("Field"), tr("Value")});
    tbl->verticalHeader()->setVisible(false);
    tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
    tbl->setFont(UiStyle::monoFont());
    tbl->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tbl->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tabs->addTab(tbl, QString::fromLatin1(CaptureDecoder::typeLabel(t)));
    m_typeTables.insert(int(t), tbl);
    tbl->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tbl, &QWidget::customContextMenuRequested, this, [this, t, tbl](const QPoint &pos) {
        showFieldMenu(t, tbl, pos);
    });
}

void LocoConsoleWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 8, 10, 10);
    root->setSpacing(8);

    // ---- top strip ------------------------------------------------------
    QHBoxLayout *top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Loco / Ctrl:"), central));
    m_selector = new QComboBox(central);
    m_selector->setMinimumWidth(120);
    top->addWidget(m_selector);
    top->addSpacing(16);
    m_lblRtc  = new QLabel(tr("RTC --"), central);
    m_lblRate = new QLabel(tr("-- pkt/s"), central);
    m_lblCrc  = new QLabel(tr("CRC --"), central);
    m_lblSeq  = new QLabel(tr("seq --"), central);
    // Session 124: the readouts in the mono face, CRC and seq as chips.
    UiStyle::makeMono(m_lblRtc);
    UiStyle::makeMono(m_lblRate);
    UiStyle::makeChip(m_lblCrc, UiStyle::Tone::Neutral);
    UiStyle::makeChip(m_lblSeq, UiStyle::Tone::Neutral);
    top->addWidget(m_lblRtc);
    top->addStretch(1);
    top->addWidget(m_lblRate);
    top->addSpacing(12);
    top->addWidget(m_lblCrc);
    top->addSpacing(12);
    top->addWidget(m_lblSeq);

    // Session 124: the actions on a row of their own. One row of readouts
    // AND every action set the window's minimum width past 1,700 px, off a
    // laptop screen. `top` is the readouts row from here on; `actions` is
    // added under it.
    QHBoxLayout *readouts = top;
    top = new QHBoxLayout();
    // Session 168: start of mission, from the loco's ARP. On the actions
    // row: the readouts row is full at a laptop's width (session 150).
    m_lblMission = new QLabel(tr("start of mission --"), central);
    m_lblMission->setObjectName(QStringLiteral("locoMissionChip"));
    UiStyle::makeChip(m_lblMission, UiStyle::Tone::Neutral);
    m_lblMission->setMinimumWidth(140);              // may be cut short at a laptop's width; the tooltip has it all
    top->addWidget(m_lblMission);
    top->addStretch(1);
    m_btnRecord = new QPushButton(tr("\u25CF Record"), central);
    m_btnRecord->setToolTip(tr("Record one or more live sources, each to its own .cap file for replay"));
    top->addWidget(m_btnRecord);
    QPushButton *btnReplay = new QPushButton(tr("Replay\u2026"), central);
    btnReplay->setToolTip(tr("Open a recorded .cap file in the replay window"));
    top->addWidget(btnReplay);
    QPushButton *btnFind = new QPushButton(tr("Find\u2026"), central);
    btnFind->setToolTip(tr("Find a field on this tab (Ctrl+F)"));
    top->addWidget(btnFind);

    // Big numbers and change highlighting: both remembered.
    QSettings consoleSettings(Settings::iniPath(), QSettings::IniFormat);
    m_btnBigNumbers = new QPushButton(tr("Big numbers"), central);
    m_btnBigNumbers->setCheckable(true);
    m_btnBigNumbers->setToolTip(tr("Chosen fields in large type, readable across the room (F11 for full screen).\n"
                                   "Right-click a field in any tab ▸ Show as big number; right-click a tile to change it."));
    top->addWidget(m_btnBigNumbers);
    m_chkHighlight = new QCheckBox(tr("Highlight changes"), central);
    m_chkHighlight->setToolTip(tr("Mark a field whose value just changed; the mark fades after a few seconds"));
    m_chkHighlight->setChecked(consoleSettings.value(QStringLiteral("lococonsole/highlightChanges"), true).toBool());
    top->addWidget(m_chkHighlight);
    connect(m_chkHighlight, &QCheckBox::toggled, this, [this](bool on) {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("lococonsole/highlightChanges"), on);
        highlightChanges(QDateTime::currentMSecsSinceEpoch());
    });

    root->addLayout(readouts);
    root->addLayout(top);
    connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged),
            this,       &LocoConsoleWindow::onSelectionChanged);
    connect(m_btnRecord, &QPushButton::clicked, this, &LocoConsoleWindow::onToggleRecord);
    connect(btnReplay,   &QPushButton::clicked, this, &LocoConsoleWindow::onOpenReplay);

    // ---- link and health lights, always shown (session 82) -------------------
    // Two rows: a loco with several faulting modules needs the width.
    m_linkLights = new LinkLights(central);
    m_linkLights->setObjectName(QStringLiteral("linkLights"));
    m_healthLights = new HealthLights(central);
    m_healthLights->setObjectName(QStringLiteral("healthLights"));
    root->addWidget(m_linkLights);
    root->addWidget(m_healthLights);

    // ---- cab view (session 82) ------------------------------------------------
    m_btnCab = new QPushButton(tr("Cab view"), central);
    m_btnCab->setCheckable(true);
    m_btnCab->setToolTip(tr("What the driver's DMI shows: speed dial with the permitted speed, distance to "
                            "the target, mode, brake, and the signals ahead"));
    top->insertWidget(top->indexOf(m_btnBigNumbers), m_btnCab);
    m_cab = new CabDisplay(central);
    root->addWidget(m_cab);
    const bool cabShown = consoleSettings.value(QStringLiteral("lococonsole/cabShown"), false).toBool();
    m_btnCab->setChecked(cabShown);
    m_cab->setVisible(cabShown);
    // Added fields (session 83): right-click the cab view.
    for (const QString &saved : consoleSettings.value(QStringLiteral("lococonsole/cabFields")).toStringList()) {
        const LiveFieldRef ref = LiveFieldRef::parse(saved);
        if (ref.isValid()) m_cabFields << ref;
    }
    m_cab->setContextMenuPolicy(Qt::CustomContextMenu);
    m_cab->setToolTip(tr("Right-click to add a field of your own to the cab view"));
    connect(m_cab, &QWidget::customContextMenuRequested, this, &LocoConsoleWindow::showCabMenu);
    connect(m_btnCab, &QPushButton::toggled, this, [this](bool on) {
        m_cab->setVisible(on);
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("lococonsole/cabShown"), on);
    });

    // ---- big numbers, above the tabs -------------------------------------
    m_bigNumbers = new BigNumberPanel(central);
    root->addWidget(m_bigNumbers);
    // Ctrl+Z takes back a removed tile or a reset (session 79). The window
    // has no menu bar; the action is added to the window so the key works.
    m_undo = new UndoLog(this);
    m_bigNumbers->setUndoLog(m_undo);
    m_actUndo = m_undo->createAction(this);
    addAction(m_actUndo);
    const bool bigShown = consoleSettings.value(QStringLiteral("lococonsole/bigNumbersShown"), false).toBool();
    m_btnBigNumbers->setChecked(bigShown);
    m_bigNumbers->setVisible(bigShown);
    connect(m_btnBigNumbers, &QPushButton::toggled, this, [this](bool on) {
        m_bigNumbers->setVisible(on);
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("lococonsole/bigNumbersShown"), on);
        m_dirty = true;
    });
    connect(m_bigNumbers, &BigNumberPanel::tilesChanged, this, [this]() { m_dirty = true; });
    // Double-click a tile: that field over time, for the selected loco.
    connect(m_bigNumbers, &BigNumberPanel::plotRequested, this, &LocoConsoleWindow::plotTile);

    // ---- tabs: Link overview + one per packet type ----------------------
    m_tabs = new QTabWidget(central);
    UiStyle::useTabTooltips(m_tabs);

    m_linkTable = new QTableWidget(0, 7, m_tabs);
    m_linkTable->setHorizontalHeaderLabels(
        {tr("Packet"), tr("Dir"), tr("Count"), tr("Rate/s"),
         tr("Last seen"), tr("Last seq"), tr("CRC")});
    m_linkTable->verticalHeader()->setVisible(false);
    m_linkTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_linkTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_linkTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tabs->addTab(m_linkTable, tr("Link"));

    for (int i = 0; i < kTabOrderCount; ++i) {
        addTypeTab(kTabOrder[i]);
    }

    root->addWidget(m_tabs);

    // ---- find bar under the tabs -----------------------------------------
    m_find = new TableFindBar(central);
    root->addWidget(m_find);
    QVector<QPair<QString, QTableWidget *>> allTables;
    for (int index = 0; index < m_tabs->count(); ++index) {
        if (auto *table = qobject_cast<QTableWidget *>(m_tabs->widget(index))) {
            allTables.append(qMakePair(m_tabs->tabText(index), table));
        }
    }
    m_find->setAllTables(allTables);
    connect(btnFind, &QPushButton::clicked, m_find, &TableFindBar::open);
    connect(m_tabs, &QTabWidget::currentChanged, this, &LocoConsoleWindow::onTabChanged);
    connect(m_find, &TableFindBar::switchToTable, this, [this](QTableWidget *table) {
        m_tabs->setCurrentWidget(table);
    });
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, m_find, &TableFindBar::open);
    auto *nextShortcut = new QShortcut(QKeySequence::FindNext, this);
    connect(nextShortcut, &QShortcut::activated, this, [this]() {
        if (m_find->isVisible()) {
            m_find->findNext();
        } else {
            m_find->open();
        }
    });
    auto *previousShortcut = new QShortcut(QKeySequence::FindPrevious, this);
    connect(previousShortcut, &QShortcut::activated, m_find, &TableFindBar::findPrevious);
    onTabChanged(m_tabs->currentIndex());
    connect(m_tags, &TabTags::changed, this, [this]() { refreshTagDisplay(); });
    UiColor::onThemeChange(this, [this]() { refreshTagDisplay(); });

    setCentralWidget(central);
}

void LocoConsoleWindow::onEntryAppended(QString tabKey, LogEntryPtr entry)
{
    if (entry.isNull()) { return; }

    const CaptureLine c = CaptureDecoder::parseLine(entry->text);
    if (!c.valid) { return; }

    if (m_recording) {                       // route to the matching per-source file
        const QString k = c.key();
        if (m_recAutoNew || m_recWanted.contains(k)) {
            routeRecord(k, entry->text);
        }
    }

    const qint64 nowMs = (entry->epochMs != 0)
                         ? entry->epochMs
                         : QDateTime::currentMSecsSinceEpoch();
    ingest(c, nowMs);

    const QString key = c.key();
    const bool newTabMapping = (m_tabKeyFor.value(key) != tabKey);
    m_tabKeyFor.insert(key, tabKey);
    if (m_selector->findText(key) < 0) {
        m_selector->addItem(key);
        if (m_selectedKey.isEmpty()) {
            m_selectedKey = key;
            m_selector->setCurrentText(key);
        }
        refreshTagDisplay();
    } else if (newTabMapping) {
        refreshTagDisplay();
    }
    if (key == m_selectedKey) { m_dirty = true; }
}

void LocoConsoleWindow::ingest(const CaptureLine &c, qint64 nowMs)
{
    LocoState &st = m_states[c.key()];
    st.locoId = c.locoId;
    st.ctrlId = c.ctrlId;
    if (c.rtc.isValid()) { st.rtc = c.rtc; }
    st.total++;

    if (st.seqInit) {
        const quint32 expected = st.lastSeq + 1u;
        if (c.seq > expected) { st.seqGaps += (c.seq - expected); }
    }
    st.lastSeq = c.seq;
    st.seqInit = true;

    LinkStat &ls = st.link[int(c.type)];
    ls.count++;
    // This type's own rhythm, for its heartbeat light: a running average of
    // the gap, so one late frame does not redefine "usual".
    if (ls.lastSeenMs > 0) {
        const double gap = double(nowMs - ls.lastSeenMs);
        ls.intervalMs = ls.intervalMs <= 0.0 ? gap : ls.intervalMs * 0.8 + gap * 0.2;
    }
    ls.lastSeenMs = nowMs;
    ls.lastSeq    = c.seq;
    if (c.crcChecked) { if (c.crcOk) { ls.crcOkCount++; } else { ls.crcFail++; } }

    st.latest[int(c.type)] = c;          // keep the whole frame for the field tab

    // Session 168: start of mission. Only ARP is decoded here (~1 frame/s).
    if (c.type == CapType::ARP) {
        QHash<QString, qint64> raw;
        const QVector<FieldRow> rows = CaptureDecoder::describe(c, nullptr, 0, &raw);
        const bool som = CaptureDecoder::isStartOfMission(raw);
        const QDateTime at = c.rtc.isValid() ? c.rtc : QDateTime::fromMSecsSinceEpoch(nowMs);
        if (som && !st.inMissionStart) {
            st.missionFrom = at;
            st.missionThen.clear();
        }
        if (som) st.missionTo = at;
        if (!som && st.inMissionStart) {
            for (const FieldRow &r : rows) {
                if (r.field.trimmed() == QLatin1String("LOCO_MODE")) { st.missionThen = r.value.trimmed(); break; }
            }
        }
        st.inMissionStart = som;
    }
}

void LocoConsoleWindow::onSelectionChanged(int index)
{
    Q_UNUSED(index);
    m_selectedKey = m_selector->currentText();
    m_dirty = true;
    // Another loco: its values are not "changes" from the last one's.
    m_lastValues.clear();
    m_changedAt.clear();
    refreshTagDisplay();
}

void LocoConsoleWindow::refreshTagDisplay()
{
    TabTags *tags = m_tags;
    for (int index = 0; index < m_selector->count(); ++index) {
        const QString tabKey = m_tabKeyFor.value(m_selector->itemText(index));
        m_selector->setItemIcon(index, tags->dotIcon(tabKey));
    }
    // Title and icon name the loco being watched, so a minimised console's
    // chip says which one it is.
    if (m_selectedKey.isEmpty()) {
        setWindowTitle(tr("Live Loco Console"));
        setWindowIcon(QIcon());
        return;
    }
    const QString tabKey = m_tabKeyFor.value(m_selectedKey);
    setWindowTitle(tr("Live Loco Console — %1").arg(tags->decoratedName(tabKey, m_selectedKey)));
    setWindowIcon(tags->dotIcon(tabKey));
}

void LocoConsoleWindow::onRefreshTick()
{
    const double dt = double(kRefreshMs) / 1000.0;
    for (LocoState &st : m_states) {
        for (LinkStat &ls : st.link) {
            const quint64 delta = ls.count - ls.prevCount;
            ls.prevCount = ls.count;
            ls.rate = double(delta) / dt;
        }
    }

    if (m_selectedKey.isEmpty() || !m_states.contains(m_selectedKey)) { return; }
    const LocoState &st = m_states[m_selectedKey];
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    refreshHeader(st, nowMs);
    refreshLink(st, nowMs);              // ages tick every refresh
    if (m_dirty) {
        refreshTypeTables(st, nowMs);
        m_dirty = false;
    }
    refreshBigNumbers(st, nowMs);
    refreshCabAndLights(st, nowMs);
    // Change marks every tick (they fade), then the find's tint on top, so
    // a match found by Find is never hidden by a change mark.
    highlightChanges(nowMs);
    // The tables were just rebuilt: put the find's tint, hidden rows and
    // current match back (by field name; no scrolling).
    m_find->reapply();
}

void LocoConsoleWindow::refreshBigNumbers(const LocoState &st, qint64 nowMs)
{
    if (!m_bigNumbers->isVisible()) {
        return;
    }
    // describe() once per type per tick, however many tiles use it.
    QHash<int, QVector<FieldRow>> described;
    QVector<BigNumberPanel::TileValue> values;
    // When each type last arrived: a tile reads the FRESHEST of its sources
    // that has the field, not the first listed (see LiveFields::byFreshness).
    QHash<int, qint64> lastSeen;
    for (auto it = st.link.constBegin(); it != st.link.constEnd(); ++it) lastSeen.insert(it.key(), it.value().lastSeenMs);
    for (const LiveFieldRef &ref : m_bigNumbers->tiles()) {
        BigNumberPanel::TileValue value;
        for (const QString &source : LiveFields::byFreshness(ref.sources, lastSeen)) {
            CapType type = CapType::Unknown;
            QString field;
            if (!LiveFields::parseSource(source, &type, &field) || !st.latest.contains(int(type))) {
                continue;
            }
            if (!described.contains(int(type))) {
                described.insert(int(type), CaptureDecoder::describe(st.latest[int(type)]));
            }
            const QString text = LiveFields::valueIn(described.value(int(type)), field);
            if (text.isNull()) {
                continue;
            }
            const QString token = source.section(QLatin1Char(':'), 0, 0);
            value.missing = false;
            value.text = text;
            value.detail = tr("from %1").arg(token);
            bool numeric = false;
            value.number = parseFieldNumber(text, &numeric);
            value.hasNumber = numeric;
            if (LiveFields::isFrameNumber(field)) {
                const QString clock = LiveFields::frameClock(text);
                if (!clock.isEmpty()) {
                    value.text = clock;
                    value.detail = tr("FRAME_NUM %1 · %2").arg(text.trimmed(), token);
                    value.hasNumber = false;      // a clock: no sparkline
                    value.trackState = false;     // and it changes every second
                }
            }
            // The tile's colour rule (session 82). A relative rule reads its
            // limit from the same loco's latest frame of the named type.
            if (value.hasNumber && ref.rule.isSet()) {
                bool haveRef = false;
                double limit = 0.0;
                if (ref.rule.kind == TileRule::Kind::Relative) {
                    CapType refType = CapType::Unknown;
                    QString refField;
                    if (LiveFields::parseSource(ref.rule.refSource, &refType, &refField)
                        && st.latest.contains(int(refType))) {
                        if (!described.contains(int(refType))) {
                            described.insert(int(refType), CaptureDecoder::describe(st.latest[int(refType)]));
                        }
                        const QString refText = LiveFields::valueIn(described.value(int(refType)), refField);
                        limit = parseFieldNumber(refText, &haveRef);
                        // An old limit is not the limit now.
                        if (haveRef && nowMs - st.link.value(int(refType)).lastSeenMs > 3000) haveRef = false;
                    }
                    if (haveRef) value.detail += tr(" · limit %1").arg(limit, 0, 'g', 6);
                }
                value.level = ref.rule.level(value.number, haveRef, limit);
            }
            const qint64 ageMs = nowMs - st.link.value(int(type)).lastSeenMs;
            if (ageMs > 3000) {
                value.stale = true;
                value.detail += tr(" · %1 ago").arg(ageText(ageMs));
            }
            break;   // first source that has it wins
        }
        if (value.missing) {
            value.detail = tr("not received yet");
        }
        values.append(value);
    }
    m_bigNumbers->setValues(values);
}

void LocoConsoleWindow::refreshCabAndLights(const LocoState &st, qint64 nowMs)
{
    // ---- heartbeats: the types this loco sends, in the tabs' order -------------
    QVector<Heartbeat> beats;
    for (auto it = st.link.constBegin(); it != st.link.constEnd(); ++it) {
        const LinkStat &ls = it.value();
        if (ls.count == 0) continue;
        Heartbeat b;
        // The name the tabs use ("nms fault"), not the capture token, which
        // for a few types does not round-trip and would read "#6".
        b.name = QString::fromLatin1(CaptureDecoder::typeLabel(CapType(it.key())));
        b.rate = ls.rate;
        b.ageMs = nowMs - ls.lastSeenMs;
        b.intervalMs = qint64(ls.intervalMs);
        b.state = heartbeatState(b.ageMs, b.intervalMs);
        b.pulse = b.ageMs < kRefreshMs;
        beats << b;
    }
    m_linkLights->setBeats(beats);

    // ---- the DMI and SLRP frames, decoded with raw values ------------------------
    auto decode = [&st](CapType t, QHash<QString, qint64> *raw, QHash<QString, QString> *text) {
        if (!st.latest.contains(int(t))) return false;
        for (const FieldRow &r : CaptureDecoder::describe(st.latest[int(t)], nullptr, 0, raw)) {
            const QString f = r.field.trimmed();
            if (!text->contains(f)) text->insert(f, r.value);
        }
        return true;
    };
    QHash<QString, qint64> dmiRaw, slrpRaw;
    QHash<QString, QString> dmiText, slrpText;
    const bool dmiSeen = decode(CapType::Dmi, &dmiRaw, &dmiText);
    const bool slrpSeen = decode(CapType::SLRP, &slrpRaw, &slrpText);
    const bool slrpFresh = slrpSeen && nowMs - st.link.value(int(CapType::SLRP)).lastSeenMs <= 10000;

    if (m_cab->isVisible()) {
        // Added fields, each from its freshest source (as the tiles).
        QHash<int, qint64> lastSeen;
        for (auto it = st.link.constBegin(); it != st.link.constEnd(); ++it) lastSeen.insert(it.key(), it.value().lastSeenMs);
        QHash<int, QVector<FieldRow>> described;
        QVector<CabDisplay::Extra> extras;
        for (const LiveFieldRef &ref : m_cabFields) {
            CabDisplay::Extra e;
            e.label = ref.label;
            e.value = QStringLiteral("—");
            e.stale = true;
            for (const QString &source : LiveFields::byFreshness(ref.sources, lastSeen)) {
                CapType type = CapType::Unknown;
                QString field;
                if (!LiveFields::parseSource(source, &type, &field) || !st.latest.contains(int(type))) continue;
                if (!described.contains(int(type))) described.insert(int(type), CaptureDecoder::describe(st.latest[int(type)]));
                const QString value = LiveFields::valueIn(described.value(int(type)), field);
                if (value.isNull()) continue;
                e.value = value;
                e.stale = nowMs - st.link.value(int(type)).lastSeenMs > kStaleMs;
                break;
            }
            extras << e;
        }
        m_cab->setExtras(extras);
        CabState cab = cabStateFrom(dmiRaw, dmiText, slrpFresh ? slrpRaw : QHash<QString, qint64>(),
                                    slrpFresh ? slrpText : QHash<QString, QString>());
        cab.stale = dmiSeen && nowMs - st.link.value(int(CapType::Dmi)).lastSeenMs > 3000;
        m_cab->setState(cab);
    }

    // ---- health: DMI bits and the latest NMS fault report ---------------------------
    QHash<QString, QStringList> faults;
    const bool nmsSeen = st.latest.contains(int(CapType::NmsFault));
    if (nmsSeen) {
        for (const ActiveFaultInfo &f : CaptureDecoder::faultsOf(st.latest[int(CapType::NmsFault)])) {
            const QString module = f.moduleName.isEmpty() ? f.subsystemName : f.moduleName;
            faults[module] << f.faultName;
        }
    }
    m_healthLights->setLights(healthLightsFrom(dmiRaw, dmiSeen, faults, nmsSeen));
}

bool LocoConsoleWindow::addCabField(const LiveFieldRef &ref)
{
    if (!ref.isValid()) return false;
    for (const LiveFieldRef &r : m_cabFields) if (r.sources == ref.sources) return false;
    m_cabFields << ref;
    saveCabFields();
    m_dirty = true;
    return true;
}

bool LocoConsoleWindow::removeCabField(int index)
{
    if (index < 0 || index >= m_cabFields.size()) return false;
    m_cabFields.removeAt(index);
    saveCabFields();
    m_dirty = true;
    return true;
}

void LocoConsoleWindow::saveCabFields() const
{
    QStringList out;
    for (const LiveFieldRef &r : m_cabFields) out << r.serialise();
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("lococonsole/cabFields"), out);
}

void LocoConsoleWindow::showCabMenu(const QPoint &pos)
{
    QMenu menu(this);
    // Add field ▸ <packet> ▸ <field>, from what THIS loco has sent.
    QMenu *add = menu.addMenu(tr("Add field"));
    const LocoState st = m_states.value(m_selectedKey);
    QList<int> types = st.latest.keys();
    std::sort(types.begin(), types.end(), [](int a, int b) {
        return LiveFields::tokenFor(CapType(a)) < LiveFields::tokenFor(CapType(b));
    });
    for (int t : types) {
        const QString token = LiveFields::tokenFor(CapType(t));
        if (token.isEmpty()) continue;
        QMenu *sub = add->addMenu(token);
        QStringList seen;
        for (const FieldRow &r : CaptureDecoder::describe(st.latest[t])) {
            if (!r.field.startsWith(QLatin1Char(' '))) continue;   // header rows (type, seq, CRC)
            const QString field = r.field.trimmed();
            if (field.isEmpty() || seen.contains(field)) continue;
            seen << field;
            QAction *a = sub->addAction(QStringLiteral("%1   %2").arg(field, r.value.trimmed().left(24)));
            connect(a, &QAction::triggered, this, [this, token, field]() {
                LiveFieldRef ref;
                ref.label = field;
                ref.sources = QStringList{ token + QLatin1Char(':') + field };
                addCabField(ref);
            });
        }
    }
    if (types.isEmpty()) add->addAction(tr("(nothing received from this loco yet)"))->setEnabled(false);
    if (!m_cabFields.isEmpty()) {
        QMenu *remove = menu.addMenu(tr("Remove field"));
        for (int i = 0; i < m_cabFields.size(); ++i) {
            QAction *a = remove->addAction(QStringLiteral("%1  (%2)").arg(m_cabFields.at(i).label, m_cabFields.at(i).sources.join(QStringLiteral(", "))));
            connect(a, &QAction::triggered, this, [this, i]() { removeCabField(i); });
        }
        menu.addAction(tr("Remove all added fields"), this, [this]() {
            m_cabFields.clear();
            saveCabFields();
            m_dirty = true;
        });
    }
    menu.exec(m_cab->mapToGlobal(pos));
}

void LocoConsoleWindow::selectSource(const QString &key)
{
    const int i = m_selector->findData(key);
    if (i >= 0) m_selector->setCurrentIndex(i);
    else if ((m_selector->findText(key)) >= 0) m_selector->setCurrentIndex(m_selector->findText(key));
}

void LocoConsoleWindow::plotTile(int index)
{
    const QVector<LiveFieldRef> tiles = m_bigNumbers->tiles();
    if (index < 0 || index >= tiles.size() || m_selectedKey.isEmpty() || !m_dispatcher) return;
    LogModel *model = m_dispatcher->modelForKey(m_selectedKey);
    if (!model) return;
    // The first source this loco has actually sent; else the first listed.
    const LocoState st = m_states.value(m_selectedKey);
    QHash<int, qint64> lastSeen;
    for (auto it = st.link.constBegin(); it != st.link.constEnd(); ++it) lastSeen.insert(it.key(), it.value().lastSeenMs);
    QString type, field;
    for (const QString &source : LiveFields::byFreshness(tiles.at(index).sources, lastSeen)) {
        CapType t = CapType::Unknown;
        QString f;
        if (!LiveFields::parseSource(source, &t, &f)) continue;
        if (type.isEmpty() || st.latest.contains(int(t))) {
            type = source.section(QLatin1Char(':'), 0, 0);
            field = f;
            if (st.latest.contains(int(t))) break;
        }
    }
    if (field.isEmpty()) return;
    auto *plot = new FieldPlotWindow(model, m_selectedKey, this);
    connect(plot, &FieldPlotWindow::jumpRequested, this, &LocoConsoleWindow::jumpRequested);
    plot->plotField(field, type);
    // A relative rule's limit belongs beside it: speed with its permitted speed.
    const TileRule rule = tiles.at(index).rule;
    if (rule.kind == TileRule::Kind::Relative) {
        plot->addField(rule.refSource.section(QLatin1Char(':'), 1), rule.refSource.section(QLatin1Char(':'), 0, 0));
    }
    plot->show();
    plot->raise();
}

void LocoConsoleWindow::highlightChanges(qint64 nowMs)
{
    const bool on = m_chkHighlight->isChecked();
    QColor mark = UiColor::accent();
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    int fullAlpha = 60;
    if (dark) {
        fullAlpha = 90;
    }
    for (auto it = m_typeTables.constBegin(); it != m_typeTables.constEnd(); ++it) {
        QTableWidget *table = it.value();
        const QHash<QString, qint64> changed = m_changedAt.value(it.key());
        QHash<QString, int> seen;
        for (int row = 0; row < table->rowCount(); ++row) {
            QTableWidgetItem *name = table->item(row, 0);
            QTableWidgetItem *value = table->item(row, 1);
            if (name == nullptr || value == nullptr) {
                continue;
            }
            const QString field = name->text().trimmed();
            const QString rowKey = field + QLatin1Char('#') + QString::number(seen.value(field));
            seen[field] += 1;
            const qint64 ageMs = nowMs - changed.value(rowKey, -1000000);
            int alpha = 0;
            if (on && ageMs >= 0 && ageMs < kChangeHoldMs) {
                alpha = fullAlpha;
            } else if (on && ageMs >= kChangeHoldMs && ageMs < kChangeFadeMs) {
                alpha = fullAlpha * (kChangeFadeMs - ageMs) / (kChangeFadeMs - kChangeHoldMs);
            }
            QBrush brush;
            if (alpha > 0) {
                QColor tint = mark;
                tint.setAlpha(alpha);
                brush = QBrush(tint);
            }
            name->setBackground(brush);
            value->setBackground(brush);
            QFont font = value->font();
            font.setBold(alpha > 0 && ageMs < kChangeHoldMs);
            value->setFont(font);
        }
    }
}

bool LocoConsoleWindow::isRowHighlighted(const QString &typeLabel, int row) const
{
    QTableWidget *table = tableForLabel(typeLabel);
    if (table == nullptr || table->item(row, 1) == nullptr) {
        return false;
    }
    return table->item(row, 1)->background().style() != Qt::NoBrush;
}

void LocoConsoleWindow::showFieldMenu(CapType type, QTableWidget *table, const QPoint &pos)
{
    const int row = table->rowAt(pos.y());
    if (row < 0 || table->item(row, 0) == nullptr || table->item(row, 1) == nullptr) {
        return;
    }
    const QString field = table->item(row, 0)->text().trimmed();
    const QString value = table->item(row, 1)->text();
    if (field.isEmpty() || field.startsWith(QLatin1Char('('))) {
        return;   // "(no data yet)"
    }
    QMenu menu(this);
    QAction *big = menu.addAction(tr("Show as big number"));
    QAction *pin = menu.addAction(tr("Pin to status bar"));
    pin->setEnabled(StatusPins::instance() != nullptr && !m_selectedKey.isEmpty());
    menu.addSeparator();
    QAction *copy = menu.addAction(tr("Copy value"));
    QAction *chosen = menu.exec(table->viewport()->mapToGlobal(pos));

    const LiveFieldRef ref{ field, { LiveFields::sourceFor(type, field) } };
    if (chosen == big) {
        m_bigNumbers->addTile(ref);
        m_btnBigNumbers->setChecked(true);   // show the panel it went to
    } else if (chosen == pin) {
        if (!StatusPins::instance()->addPin(m_selectedKey, ref, value)) {
            QMessageBox::information(this, windowTitle(),
                                     tr("Not pinned: it is already pinned, or %1 values are pinned already.")
                                         .arg(StatusPins::kMaxPins));
        }
    } else if (chosen == copy) {
        QApplication::clipboard()->setText(value);
    }
}

void LocoConsoleWindow::onTabChanged(int index)
{
    m_find->setTable(qobject_cast<QTableWidget *>(m_tabs->widget(index)));
}

QTableWidget *LocoConsoleWindow::tableForLabel(const QString &label) const
{
    for (int index = 0; index < m_tabs->count(); ++index) {
        if (m_tabs->tabText(index) == label) {
            return qobject_cast<QTableWidget *>(m_tabs->widget(index));
        }
    }
    return nullptr;
}

QString LocoConsoleWindow::ageText(qint64 ageMs)
{
    return UiStyle::durationText(ageMs);      // one format for durations everywhere (session 124)
}

void LocoConsoleWindow::refreshHeader(const LocoState &st, qint64 nowMs)
{
    m_lblRtc->setText(st.rtc.isValid()
                      ? tr("RTC %1").arg(st.rtc.toString("yyyy-MM-dd HH:mm:ss"))
                      : tr("RTC --"));

    double totalRate = 0.0;
    qint64 newestSeen = 0;
    quint64 crcOk = 0, crcFail = 0;
    for (auto it = st.link.constBegin(); it != st.link.constEnd(); ++it) {
        totalRate += it.value().rate;
        newestSeen = qMax(newestSeen, it.value().lastSeenMs);
        crcOk   += it.value().crcOkCount;
        crcFail += it.value().crcFail;
    }
    m_lblRate->setText(tr("%1 pkt/s").arg(totalRate, 0, 'f', 0));

    const quint64 crcTot = crcOk + crcFail;
    // Chips by tone (session 124): "CRC 195/204 ok" was painted red, which
    // said "fail" in a sentence that said "ok". Now the count of failures
    // is in the words and the tone.
    m_lblCrc->setText(!crcTot ? tr("CRC --")
                      : crcFail ? tr("\u2715 CRC %1 failed of %2").arg(crcFail).arg(crcTot)
                                : tr("\u2713 CRC %1/%2").arg(crcOk).arg(crcTot));
    UiStyle::setTone(m_lblCrc, !crcTot ? UiStyle::Tone::Neutral : crcFail ? UiStyle::Tone::Fail : UiStyle::Tone::Ok);

    // Session 168: start of mission.
    {
        const QString hms = QStringLiteral("HH:mm:ss");
        const QString what = tr("ARP in Stand_By with no direction, no RFID tag and no location");
        if (!st.missionFrom.isValid()) {
            m_lblMission->setText(tr("start of mission --"));
            m_lblMission->setToolTip(tr("No start of mission seen from this loco yet (%1).").arg(what));
            UiStyle::setTone(m_lblMission, UiStyle::Tone::Neutral);
        } else if (st.inMissionStart) {
            m_lblMission->setText(tr("\u23F5 at start of mission since %1").arg(st.missionFrom.toString(hms)));
            m_lblMission->setToolTip(tr("At start of mission: every ARP since %1 is %2.").arg(st.missionFrom.toString(hms), what));
            UiStyle::setTone(m_lblMission, UiStyle::Tone::Accent);
        } else {
            QString mode = st.missionThen;
            const int open = mode.indexOf(QLatin1Char('(')), close = mode.lastIndexOf(QLatin1Char(')'));
            if (open > 0 && close > open) mode = mode.mid(open + 1, close - open - 1);
            m_lblMission->setText(tr("mission started %1 \u2192 %2").arg(st.missionFrom.toString(hms), mode));
            m_lblMission->setToolTip(tr("Start of mission from %1 to %2 (%3); the next ARP reported %4.")
                                         .arg(st.missionFrom.toString(hms), st.missionTo.toString(hms), what, st.missionThen));
            UiStyle::setTone(m_lblMission, UiStyle::Tone::Neutral);
        }
    }

    m_lblSeq->setText(tr("seq %1 \u00B7 gaps %2").arg(st.lastSeq).arg(st.seqGaps));
    UiStyle::setTone(m_lblSeq, st.seqGaps ? UiStyle::Tone::Warn : UiStyle::Tone::Neutral);

    const qint64 age = (newestSeen > 0) ? (nowMs - newestSeen) : -1;
    const QString live = (age < 0) ? tr("offline")
                       : (age > kDeadMs) ? tr("offline %1").arg(ageText(age))
                       : tr("live");
    setWindowTitle(tr("Live Loco Console  \u2014  %1  (%2)").arg(m_selectedKey, live));
}

void LocoConsoleWindow::refreshLink(const LocoState &st, qint64 nowMs)
{
    int rows = 0;
    for (int i = 0; i < kLinkOrderCount; ++i) {
        if (st.link.contains(int(kLinkOrder[i]))) { ++rows; }
    }
    m_linkTable->setRowCount(rows);

    int r = 0;
    for (int i = 0; i < kLinkOrderCount; ++i) {
        const CapType t = kLinkOrder[i];
        if (!st.link.contains(int(t))) { continue; }
        const LinkStat &ls = st.link[int(t)];
        const qint64 age = nowMs - ls.lastSeenMs;

        auto cell = [&](int col, const QString &txt) {
            QTableWidgetItem *it = new QTableWidgetItem(txt);
            m_linkTable->setItem(r, col, it);
            return it;
        };
        cell(0, QString::fromLatin1(CaptureDecoder::typeLabel(t)));
        cell(1, QString::fromLatin1(dirText(CaptureDecoder::directionFor(t))));
        cell(2, QString::number(ls.count));
        cell(3, QString::number(ls.rate, 'f', 1));
        QTableWidgetItem *seenIt = cell(4, ageText(age));
        cell(5, QString::number(ls.lastSeq));

        QString crcTxt; QColor crcCol = dimColor();
        if (ls.crcFail)        { crcTxt = tr("FAIL %1").arg(ls.crcFail); crcCol = failColor(); }
        else if (ls.crcOkCount){ crcTxt = tr("ok");                      crcCol = okColor(); }
        else                   { crcTxt = tr("--"); }
        cell(6, crcTxt)->setForeground(crcCol);

        if (age > kDeadMs)       { seenIt->setForeground(failColor()); }
        else if (age > kStaleMs) { seenIt->setForeground(warnColor()); }
        ++r;
    }
}

void LocoConsoleWindow::refreshTypeTables(const LocoState &st, qint64 nowMs)
{
    for (auto it = m_typeTables.begin(); it != m_typeTables.end(); ++it) {
        const int     t   = it.key();
        QTableWidget *tbl = it.value();

        if (!st.latest.contains(t)) {
            tbl->setRowCount(1);
            tbl->setItem(0, 0, new QTableWidgetItem(tr("(no data yet)")));
            tbl->setItem(0, 1, new QTableWidgetItem(QString()));
            continue;
        }

        const QVector<FieldRow> rows = CaptureDecoder::describe(st.latest[t], nullptr, 0, nullptr, m_keys);
        // Change tracking: compare with this table's previous values. The
        // first frame of a type marks nothing (there is no "before").
        {
            // Stamped with the TICK's time (session 93), the same clock the
            // highlight is judged by a moment later. Reading the clock again
            // here made a change look 1 ms in the future on a busy PC, so
            // it went unmarked until the next tick.
            QHash<QString, QString> &previous = m_lastValues[t];
            QHash<QString, qint64> &changedAt = m_changedAt[t];
            const bool hadBefore = !previous.isEmpty();
            QHash<QString, int> seen;
            QHash<QString, QString> current;
            for (const FieldRow &row : rows) {
                const QString field = row.field.trimmed();
                const QString rowKey = field + QLatin1Char('#') + QString::number(seen.value(field));
                seen[field] += 1;
                current.insert(rowKey, row.value);
                if (hadBefore && previous.contains(rowKey) && previous.value(rowKey) != row.value) {
                    changedAt.insert(rowKey, nowMs);
                }
            }
            previous = current;
        }
        tbl->setRowCount(rows.size());
        for (int r = 0; r < rows.size(); ++r) {
            tbl->setItem(r, 0, new QTableWidgetItem(rows.at(r).field));
            QTableWidgetItem *v = new QTableWidgetItem(rows.at(r).value);
            if (rows.at(r).field == QLatin1String("CRC")) {
                if (rows.at(r).value == QLatin1String("FAIL")) { v->setForeground(failColor()); }
                else if (rows.at(r).value == QLatin1String("PASS")) { v->setForeground(okColor()); }
            }
            tbl->setItem(r, 1, v);
        }
    }
}

// ============================ recording ============================

void LocoConsoleWindow::onToggleRecord()
{
    if (m_recording) { stopRecording(); return; }

    QSet<QString> keys;
    bool autoNew = false;
    if (!promptRecordSelection(keys, autoNew)) { return; }   // cancelled
    if (keys.isEmpty() && !autoNew) {                        // nothing to record
        QMessageBox::information(this, tr("Record"),
            tr("No sources selected."));
        return;
    }
    startRecording(keys, autoNew);
}

bool LocoConsoleWindow::promptRecordSelection(QSet<QString> &outKeys, bool &outAutoNew)
{
    QStringList keys;
    for (int i = 0; i < m_selector->count(); ++i) { keys << m_selector->itemText(i); }
    keys.sort();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Record sources"));
    QVBoxLayout *lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(
        tr("Choose which loco / controller streams to record.\n"
           "Each selected source is written to its own .cap file."), &dlg));

    QCheckBox *cbAll = new QCheckBox(tr("Select all current sources"), &dlg);
    lay->addWidget(cbAll);

    QScrollArea *scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(160);
    QWidget *list = new QWidget(scroll);
    QVBoxLayout *listLay = new QVBoxLayout(list);
    listLay->setContentsMargins(8, 4, 8, 4);

    QVector<QCheckBox*> boxes;
    for (const QString &k : keys) {
        QCheckBox *cb = new QCheckBox(k, list);
        if (k == m_selectedKey) { cb->setChecked(true); }
        listLay->addWidget(cb);
        boxes.push_back(cb);
    }
    listLay->addStretch(1);
    scroll->setWidget(list);
    lay->addWidget(scroll);

    if (keys.isEmpty()) {
        QLabel *none = new QLabel(
            tr("(no sources seen yet \u2014 enable the option below to capture "
               "whatever arrives)"), &dlg);
        none->setStyleSheet(UiColor::mutedStyle());
        none->setWordWrap(true);
        lay->addWidget(none);
    }

    QCheckBox *cbAuto = new QCheckBox(
        tr("Also capture sources that appear after recording starts"), &dlg);
    lay->addWidget(cbAuto);

    connect(cbAll, &QCheckBox::toggled, &dlg, [&boxes](bool on){
        for (QCheckBox *cb : boxes) { cb->setChecked(on); }
    });

    QDialogButtonBox *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) { return false; }

    outKeys.clear();
    for (QCheckBox *cb : boxes) {
        if (cb->isChecked()) { outKeys.insert(cb->text()); }
    }
    outAutoNew = cbAuto->isChecked();
    return true;
}

void LocoConsoleWindow::startRecording(const QSet<QString> &keys, bool autoNew)
{
    const QString dir = replayDirPath();
    if (dir.isEmpty()) {
        QMessageBox::warning(this, tr("Record"),
            tr("Could not create the \"%1\" folder for captures.")
                .arg(QString::fromLatin1(kReplaySubdir)));
        return;
    }

    m_recDir      = dir;
    m_recStamp    = QDateTime::currentDateTime().toString(QStringLiteral("ddMMyyyy_HHmmss"));
    m_recWanted   = keys;
    m_recAutoNew  = autoNew;
    m_recSinks.clear();
    m_recording   = true;
    updateRecordButton();
}

QString LocoConsoleWindow::replayDirPath()
{
    QDir dir(QString::fromLatin1(kReplaySubdir));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return QString();
    }
    return dir.absolutePath();
}

void LocoConsoleWindow::routeRecord(const QString &key, const QString &rawLine)
{
    auto it = m_recSinks.find(key);
    if (it == m_recSinks.end()) {
        // Lazily open a file for this source on first sight.
        RecordSink s;
        s.file = new QFile(sinkPathFor(key));
        if (s.file->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            s.stream = new QTextStream(s.file);
            it = m_recSinks.insert(key, s);
            updateRecordButton();            // active-file count changed
        } else {
            delete s.file;                   // open failed
            s.file = nullptr;
            it = m_recSinks.insert(key, s);  // null sink: suppress retry every line
        }
    }
    if (it.value().stream) {
        *it.value().stream << rawLine.trimmed() << '\n';
    }
}

QString LocoConsoleWindow::sinkPathFor(const QString &key) const
{
    QString safe;
    safe.reserve(key.size());
    for (const QChar ch : key) {
        safe += (ch.isLetterOrNumber() || ch == QLatin1Char('_')) ? ch : QLatin1Char('_');
    }
    // <replay>/<stem>_<loco_ctrl>_<ddMMyyyy_HHmmss>.cap — stamp fixed per
    // session, so files never clobber a previous recording.
    return m_recDir + QLatin1Char('/')
           + QString::fromLatin1(kCapStem)
           + QLatin1Char('_') + safe
           + QLatin1Char('_') + m_recStamp
           + QStringLiteral(".cap");
}

void LocoConsoleWindow::stopRecording()
{
    QStringList written;
    for (auto it = m_recSinks.begin(); it != m_recSinks.end(); ++it) {
        RecordSink &s = it.value();
        if (s.stream) { s.stream->flush(); delete s.stream; s.stream = nullptr; }
        if (s.file) {
            written << s.file->fileName();
            s.file->close();
            delete s.file;
            s.file = nullptr;
        }
    }
    m_recSinks.clear();
    m_recWanted.clear();
    m_recAutoNew = false;
    m_recording  = false;
    updateRecordButton();

    if (!written.isEmpty()) {
        written.sort();
        QMessageBox::information(this, tr("Recording saved"),
            tr("Saved %1 capture file(s):\n%2")
                .arg(written.size()).arg(written.join(QLatin1Char('\n'))));
    }
}

void LocoConsoleWindow::updateRecordButton()
{
    if (!m_recording) {
        m_btnRecord->setText(tr("\u25CF Record"));
        m_btnRecord->setStyleSheet(QString());
        return;
    }
    int active = 0;
    for (const RecordSink &s : m_recSinks) {
        if (s.stream) { ++active; }
    }
    m_btnRecord->setText(tr("\u25A0 Stop (%1)").arg(active));
    m_btnRecord->setStyleSheet(UiColor::errorStyle() + QStringLiteral(" font-weight:bold;"));
}

void LocoConsoleWindow::onOpenReplay()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open capture(s) for replay \u2014 select several to compare sources"),
        replayDirPath(), tr("Capture files (*.cap);;All files (*)"));
    if (paths.isEmpty()) { return; }
    ReplayWindow *w = new ReplayWindow(paths, nullptr);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->show();
}

void LocoConsoleWindow::closeEvent(QCloseEvent *event)
{
    // These windows are WA_DeleteOnClose, so this is the last
    // point at which the geometry still exists to be read.
    WindowGeometry::save(this, QStringLiteral("locoConsole"));
    QMainWindow::closeEvent(event);
}
