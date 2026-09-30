#include "brakingpanel.h"

#include "brakingcurveplot.h"
#include "capturedecoder.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "uicolors.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QRegExp>
#include <QSlider>
#include <QSpinBox>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>

// A one-line map of the whole session: a tick wherever the curve changed, and
// a cursor at the frame being shown. The point is to answer "is anything
// happening in this capture" at a glance — a strip with one tick at the far
// left says 4694 frames of nothing far faster than dragging the scrubber does.
class ChangeStrip : public QWidget
{
public:
    explicit ChangeStrip(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(16);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tr("Every curve change in the session. Click to jump."));
    }

    void setData(int total, const QVector<int> &changes, int cursor)
    { m_total = total; m_changes = changes; m_cursor = cursor; update(); }

    // Emitted through the parent panel rather than a signal, to keep this
    // class private to the .cpp.
    std::function<void(int)> onJump;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRectF r = rect().adjusted(0, 3, -1, -3);
        p.setPen(Qt::NoPen);
        p.setBrush(UiColor::grid());
        p.drawRect(r);
        if (m_total <= 1) { return; }

        p.setPen(QPen(UiColor::accent(), 1));
        for (int idx : m_changes) {
            const double x = r.left() + r.width() * double(idx) / double(m_total - 1);
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        }
        if (m_cursor >= 0) {
            const double x = r.left() + r.width() * double(m_cursor) / double(m_total - 1);
            p.setPen(QPen(palette().color(QPalette::WindowText), 2));
            p.drawLine(QPointF(x, rect().top()), QPointF(x, rect().bottom()));
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (m_total <= 1 || !onJump) { return; }
        const double f = double(e->pos().x()) / double(qMax(1, width() - 1));
        onJump(qBound(0, int(f * (m_total - 1) + 0.5), m_total - 1));
    }

private:
    int m_total = 0;
    QVector<int> m_changes;
    int m_cursor = -1;
};

namespace {

QString mps(double v)
{
    return QStringLiteral("%1 m/s  (%2 km/h)")
        .arg(v, 0, 'f', 4)
        .arg(v * Braking::kMpsToKmph, 0, 'f', 1);
}

QTableWidget *makeTable(const QStringList &headers)
{
    auto *t = new QTableWidget;
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

void setCell(QTableWidget *t, int row, int col, const QString &text,
             const QColor &fg = QColor())
{
    auto *item = new QTableWidgetItem(text);
    if (fg.isValid()) { item->setForeground(fg); }
    t->setItem(row, col, item);
}

} // namespace

BrakingPanel::BrakingPanel(MessageDispatcher *dispatcher, QWidget *parent)
    : QMainWindow(parent)
    , m_dispatcher(dispatcher)
{
    setWindowTitle(tr("Braking Curves"));
    resize(1080, 720);

    auto *central = new QWidget(this);
    auto *outer   = new QVBoxLayout(central);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    // ---- top row: which loco, reload -------------------------------------
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Tab:")));
        m_tabCombo = new QComboBox;
        m_tabCombo->setMinimumWidth(180);
        row->addWidget(m_tabCombo);

        auto *reload = new QToolButton;
        reload->setText(tr("Reload"));
        reload->setToolTip(tr("Re-scan the tab for @uba frames"));
        connect(reload, &QToolButton::clicked, this, &BrakingPanel::onReload);
        row->addWidget(reload);

        row->addSpacing(16);
        row->addWidget(new QLabel(tr("Curve:")));
        m_curveCombo = new QComboBox;
        m_curveCombo->setMinimumWidth(110);
        m_curveCombo->setToolTip(tr("Which of curves_for_target[] to draw"));
        row->addWidget(m_curveCombo);

        row->addSpacing(16);

        m_lock = new QCheckBox(tr("Lock axes"));
        m_lock->setChecked(true);
        m_lock->setToolTip(tr("Fit the axes once over the whole session and hold "
                              "them.\nWithout this, a curve that is not moving "
                              "looks like it is."));
        row->addWidget(m_lock);

        row->addSpacing(8);
        row->addWidget(new QLabel(tr("Ghosts:")));
        m_ghosts = new QSpinBox;
        m_ghosts->setRange(0, 40);
        m_ghosts->setValue(6);
        m_ghosts->setToolTip(tr("Draw this many earlier curves faintly behind "
                                "the current one"));
        row->addWidget(m_ghosts);

        row->addWidget(new QLabel(tr("over")));
        m_ghostSpan = new QSpinBox;
        m_ghostSpan->setRange(1, 600);
        m_ghostSpan->setValue(5);
        m_ghostSpan->setSuffix(tr(" s"));
        m_ghostSpan->setToolTip(tr("Spread the ghosts over this much history.\n"
                                   "@uba arrives on a 10 ms cycle, so the six "
                                   "frames immediately before this one are only "
                                   "60 ms of history and lie exactly on top of "
                                   "each other — ghosts have to be spaced by "
                                   "TIME to show anything."));
        row->addWidget(m_ghostSpan);

        row->addStretch(1);

        auto *jump = new QToolButton;
        jump->setText(tr("Show frame in log"));
        connect(jump, &QToolButton::clicked, this, &BrakingPanel::onJumpToRow);
        row->addWidget(jump);

        outer->addLayout(row);
    }

    // ---- plot + tables ----------------------------------------------------
    auto *split = new QSplitter(Qt::Vertical);

    m_plot = new BrakingCurvePlot;
    split->addWidget(m_plot);

    auto *tables = new QWidget;
    auto *tl = new QHBoxLayout(tables);
    tl->setContentsMargins(0, 0, 0, 0);

    m_summary = makeTable(QStringList() << tr("Field") << tr("Value"));
    m_summary->setMaximumWidth(420);
    tl->addWidget(m_summary, 0);

    m_segments = makeTable(QStringList()
                           << tr("target") << tr("curve") << tr("#")
                           << tr("start (m)") << tr("end (m)")
                           << tr("from") << tr("to")
                           << tr("decel (m/s²)") << tr("A") << tr("C"));
    tl->addWidget(m_segments, 1);

    split->addWidget(tables);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    outer->addWidget(split, 1);

    // ---- search -----------------------------------------------------------
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Find:")));

        m_searchMode = new QComboBox;
        m_searchMode->addItem(tr("Time"), QStringLiteral("time"));
        m_searchMode->addItem(tr("Location (m)"), QStringLiteral("loc"));
        row->addWidget(m_searchMode);

        m_searchBox = new QLineEdit;
        m_searchBox->setClearButtonEnabled(true);
        row->addWidget(m_searchBox, 1);

        auto *find = new QToolButton;
        find->setText(tr("Find / Next"));
        connect(find, &QToolButton::clicked, this, &BrakingPanel::onSearch);
        row->addWidget(find);

        m_searchInfo = new QLabel;
        m_searchInfo->setMinimumWidth(190);
        m_searchInfo->setStyleSheet(
            QStringLiteral("color: %1;").arg(UiColor::muted().name()));
        row->addWidget(m_searchInfo);

        outer->addLayout(row);

        connect(m_searchBox, &QLineEdit::returnPressed, this, &BrakingPanel::onSearch);
        connect(m_searchBox, &QLineEdit::textChanged,
                this, &BrakingPanel::onSearchTextChanged);
        connect(m_searchMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &BrakingPanel::onSearchModeChanged);
    }
    onSearchModeChanged(0);

    // ---- scrubber ---------------------------------------------------------
    m_strip = new ChangeStrip;
    m_strip->onJump = [this](int snapIndex) {
        if (m_follow && m_follow->isChecked()) {
            QSignalBlocker b(m_follow); m_follow->setChecked(false);
        }
        setScrubForSnap(snapIndex);
    };
    outer->addWidget(m_strip);

    {
        auto *row = new QHBoxLayout;

        m_prevChange = new QToolButton;
        m_prevChange->setText(QStringLiteral("|◀"));
        m_prevChange->setToolTip(tr("Previous CHANGE — skips frames where the "
                                    "curve is the same"));
        connect(m_prevChange, &QToolButton::clicked, this, &BrakingPanel::onPrevChange);
        row->addWidget(m_prevChange);

        m_prev = new QToolButton;
        m_prev->setText(QStringLiteral("◀"));
        m_prev->setToolTip(tr("Previous frame"));
        connect(m_prev, &QToolButton::clicked, this, &BrakingPanel::onStepPrev);
        row->addWidget(m_prev);

        m_scrub = new QSlider(Qt::Horizontal);
        m_scrub->setRange(0, 0);
        connect(m_scrub, &QSlider::valueChanged, this, &BrakingPanel::onScrubbed);
        row->addWidget(m_scrub, 1);

        m_next = new QToolButton;
        m_next->setText(QStringLiteral("▶"));
        m_next->setToolTip(tr("Next frame"));
        connect(m_next, &QToolButton::clicked, this, &BrakingPanel::onStepNext);
        row->addWidget(m_next);

        m_nextChange = new QToolButton;
        m_nextChange->setText(QStringLiteral("▶|"));
        m_nextChange->setToolTip(tr("Next CHANGE — skips frames where the "
                                    "curve is the same"));
        connect(m_nextChange, &QToolButton::clicked, this, &BrakingPanel::onNextChange);
        row->addWidget(m_nextChange);

        m_changesOnly = new QCheckBox(tr("Changes only"));
        m_changesOnly->setToolTip(tr("Make the scrubber address only the frames "
                                     "where the curve changed, instead of all "
                                     "of them."));
        row->addWidget(m_changesOnly);

        m_timeLabel = new QLabel;
        m_timeLabel->setMinimumWidth(240);
        m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_timeLabel);

        m_follow = new QCheckBox(tr("Follow live"));
        m_follow->setChecked(true);
        row->addWidget(m_follow);

        outer->addLayout(row);
    }

    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("color: %1;").arg(UiColor::muted().name()));
    outer->addWidget(m_status);

    setCentralWidget(central);

    connect(m_tabCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BrakingPanel::onTabChanged);
    connect(m_follow, &QCheckBox::toggled, this, &BrakingPanel::onFollowToggled);
    connect(m_lock,   &QCheckBox::toggled, this, &BrakingPanel::onLockToggled);
    connect(m_changesOnly, &QCheckBox::toggled,
            this, &BrakingPanel::onChangesOnlyToggled);
    connect(m_ghosts, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &BrakingPanel::onGhostCountChanged);
    connect(m_ghostSpan, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &BrakingPanel::onGhostCountChanged);
    connect(m_curveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BrakingPanel::onCurveFilterChanged);

    if (m_dispatcher) {
        connect(m_dispatcher, &MessageDispatcher::entryAppended,
                this, &BrakingPanel::onEntryAppended);
    }

    rebuildTabList();
}

// ---- tab selection ------------------------------------------------------

void BrakingPanel::rebuildTabList()
{
    if (!m_dispatcher || !m_tabCombo) { return; }

    const QString want = m_tabKey;
    QSignalBlocker block(m_tabCombo);
    m_tabCombo->clear();

    const QStringList keys = m_dispatcher->knownKeys();
    for (const QString &k : keys) {
        m_tabCombo->addItem(m_dispatcher->friendlyNameFor(k), k);
    }

    int idx = m_tabCombo->findData(want);
    if (idx < 0) { idx = m_tabCombo->count() > 0 ? 0 : -1; }
    if (idx >= 0) {
        m_tabCombo->setCurrentIndex(idx);
        m_tabKey = m_tabCombo->itemData(idx).toString();
    } else {
        m_tabKey.clear();
    }
    reloadSnapshots();
}

void BrakingPanel::onTabChanged(int index)
{
    if (index < 0 || !m_tabCombo) { return; }
    m_tabKey = m_tabCombo->itemData(index).toString();
    reloadSnapshots();
}

void BrakingPanel::onReload()
{
    rebuildTabList();
}

// ---- snapshot timeline --------------------------------------------------

void BrakingPanel::reloadSnapshots()
{
    m_snaps.clear();
    m_index  = -1;
    m_hitCap = false;

    if (m_dispatcher && !m_tabKey.isEmpty()) {
        LogModel *model = m_dispatcher->modelForKey(m_tabKey);
        m_snaps = Braking::collect(model, kSnapshotCap, &m_hitCap);
    }

    m_cycles = Braking::groupIntoCycles(m_snaps);
    rebuildChangeIndex();

    const bool only = m_changesOnly && m_changesOnly->isChecked();
    {
        QSignalBlocker block(m_scrub);
        m_scrub->setRange(0, only ? qMax(0, m_changeIdx.size() - 1)
                                  : qMax(0, m_snaps.size() - 1));
        m_scrub->setValue(m_scrub->maximum());
    }
    m_scrub->setEnabled(!m_snaps.isEmpty());
    m_prev->setEnabled(!m_snaps.isEmpty());
    m_next->setEnabled(!m_snaps.isEmpty());
    m_prevChange->setEnabled(m_changeIdx.size() > 1);
    m_nextChange->setEnabled(m_changeIdx.size() > 1);

    // Populate the curve selector from what the frames actually contain, not
    // from NUM_OF_CURVES_FOR_EACH_TARGET: the capture decides how many curves
    // there are, and offering an SBD entry that no frame carries would be a
    // dead control.
    if (m_curveCombo) {
        QSignalBlocker block(m_curveCombo);
        m_curveCombo->clear();
        int n = 0;
        for (const Braking::Snapshot &s : m_snaps) { n = qMax(n, s.curveCount()); }
        if (n > 1) { m_curveCombo->addItem(tr("All"), -1); }
        for (int i = 0; i < n; ++i) {
            m_curveCombo->addItem(Braking::curveName(i), i);
        }
        m_curveCombo->setEnabled(n > 1);
        if (m_curveCombo->count() > 0) { m_curveCombo->setCurrentIndex(0); }
        m_plot->setCurveFilter(m_curveCombo->count() > 0
                                   ? m_curveCombo->itemData(0).toInt() : -1);
    }

    refreshRangeLock();
    applyIndex(m_snaps.isEmpty() ? -1 : m_snaps.size() - 1);
}

void BrakingPanel::rebuildChangeIndex()
{
    m_changeIdx.clear();
    m_distinctSeen.clear();
    QSet<quint64> &seen = m_distinctSeen;
    for (int i = 0; i < m_snaps.size(); ++i) {
        seen.insert(m_snaps.at(i).fingerprint);
        if (i == 0 || Braking::significantlyDifferent(m_snaps.at(i - 1), m_snaps.at(i))) {
            m_changeIdx.push_back(i);
        }
    }
    m_distinctFrames = seen.size();
}

int BrakingPanel::scrubToSnap(int scrubValue) const
{
    if (m_changesOnly && m_changesOnly->isChecked()) {
        if (m_changeIdx.isEmpty()) { return -1; }
        return m_changeIdx.at(qBound(0, scrubValue, m_changeIdx.size() - 1));
    }
    return scrubValue;
}

int BrakingPanel::snapToScrub(int snapIndex) const
{
    if (!(m_changesOnly && m_changesOnly->isChecked())) { return snapIndex; }
    // The nearest change at or before the frame — scrubbing in changes-only
    // mode cannot address a frame that is not itself a change.
    int best = 0;
    for (int k = 0; k < m_changeIdx.size(); ++k) {
        if (m_changeIdx.at(k) <= snapIndex) { best = k; } else { break; }
    }
    return best;
}

void BrakingPanel::setScrubForSnap(int snapIndex)
{
    const int v = snapToScrub(snapIndex);
    if (m_scrub->value() == v) { applyIndex(scrubToSnap(v)); }
    else                       { m_scrub->setValue(v); }
}

void BrakingPanel::onChangesOnlyToggled(bool)
{
    const int keep = m_index;
    const bool only = m_changesOnly->isChecked();
    {
        QSignalBlocker block(m_scrub);
        m_scrub->setRange(0, only ? qMax(0, m_changeIdx.size() - 1)
                                  : qMax(0, m_snaps.size() - 1));
        m_scrub->setValue(snapToScrub(qMax(0, keep)));
    }
    applyIndex(scrubToSnap(m_scrub->value()));
}

void BrakingPanel::onPrevChange()
{
    if (m_changeIdx.isEmpty()) { return; }
    int target = m_changeIdx.first();
    for (int idx : m_changeIdx) { if (idx < m_index) { target = idx; } }
    setScrubForSnap(target);
}

void BrakingPanel::onNextChange()
{
    if (m_changeIdx.isEmpty()) { return; }
    for (int idx : m_changeIdx) {
        if (idx > m_index) { setScrubForSnap(idx); return; }
    }
}

// ---- search -------------------------------------------------------------

void BrakingPanel::onSearchModeChanged(int)
{
    const bool byTime = m_searchMode->currentData().toString() == QLatin1String("time");
    m_searchBox->setPlaceholderText(
        byTime ? tr("12:16:04   or   12:16:04.250   or   12:16")
               : tr("6248   or   6248.5   (absolute track location, m)"));
    m_searchBox->setToolTip(
        byTime ? tr("Jump to the frame in force at that time.\n"
                    "Matched against host receive time, to millisecond "
                    "resolution — the loco RTC in the capture line is only "
                    "1 s, which cannot separate frames arriving every 10 ms.")
               : tr("Find frames whose TARGET is at that location.\n"
                    "The location is also marked on the plot with the allowed "
                    "speed there, so the search answers what the curve "
                    "permitted at that chainage as well as which frame."));
    m_matches.clear();
    m_matchPos = -1;
    m_searchInfo->clear();
    if (!byTime) { runSearch(false); }
    else if (m_plot) { m_plot->setQueryLocation(0, false); }
}

void BrakingPanel::onSearchTextChanged(const QString &)
{
    // Editing invalidates the match set; the next Enter starts a fresh search
    // rather than stepping through stale hits.
    m_matches.clear();
    m_matchPos = -1;
}

void BrakingPanel::onSearch()
{
    runSearch(true);
}

void BrakingPanel::runSearch(bool advance)
{
    if (!m_searchBox || !m_searchInfo) { return; }
    const QString text = m_searchBox->text().trimmed();
    const bool byTime = m_searchMode->currentData().toString() == QLatin1String("time");

    if (text.isEmpty()) {
        m_matches.clear();
        m_matchPos = -1;
        m_searchInfo->clear();
        if (m_plot) { m_plot->setQueryLocation(0, false); }
        return;
    }
    if (m_snaps.isEmpty()) {
        m_searchInfo->setText(tr("no frames loaded"));
        return;
    }

    if (m_matches.isEmpty()) {          // build the match set once per query
        if (byTime) {
            // Accept a few shapes rather than insisting on one. A bare time is
            // resolved against the day the capture was taken.
            QTime t;
            for (const char *fmt : { "HH:mm:ss.zzz", "HH:mm:ss", "HH:mm" }) {
                t = QTime::fromString(text, QString::fromLatin1(fmt));
                if (t.isValid()) { break; }
            }
            QDateTime want;
            if (t.isValid()) {
                want = QDateTime::fromMSecsSinceEpoch(m_snaps.first().epochMs);
                want.setTime(t);
            } else {
                want = QDateTime::fromString(text, Qt::ISODate);
            }
            if (!want.isValid()) {
                m_searchInfo->setText(tr("⚠ not a time"));
                return;
            }
            const int idx = Braking::indexAtOrBefore(m_snaps, want.toMSecsSinceEpoch());
            // Before the first frame, indexAtOrBefore gives -1; the first frame
            // is the honest answer there, flagged below by the time shown.
            m_matches.clear();
            m_matches.push_back(idx >= 0 ? idx : 0);
            m_matchPos = -1;
        } else {
            bool ok = false;
            QString num = text;
            num.remove(QRegExp(QStringLiteral("\\s*m$")));
            const double want = num.toDouble(&ok);
            if (!ok) {
                m_searchInfo->setText(tr("⚠ not a location"));
                return;
            }
            if (m_plot) { m_plot->setQueryLocation(want, true); }

            // Frames whose target is within a metre. If none is that close,
            // fall back to the single nearest and say so, rather than
            // reporting "no match" for a location that is merely between
            // targets.
            m_matches.clear();
            double bestErr = 0.0;
            int    bestIdx = -1;
            for (int i = 0; i < m_snaps.size(); ++i) {
                const double err = qAbs(m_snaps.at(i).targetLocation - want);
                if (err <= 1.0) { m_matches.push_back(i); }
                if (bestIdx < 0 || err < bestErr) { bestErr = err; bestIdx = i; }
            }
            if (m_matches.isEmpty() && bestIdx >= 0) {
                m_matches.push_back(bestIdx);
                m_searchInfo->setText(tr("nearest target %1 m away")
                                          .arg(bestErr, 0, 'f', 1));
            }
            m_matchPos = -1;
        }
    }

    if (m_matches.isEmpty()) {
        m_searchInfo->setText(tr("no match"));
        return;
    }

    if (advance || m_matchPos < 0) {
        m_matchPos = (m_matchPos + 1) % m_matches.size();
    }

    if (m_follow && m_follow->isChecked()) {
        QSignalBlocker block(m_follow);
        m_follow->setChecked(false);
    }
    setScrubForSnap(m_matches.at(m_matchPos));

    const QString when = QDateTime::fromMSecsSinceEpoch(
                             m_snaps.at(m_matches.at(m_matchPos)).epochMs)
                             .toString(QStringLiteral("HH:mm:ss.zzz"));
    m_searchInfo->setText(m_matches.size() > 1
                              ? tr("match %1 of %2   %3")
                                    .arg(m_matchPos + 1).arg(m_matches.size()).arg(when)
                              : tr("1 match   %1").arg(when));
}

void BrakingPanel::refreshRangeLock()
{
    if (!m_plot) { return; }
    if (!m_lock->isChecked() || m_snaps.isEmpty()) {
        m_plot->setLockedRange(false);
        return;
    }

    // Fit once over every snapshot in the session, so the frame stays still
    // while the curve inside it moves.
    bool any = false;
    double lo = 0, hi = 0, vmax = 0;
    for (const Braking::Snapshot &s : m_snaps) {
        double a = 0, b = 0, c = 0, d = 0;
        if (!s.span(&a, &b, &c, &d)) { continue; }
        a = qMin(a, s.targetLocation);
        b = qMax(b, s.targetLocation);
        if (!any) { lo = a; hi = b; vmax = d; any = true; }
        else      { lo = qMin(lo, a); hi = qMax(hi, b); vmax = qMax(vmax, d); }
    }
    if (!any || hi <= lo) { m_plot->setLockedRange(false); return; }

    const double pad = qMax(1.0, (hi - lo) * 0.04);
    m_plot->setLockedRange(true, lo - pad, hi + pad,
                           qMax(1.0, vmax * Braking::kMpsToKmph * 1.10));
}

void BrakingPanel::applyIndex(int index)
{
    m_index = index;

    if (index < 0 || index >= m_snaps.size()) {
        m_plot->setSnapshot(Braking::Snapshot());
        m_plot->setGhosts(QVector<Braking::Snapshot>());
        if (m_strip) { m_strip->setData(m_snaps.size(), m_changeIdx, -1); }
        m_summary->setRowCount(0);
        m_segments->setRowCount(0);
        m_timeLabel->clear();
        updateStatus();
        return;
    }

    const Braking::Snapshot &snap = m_snaps.at(index);

    // Hand the plot the whole cycle this frame belongs to, so every target
    // ahead of the loco is drawn, not just the one that happens to be under
    // the scrubber.
    Braking::Cycle shown;
    for (const Braking::Cycle &c : m_cycles) {
        if (c.frames.isEmpty()) { continue; }
        if (snap.epochMs >= c.frames.first().epochMs
            && snap.epochMs <= c.frames.last().epochMs) { shown = c; break; }
    }
    if (shown.frames.isEmpty()) { shown.frames.push_back(snap); shown.epochMs = snap.epochMs; }
    m_plot->setCycle(shown);

    // Ghosts are spaced by TIME, not by frame index. At a 10 ms cycle the six
    // frames before this one are 60 ms of history and draw exactly on top of
    // each other; spread over a few seconds they actually show the curve
    // moving. Walk backwards taking one frame per time step.
    QVector<Braking::Snapshot> ghosts;
    const int    n       = m_ghosts    ? m_ghosts->value()    : 0;
    const qint64 spanMs  = qint64(m_ghostSpan ? m_ghostSpan->value() : 5) * 1000;
    if (n > 0 && spanMs > 0) {
        const qint64 stepMs = qMax<qint64>(1, spanMs / n);
        qint64 nextWant = snap.epochMs - stepMs;
        for (int i = index - 1; i >= 0 && ghosts.size() < n; --i) {
            if (m_snaps.at(i).epochMs <= nextWant) {
                ghosts.push_front(m_snaps.at(i));
                nextWant = m_snaps.at(i).epochMs - stepMs;
            }
        }
    }
    m_plot->setGhosts(ghosts);

    refreshTables(shown);

    if (m_strip) { m_strip->setData(m_snaps.size(), m_changeIdx, index); }

    const QString host = QDateTime::fromMSecsSinceEpoch(snap.epochMs)
                             .toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString rtc = snap.rtc.isValid()
                            ? snap.rtc.toString(QStringLiteral("HH:mm:ss"))
                            : QStringLiteral("--");
    m_timeLabel->setText(tr("frame %1 of %2   host %3   rtc %4   seq %5")
                             .arg(index + 1).arg(m_snaps.size())
                             .arg(host).arg(rtc).arg(snap.seq));
    updateStatus();
}

void BrakingPanel::refreshTables(const Braking::Cycle &cycle)
{
    if (cycle.frames.isEmpty()) { return; }
    const Braking::Snapshot &snap = cycle.frames.first();

    const int filter = (m_curveCombo && m_curveCombo->currentIndex() >= 0)
                           ? m_curveCombo->currentData().toInt() : -1;

    // ---- summary ---------------------------------------------------------
    m_summary->setRowCount(0);
    auto addSummary = [&](const QString &k, const QString &v) {
        const int r = m_summary->rowCount();
        m_summary->insertRow(r);
        setCell(m_summary, r, 0, k);
        setCell(m_summary, r, 1, v);
    };

    if (cycle.frames.size() > 1) {
        QStringList names;
        for (const Braking::Snapshot &f : cycle.frames) {
            names << tr("%1 @ %2 m").arg(f.targetTypeName)
                         .arg(f.targetLocation, 0, 'f', 0);
        }
        addSummary(tr("targets this cycle"),
                   tr("%1 — %2").arg(cycle.frames.size())
                                .arg(names.join(QStringLiteral(", "))));
        // The grouping is inferred, not framed by the protocol. Say so where
        // the numbers derived from it are read.
        addSummary(tr("cycle grouping"),
                   tr("⚠ inferred from timing and target identity — the frame "
                      "carries no target index or cycle counter"));
    } else {
        addSummary(tr("target type"), snap.targetTypeName);
        addSummary(tr("target location"),
                   QStringLiteral("%1 m").arg(snap.targetLocation, 0, 'f', 3));
        addSummary(tr("target speed"), mps(snap.targetSpeed));
    }

    // The layout is stated outright. A frame whose shape was guessed rather
    // than recognised must say so here, because every number below it is only
    // as trustworthy as the stride that produced it.
    addSummary(tr("wire layout"),
               snap.layout.documented
                   ? snap.layout.name
                   : tr("⚠ %1").arg(snap.layout.name));

    for (int ci = 0; ci < snap.curves.size(); ++ci) {
        if (filter >= 0 && ci != filter) { continue; }
        const Braking::Curve &c = snap.curves.at(ci);
        addSummary(tr("%1 segments").arg(c.name),
                   tr("%1 of %2 slots").arg(c.activeCount()).arg(c.segments.size()));

        double lo = 0, hi = 0, vlo = 0, vhi = 0;
        if (!c.span(&lo, &hi, &vlo, &vhi)) { continue; }
        addSummary(tr("%1 extent").arg(c.name),
                   QStringLiteral("%1 → %2 m  (%3 m)")
                       .arg(lo, 0, 'f', 1).arg(hi, 0, 'f', 1).arg(hi - lo, 0, 'f', 1));
        addSummary(tr("%1 entry speed").arg(c.name), mps(vhi));

        // Where the curve reaches its low-speed end, against the target
        // itself. In the captures seen so far the curve runs 100 m PAST the
        // target, which has to be visible rather than inferred. `hi` is the
        // curve's terminus in track order — NOT the segment end nearest the
        // target, which is a different and meaningless number.
        addSummary(tr("%1 ends at").arg(c.name),
                   QStringLiteral("%1 m  (%2 m vs target)")
                       .arg(hi, 0, 'f', 3)
                       .arg(hi - snap.targetLocation, 0, 'f', 3));

        double atTarget = 0.0;
        addSummary(tr("%1 speed at target").arg(c.name),
                   c.speedAt(snap.targetLocation, &atTarget)
                       ? mps(atTarget)
                       : tr("(target is off the curve)"));

        // Deceleration signature. net_accln = brake decel -/+ gradient*9.81
        // (gradient_manager.c), so the LARGEST decel across the segments is
        // the closest thing in the frame to the unloaded brake rate — and that
        // is the only hint the frame carries about which brake built it.
        // Reference table in braking_params_manager.c: EB 1.0 m/s^2 flat,
        // FSB 0.9 / 0.6 by speed range.
        const QVector<Braking::Segment> sg = c.activeSegments();
        double dmax = 0.0, dmin = 0.0;
        bool haveD = false;
        for (const Braking::Segment &x : sg) {
            if (!x.hasDecel()) { continue; }
            const double d = x.decel();
            if (!haveD) { dmax = dmin = d; haveD = true; }
            else { dmax = qMax(dmax, d); dmin = qMin(dmin, d); }
        }
        if (haveD) {
            QString txt = (qAbs(dmax - dmin) < 1e-9)
                              ? QStringLiteral("%1 m/s²").arg(dmax, 0, 'f', 4)
                              : QStringLiteral("%1 … %2 m/s²")
                                    .arg(dmin, 0, 'f', 4).arg(dmax, 0, 'f', 4);
            // The spread between segments is gradient, not a different brake.
            if (qAbs(dmax - dmin) > 1e-9) {
                txt += tr("   (spread = gradient %1 ‰)")
                           .arg((dmax - dmin) / 9.81 * 1000.0, 0, 'f', 2);
            }
            addSummary(tr("%1 deceleration").arg(c.name), txt);
        }
    }

    // ---- segments --------------------------------------------------------
    m_segments->setRowCount(0);
    for (const Braking::Snapshot &frame : cycle.frames) {
        for (int ci = 0; ci < frame.curves.size(); ++ci) {
            if (filter >= 0 && ci != filter) { continue; }
            const Braking::Curve &c = frame.curves.at(ci);
            const QVector<Braking::Segment> act = c.activeSegments();
            for (int i = 0; i < act.size(); ++i) {
                const Braking::Segment &sg = act.at(i);
                const int r = m_segments->rowCount();
                m_segments->insertRow(r);
                setCell(m_segments, r, 0,
                        tr("%1 @ %2").arg(frame.targetTypeName)
                                     .arg(frame.targetLocation, 0, 'f', 0));
                setCell(m_segments, r, 1, c.name);
                setCell(m_segments, r, 2, QString::number(i));
                setCell(m_segments, r, 3, QString::number(sg.startLoc, 'f', 3));
                setCell(m_segments, r, 4, QString::number(sg.endLoc, 'f', 3));
                setCell(m_segments, r, 5, QStringLiteral("%1 km/h")
                            .arg(sg.higherSpeed * Braking::kMpsToKmph, 0, 'f', 1));
                setCell(m_segments, r, 6, QStringLiteral("%1 km/h")
                            .arg(sg.lowerSpeed * Braking::kMpsToKmph, 0, 'f', 1));
                // A horizontal segment has no deceleration; 0.000 would read
                // as "coasting", a different claim from "flat speed limit".
                setCell(m_segments, r, 7,
                        sg.isHorizontal() ? tr("— (flat)")
                                          : QString::number(sg.decel(), 'f', 4));
                setCell(m_segments, r, 8, QString::number(sg.a, 'f', 6));
                setCell(m_segments, r, 9, QString::number(sg.c, 'f', 3));
            }
        }
    }
    m_segments->resizeColumnsToContents();
    m_summary->resizeColumnsToContents();
}

void BrakingPanel::updateStatus()
{
    QStringList bits;
    if (m_tabKey.isEmpty()) {
        bits << tr("no tab selected");
    } else if (m_snaps.isEmpty()) {
        bits << tr("no @uba frames in this tab yet");
    } else {
        bits << tr("%1 frames").arg(m_snaps.size());
        if (!m_cycles.isEmpty()) {
            int maxT = 0;
            for (const Braking::Cycle &c : m_cycles) { maxT = qMax(maxT, c.targetCount()); }
            bits << tr("%1 cycles, up to %2 targets each")
                        .arg(m_cycles.size()).arg(maxT);
        }

        // The headline number for navigation. When a whole session collapses
        // to one curve, saying so up front saves the operator dragging 4694
        // positions to find that nothing ever moves.
        if (m_changeIdx.size() <= 1) {
            bits << tr("the curve NEVER CHANGES in this capture "
                       "(%1 identical frames)").arg(m_snaps.size());
        } else {
            bits << tr("%1 changes").arg(m_changeIdx.size());
            bits << tr("%1 distinct frames").arg(m_distinctFrames);
        }

        // Rate and gaps. The loco builds targets every 10 ms, so a stream that
        // is not arriving near 100 Hz — or that stops entirely for a while —
        // is itself a finding, and one that is invisible if the panel only
        // ever reports a frame count.
        const qint64 spanMs = m_snaps.last().epochMs - m_snaps.first().epochMs;
        qint64 worstGapMs = 0;
        qint64 worstAt    = 0;
        qint64 coveredMs  = 0;
        for (int i = 1; i < m_snaps.size(); ++i) {
            const qint64 d = m_snaps.at(i).epochMs - m_snaps.at(i - 1).epochMs;
            coveredMs += qMin<qint64>(d, 1000);
            if (d > worstGapMs) { worstGapMs = d; worstAt = m_snaps.at(i - 1).epochMs; }
        }
        if (coveredMs > 0) {
            bits << tr("%1 frames/s while streaming")
                        .arg(m_snaps.size() * 1000.0 / coveredMs, 0, 'f', 0);
        }
        if (worstGapMs >= 1000) {
            bits << tr("⚠ longest gap %1 s at %2")
                        .arg(worstGapMs / 1000.0, 0, 'f', 1)
                        .arg(QDateTime::fromMSecsSinceEpoch(worstAt)
                                 .toString(QStringLiteral("HH:mm:ss")));
        }
        Q_UNUSED(spanMs);

        const Braking::Layout &l = m_snaps.last().layout;
        bits << (l.documented ? l.name : tr("⚠ %1").arg(l.name));
        const qint64 span = m_snaps.last().epochMs - m_snaps.first().epochMs;
        if (span > 0) { bits << tr("spanning %1 s").arg(span / 1000.0, 0, 'f', 1); }
    }
    if (m_hitCap) {
        bits << tr("TIMELINE TRUNCATED at %1 frames — later curves are not "
                   "shown").arg(int(kSnapshotCap));
    }
    m_status->setText(bits.join(QStringLiteral("   ·   ")));
}

// ---- scrubbing ----------------------------------------------------------

void BrakingPanel::onScrubbed(int value)
{
    // Dragging means the operator wants to look at something; stop dragging
    // the view out from under them.
    if (m_follow && m_follow->isChecked() && value != m_scrub->maximum()) {
        QSignalBlocker block(m_follow);
        m_follow->setChecked(false);
    }
    applyIndex(scrubToSnap(value));
}

void BrakingPanel::onStepPrev()
{
    if (m_snaps.isEmpty()) { return; }
    m_scrub->setValue(qMax(m_scrub->minimum(), m_scrub->value() - 1));
}

void BrakingPanel::onStepNext()
{
    if (m_snaps.isEmpty()) { return; }
    m_scrub->setValue(qMin(m_scrub->maximum(), m_scrub->value() + 1));
}

void BrakingPanel::onFollowToggled(bool on)
{
    if (on && !m_snaps.isEmpty()) { m_scrub->setValue(m_scrub->maximum()); }
}

void BrakingPanel::onLockToggled(bool)
{
    refreshRangeLock();
    applyIndex(m_index);
}

void BrakingPanel::onGhostCountChanged(int)
{
    applyIndex(m_index);
}

void BrakingPanel::onCurveFilterChanged(int index)
{
    if (!m_curveCombo || index < 0) { return; }
    m_plot->setCurveFilter(m_curveCombo->itemData(index).toInt());
    applyIndex(m_index);
}

void BrakingPanel::onJumpToRow()
{
    if (m_index < 0 || m_index >= m_snaps.size()) { return; }
    emit jumpToTimeRequested(m_tabKey, m_snaps.at(m_index).epochMs);
}

// ---- live capture -------------------------------------------------------

void BrakingPanel::onEntryAppended(QString tabKey, LogEntryPtr entry)
{
    if (tabKey != m_tabKey || !entry) { return; }
    if (!entry->text.startsWith(QLatin1String("@uba"))) { return; }

    const CaptureLine cl = CaptureDecoder::parseLine(entry->text);
    if (!cl.valid || cl.type != CapType::UBA) { return; }

    Braking::Snapshot s = Braking::parseFrame(cl.bytes);
    if (!s.valid) { return; }
    s.epochMs = entry->epochMs;
    s.rtc     = cl.rtc;
    s.seq     = cl.seq;
    // No row on the live path — the model appends independently. Nothing
    // depends on it: the jump is by timestamp.
    s.row = -1;

    m_snaps.push_back(s);

    // Extend the change index incrementally rather than rebuilding it: on a
    // live feed this runs once per frame.
    if (m_snaps.size() == 1
        || Braking::significantlyDifferent(m_snaps.at(m_snaps.size() - 2), s)) {
        m_changeIdx.push_back(m_snaps.size() - 1);
    }
    m_distinctSeen.insert(s.fingerprint);
    m_distinctFrames = m_distinctSeen.size();

    const bool follow = m_follow && m_follow->isChecked();
    const bool only   = m_changesOnly && m_changesOnly->isChecked();
    {
        QSignalBlocker block(m_scrub);
        m_scrub->setRange(0, only ? qMax(0, m_changeIdx.size() - 1)
                                  : m_snaps.size() - 1);
        if (follow) { m_scrub->setValue(m_scrub->maximum()); }
    }
    m_prevChange->setEnabled(m_changeIdx.size() > 1);
    m_nextChange->setEnabled(m_changeIdx.size() > 1);
    m_scrub->setEnabled(true);
    m_prev->setEnabled(true);
    m_next->setEnabled(true);

    if (follow) {
        applyIndex(scrubToSnap(m_scrub->value()));
    } else {
        if (m_strip) { m_strip->setData(m_snaps.size(), m_changeIdx, m_index); }
        updateStatus();
    }
}
