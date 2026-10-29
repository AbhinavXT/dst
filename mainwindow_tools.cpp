// =============================================================================
//  mainwindow_tools.cpp -- Tools-menu windows and the hand-offs between them (workbench, packet maker, frame diff, flasher, reports)
//  Part of MainWindow: split out of mainwindow.cpp in session 89 so each
//  concern can be read, and recompiled, on its own. No behaviour change.
// =============================================================================
#include "mainwindow.h"
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
#include <QFileInfo>
#include <QTimer>
#include "settingsdialog.h"
#include "stickymenu.h"
#include "schema/schemadecoder.h"
#include "schemaeditorwindow.h"
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
#include "brakebenchwindow.h"
#include "locosimwindow.h"
#include "benchrecord.h"
#include "benchrecordwindow.h"
#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"
#endif
#include "runreportwindow.h"
#include "radiohealthwindow.h"
#include "nmslogwindow.h"
#include "odometrywindow.h"
#include "fleetwindow.h"
#include "faulttimelinewindow.h"
#include "iotimelinewindow.h"
#include "incidentreportdialog.h"
#include "incidentreportwindow.h"
#include "speeddistance.h"
#include "twolocowindow.h"
#include "soswindow.h"
#include "readerdirwindow.h"
#include "stationlayoutwindow.h"
#include "testcatalogwindow.h"
#include "tagbuilderwindow.h"
#include "trackdiagramwindow.h"
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

void MainWindow::onActionLocoConsole()
{
    // A standalone live view of current loco state, built from the capture
    // stream. Like CompareWindow, it manages its own lifetime; multiple may
    // be opened (e.g. one per monitor) and each subscribes to the dispatcher.
    auto *console = new LocoConsoleWindow(m_dispatcher, this, m_tabTags, m_sessionKeys);
    console->setAttribute(Qt::WA_DeleteOnClose);
    connect(console, &LocoConsoleWindow::jumpRequested, this, &MainWindow::jumpToEntry);
    attachTestStrip(console);
    console->show();
    console->raise();
    console->activateWindow();
}

void MainWindow::onActionFaultPanel()
{
    // Standalone live "active faults" board from the NMS fault stream. Manages
    // its own lifetime; multiple may be opened, each subscribing to dispatcher.
    auto *panel = new FaultPanelWindow(m_dispatcher, this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->show();
    panel->raise();
    panel->activateWindow();
}

void MainWindow::onActionBrakingPanel()
{
    // Standalone braking-curve board over the @uba stream. Owns its lifetime
    // and subscribes to the dispatcher itself, like the fault panel.
    auto *panel = new BrakingPanel(m_dispatcher, this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    // "Show frame in log" is the panel asking the main window to do the one
    // thing it cannot: move the selection in the capture table.
    connect(panel, &BrakingPanel::jumpToTimeRequested,
            this,  [this](const QString &tabKey, qint64 epochMs) {
                jumpToEntry(tabKey, epochMs);
            });
    panel->show();
    panel->raise();
    panel->activateWindow();
}

void MainWindow::onActionDecodeWorkbench()
{
    // Standalone "paste a frame, see it decoded" tool. Independent of the
    // capture stream and of its own lifetime; multiple may be opened.
    auto *wb = new DecodeWorkbench(this, m_sessionKeys);
    wb->setAttribute(Qt::WA_DeleteOnClose);
    wb->show();
    wb->raise();
    wb->activateWindow();
}

void MainWindow::onActionFirmwareFlasher()
{
    // Single instance (see m_flasher). Its lifetime is its own, like the
    // workbench, but it refuses to close while a flash is running.
    if (!m_flasher) {
        m_flasher = new FlasherWindow(this);
        m_flasher->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_flasher->show();
    m_flasher->raise();
    m_flasher->activateWindow();
}

void MainWindow::onActionSchemaEditor()
{
    if (!m_schemaEditor) {
        // The file the decoder uses: the external schema when one is set.
        const QString external = Settings::schemaPath();
        m_schemaEditor = new SchemaEditorWindow(external.isEmpty() ? QStringLiteral(":/schema/kavach.xml") : external, this);
        m_schemaEditor->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_schemaEditor, &SchemaEditorWindow::schemaSaved, this,
                [this](const QString &path, const QString &summary) {
            if (Settings::schemaPath() == path) onActionReloadSchema();
            notify(NoteLevel::Info, tr("Schema saved: %1").arg(QFileInfo(path).fileName()), summary);
            // Loco Configuration lays its form out from LINFO when it opens:
            // rebuilt on the new file, as after its own field edits (session 307).
            if (m_locoConfig) {
                QTimer::singleShot(0, this, [this]() {
                    if (!m_locoConfig) return;
                    m_locoConfig->close();
                    m_locoConfig = nullptr;
                    onActionLocoConfig();
                });
            }
        });
        // An editable copy of the built-in schema, now the external schema.
        connect(m_schemaEditor, &SchemaEditorWindow::schemaFileChanged, this,
                [this](const QString &) { onActionReloadSchema(); });
    }
    m_schemaEditor->show();
    m_schemaEditor->raise();
    m_schemaEditor->activateWindow();
}

void MainWindow::onActionLocoConfig()
{
    if (!m_locoConfig) {
        m_locoConfig = new LocoConfigWindow(this);
        m_locoConfig->setAttribute(Qt::WA_DeleteOnClose);
        // Checks each loco's periodic @linfo against what was sent to it.
        m_locoConfig->setLiveSource(m_dispatcher);
        // The loco's answer to a send, as a notification: the window may be
        // behind others by the time the next @linfo arrives (session 81).
        connect(m_locoConfig, &LocoConfigWindow::verified, this,
                [this](const QString &text, bool match, const QString &detail) {
            notify(match ? NoteLevel::Info : NoteLevel::Warning, text, detail);
        });
        // Session 307: a field added or removed in kavach.xml. The decoder
        // reads the same file (@linfo must follow), and the window is
        // rebuilt on the new layout: closed (saving) and opened again.
        connect(m_locoConfig, &LocoConfigWindow::schemaEdited, this,
                [this](const QString &path, const QString &summary) {
            if (Settings::schemaPath() == path) onActionReloadSchema();
            notify(NoteLevel::Info, tr("LOCO_INFO changed in %1").arg(QFileInfo(path).fileName()), summary);
            QTimer::singleShot(0, this, [this]() {
                if (m_locoConfig) {
                    m_locoConfig->close();
                    m_locoConfig = nullptr;
                }
                onActionLocoConfig();
            });
        });
    }
    m_locoConfig->show();
    m_locoConfig->raise();
    m_locoConfig->activateWindow();
}

void MainWindow::connectLoopback(PacketMakerDialog *pm)
{
    // Session 242: the frame enters the pipeline as a serial line does, under
    // its own source, so it decodes, tabs and records like a received one.
    pm->setLoopbackAvailable(true);
    connect(pm, &PacketMakerDialog::loopbackRequested, this, [this](const QString &line) {
        m_dispatcher->ingestLocal(PacketMakerDialog::kLoopbackSourceId, 0, line.toUtf8(),
                                  QDateTime::currentMSecsSinceEpoch(), tr("Packet Maker loopback"));
    });
    // Session 243: the reply watch sees every accepted entry.
    connect(m_dispatcher, &MessageDispatcher::entryAppended, pm,
            [pm](const QString &, const LogEntryPtr &e) { pm->observe(e); });
}

void MainWindow::onActionPacketMaker()
{
    // Compose a station/loco packet, self-verify it, and (optionally) send it.
    // The only transmit path in DLConsole; independent lifetime like the
    // workbench, so more than one can be open.
    auto *pm = new PacketMakerDialog(this, m_frameWatch, m_sessionKeys);
    pm->setAttribute(Qt::WA_DeleteOnClose);
    connectLoopback(pm);
    pm->show();
    pm->raise();
    pm->activateWindow();
}

// ---------------------------------------------------------------------------
//  Taking a buffer to the Decode Workbench / Packet Maker
// ---------------------------------------------------------------------------
void MainWindow::openBufferInWorkbench(const QString &bufferText)
{
    if (bufferText.trimmed().isEmpty()) {
        notify(NoteLevel::Warning, tr("Nothing to decode — the buffer is empty."));
        return;
    }
    if (!m_bufferWorkbench) {
        m_bufferWorkbench = new DecodeWorkbench(this, m_sessionKeys);
        m_bufferWorkbench->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_bufferWorkbench->loadBuffer(bufferText);
    m_bufferWorkbench->show();
    m_bufferWorkbench->raise();
    m_bufferWorkbench->activateWindow();
}

void MainWindow::openBufferInPacketMaker(const QString &bufferText)
{
    if (bufferText.trimmed().isEmpty()) {
        notify(NoteLevel::Warning, tr("Nothing to load — the buffer is empty."));
        return;
    }
    // A maker that is mid-send is doing something the operator started and
    // is watching. Overwriting its fields under it would change what goes on
    // the wire with no warning, so that one gets a window of its own.
    if (m_bufferPacketMaker && m_bufferPacketMaker->isSending()) {
        m_bufferPacketMaker = nullptr;
    }
    if (!m_bufferPacketMaker) {
        m_bufferPacketMaker = new PacketMakerDialog(this, m_frameWatch, m_sessionKeys);
        m_bufferPacketMaker->setAttribute(Qt::WA_DeleteOnClose);
        connectLoopback(m_bufferPacketMaker);
    }
    m_bufferPacketMaker->loadBuffer(bufferText);
    m_bufferPacketMaker->show();
    m_bufferPacketMaker->raise();
    m_bufferPacketMaker->activateWindow();
}

void MainWindow::openEntryInWorkbench(const LogEntryPtr &entry)
{
    const QString buf = entryBufferText(entry);
    if (buf.isEmpty()) {
        notify(NoteLevel::Warning, tr("That row carries no bytes to decode."));
        return;
    }
    openBufferInWorkbench(buf);
}

void MainWindow::openEntryInPacketMaker(const LogEntryPtr &entry)
{
    const QString buf = entryBufferText(entry);
    if (buf.isEmpty()) {
        notify(NoteLevel::Warning, tr("That row carries no bytes to load."));
        return;
    }
    openBufferInPacketMaker(buf);
}

void MainWindow::openFrameDiff(const LogEntryPtr &a, const LogEntryPtr &b)
{
    openFrameDiff(a && b ? QVector<LogEntryPtr>{ a, b } : QVector<LogEntryPtr>());
}

void MainWindow::openFrameDiff(const QVector<LogEntryPtr> &entries)
{
    if (!m_frameDiff) {
        m_frameDiff = new FrameDiffWindow(this);
        m_frameDiff->setAttribute(Qt::WA_DeleteOnClose);
    }
    if (entries.size() >= 2) { m_frameDiff->setEntries(entries); }
    m_frameDiff->show();
    m_frameDiff->raise();
    m_frameDiff->activateWindow();
}

void MainWindow::onActionFieldSweep()
{
    if (!m_fieldSweep) {
        m_fieldSweep = new FieldSweepDialog(this, m_sessionKeys);
        m_fieldSweep->setAttribute(Qt::WA_DeleteOnClose);
    }
    // Seed from the selected row when there is one: a sweep that starts from a
    // frame the target already accepted isolates the field being swept, which
    // a form full of zeros does not.
    if (const LogEntryPtr e = currentSelectedEntry()) {
        const QString buf = entryBufferText(e);
        if (!buf.isEmpty()) { m_fieldSweep->seedFromBuffer(buf); }
    }
    m_fieldSweep->show();
    m_fieldSweep->raise();
    m_fieldSweep->activateWindow();
}

QVector<LogEntryPtr> MainWindow::allLogEntries() const
{
    QVector<LogEntryPtr> out;
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        LogModel *m = m_dispatcher->modelForKey(it.key());
        if (!m) { continue; }
        const int n = m->count();
        out.reserve(out.size() + n);
        for (int i = 0; i < n; ++i) {
            if (const LogEntryPtr e = m->entryAt(i)) { out.push_back(e); }
        }
    }
    return out;
}

void MainWindow::onActionRoundTrip()
{
    if (!m_roundTrip) {
        m_roundTrip = new RoundTripWindow(this);
        m_roundTrip->setAttribute(Qt::WA_DeleteOnClose);

        // A failing frame and its rebuild differ only in the body, so the
        // pair goes to Frame Diff and the byte difference is read out as
        // fields. That is the diagnosis; "byte 20 differs" is only a
        // pointer to it.
        connect(m_roundTrip, &RoundTripWindow::diffRequested, this,
                [this](const QString &captured, const QString &rebuilt) {
                    if (!m_frameDiff) {
                        m_frameDiff = new FrameDiffWindow(this);
                        m_frameDiff->setAttribute(Qt::WA_DeleteOnClose);
                    }
                    m_frameDiff->setSide(0, captured);
                    m_frameDiff->setSide(1, rebuilt);
                    m_frameDiff->show();
                    m_frameDiff->raise();
                    m_frameDiff->activateWindow();
                });
    }
    // Re-snapshotted on every open: the log has grown since last time, and a
    // validator reporting on traffic from ten minutes ago would be quietly
    // answering a different question.
    m_roundTrip->setLiveEntries(allLogEntries());
    m_roundTrip->show();
    m_roundTrip->raise();
    m_roundTrip->activateWindow();
}

void MainWindow::onActionFrameDiff()
{
    // From the menu: take the selection if it is exactly two rows, otherwise
    // just open the window and let the operator paste.
    const QString key = currentTabKey();
    QVector<LogEntryPtr> sel;
    if (!key.isEmpty()) {
        auto it = m_tabs.find(key);
        if (it != m_tabs.end() && it->view) { sel = selectedEntries(it->view, key); }
    }
    openFrameDiff(sel);
}

LogEntryPtr MainWindow::currentSelectedEntry() const
{
    const QString key = currentTabKey();
    if (key.isEmpty()) { return LogEntryPtr(); }
    auto it = m_tabs.constFind(key);
    if (it == m_tabs.constEnd() || !it->view) { return LogEntryPtr(); }
    return entryFromProxyIndex(it->view->currentIndex());
}

void MainWindow::onActionSelectedToWorkbench()
{
    const LogEntryPtr e = currentSelectedEntry();
    if (!e) {
        notify(NoteLevel::Info, tr("Select a row first, then send it to the "
                                   "Decode Workbench."));
        return;
    }
    openEntryInWorkbench(e);
}

void MainWindow::onActionSelectedToPacketMaker()
{
    const LogEntryPtr e = currentSelectedEntry();
    if (!e) {
        notify(NoteLevel::Info, tr("Select a row first, then send it to the "
                                   "Packet Maker."));
        return;
    }
    openEntryInPacketMaker(e);
}

void MainWindow::onActionPacketSequence()
{
    // Runs a list of saved presets in order. Separate from the Packet Maker
    // because the Maker is already a dense form, and because a sequence is a
    // different unit of work — composed once, run many times.
    auto *sq = new PacketSequenceDialog(this);
    sq->setAttribute(Qt::WA_DeleteOnClose);
    sq->show();
    sq->raise();
    sq->activateWindow();
}

void MainWindow::onActionStreamSession()
{
    // Replays a recorded .dlr over UDP at its original timing, so the whole
    // live path — socket, dispatcher batching, colour rules, LogWriter,
    // Loco Console — runs on known traffic. Independent lifetime like the
    // other tools, though there is rarely a reason to open two.
    auto *dp = new DlrPlayerDialog(&m_colorRules, this);
    dp->setAttribute(Qt::WA_DeleteOnClose);
    dp->show();
    dp->raise();
    dp->activateWindow();
}

void MainWindow::onActionSessionKey()
{
    // Derive a session key from the auth key sets + randoms + ids.
    auto *sk = new SessionKeyDialog(this, m_sessionKeys);
    sk->setAttribute(Qt::WA_DeleteOnClose);
    sk->show();
    sk->raise();
    sk->activateWindow();
}

void MainWindow::onActionSpeedDistance()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }
    auto *w = new SpeedDistanceWindow(model, key, this);
    connect(w, &SpeedDistanceWindow::jumpRequested, this, &MainWindow::jumpToEntry);
    w->show();
    w->raise();
}

void MainWindow::onActionRunReport()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }
    QString name = m_tabs.value(key).friendlyName;
    auto *w = new RunReportWindow(model, key, name.isEmpty() ? key : name, this);
    w->show();
    w->raise();
}

void MainWindow::onActionMissionReport()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }
    QString name = m_tabs.value(key).friendlyName;
    auto *w = new RunReportWindow(model, key, name.isEmpty() ? key : name, this, RunReportWindow::Kind::Missions);
    w->show();
    w->raise();
}

void MainWindow::onActionRadioHealth()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }
    QString name = m_tabs.value(key).friendlyName;
    auto *w = new RadioHealthWindow(model, key, name.isEmpty() ? key : name, this);
    connect(w, &RadioHealthWindow::jumpRequested, this, &MainWindow::jumpToEntry);
    w->show();
    w->raise();
}

void MainWindow::onActionOdometry()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }
    QString name = m_tabs.value(key).friendlyName;
    auto *w = new OdometryWindow(model, key, name.isEmpty() ? key : name, this);
    connect(w, &OdometryWindow::jumpRequested, this, &MainWindow::jumpToEntry);
    w->show();
    w->raise();
}

void MainWindow::onActionFleetSummary()
{
    auto *w = new FleetWindow(this);
    w->show();
    w->raise();
}

void MainWindow::onActionFaultTimeline()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }
    QString name = m_tabs.value(key).friendlyName;
    auto *w = new FaultTimelineWindow(model, key, name.isEmpty() ? key : name, this);
    connect(w, &FaultTimelineWindow::jumpRequested, this, &MainWindow::jumpToEntry);
    w->show();
    w->raise();
}

void MainWindow::onActionCabIo()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }
    QString name = m_tabs.value(key).friendlyName;
    auto *w = new IoTimelineWindow(model, key, name.isEmpty() ? key : name, this);
    connect(w, &IoTimelineWindow::jumpRequested, this, &MainWindow::jumpToEntry);
    w->show();
    w->raise();
}

// Session 247: one table per NMS packet type, following the tab.
void MainWindow::onActionNmsLog(int kind)
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model) {
        notify(NoteLevel::Info, tr("Select a loco's tab first."));
        return;
    }
    QString name = m_tabs.value(key).friendlyName;
    auto *w = new NmsLogWindow(static_cast<NmsLog::Kind>(kind), model, key, name.isEmpty() ? key : name, this);
    connect(w, &NmsLogWindow::jumpRequested, this, &MainWindow::jumpToEntry);
    w->show();
    w->raise();
}

void MainWindow::onActionIncidentReport()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }
    const LogEntryPtr first = model->entryAt(0);
    const LogEntryPtr last = model->entryAt(model->count() - 1);
    if (!first || !last) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }

    IncidentReportDialog dlg(last->epochMs, first->epochMs, last->epochMs, this);
    if (dlg.exec() != QDialog::Accepted) return;

    IncidentReport::Options options;
    options.beforeMs = dlg.beforeMs();
    options.afterMs = dlg.afterMs();

    QString name = m_tabs.value(key).friendlyName;
    auto *w = new IncidentReportWindow(model, key, name.isEmpty() ? key : name, dlg.atMs(), options, this);
    w->setBookmarks(&m_bookmarks);   // session 343: notes in the pack
    w->show();
    w->raise();
}

void MainWindow::onActionTwoLocoView()
{
    auto *w = new TwoLocoWindow(m_dispatcher, this);
    w->show();
    w->raise();
}

void MainWindow::onActionSosWindow()
{
    auto *w = new SosWindow(m_dispatcher, this);
    // Prefer the tab in view when it carries @sos.
    const QString key = currentTabKey();
    if (!key.isEmpty() && SosLog::hasSos(m_dispatcher->modelForKey(key))) w->setSource(key);
    connect(w, &SosWindow::jumpRequested, this, [this](const QString &tabKey, qint64 ms) { jumpToEntry(tabKey, ms); });
    w->show();
    w->raise();
}

void MainWindow::onActionTagBuilder()
{
    auto *w = new TagBuilderWindow(m_dispatcher, this);
    w->setRunSource(currentTabKey());
    w->reopenLast();
    connect(w, &TagBuilderWindow::jumpRequested, this, [this](const QString &tabKey, qint64 ms) { jumpToEntry(tabKey, ms); });
    w->show();
    w->raise();
}

void MainWindow::onActionStationLayout()
{
    auto *w = new StationLayoutWindow(this);
    w->setDispatcher(m_dispatcher);
    connect(w, &StationLayoutWindow::jumpRequested, this, [this](const QString &tabKey, qint64 ms) { jumpToEntry(tabKey, ms); });
    w->reopenLast();
    w->show();
    w->raise();
}

void MainWindow::onActionTestCatalog()
{
    auto *w = new TestCatalogWindow(this);
    w->reopenLast();
    w->show();
    w->raise();
}

void MainWindow::onActionReaderDirection()
{
    auto *w = new ReaderDirWindow(m_dispatcher, this);
    const QString key = currentTabKey();
    if (!key.isEmpty() && key != w->sourceKey() && ReaderDir::hasRdir(m_dispatcher->modelForKey(key))) w->setSource(key);
    connect(w, &ReaderDirWindow::jumpRequested, this, [this](const QString &tabKey, qint64 ms) { jumpToEntry(tabKey, ms); });
    w->show();
    w->raise();
}

void MainWindow::onActionTrackDiagram()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_dispatcher->modelForKey(key);
    if (!model || model->count() == 0) {
        notify(NoteLevel::Info, tr("Select a tab with messages first."));
        return;
    }
    QString name = m_tabs.value(key).friendlyName;
    auto *w = new TrackDiagramWindow(model, key, name.isEmpty() ? key : name, this);
    w->show();
    w->raise();
}

void MainWindow::onActionCompareTabs()
{
    // Spawn a new CompareWindow. Multiple may exist simultaneously —
    // useful for 3-way correlations. We don't keep a pointer; the
    // window manages its own lifetime via WA_DeleteOnClose.
    auto *cmp = new CompareWindow(m_dispatcher, &m_nameMap, this);
    cmp->setSessionKeys(m_sessionKeys);
    cmp->setAttribute(Qt::WA_DeleteOnClose);

    // The stores it borrows rather than duplicates. A bookmark set in a
    // compare pane is the same bookmark on the same frame as one set in a
    // log tab, and two stores would mean a mark visible in one window and
    // not the other.
    cmp->setBookmarks(&m_bookmarks);
    cmp->setColorRules(&m_colorRules);
    cmp->setLocoIdentity(&m_locoIdentity);

    // Opening windows stays this window's job: the compare window knows
    // which field and which frame, and nothing about where the pin board,
    // the plot or the Packet Maker live.
    connect(cmp, &CompareWindow::pinFieldRequested,
            this, [this](const QString &field, const QString &sourceKey,
                         const QString &captype) {
                pinFieldNarrowed(field, sourceKey, captype);
            });
    connect(cmp, &CompareWindow::plotFieldRequested,
            this, [this](const QString &field, const QString &sourceKey) {
                plotFieldIn(sourceKey, field);
            });
    connect(cmp, &CompareWindow::packetMakerRequested,
            this, &MainWindow::openBufferInPacketMaker);
    connect(cmp, &CompareWindow::locateFieldRequested,
            this, [this](const QString &f) { showFieldIndex(f); });

    cmp->show();
}

// Session 102: a terminal is a viewer of a port the SerialManager runs.
// Closing it leaves the port capturing; `port` selects one (a status-bar
// chip passes its own).
void MainWindow::openSerialTerminal(const QString &port)
{
#ifdef DL_HAVE_SERIAL
    auto *w = new SerialConsoleWindow(m_serial, this);
    if (!port.isEmpty()) w->showPort(port);
    w->show();
    w->raise();
    w->activateWindow();
#else
    Q_UNUSED(port);
#endif
}

// Session 324: one simulator window; triggering it again raises it.
void MainWindow::openCabIoSimulator()
{
#ifdef DL_HAVE_SERIAL
    if (!m_cabIoSim) {
        auto *sim = new CabIoSimWindow(m_serial, this);
        // Session 356: its rules read the loco mode from the DMI frames;
        // session 359: the output word from @lout.
        connect(m_dispatcher, &MessageDispatcher::entryAppended, sim, [sim](const QString &, const LogEntryPtr &e) {
            if (e.isNull()) return;
            const QString line = e->text.trimmed();
            if (line.startsWith(QLatin1String("@dmi_"))) sim->observeDmiLine(line);
            else if (line.startsWith(QLatin1String("@lout_"))) sim->observeLoutLine(line);
        });
        m_cabIoSim = sim;
        linkCabIoToBench();
    }
    m_cabIoSim->show();
    m_cabIoSim->raise();
    m_cabIoSim->activateWindow();
#endif
}

// Session 360: one bench; triggering it again raises it.
void MainWindow::openBrakeBench()
{
    if (!m_brakeBench) {
        m_brakeBench = new BrakeBenchWindow(this);
        static_cast<BrakeBenchWindow *>(m_brakeBench.data())->setRecorder(benchRecorder());
        linkCabIoToBench();
        linkLocoSimToBench();
    }
    m_brakeBench->show();
    m_brakeBench->raise();
    m_brakeBench->activateWindow();
}

void MainWindow::openLocoSimulator()
{
    if (!m_locoSim) {
        m_locoSim = new LocoSimWindow(m_serial, this);
        static_cast<LocoSimWindow *>(m_locoSim.data())->setRecorder(benchRecorder());
        linkLocoSimToBench();
        // Session 402: the RFID tab can run a Tag Builder's open route.
        static_cast<LocoSimWindow *>(m_locoSim.data())->setTagBuilders([this]() { return findChildren<TagBuilderWindow *>(); });
    }
    m_locoSim->show();
    m_locoSim->raise();
    m_locoSim->activateWindow();
}

// The bench's brake relays follow the Cab I/O Simulator's output word, and
// its analog card the simulator's wire format, while both are open.
void MainWindow::linkCabIoToBench()
{
#ifdef DL_HAVE_SERIAL
    auto *sim = qobject_cast<CabIoSimWindow *>(m_cabIoSim.data());
    auto *bench = qobject_cast<BrakeBenchWindow *>(m_brakeBench.data());
    if (!sim || !bench) return;
    connect(sim, &CabIoSimWindow::outputWordChanged, bench, &BrakeBenchWindow::observeOutputWord, Qt::UniqueConnection);
    connect(sim, &CabIoSimWindow::wireFormatChanged, bench, &BrakeBenchWindow::setWireFormat, Qt::UniqueConnection);
    bench->setWireFormat(sim->wireFormat());
    if (sim->outputSeen()) bench->observeOutputWord(int(sim->cab()), sim->outputWord());
#endif
}

// Session 404: one recorder for both simulators. A recording started from
// either opens the other, so one Record starts the whole test.
BenchRecord::Recorder *MainWindow::benchRecorder()
{
    if (!m_benchRecorder) {
        m_benchRecorder = new BenchRecord::Recorder(this);
        connect(m_benchRecorder, &BenchRecord::Recorder::started, this, [this]() {
            if (!m_locoSim) openLocoSimulator();
            if (!m_brakeBench) openBrakeBench();
        });
    }
    return m_benchRecorder;
}

void MainWindow::openBenchRecording()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open a test recording"),
                                                      QDir(Settings::diskLogRoot()).filePath(QStringLiteral("bench")),
                                                      tr("Test recordings (*.benchrec);;All files (*)"));
    if (path.isEmpty()) return;
    auto *w = new BenchRecordWindow(this);
    w->openFile(path);
    w->show();
    w->raise();
}

// Session 398: the bench's air brakes the Loco Simulator's loco while both are open.
void MainWindow::linkLocoSimToBench()
{
    auto *loco = qobject_cast<LocoSimWindow *>(m_locoSim.data());
    auto *bench = qobject_cast<BrakeBenchWindow *>(m_brakeBench.data());
    if (loco && bench) loco->followBench(bench);
}

void MainWindow::openSerialProfiles(bool autoOpenOnly)
{
#ifdef DL_HAVE_SERIAL
    if (!m_serial) return;
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    QVector<SerialProfile> chosen;
    for (const SerialProfile &p : SerialProfile::loadAll(s))
        if (!autoOpenOnly || p.autoOpen) chosen << p;
    if (chosen.isEmpty()) return;
    // Session 156: without waiting -- the adapters are found on the scan
    // thread and each port opens on its reader thread. profilesOpened()
    // (wired in the constructor) reports how it went.
    m_serial->openProfilesAsync(chosen);
#else
    Q_UNUSED(autoOpenOnly);
#endif
}
