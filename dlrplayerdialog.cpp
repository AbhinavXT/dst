#include "dlrplayerdialog.h"
#include "uistyle.h"
#include "windowgeometry.h"
#include "sendguard.h"
#include "uicolors.h"

#include "capturedecoder.h"
#include "colorrules.h"
#include "fieldinspector.h"
#include "messagedispatcher.h"
#include "rawbytespanel.h"
#include "logquery.h"
#include "querylineedit.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Playback rates offered in the combo. 0.0 is the burst entry: it is here
// rather than hidden behind a checkbox because "as fast as the socket will
// take it" is the setting that exercises the receiver's drop counter and the
// dispatcher's queue cap, which is a thing you deliberately go looking for.
struct SpeedOpt { const char *label; double mult; };
const SpeedOpt kSpeeds[] = {
    { "0.25x  (quarter speed)", 0.25 },
    { "0.5x",                   0.5  },
    { "1x  (real time)",        1.0  },
    { "2x",                     2.0  },
    { "5x",                     5.0  },
    { "10x",                   10.0  },
    { "25x",                   25.0  },
    { "Burst — no pacing",      0.0  },
};

}  // namespace

DlrPlayerDialog::DlrPlayerDialog(const ColorRules *rules, QWidget *parent)
    : QDialog(parent), m_rules(rules)
{
    setWindowTitle(tr("Stream session (.dlr) as live traffic"));
    WindowGeometry::makeResizableWindow(this);
    setMinimumWidth(680);

    auto *root = new QVBoxLayout(this);

    // ---- the warning -------------------------------------------------
    // Permanent and at the top. Replayed datagrams are real datagrams:
    // LogWriter records them into today's files and they interleave with
    // anything the real Kavach is sending. That is exactly what makes the
    // tool useful for testing recording, and exactly what makes it a
    // hazard on a console someone is actually watching.
    auto *warn = new QLabel(
        tr("<b>This sends real UDP datagrams.</b> Nothing downstream can tell "
           "them from live traffic — which is the point, but it means replayed "
           "packets are written into today's .log and .dlr and will interleave "
           "with anything the real Kavach is sending. Point this at a test "
           "instance if you don't want that."),
        this);
    warn->setWordWrap(true);
    auto paintWarn = [warn] {
        warn->setStyleSheet(QStringLiteral(
            "QLabel { background:%1; color:%2; border:1px solid %3;"
            " border-radius:4px; padding:8px; }")
            .arg(UiColor::bannerBg().name(), UiColor::bannerFg().name(),
                 UiColor::bannerBorder().name()));
    };
    paintWarn();
    UiColor::onThemeChange(this, paintWarn);
    root->addWidget(warn);

    // ---- archives ----------------------------------------------------
    auto *filesBox = new QGroupBox(tr("Archives"), this);
    auto *filesLay = new QVBoxLayout(filesBox);

    m_list = new QTreeWidget(filesBox);
    m_list->setColumnCount(5);
    m_list->setHeaderLabels({ tr("File"), tr("Tab"), tr("Records"),
                              tr("Span"), tr("Starts") });
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setMinimumHeight(120);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    filesLay->addWidget(m_list);

    auto *fileBtns = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("Add .dlr…"), filesBox);
    m_rmBtn  = new QPushButton(tr("Remove"),    filesBox);
    fileBtns->addWidget(m_addBtn);
    fileBtns->addWidget(m_rmBtn);
    fileBtns->addStretch(1);
    m_timeline = new QLabel(tr("No archives loaded."), filesBox);
    fileBtns->addWidget(m_timeline);
    filesLay->addLayout(fileBtns);

    root->addWidget(filesBox);

    // ---- playback ----------------------------------------------------
    auto *cfgBox = new QGroupBox(tr("Playback"), this);
    auto *cfg    = new QFormLayout(cfgBox);

    auto *targetRow = new QHBoxLayout;
    m_host = new QLineEdit(QStringLiteral("127.0.0.1"), cfgBox);
    m_host->setMaximumWidth(160);
    m_port = new QSpinBox(cfgBox);
    m_port->setRange(1, 65535);
    // Default to the port this instance is bound to: the common case is
    // replaying into yourself.
    m_port->setValue(Settings::udpPort());
    targetRow->addWidget(m_host);
    targetRow->addWidget(new QLabel(QStringLiteral(":"), cfgBox));
    targetRow->addWidget(m_port);
    targetRow->addStretch(1);
    cfg->addRow(tr("Send to"), targetRow);

    m_speed = new QComboBox(cfgBox);
    for (const SpeedOpt &o : kSpeeds) { m_speed->addItem(QString::fromLatin1(o.label)); }
    m_speed->setCurrentIndex(2);   // 1x
    cfg->addRow(tr("Speed"), m_speed);

    m_maxGap = new QSpinBox(cfgBox);
    m_maxGap->setRange(0, 600000);
    m_maxGap->setSingleStep(500);
    m_maxGap->setValue(0);
    m_maxGap->setSpecialValueText(tr("off — replay idle time faithfully"));
    m_maxGap->setSuffix(tr(" ms"));
    m_maxGap->setToolTip(tr("Clamp gaps longer than this. A diagnostic archive "
                            "can sit quiet for minutes; clamping skips the dead "
                            "air while leaving the busy stretches at true speed."));
    cfg->addRow(tr("Cap idle gaps at"), m_maxGap);

    m_startAt = new QSpinBox(cfgBox);
    m_startAt->setRange(0, 24 * 60 * 60);
    m_startAt->setSuffix(tr(" s"));
    m_startAt->setToolTip(tr("Skip this far into the merged timeline before "
                             "sending anything."));
    cfg->addRow(tr("Start at"), m_startAt);

    // Seeking by content. A time offset assumes you know when the thing
    // happened; usually you know what it was. Same LogQuery language as the
    // filter bar, so the expression that found the row seeks to it.
    m_seekQuery = new QueryLineEdit(cfgBox);
    m_seekQuery->setPlaceholderText(
        tr("e.g.  sev:error   •   src:21_2 RFID   •   field:TRAIN_SPEED>60"));
    m_seekQuery->setToolTip(tr("Discard records until one matches, then play "
                               "normally from there. Same query language as "
                               "the filter bar and archive search."));
    cfg->addRow(tr("Seek to first match"), m_seekQuery);

    m_loop = new QCheckBox(tr("Loop when the archive ends"), cfgBox);
    cfg->addRow(QString(), m_loop);

    root->addWidget(cfgBox);

    // ---- transport ---------------------------------------------------
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("%p%"));
    root->addWidget(m_progress);

    m_stats = new QLabel(tr("Idle."), this);
    m_stats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_stats);

    // ---- decode ------------------------------------------------------
    //
    // The same RawBytesPanel and FieldInspector the live console and the
    // archive viewer use. Sharing the widgets rather than growing a third
    // decode view is the point: a frame must read the same here as it does
    // when it lands in a tab, or a replay stops being evidence of what the
    // live view will show.
    //
    // Hidden by default. Decoding costs a schema walk per frame, and the
    // player's tap is only armed while this is visible, so a run with the
    // panel closed pays nothing for it.
    auto *decodeRow = new QHBoxLayout;
    m_showDecode = new QCheckBox(tr("Show decode"), this);
    m_follow     = new QCheckBox(tr("Follow the stream"), this);
    m_follow->setChecked(true);
    m_follow->setEnabled(false);
    m_follow->setToolTip(tr("Sample the outgoing stream about ten times a "
                            "second. Uncheck to hold the current frame — "
                            "including whatever a seek landed on — while "
                            "playback continues."));
    decodeRow->addWidget(m_showDecode);
    decodeRow->addWidget(m_follow);
    decodeRow->addStretch(1);
    root->addLayout(decodeRow);

    m_decodeTabs = new QTabWidget(this);
    UiStyle::useTabTooltips(m_decodeTabs);
    m_rawPanel   = new RawBytesPanel(this);
    m_fieldPanel = new FieldInspector(this);
    m_fieldPanel->setDecoder(&kavachSchema());
    m_decodeTabs->addTab(m_rawPanel,   tr("Raw bytes"));
    m_decodeTabs->addTab(m_fieldPanel, tr("Decoded fields"));
    m_decodeTabs->setMinimumHeight(240);
    m_decodeTabs->setVisible(false);
    root->addWidget(m_decodeTabs, 1);

    connect(m_fieldPanel, &FieldInspector::byteRangeSelected,
            this, [this](int from, int to) {
                if (m_rawPanel) { m_rawPanel->highlightBytes(from, to); }
            });

    auto *btns = new QHBoxLayout;
    m_playBtn = new QPushButton(tr("Play"), this);
    m_stopBtn = new QPushButton(tr("Stop"), this);
    m_stopBtn->setEnabled(false);
    m_skipBtn = new QPushButton(tr("Skip to next match"), this);
    m_skipBtn->setEnabled(false);
    m_skipBtn->setToolTip(tr("Jump forward to the next record matching the "
                             "seek query. Immediate even across a long idle "
                             "gap; pacing resumes at true speed from there."));
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btns->addWidget(m_playBtn);
    btns->addWidget(m_stopBtn);
    btns->addWidget(m_skipBtn);
    btns->addStretch(1);
    m_status = new StatusLine(this);
    btns->addWidget(m_status);
    btns->addWidget(closeBtn);
    root->addLayout(btns);

    // ---- player ------------------------------------------------------
    m_player = new DlrPlayer(this);
    connect(m_player, &DlrPlayer::started,  this, &DlrPlayerDialog::onStarted);
    connect(m_player, &DlrPlayer::progress, this, &DlrPlayerDialog::onProgress);
    connect(m_player, &DlrPlayer::finished, this, &DlrPlayerDialog::onFinished);
    connect(m_player, &DlrPlayer::failed,   this, &DlrPlayerDialog::onFailed);

    connect(m_addBtn,  &QPushButton::clicked, this, &DlrPlayerDialog::onAdd);
    connect(m_rmBtn,   &QPushButton::clicked, this, &DlrPlayerDialog::onRemove);
    connect(m_playBtn, &QPushButton::clicked, this, &DlrPlayerDialog::onPlayPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &DlrPlayerDialog::onStop);
    connect(closeBtn,  &QPushButton::clicked, this, &QDialog::close);
    connect(m_speed, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,    &DlrPlayerDialog::onSpeedChanged);
    connect(m_skipBtn,   &QPushButton::clicked, this, &DlrPlayerDialog::onSkipToMatch);
    connect(m_seekQuery, &QLineEdit::textChanged,
            this,        &DlrPlayerDialog::onSeekQueryEdited);
    connect(m_showDecode, &QCheckBox::toggled, this, &DlrPlayerDialog::onToggleDecode);
    connect(m_follow, &QCheckBox::toggled, this, [this](bool on) {
        if (m_player) { m_player->setTapEnabled(on && m_decodeTabs->isVisible()); }
    });
    connect(m_player, &DlrPlayer::seekLanded,    this, &DlrPlayerDialog::onSeekLanded);
    connect(m_player, &DlrPlayer::seekExhausted, this, &DlrPlayerDialog::onSeekExhausted);

    setRunningUi(false);
}

DlrPlayerDialog::~DlrPlayerDialog()
{
    if (m_player) { m_player->stop(); m_player->wait(3000); }
}

double DlrPlayerDialog::selectedSpeed() const
{
    const int i = m_speed->currentIndex();
    const int n = int(sizeof(kSpeeds) / sizeof(kSpeeds[0]));
    return (i >= 0 && i < n) ? kSpeeds[i].mult : 1.0;
}

QString DlrPlayerDialog::fmtDuration(qint64 ms)
{
    if (ms < 0) { ms = 0; }
    const qint64 total = ms / 1000;
    return QStringLiteral("%1:%2").arg(total / 60, 2, 10, QLatin1Char('0'))
                                  .arg(total % 60, 2, 10, QLatin1Char('0'));
}

void DlrPlayerDialog::onAdd()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add session archives"), QString(),
        tr("DL Console raw session (*.dlr);;All files (*)"));
    addFiles(paths);
}

void DlrPlayerDialog::addFiles(const QStringList &paths)
{
    QStringList problems;
    for (const QString &p : paths) {
        bool dup = false;
        for (const DlrPlayer::Source &s : m_sources) {
            if (s.path == p) { dup = true; break; }
        }
        if (dup) { continue; }

        DlrPlayer::Source s;
        QString err;
        if (!DlrPlayer::probe(p, &s, &err)) {
            problems << tr("%1: %2").arg(QFileInfo(p).fileName(), err);
            continue;
        }
        if (s.truncated) {
            problems << tr("%1: ends mid-record; the complete part will play")
                            .arg(QFileInfo(p).fileName());
        }
        m_sources.push_back(s);
    }

    refreshList();
    refreshTimeline();

    if (!problems.isEmpty()) {
        // Sticky: a problem found while loading is the reason the replay may
        // not do what the operator expects, and it must not be cleared by the
        // next routine message before it has been read.
        m_status->warn(problems.first());
        m_status->setToolTip(problems.join(QLatin1Char('\n')));
    }
}

void DlrPlayerDialog::onRemove()
{
    const auto sel = m_list->selectedItems();
    QStringList drop;
    for (QTreeWidgetItem *it : sel) { drop << it->data(0, Qt::UserRole).toString(); }

    for (int i = m_sources.size() - 1; i >= 0; --i) {
        if (drop.contains(m_sources[i].path)) { m_sources.remove(i); }
    }
    refreshList();
    refreshTimeline();
}

void DlrPlayerDialog::refreshList()
{
    m_list->clear();
    for (const DlrPlayer::Source &s : m_sources) {
        auto *it = new QTreeWidgetItem(m_list);
        it->setText(0, QFileInfo(s.path).fileName());
        it->setData(0, Qt::UserRole, s.path);
        it->setToolTip(0, s.path);
        it->setText(1, s.tabKey);
        it->setText(2, QString::number(s.records));
        it->setText(3, fmtDuration(s.lastMs - s.firstMs));
        it->setText(4, QDateTime::fromMSecsSinceEpoch(s.firstMs)
                           .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        if (s.truncated) {
            it->setForeground(0, QBrush(UiColor::warning()));
        }
    }
    for (int c = 1; c < m_list->columnCount(); ++c) { m_list->resizeColumnToContents(c); }
}

void DlrPlayerDialog::refreshTimeline()
{
    if (m_sources.isEmpty()) {
        m_timeline->setText(tr("No archives loaded."));
        m_playBtn->setEnabled(false);
        m_startAt->setMaximum(0);
        return;
    }

    qint64 first = std::numeric_limits<qint64>::max();
    qint64 last  = std::numeric_limits<qint64>::min();
    qint64 recs  = 0;
    qint64 bytes = 0;
    for (const DlrPlayer::Source &s : m_sources) {
        first = qMin(first, s.firstMs);
        last  = qMax(last,  s.lastMs);
        recs += s.records;
        bytes += s.bytes;
    }
    const qint64 span = qMax<qint64>(0, last - first);

    // Merged, not summed: the archives from one session overlap in time, so
    // the combined timeline is the union of their spans and the rate is
    // records over that union.
    const double rate = span > 0 ? (double(recs) * 1000.0 / double(span)) : 0.0;
    m_timeline->setText(tr("%1 records over %2  •  %3/s  •  %4 KB")
                            .arg(recs)
                            .arg(fmtDuration(span))
                            .arg(rate, 0, 'f', 1)
                            .arg(bytes / 1024));

    m_startAt->setMaximum(int(span / 1000));
    m_playBtn->setEnabled(true);
}

void DlrPlayerDialog::onSpeedChanged()
{
    // Live: speed is the one setting that can change mid-playback, because
    // wanting to slow down to look at something is the normal reason to
    // touch it.
    if (m_player) { m_player->setSpeed(selectedSpeed()); }
}

void DlrPlayerDialog::onPlayPause()
{
    if (!m_running) {
        if (m_sources.isEmpty()) { return; }

        m_player->setSources(m_sources);
        m_player->setTarget(QHostAddress(m_host->text().trimmed()),
                            quint16(m_port->value()));
        m_player->setSpeed(selectedSpeed());
        m_player->setMaxGapMs(m_maxGap->value());
        m_player->setStartOffsetMs(qint64(m_startAt->value()) * 1000);
        m_player->setLoop(m_loop->isChecked());

        // Refuse to start on a query that does not parse. Starting anyway
        // would send nothing and look exactly like an archive that never
        // contained the event.
        QString qerr;
        static ColorRules s_empty;
        if (!m_player->setSeekQuery(m_seekQuery->text(),
                                    m_rules ? *m_rules : s_empty, &qerr)) {
            m_seekQuery->setQueryError(qerr, 0);
            m_status->fail(tr("Seek query: %1").arg(qerr));
            return;
        }
        m_seekQuery->clearQueryError();

        m_player->setTapEnabled(m_decodeTabs->isVisible() && m_follow->isChecked());
        m_status->clear();
        m_player->play();
        m_running = true;
        setRunningUi(true);
        return;
    }

    if (m_player->isPaused()) {
        m_player->resume();
        m_playBtn->setText(tr("Pause"));
    } else {
        m_player->pause();
        m_playBtn->setText(tr("Resume"));
    }
}

void DlrPlayerDialog::onStop()
{
    if (m_player) { m_player->stop(); }
}

void DlrPlayerDialog::setRunningUi(bool running)
{
    m_playBtn->setText(running ? tr("Pause") : tr("Play"));
    m_stopBtn->setEnabled(running);
    m_addBtn->setEnabled(!running);
    m_rmBtn->setEnabled(!running);
    m_host->setEnabled(!running);
    m_port->setEnabled(!running);
    m_maxGap->setEnabled(!running);
    m_startAt->setEnabled(!running);
    m_loop->setEnabled(!running);
    m_seekQuery->setEnabled(!running);
    m_skipBtn->setEnabled(running && !m_seekQuery->text().trimmed().isEmpty());
    m_playBtn->setEnabled(running || !m_sources.isEmpty());
}

void DlrPlayerDialog::onStarted(qint64 totalRecords, qint64 spanMs)
{
    Q_UNUSED(totalRecords);
    Q_UNUSED(spanMs);
    m_progress->setValue(0);
}

void DlrPlayerDialog::onProgress(DlrPlayer::Stats s)
{
    const qint64 span = qMax<qint64>(1, s.spanMs - qint64(m_startAt->value()) * 1000);
    m_progress->setValue(int(qBound(qint64(0), s.playbackMs * 1000 / span, qint64(1000))));

    QString line = tr("%1 / %2 records  •  %3 / %4  •  %5 KB sent")
                       .arg(s.sent).arg(s.total)
                       .arg(fmtDuration(s.playbackMs))
                       .arg(fmtDuration(span))
                       .arg(s.bytesSent / 1024);

    if (s.elapsedMs > 0) {
        line += tr("  •  %1 pkt/s")
                    .arg(double(s.sent) * 1000.0 / double(s.elapsedMs), 0, 'f', 0);
    }
    // Lateness is the honest measure of whether the timing claim holds. If
    // it climbs, the pacing is not keeping up and the replay is no longer a
    // faithful reproduction of the original rate.
    if (s.lateMs > 0)      { line += tr("  •  max lateness %1 ms").arg(s.lateMs); }
    if (s.sendErrors > 0)  { line += tr("  •  %1 send errors").arg(s.sendErrors); }
    if (s.loops > 0)       { line += tr("  •  loop %1").arg(s.loops + 1); }
    if (s.seekSkipped > 0) { line += tr("  •  %1 skipped by seek").arg(s.seekSkipped); }

    m_stats->setText(line);

    // Sampled, not per-record: the tick is ~100 ms and the panel can only
    // render one frame anyway.
    if (m_decodeTabs->isVisible() && m_follow->isChecked()) {
        qint64 ms = 0;
        const QByteArray w = m_player->tappedWire(&ms);
        if (!w.isEmpty()) { showDecoded(w, ms); }
    }
}

void DlrPlayerDialog::onToggleDecode(bool on)
{
    m_decodeTabs->setVisible(on);
    m_follow->setEnabled(on);
    if (m_player) { m_player->setTapEnabled(on && m_follow->isChecked()); }

    if (on) {
        // Show whatever is already there — after a seek that is the record
        // the query landed on, which is usually the reason the panel is
        // being opened at all.
        qint64 ms = 0;
        showDecoded(m_player ? m_player->tappedWire(&ms) : QByteArray(), ms);
    } else {
        adjustSize();
    }
}

void DlrPlayerDialog::showDecoded(const QByteArray &wire, qint64 arrivalMs)
{
    if (wire.isEmpty()) {
        m_rawPanel->clear();
        m_fieldPanel->clear();
        return;
    }
    // Same conversion the dispatcher does on ingest, so the panel shows what
    // the receiving console will show and not a second interpretation.
    static ColorRules s_empty;
    const LogEntryPtr e = MessageDispatcher::buildEntry(
        wire, arrivalMs, m_rules ? m_rules : &s_empty, false);
    m_rawPanel->showEntry(e);
    m_fieldPanel->showEntry(e);
}

void DlrPlayerDialog::onSeekQueryEdited()
{
    // Validate as typed rather than at play time, so a typo is visible
    // before it costs a run.
    const QString text = m_seekQuery->text();
    if (text.trimmed().isEmpty()) {
        m_seekQuery->clearQueryError();
        m_skipBtn->setEnabled(false);
        return;
    }

    LogQuery q;
    q.parse(text);
    if (q.isValid()) { m_seekQuery->clearQueryError(); }
    else             { m_seekQuery->setQueryError(q.errorString(), q.errorOffset()); }

    m_skipBtn->setEnabled(m_running && q.isValid());
}

void DlrPlayerDialog::onSkipToMatch()
{
    if (m_player) { m_player->skipToNextMatch(); }
    m_status->say(tr("Skipping…"));
}

void DlrPlayerDialog::onSeekLanded(qint64 playbackMs, QString text)
{
    // Show WHAT was matched, not just where. A position alone leaves you
    // wondering whether the query found the record you meant.
    QString line = text.simplified();
    if (line.size() > 110) { line = line.left(107) + QStringLiteral("…"); }
    m_status->ok(tr("Landed at %1 — %2").arg(fmtDuration(playbackMs), line));

    // The landed record is the one that was being hunted, so it is shown
    // whether or not the panel is following the stream.
    if (m_decodeTabs->isVisible()) {
        qint64 ms = 0;
        showDecoded(m_player->tappedWire(&ms), ms);
    }
}

void DlrPlayerDialog::onSeekExhausted()
{
    m_status->fail(tr("Seek query never matched — nothing was sent."));
}

void DlrPlayerDialog::onFinished(bool completed)
{
    m_running = false;
    setRunningUi(false);
    // "Finished." must not paper over the reason the run produced nothing.
    // This used to be a startsWith() against the exact sentence, which is a
    // comparison that survives exactly as long as nobody rewords it.
    if (!m_status->isSticky()) {
        m_status->say(completed ? tr("Finished.") : tr("Stopped."));
    }
    if (completed) { m_progress->setValue(1000); }
}

void DlrPlayerDialog::onFailed(QString reason)
{
    m_running = false;
    setRunningUi(false);
    m_status->fail(reason);
    QMessageBox::warning(this, tr("Replay failed"), reason);
}

void DlrPlayerDialog::reject()
{
    if (SendGuard::interceptReject(m_running, [this] {
            onStop();
            m_status->warn(tr("Replay stopped. Press Escape again to close."));
        })) {
        return;
    }
    QDialog::reject();
}
