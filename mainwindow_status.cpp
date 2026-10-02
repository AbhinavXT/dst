// =============================================================================
//  mainwindow_status.cpp -- status bar, frame clock, bind state, notifications, drop banner, decode failures
//  Part of MainWindow: split out of mainwindow.cpp in session 89 so each
//  concern can be read, and recompiled, on its own. No behaviour change.
// =============================================================================
#include "mainwindow.h"
#include "udpcommunication.h"
#include <QSettings>
#include "frameclock.h"
#include "pinpanel.h"
#include "watchpanel.h"
#include "framenumberwatch.h"
#include "emptystate.h"
#include "uistyle.h"
#include "uicolors.h"
#include "ui_mainwindow.h"

#include "exporter.h"
#include "exportdialog.h"
#include "commandpalette.h"
#include "comparewindow.h"
#include "archivesearchwindow.h"
#include "fieldcatalog.h"
#include "fieldindexdialog.h"
#include "fieldinspector.h"
#include "fieldplot.h"
#include "filterbar.h"
#include "findbar.h"
#include "gototimestampdialog.h"
#include "logmodel.h"
#include "logtableview.h"
#include "logwriter.h"
#include "rawbytespanel.h"
#include "markerscrollbar.h"
#include "mergedwindow.h"
#include "notificationcenter.h"
#include "searchwindow.h"
#include "savedata.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "settings.h"
#include "settingsdialog.h"
#include "stickymenu.h"
#include "schema/schemadecoder.h"
#include "timelineribbon.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDateTime>
#include <QClipboard>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QToolButton>
#include <QHeaderView>
#include <QInputDialog>
#include <QMouseEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QMessageBox>
#include <QMetaType>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QTabBar>
#include <QTableView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <limits>

#include "lococonsolewindow.h"
#include "brakingpanel.h"
#include "faultpanelwindow.h"
#include "decodeworkbench.h"
#include "flasherwindow.h"
#include "lococonfigwindow.h"
#include "minimizeddock.h"
#include "statuspins.h"
#include "presentationmode.h"
#include "tabpopoutwindow.h"
#include "undolog.h"
#include "clockskewalarm.h"
#include "dmipanel.h"
#include "dmitimetravel.h"
#ifdef DL_HAVE_SERIAL
#include "serialconsolewindow.h"
#include "serialmanager.h"
#endif
#include "runreportwindow.h"
#include "speeddistance.h"
#include "workspacesnapshot.h"
#include "tabtags.h"
#include "textzoom.h"
#include "windowgeometry.h"
#include "fieldsweepdialog.h"
#include "framediffwindow.h"
#include "roundtripwindow.h"
#include "packetmakerdialog.h"
#include "packetsequencedialog.h"
#include "dlrplayerdialog.h"
#include "sessionkeydialog.h"
#include "sessionkeystore.h"

// tshark -i any -f "udp port 50002" -T fields -e data

// The status-bar widgets, left to right. Called once, from the constructor.
void MainWindow::buildStatusBar()
{
    m_lblBindStatus = new QLabel(QStringLiteral("Binding…"), this);
    m_lblQueueDepth = new QLabel(QStringLiteral("Queue: 0"), this);
    m_lblDropStatus = new QLabel(QStringLiteral("Drops: 0"), this);
    m_lblDiskStatus = new QLabel(QStringLiteral("Disk: ✓"), this);
    m_lblDiskStatus->setStyleSheet(UiColor::okStyle());
    statusBar()->addPermanentWidget(m_lblBindStatus);
    statusBar()->addPermanentWidget(m_lblQueueDepth);
    statusBar()->addPermanentWidget(m_lblDropStatus);
    // The equipment's clock. It sits first because the number an operator
    // wants at a glance is not the port but whether this laptop still agrees
    // with the equipment about what time it is — when it stops agreeing,
    // everything sent from here is discarded at the far end and nothing else
    // on screen says so.
    m_lblFrameClock = new QLabel(this);
    m_lblFrameClock->setObjectName(QStringLiteral("frameClockLabel"));
    statusBar()->addPermanentWidget(m_lblFrameClock);

    // The station's clock beside the loco's, and the gap between them. Two
    // clocks that are meant to agree are worth showing together; the gap is
    // spelled out because it is the number the two ends actually check each
    // other on, and subtracting two times of day in one's head at a glance is
    // exactly the sort of thing that gets a test signed off wrongly.
    m_lblStationClock = new QLabel(this);
    m_lblStationClock->setObjectName(QStringLiteral("stationClockLabel"));
    statusBar()->addPermanentWidget(m_lblStationClock);

    m_lblClockGap = new QLabel(this);
    m_lblClockGap->setObjectName(QStringLiteral("clockGapLabel"));
    statusBar()->addPermanentWidget(m_lblClockGap);
    // Click any of the three: the last 30 minutes of them (session 82).
    for (QLabel *l : { m_lblFrameClock, m_lblStationClock, m_lblClockGap }) {
        l->installEventFilter(this);
        l->setCursor(Qt::PointingHandCursor);
    }
    statusBar()->addPermanentWidget(m_lblDiskStatus);

    // Session 102: one chip per open serial port, so a port capturing with
    // no terminal window open is never invisible. Empty and hidden until
    // one opens.
    m_serialChips = new QWidget(this);
    m_serialChips->setObjectName(QStringLiteral("serialChips"));
    auto *chipRow = new QHBoxLayout(m_serialChips);
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->setSpacing(2);
    m_serialChips->hide();
    statusBar()->addPermanentWidget(m_serialChips);

    connect(m_frameWatch, &FrameNumberWatch::observed,
            this, [this](qint64, int) { refreshFrameClock(); });
    {
        // The skew is against this laptop's clock, so it goes stale on its own
        // between frames: a second of wall time is a second of drift whether
        // or not anything arrived. Once a second is enough to keep the reading
        // honest and is not worth noticing.
        auto *tick = new QTimer(this);
        tick->setInterval(1000);
        connect(tick, &QTimer::timeout, this, &MainWindow::refreshFrameClock);
        tick->start();
    }
    refreshFrameClock();
}

// -----------------------------------------------------------------------
//  Tab construction (Patch C: builds new OR re-shows hidden)
// -----------------------------------------------------------------------
// =============================================================================
//  The equipment's clock in the status bar.
//
//  FRAME_NUM is seconds since midnight plus one, so every ARP and LSRP that
//  arrives carries the equipment's time of day. What is worth showing is not
//  that time on its own — the operator can read a clock — but the DIFFERENCE
//  between it and this laptop's, because the far end rejects anything more
//  than 4 s old or 2 s early and says nothing when it does.
// =============================================================================
// One side's readout. Both clocks are shown the same way, so they can be
// compared at a glance — the reason for having both is the moment they stop
// agreeing, and that is only visible if they are presented identically.
void MainWindow::paintClockLabel(QLabel *lbl, const QString &who,
                                 const FrameNumberWatch::Seen &seen,
                                 qint64 ageMs)
{
    if (!lbl) { return; }

    if (!seen.valid()) {
        // Say nothing was seen rather than showing this laptop's own clock,
        // which would read as agreement between two things never compared.
        lbl->setText(tr("%1 —").arg(who));
        lbl->setStyleSheet(UiColor::mutedStyle());
        lbl->setToolTip(tr("No %1 frame seen yet, so that clock is unknown.\n"
                           "It is read from FRAME_NUM, which is seconds since "
                           "midnight plus one.")
                            .arg(who == tr("Stn") ? tr("SLRP")
                                                  : tr("ARP or LSRP")));
        return;
    }

    const QString when = FrameClock::timeText(seen.value);
    if (when.isEmpty()) {
        lbl->setText(tr("%1 ?").arg(who));
        lbl->setStyleSheet(UiColor::errorStyle());
        lbl->setToolTip(tr("FRAME_NUM %1 is not a time of day. Seconds in a "
                           "day run to 86400, so a number outside that is not "
                           "a clock reading.").arg(seen.value));
        return;
    }

    const int skew = FrameClock::skewSeconds(seen.value, QDateTime::currentDateTime());
    const FrameClock::Accept a = FrameClock::classify(skew);

    lbl->setText(skew == 0
        ? tr("%1 %2").arg(who, when)
        : tr("%1 %2 (%3%4s)").arg(who, when,
                                  skew > 0 ? QStringLiteral("+") : QStringLiteral("-"))
                             .arg(qAbs(skew)));

    switch (a) {
    case FrameClock::Accept::Ok:       lbl->setStyleSheet(UiColor::mutedStyle());   break;
    case FrameClock::Accept::Marginal: lbl->setStyleSheet(UiColor::warningStyle()); break;
    case FrameClock::Accept::Stale:
    case FrameClock::Accept::Ahead:    lbl->setStyleSheet(UiColor::errorStyle());   break;
    }

    lbl->setToolTip(
        tr("%1 %2, FRAME_NUM %3, seen %4 s ago.\n\n"
           "This laptop reads %5.\n"
           "%6")
            .arg(seen.captype.toUpper(),
                 seen.locoId >= 0 ? tr("from loco %1").arg(seen.locoId)
                                  : tr("from an unnamed source"))
            .arg(seen.value)
            .arg(ageMs >= 0 ? ageMs / 1000 : 0)
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                 FrameClock::acceptText(a)));
}

void MainWindow::refreshFrameClock()
{
    FrameNumberWatch &w = *m_frameWatch;
    // Session 82: after the labels are painted, one history sample a second.
    struct Recorder {
        MainWindow *self;
        ~Recorder() { self->recordClockHistory(); }
    } recorder{ this };

    const FrameNumberWatch::Seen loco = w.latest();
    const FrameNumberWatch::Seen stn  = w.latestStation();

    paintClockLabel(m_lblFrameClock,   tr("Loco"), loco, w.ageMs());
    paintClockLabel(m_lblStationClock, tr("Stn"),  stn,
                    stn.valid() ? QDateTime::currentMSecsSinceEpoch() - stn.atMs : -1);

    // The two clocks against EACH OTHER. This is the number that matters
    // during an acceptance run: the loco rejects the station's packets, and
    // the station the loco's, on exactly this difference — not on how either
    // compares to a laptop that is not part of the system at all.
    if (m_lblClockGap) {
        if (!loco.valid() || !stn.valid()) {
            m_lblClockGap->clear();
            m_lblClockGap->setToolTip(QString());
        } else {
            const int lsec = FrameClock::secondsSinceMidnight(loco.value);
            const int ssec = FrameClock::secondsSinceMidnight(stn.value);
            int gap = lsec - ssec;
            if (gap >  FrameClock::kSecondsPerDay / 2) { gap -= FrameClock::kSecondsPerDay; }
            if (gap < -FrameClock::kSecondsPerDay / 2) { gap += FrameClock::kSecondsPerDay; }

            m_lblClockGap->setText(gap == 0 ? tr("in step")
                                            : tr("%1%2s apart")
                                                  .arg(gap > 0 ? QStringLiteral("+")
                                                               : QStringLiteral("-"))
                                                  .arg(qAbs(gap)));
            const FrameClock::Accept ga = FrameClock::classify(gap);
            m_lblClockGap->setStyleSheet(ga == FrameClock::Accept::Ok
                                             ? UiColor::mutedStyle()
                                             : (ga == FrameClock::Accept::Marginal
                                                    ? UiColor::warningStyle()
                                                    : UiColor::errorStyle()));
            // The alarm (session 81): only while both clocks are being heard
            // now — a gap between two stale readings is history.
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const bool fresh = (nowMs - loco.atMs) <= 10000 && (nowMs - stn.atMs) <= 10000;
            if (m_skewAlarm) m_skewAlarm->sample(nowMs, fresh, gap);
            m_clockGapNow = fresh ? gap : INT_MIN;
            m_lblClockGap->setToolTip(
                tr("The loco's counter minus the station's. Click for the last 30 minutes.\n\n"
                   "The two ends check each other on this difference, so it "
                   "matters more than either one's gap to this laptop.\n%1")
                    .arg(FrameClock::acceptText(ga)));
        }
    }
}

void MainWindow::recordClockHistory()
{
    FrameNumberWatch &w = *m_frameWatch;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const FrameNumberWatch::Seen loco = w.latest();
    const FrameNumberWatch::Seen stn = w.latestStation();
    ClockHistory::Sample s;
    s.ms = nowMs;
    if (loco.valid() && nowMs - loco.atMs <= 10000 && !FrameClock::timeText(loco.value).isEmpty()) {
        s.hasLoco = true;
        s.locoSkew = FrameClock::skewSeconds(loco.value, QDateTime::currentDateTime());
    }
    if (stn.valid() && nowMs - stn.atMs <= 10000 && !FrameClock::timeText(stn.value).isEmpty()) {
        s.hasStn = true;
        s.stnSkew = FrameClock::skewSeconds(stn.value, QDateTime::currentDateTime());
    }
    if (m_clockGapNow != INT_MIN && s.hasLoco && s.hasStn) {
        s.hasGap = true;
        s.gap = m_clockGapNow;
    }
    if (s.hasLoco || s.hasStn) m_clockHistory.add(s);
}

void MainWindow::showClockHistory()
{
    auto *w = new ClockHistoryWindow(&m_clockHistory, this);
    w->show();
    w->raise();
}

// -----------------------------------------------------------------------
//  Status-bar slots
// -----------------------------------------------------------------------
void MainWindow::onBindSucceeded(quint16 port)
{
    m_lblBindStatus->setText(QString("\u25CF Listening on UDP/%1").arg(port));
    // A chip in the top strip (session 118): its tone, not a colour of its
    // own, so its text keeps the contrast floor in every theme.
    UiStyle::setTone(m_lblBindStatus, UiStyle::Tone::Ok);
    m_bindOk    = true;
    m_boundPort = port;
    m_bindError.clear();
    updateEmptyState();
}

void MainWindow::onBindFailed(QString reason)
{
    m_lblBindStatus->setText(QString("\u2715 Bind failed: %1").arg(reason));
    UiStyle::setTone(m_lblBindStatus, UiStyle::Tone::Fail);
    m_bindOk    = false;
    m_bindError = reason;
    updateEmptyState();
    // A failed bind means the application will never receive anything. That
    // is not a status-bar detail; it is the whole purpose of the tool
    // failing, so it goes through the notification centre as an error and
    // stays until acknowledged.
    notify(NoteLevel::Error,
           tr("Not listening — UDP bind failed: %1").arg(reason),
           tr("Nothing will be received until this is resolved. Check that "
              "no other process is using the port, then change it under "
              "Settings if needed."));
}

void MainWindow::updateEmptyState()
{
    if (!m_emptyState) return;

    // Any tab at all means there is something to look at.
    if (!m_tabs.isEmpty()) {
        m_emptyState->hide();
        return;
    }

    QString html;
    if (!m_bindOk && !m_bindError.isEmpty()) {
        html = tr("<h3>Not listening</h3>"
                  "<p>The UDP socket could not be opened:<br><b>%1</b></p>"
                  "<p>Nothing can be received until this is fixed.<br>"
                  "Check whether another process holds the port, or change "
                  "it in <b>Settings</b>.</p>")
                   .arg(m_bindError.toHtmlEscaped());
    } else if (m_bindOk) {
        // The distinction that matters: we ARE listening and nothing has
        // arrived. Without saying so, silence is indistinguishable from a
        // broken application.
        html = tr("<h3>Listening on UDP port %1</h3>"
                  "<p>No messages have arrived yet. A tab will appear here "
                  "for each source that reports in.</p>"
                  // palette(mid) here was a frame colour, not a text one:
                  // #DCDEE0 on the light panel, which rendered this
                  // paragraph as barely-there grey-on-grey. The disabled
                  // text role is the dim-but-still-readable one.
                  "<p style='color:%3'>Only datagrams addressed to "
                  "this console (destination&nbsp;id&nbsp;%2) are shown.<br>"
                  "Recorded sessions can be opened from "
                  "<b>File&nbsp;→&nbsp;Open recorded session</b>.</p>")
                   .arg(m_boundPort).arg(kThisConsoleId)
                   .arg(palette().color(QPalette::Disabled, QPalette::Text).name());
    } else {
        html = tr("<h3>Starting…</h3><p>Opening the UDP socket.</p>");
    }

    m_emptyState->setText(html);
    m_emptyState->resize(ui->tabWidget->size());
    m_emptyState->move(0, 0);
    m_emptyState->raise();
    m_emptyState->show();
}

void MainWindow::onStatusTick()
{
    // Resolve any implication whose window has closed. observe() does this
    // on message time, but a precondition arriving just before traffic
    // stops would otherwise sit armed for ever and be reported as neither
    // answered nor unanswered.
    m_tests.expireArmed(QDateTime::currentMSecsSinceEpoch());

    if (!m_receiver) return;

    // Session 90: by cause, so the operator can tell a network fault from
    // a PC that cannot keep up.
    const quint64 malformed = m_receiver->malformedCount();
    const quint64 queueFull = m_receiver->queueFullCount();
    const int     depth     = m_receiver->currentQueueDepth();

    m_lblQueueDepth->setText(QString("Queue: %1").arg(depth));

    m_lblDropStatus->setText(dropStatusText(malformed, queueFull));
    m_lblDropStatus->setToolTip(dropStatusTooltip(malformed, queueFull));
    if (malformed == 0 && queueFull == 0) m_lblDropStatus->setStyleSheet("");
    else m_lblDropStatus->setStyleSheet(UiColor::errorStyle() + QStringLiteral(" font-weight:bold;"));

    // Disk status (2b).
    if (!m_writer) {
        // Continuous logging switched off. Shown explicitly rather than as
        // a green tick, so nobody reads "no errors" as "being recorded".
        m_lblDiskStatus->setText(tr("Disk: off"));
        m_lblDiskStatus->setStyleSheet(UiColor::mutedStyle());
        m_lblDiskStatus->setToolTip(
            tr("Continuous logging is disabled in Settings. "
               "Nothing is being written to disk."));
    } else if (m_diskQuotaSuspended) {
        m_lblDiskStatus->setText(tr("Disk: full"));
        m_lblDiskStatus->setStyleSheet(UiColor::warningStyle() + QStringLiteral(" font-weight:bold;"));
        m_lblDiskStatus->setToolTip(
            tr("Log folder reached its %1 MiB limit; logging is suspended.\n"
               "Free space under %2 and it resumes automatically.")
                .arg(double(m_diskQuotaLimit) / (1024.0 * 1024.0), 0, 'f', 0)
                .arg(Settings::diskLogRoot()));
    } else if (m_writer->hadError()) {
        m_lblDiskStatus->setText("Disk: ✗");
        m_lblDiskStatus->setStyleSheet(UiColor::errorStyle() + QStringLiteral(" font-weight:bold;"));
        m_lblDiskStatus->setToolTip(
            tr("At least one log file could not be written. See the "
               "application log for details."));
    } else {
        // Was one-way: once any single file poisoned, the indicator stuck
        // at ✗ for the rest of the session even after the condition cleared
        // (day rollover reopens every file), which trains operators to
        // ignore it.
        m_lblDiskStatus->setText("Disk: ✓");
        m_lblDiskStatus->setStyleSheet("");
        const qint64 limit = m_writer->maxFolderBytes();
        m_lblDiskStatus->setToolTip(
            limit > 0
                ? tr("Log folder: %1 MiB of %2 MiB used")
                      .arg(double(m_writer->folderBytes()) / (1024.0 * 1024.0), 0, 'f', 1)
                      .arg(double(limit) / (1024.0 * 1024.0), 0, 'f', 0)
                : tr("Log folder: %1 MiB used (no limit set)")
                      .arg(double(m_writer->folderBytes()) / (1024.0 * 1024.0), 0, 'f', 1));
    }

    if (malformed > m_lastReportedMalformed || queueFull > m_lastReportedQueueFull) {
        emitDropBanner(malformed - m_lastReportedMalformed, queueFull - m_lastReportedQueueFull);
        m_lastReportedMalformed = malformed;
        m_lastReportedQueueFull = queueFull;
    }

    // (2f) Refresh tab title colors based on staleness.
    refreshTabHealth();
    refreshSourceList();
}

void MainWindow::notify(NoteLevel level, const QString &text,
                        const QString &detail)
{
    if (m_notify) m_notify->post(level, text, detail);

    // Mirror anything non-routine to the application log too: a message the
    // operator dismissed is still worth having in a support bundle.
    if (level != NoteLevel::Info) qWarning().noquote() << text;
}

void MainWindow::onShowNotificationLog()
{
    if (!m_notify) return;
    auto *log = new NotificationLog(m_notify->history(), this);
    log->show();
    log->raise();
}

void MainWindow::refreshTabHealth()
{
    if (m_offlineWarnSec <= 0 && m_offlineErrSec <= 0) return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 warnMs = qint64(m_offlineWarnSec) * 1000;
    const qint64 errMs  = qint64(m_offlineErrSec)  * 1000;

    // Active tab text color is driven by the palette in dark mode; we
    // override per-tab via QTabBar::setTabTextColor. To "reset" a tab
    // back to default (current message stream is healthy) we set an
    // invalid QColor — Qt then uses the palette default.
    QTabBar *bar = ui->tabWidget->tabBar();

    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        TabUi &t = it.value();
        if (!t.visible) continue;     // hidden tabs have no title to color
        auto idxIt = m_tabIndex.constFind(t.tabKey);
        if (idxIt == m_tabIndex.constEnd()) continue;
        const int idx = idxIt.value();
        if (idx < 0 || idx >= bar->count()) continue;

        if (t.lastSeenMs <= 0) {
            // Brand new tab, never saw a message yet — leave default.
            bar->setTabTextColor(idx, QColor());
            continue;
        }
        const qint64 silentMs = nowMs - t.lastSeenMs;

        QColor color;     // invalid → default
        if (m_offlineErrSec > 0 && silentMs >= errMs) {
            color = UiColor::error();
        } else if (m_offlineWarnSec > 0 && silentMs >= warnMs) {
            color = UiColor::warning();
        }
        bar->setTabTextColor(idx, color);
    }
}

void MainWindow::emitDropBanner(quint64 newMalformed, quint64 newQueueFull)
{
    // The dispatcher does the heavy lifting — it owns all the models and
    // can inject a synthetic Severity::Error row into each one. One row per
    // cause (session 90), so the log says which fault it was.
    for (const QString &line : dropBannerLines(newMalformed, newQueueFull))
        m_dispatcher->injectBannerToAllTabs(line);
    updateLogCount();
}

// -----------------------------------------------------------------------
//  Toast + counter helpers
// -----------------------------------------------------------------------
void MainWindow::onUserLabelTimeout()
{
    ui->userMsglb->hide();
}

void MainWindow::updateLogCount()
{
    const QString key = currentTabKey();
    if (key.isEmpty()) {
        ui->logCountLb->setText(QStringLiteral("0"));
        return;
    }
    LogModel *model = m_dispatcher->modelForKey(key);
    if (!model) return;
    // Grouped: a per-tab capacity of 200000 reads as "200,000" at a glance
    // and as an unparseable run of digits without.
    ui->logCountLb->setText(QLocale().toString(model->count()));
}

void MainWindow::onDecodeFailed(const LogEntryPtr &entry, const QString &reason)
{
    if (!entry || !m_failList) return;

    const QString key = entry->tabKey();

    // Same frame re-selected shouldn't pile up duplicates.
    for (const auto &ref : m_failRefs) {
        if (ref.first == key && ref.second == entry->epochMs) return;
    }

    // Bounded. A systematically unhandled message type would otherwise add
    // an entry per frame and turn a diagnostic aid into a memory leak.
    constexpr int kMaxFailures = 500;
    if (m_failRefs.size() >= kMaxFailures) {
        m_failRefs.removeFirst();
        delete m_failList->takeItem(0);
    }

    m_failRefs.append({ key, entry->epochMs });
    m_failList->addItem(
        tr("%1  %2  %3  —  %4")
            .arg(QDateTime::fromMSecsSinceEpoch(entry->epochMs)
                     .toString("HH:mm:ss.zzz"),
                 key, reason, entry->text.left(80)));

    m_failDock->setWindowTitle(tr("Decode failures (%1)").arg(m_failRefs.size()));
    if (m_failDock->isHidden()) m_failDock->show();
}

void MainWindow::onDecodeFailureActivated(int row)
{
    if (row < 0 || row >= m_failRefs.size()) return;
    jumpToEntry(m_failRefs.at(row).first, m_failRefs.at(row).second);
}

void MainWindow::rebuildSerialChips()
{
#ifdef DL_HAVE_SERIAL
    if (!m_serialChips || !m_serial) return;
    QLayout *row = m_serialChips->layout();
    while (QLayoutItem *it = row->takeAt(0)) {
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
    const QStringList ports = m_serial->openPorts() + m_serial->reconnectingPorts();
    for (const QString &port : ports) {
        const SerialLink *link = m_serial->link(port);
        auto *chip = new QToolButton(m_serialChips);
        chip->setAutoRaise(true);
        chip->setObjectName(QStringLiteral("serialChip"));
        chip->setProperty("port", port);
        connect(chip, &QToolButton::clicked, this, [this, port]() { openSerialTerminal(port); });
        row->addWidget(chip);
    }
    m_serialChips->setVisible(!ports.isEmpty());
    if (m_serialChipsAction) m_serialChipsAction->setVisible(!ports.isEmpty());   // in the top strip
    refreshSerialChips();
#endif
}

void MainWindow::refreshSerialChips()
{
#ifdef DL_HAVE_SERIAL
    if (!m_serialChips || !m_serial) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (QToolButton *chip : m_serialChips->findChildren<QToolButton *>(QStringLiteral("serialChip"))) {
        const QString port = chip->property("port").toString();
        const SerialLink *link = m_serial->link(port);
        const bool waiting = m_serial->isReconnecting(port);
        const bool failing = waiting || m_serial->health(port, now).failing();
        // The dot and the name, not colour alone; a warning sign when the
        // lines do not decode (wrong baud, most likely).
        const QString label = m_serial->label(port);
        chip->setText((waiting ? QStringLiteral("\u25CC ")
                               : failing ? QStringLiteral("\u26A0 ") : QStringLiteral("\u25CF "))
                      + SerialManager::shortName(port)
                      + (label.isEmpty() ? QString() : QStringLiteral(" ") + label)
                      + (waiting ? tr(" reconnecting\u2026") : QString()));
        chip->setStyleSheet(failing ? UiColor::warningStyle() : UiColor::okStyle());
        if (waiting) {
            chip->setToolTip(tr("%1 was lost (the adapter went away). It reopens by itself when the "
                                "same adapter comes back, under any port name.\nClick to open a terminal on it.")
                                 .arg(port));
            chip->setAccessibleName(tr("Serial port %1 lost, reconnecting").arg(port));
            continue;
        }
        chip->setToolTip(tr("%1 open, %2%3\n%4\nClick to open a terminal on it.")
                             .arg(port, link ? link->config().summary() : QString(),
                                  m_serial->feeds(port)
                                      ? tr(", feeding tab \u201C%1\u201D").arg(m_serial->titleFor(port))
                                      : tr(", not feeding the console"),
                                  m_serial->healthText(port, now)));
        chip->setAccessibleName(failing ? tr("Serial port %1 open, lines not decoding").arg(port)
                                        : tr("Serial port %1 open").arg(port));
    }
#endif
}
