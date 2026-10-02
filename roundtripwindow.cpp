#include "roundtripwindow.h"
#include "statusline.h"
#include "emptystate.h"
#include "sendguard.h"
#include "uicolors.h"

#include "capturedecoder.h"
#include "schema/schemadecoder.h"
#include "settings.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QTextStream>
#include <QCheckBox>
#include <QClipboard>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QCoreApplication>
#include <QSplitter>

namespace {

const int kColType = 0, kColFrames = 1, kColRepro = 2, kColDiffer = 3,
          kColNoParse = 4, kColNoBuild = 5, kColCrc = 6, kColNote = 7;

QTableWidgetItem *num(int v)
{
    auto *it = new QTableWidgetItem(QString::number(v));
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return it;
}

}  // namespace

namespace {
// "1 frame" / "45 frames" (session 142; was "frame(s)").
QString frames(int n)
{
    return n == 1 ? QCoreApplication::translate("RoundTripWindow", "1 frame")
                  : QCoreApplication::translate("RoundTripWindow", "%1 frames").arg(n);
}
}  // namespace

RoundTripWindow::RoundTripWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Round-trip validator"));
    WindowGeometry::makeResizableWindow(this);
    setWindowFlags(windowFlags() | Qt::Window);
    buildUi();
    WindowGeometry::restore(this, QStringLiteral("roundtrip"));

    // The results table colours each row by its answer, once, when the scan
    // finishes. Without this, toggling the theme leaves those rows in the
    // old palette until another scan is run.
    UiColor::onThemeChange(this, [this] {
        if (m_haveReport) { fillTable(); onTypeSelected(); }
    });
    refreshRunState();
}

RoundTripWindow::~RoundTripWindow()
{
    // A window can go without a close event — destroyed with its parent at
    // shutdown, for instance. The worker is a child of this window, so it
    // would be deleted while still running, which aborts. closeEvent covers
    // the ordinary path; this covers the rest.
    stopScan();
}

void RoundTripWindow::stopScan()
{
    if (!m_runner) { return; }
    m_runner->cancel();
    // The scan polls its cancel flag between records, so the wait is short.
    // It is still a wait and not a detach: the worker emits into this
    // object's slots, and letting it outlive the receiver is the one crash
    // this feature could plausibly cause.
    m_runner->wait(5000);
}

void RoundTripWindow::reject()
{
    const bool scanning = m_runner && m_runner->isRunning();
    if (SendGuard::interceptReject(scanning, [this] {
            onCancel();
            m_status->warn(tr("Scan cancelled. Press Escape again to close."));
        })) {
        return;
    }
    QDialog::reject();
}

void RoundTripWindow::closeEvent(QCloseEvent *event)
{
    // A scan in flight owns this window's slots. Ask it to stop and let it
    // finish before the window goes: a worker emitting into a destroyed
    // receiver is the one crash this feature could plausibly cause.
    stopScan();
    WindowGeometry::save(this, QStringLiteral("roundtrip"));
    QDialog::closeEvent(event);
}

void RoundTripWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *why = new QLabel(
        tr("For every captured frame: read it back into field values, rebuild it "
           "from them, and compare the bytes. Where the rebuild differs, a frame "
           "this program <b>builds</b> for that packet type is not the shape the "
           "equipment builds — and nothing else would tell you, because a "
           "mis-encoded frame still decodes and still has a valid CRC over its "
           "own wrong bytes."), this);
    why->setWordWrap(true);
    why->setStyleSheet(UiColor::mutedStyle());   // the explanation, not the work
    root->addWidget(why);

    // ---- corpus ------------------------------------------------------------
    auto *src = new QGroupBox(tr("Corpus"), this);
    auto *srcLay = new QVBoxLayout(src);

    m_useLive = new QCheckBox(tr("Live log"), src);
    m_useLive->setChecked(true);
    connect(m_useLive, &QCheckBox::toggled, this, &RoundTripWindow::refreshRunState);
    srcLay->addWidget(m_useLive);

    auto *btns = new QHBoxLayout;
    m_addCap  = new QPushButton(tr("Add capture files…"), src);
    m_addSess = new QPushButton(tr("Add recorded sessions…"), src);
    m_clearBtn = new QPushButton(tr("Clear"), src);
    connect(m_addCap,   &QPushButton::clicked, this, &RoundTripWindow::onAddCaptureFiles);
    connect(m_addSess,  &QPushButton::clicked, this, &RoundTripWindow::onAddSessions);
    connect(m_clearBtn, &QPushButton::clicked, this, &RoundTripWindow::onClearFiles);
    btns->addWidget(m_addCap);
    btns->addWidget(m_addSess);
    btns->addWidget(m_clearBtn);
    btns->addStretch(1);
    srcLay->addLayout(btns);

    m_fileList = new QListWidget(src);
    m_fileList->setObjectName(QStringLiteral("roundtripFiles"));
    m_fileList->setMaximumHeight(90);
    srcLay->addWidget(m_fileList);
    // Shown only once there are files in it (session 142): empty, it was a
    // 90-px blank box while the live log was the whole corpus.
    auto syncFiles = [this]() { m_fileList->setVisible(m_fileList->count() > 0); };
    connect(m_fileList->model(), &QAbstractItemModel::rowsInserted, this, syncFiles);
    connect(m_fileList->model(), &QAbstractItemModel::rowsRemoved, this, syncFiles);
    connect(m_fileList->model(), &QAbstractItemModel::modelReset, this, syncFiles);
    m_fileList->setVisible(false);
    root->addWidget(src);

    // ---- run ---------------------------------------------------------------
    auto *runRow = new QHBoxLayout;
    m_runBtn    = new QPushButton(tr("Run"), this);
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setEnabled(false);
    m_progress  = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    // No "100%" on the bar (session 142): drawn in one colour over both the
    // fill and the groove, it could not read on both; the bar and the
    // status line already say how far along it is.
    m_progress->setTextVisible(false);
    connect(m_runBtn,    &QPushButton::clicked, this, &RoundTripWindow::onRun);
    connect(m_cancelBtn, &QPushButton::clicked, this, &RoundTripWindow::onCancel);
    runRow->addWidget(m_runBtn);
    runRow->addWidget(m_cancelBtn);
    runRow->addWidget(m_progress, 1);
    root->addLayout(runRow);

    // ---- results -----------------------------------------------------------
    m_table = new QTableWidget(0, 8, this);
    m_table->setHorizontalHeaderLabels({ tr("Type"), tr("Frames"), tr("Reproduced"),
                                         tr("Differ"), tr("Not parsed"), tr("Not built"),
                                         tr("CRC ok"), tr("Note") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);   // as the other tool tables
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this,    &RoundTripWindow::onTypeSelected);

    // Three ways for this table to be empty, and they mean different things:
    // no scan yet, a scan that read nothing, and a scan that read frames but
    // recognised no packet type in any of them.
    EmptyState::attach(m_table, [this]() -> QString {
        if (!m_haveReport)          { return tr("Nothing scanned yet.\n"
                                                "Choose a corpus above and press Run."); }
        if (m_report.framesSeen == 0) { return tr("No capture frames were found in "
                                                  "that corpus."); }
        return tr("%1 read, but none belong to a packet type in the "
                  "schema.").arg(frames(m_report.framesSeen));
    });

    m_detail = new QTextEdit(this);
    m_detail->setReadOnly(true);
    m_detail->setMinimumHeight(90);

    // The results and the selected type's detail share a splitter, 3 : 1
    // for the results (session 142). Side by side at stretch 1 each, the
    // table showed four of its rows while the detail held one line.
    auto *split = new QSplitter(Qt::Vertical, this);
    split->setObjectName(QStringLiteral("roundtripSplit"));
    split->setChildrenCollapsible(false);
    split->addWidget(m_table);
    split->addWidget(m_detail);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);

    auto *foot = new QHBoxLayout;
    m_status  = new StatusLine(this);
    m_status->say(tr("Nothing scanned yet."));
    m_diffBtn = new QPushButton(tr("Diff a failing frame against its rebuild"), this);
    m_copyBtn = new QPushButton(tr("Copy report"), this);
    m_saveBtn = new QPushButton(tr("Save report…"), this);
    m_diffBtn->setEnabled(false);
    m_copyBtn->setEnabled(false);
    m_saveBtn->setEnabled(false);
    connect(m_diffBtn, &QPushButton::clicked, this, &RoundTripWindow::onDiffSample);
    connect(m_copyBtn, &QPushButton::clicked, this, &RoundTripWindow::onCopyReport);
    connect(m_saveBtn, &QPushButton::clicked, this, &RoundTripWindow::onSaveReport);
    foot->addWidget(m_status, 1);
    foot->addWidget(m_diffBtn);
    foot->addWidget(m_copyBtn);
    foot->addWidget(m_saveBtn);
    root->addLayout(foot);

    resize(940, 700);
}

void RoundTripWindow::setLiveEntries(const QVector<LogEntryPtr> &entries)
{
    // Was the live log unavailable last time? Then the box is unticked
    // because there was nothing to tick, not because the operator said no —
    // so ticking it again when traffic arrives is restoring the default, not
    // overriding a choice. Opening the validator before the first packet and
    // again after it must not leave Run permanently greyed out.
    const bool wasUnavailable = !m_useLive->isEnabled();

    m_entries = entries;
    m_useLive->setText(tr("Live log — %1 entries").arg(entries.size()));
    m_useLive->setEnabled(!entries.isEmpty());
    if (entries.isEmpty())         { m_useLive->setChecked(false); }
    else if (wasUnavailable)       { m_useLive->setChecked(true);  }
    refreshRunState();
}

void RoundTripWindow::onAddCaptureFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Capture files"), Settings::diskLogRoot(),
        tr("Capture text (*.cap *.log *.txt);;All files (*)"));
    for (const QString &p : paths) {
        if (m_capFiles.contains(p)) { continue; }
        m_capFiles << p;
        m_fileList->addItem(tr("capture: %1").arg(p));
    }
    refreshRunState();
}

void RoundTripWindow::onAddSessions()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Recorded sessions"), Settings::diskLogRoot(),
        tr("DLConsole sessions (*.dlr);;All files (*)"));
    for (const QString &p : paths) {
        if (m_sessionFiles.contains(p)) { continue; }
        m_sessionFiles << p;
        m_fileList->addItem(tr("session: %1").arg(p));
    }
    refreshRunState();
}

void RoundTripWindow::onClearFiles()
{
    m_capFiles.clear();
    m_sessionFiles.clear();
    m_fileList->clear();
    refreshRunState();
}

void RoundTripWindow::refreshRunState()
{
    const bool running = m_runner && m_runner->isRunning();
    const bool haveCorpus = (m_useLive->isChecked() && !m_entries.isEmpty())
                            || !m_capFiles.isEmpty() || !m_sessionFiles.isEmpty();
    m_runBtn->setEnabled(!running && haveCorpus);
    m_cancelBtn->setEnabled(running);
    m_addCap->setEnabled(!running);
    m_addSess->setEnabled(!running);
    m_clearBtn->setEnabled(!running);
}

void RoundTripWindow::onRun()
{
    if (m_runner && m_runner->isRunning()) { return; }

    RoundTrip::Runner::Job job;
    if (m_useLive->isChecked()) { job.entries = m_entries; }
    job.capFiles     = m_capFiles;
    job.sessionFiles = m_sessionFiles;
    // Resolved here, on the GUI thread: Settings reads an INI, and doing it
    // per-scan on the worker would be both slow and shared state.
    job.schemaPath   = Settings::schemaPath();

    m_report = RoundTrip::Report();
    m_haveReport = false;
    m_table->setRowCount(0);
    m_detail->clear();
    m_progress->setValue(0);
    m_status->say(tr("Scanning…"));

    auto *runner = new RoundTrip::Runner(job, this);
    m_runner = runner;
    connect(runner, &RoundTrip::Runner::progress, this, &RoundTripWindow::onProgress);
    connect(runner, &RoundTrip::Runner::finished, this, &RoundTripWindow::onFinished);
    // Deliberately NOT connect(QThread::finished, deleteLater): the report is
    // emitted from inside run(), so the main loop can process the deletion
    // before the thread has actually terminated, and deleting a running
    // QThread aborts the process. onFinished joins it and then deletes it.
    runner->start();
    refreshRunState();
}

void RoundTripWindow::onCancel()
{
    if (m_runner) { m_runner->cancel(); }
    m_status->say(tr("Cancelling…"));
}

void RoundTripWindow::onProgress(qint64 done, qint64 total)
{
    if (total <= 0) { return; }
    m_progress->setValue(int(qMin<qint64>(100, done * 100 / total)));
}

void RoundTripWindow::onFinished(RoundTrip::Report report)
{
    // run() emits the report and then returns; this join is that last step
    // and costs nothing measurable, but it is what makes the delete safe.
    if (m_runner) {
        m_runner->wait();
        m_runner->deleteLater();
        m_runner.clear();
    }

    m_report = report;
    m_haveReport = true;
    m_progress->setValue(100);
    fillTable();

    const int answerable = report.totalAnswerable();
    const int identical  = report.totalIdentical();
    QString s = tr("%1 frames read; %2 of %3 reproduced byte for byte")
                    .arg(report.framesSeen).arg(identical).arg(answerable);
    if (report.cancelled)             { s += tr("  ·  CANCELLED — partial"); }
    if (!report.warnings.isEmpty())   { s += tr("  ·  %1 warning(s)").arg(report.warnings.size()); }
    // Sticky when anything did not reproduce: that IS the finding, and a
    // finding that clears itself after six seconds is a finding nobody
    // acted on.
    if (report.cancelled || !report.warnings.isEmpty() || identical < answerable) {
        m_status->warn(s);
    } else {
        m_status->ok(s);
    }

    m_copyBtn->setEnabled(true);
    m_saveBtn->setEnabled(true);
    refreshRunState();

    if (m_table->rowCount() > 0) { m_table->selectRow(0); }
}

void RoundTripWindow::fillTable()
{
    const QStringList types = m_report.captypes();
    m_table->setRowCount(types.size());
    int row = 0;
    for (const QString &k : types) {
        const RoundTrip::Tally &t = m_report.byType.value(k);

        m_table->setItem(row, kColType,   new QTableWidgetItem(k));
        m_table->setItem(row, kColFrames, num(t.frames));

        // A type the encoder cannot emit has no reproduction count — showing
        // 0 there would read as a failure when the correct statement is that
        // the question was never asked.
        auto *repro = t.notEncodable
            ? new QTableWidgetItem(QStringLiteral("—"))
            : num(t.identical);
        repro->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, kColRepro, repro);

        m_table->setItem(row, kColDiffer,  num(t.differs));
        m_table->setItem(row, kColNoParse, num(t.parseFailed));
        m_table->setItem(row, kColNoBuild, num(t.encodeFailed));
        m_table->setItem(row, kColCrc,
            new QTableWidgetItem(t.crcChecked
                ? QStringLiteral("%1/%2").arg(t.crcOk).arg(t.crcChecked)
                : QStringLiteral("—")));

        QString note;
        if (t.notEncodable)               { note = tr("not encodable: %1").arg(t.firstProblem); }
        else if (t.differs || t.parseFailed || t.encodeFailed) { note = t.firstProblem; }
        else if (t.identical == t.frames) { note = tr("every frame reproduced"); }
        m_table->setItem(row, kColNote, new QTableWidgetItem(note));

        // Colour the row by the answer, not by severity: green where the
        // builder can be trusted for this type, amber where it cannot.
        const bool clean = (t.differs == 0 && t.parseFailed == 0 && t.encodeFailed == 0
                            && t.notEncodable == 0);
        for (int c = 0; c < m_table->columnCount(); ++c) {
            if (QTableWidgetItem *it = m_table->item(row, c)) {
                it->setForeground(clean ? UiColor::ok() : UiColor::warning());
            }
        }
        ++row;
    }
    m_table->resizeColumnsToContents();
}

void RoundTripWindow::onTypeSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_table->rowCount()) { m_diffBtn->setEnabled(false); return; }
    const QString k = m_table->item(row, kColType)->text();
    showDetail(k);
}

void RoundTripWindow::showDetail(const QString &captype)
{
    const RoundTrip::Tally &t = m_report.byType.value(captype);
    QStringList out;

    out << tr("<b>%1</b> — %2").arg(captype, frames(t.frames));

    if (t.notEncodable) {
        out << tr("The encoder cannot emit this packet: <b>%1</b>. Nothing can be "
                  "built for this type, so no frame of it was checked.")
                   .arg(t.firstProblem.toHtmlEscaped());
    } else if (t.differs || t.parseFailed || t.encodeFailed) {
        out << tr("First problem: %1").arg(t.firstProblem.toHtmlEscaped());
    } else {
        out << tr("Every frame was rebuilt byte for byte.");
    }

    for (const QString &n : t.notes) {
        out << tr("Note: %1").arg(n.toHtmlEscaped());
    }

    // Attribution: name the fields that own the bytes that differ. The core
    // could not do this — it runs off-thread and the decoder is GUI state.
    if (!t.diffHistogram.isEmpty() && !t.samples.isEmpty()) {
        const RoundTrip::Sample &s = t.samples.first();
        const Schema::Decoder &dec = kavachSchema();
        const QVector<FieldRow> rows = dec.decode(s.original, {}, captype, -1);

        QList<int> keys = t.diffHistogram.keys();
        std::sort(keys.begin(), keys.end());
        QStringList lines;
        for (int b : keys) {
            const QString f = RoundTrip::fieldAtByte(rows, s.bodyStart + b);
            lines << tr("body byte %1 (%2) — %3")
                         .arg(b)
                         .arg(frames(t.diffHistogram.value(b)))
                         .arg(f.isEmpty() ? tr("no field claims this byte") : f.toHtmlEscaped());
        }
        out << tr("Where the rebuild first goes wrong:");
        out << QStringLiteral("<ul><li>%1</li></ul>")
                   .arg(lines.join(QStringLiteral("</li><li>")));
    }

    if (!t.samples.isEmpty()) {
        out << tr("Samples kept: %1 (select this row and use the Diff button)")
                   .arg(t.samples.size());
        const RoundTrip::Sample &s = t.samples.first();
        out << tr("First: %1 seq %2, loco %3 / ctrl %4")
                   .arg(s.rtc.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                   .arg(s.seq).arg(s.locoId).arg(s.ctrlId);
    }

    m_detail->setHtml(out.join(QStringLiteral("<br>")));
    m_diffBtn->setEnabled(!t.samples.isEmpty()
                          && !t.samples.first().rebuiltFrame.isEmpty());
}

void RoundTripWindow::onDiffSample()
{
    const int row = m_table->currentRow();
    if (row < 0) { return; }
    const QString k = m_table->item(row, kColType)->text();
    const RoundTrip::Tally &t = m_report.byType.value(k);
    if (t.samples.isEmpty()) { return; }
    const RoundTrip::Sample &s = t.samples.first();
    if (s.rebuiltFrame.isEmpty()) { return; }

    CaptureLine c;
    c.typeToken = s.captype;
    c.locoId    = s.locoId;
    c.ctrlId    = s.ctrlId;
    c.rtc       = s.rtc;
    c.seq       = s.seq;

    emit diffRequested(RoundTrip::captureLineFor(c, s.original),
                       RoundTrip::captureLineFor(c, s.rebuiltFrame));
}

void RoundTripWindow::onCopyReport()
{
    if (!m_haveReport) { return; }
    QApplication::clipboard()->setText(m_report.toText());
    m_status->ok(tr("Report copied."));
}

void RoundTripWindow::onSaveReport()
{
    if (!m_haveReport) { return; }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save round-trip report"),
        Settings::diskLogRoot() + QStringLiteral("/roundtrip.txt"),
        tr("Text files (*.txt);;All files (*)"));
    if (path.isEmpty()) { return; }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save failed"), f.errorString());
        return;
    }
    QTextStream ts(&f);
    ts << m_report.toText() << "\n";
    f.close();
    m_status->ok(tr("Saved %1").arg(path));
}
