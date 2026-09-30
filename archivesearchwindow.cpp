#include "archivesearchwindow.h"
#include "statusline.h"
#include "emptystate.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include "colorrules.h"
#include "logquery.h"
#include "namemap.h"
#include "settings.h"

#include <QCloseEvent>
#include <QCheckBox>
#include <QDateEdit>
#include <QDateTime>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include "querylineedit.h"

#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

ArchiveSearchWindow::ArchiveSearchWindow(const ColorRules *rules,
                                         const NameMap    *names,
                                         QWidget          *parent)
    : QWidget(parent, Qt::Window)
    , m_rules(rules)
    , m_names(names)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Search recorded sessions"));
    WindowGeometry::makeResizableWindow(this);
    resize(1050, 620);
    // Default above; a remembered size/position wins over it.
    WindowGeometry::restore(this, QStringLiteral("archiveSearch"));

    m_query = new QueryLineEdit;
    m_query->setPlaceholderText(
        tr("sev:error RAD NOT \"No Error\"     ·     src:33_1 hex:0a1b"));
    m_query->setClearButtonEnabled(true);
    m_query->setToolTip(
        tr("Same query language as the filter bar.\n\n"
           "Records are re-classified with the CURRENT colour rules as they\n"
           "are read, so sev: and dir: reflect today's rules — not the ones\n"
           "in force when the session was recorded."));

    m_from = new QDateEdit(QDate::currentDate().addDays(-7));
    m_to   = new QDateEdit(QDate::currentDate());
    for (QDateEdit *e : { m_from, m_to }) {
        e->setCalendarPopup(true);
        e->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    }

    m_allDates = new QCheckBox(tr("All dates"));
    m_allDates->setToolTip(
        tr("Searching every recorded day can take a while on a large\n"
           "archive. Narrowing the dates is usually much faster than\n"
           "narrowing the query."));
    connect(m_allDates, &QCheckBox::toggled, this, [this](bool on) {
        m_from->setEnabled(!on);
        m_to->setEnabled(!on);
    });

    m_capOnly = new QCheckBox(tr("Errors and warnings only"));
    m_capOnly->setToolTip(
        tr("Shorthand for ANDing sev:error OR sev:warn onto the query."));

    m_go     = new QPushButton(tr("Search"));
    m_go->setDefault(true);
    m_cancel = new QPushButton(tr("Cancel"));
    m_cancel->setEnabled(false);

    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Query:")), 0, 0);
    grid->addWidget(m_query, 0, 1, 1, 5);
    grid->addWidget(new QLabel(tr("From:")), 1, 0);
    grid->addWidget(m_from, 1, 1);
    grid->addWidget(new QLabel(tr("To:")), 1, 2);
    grid->addWidget(m_to, 1, 3);
    grid->addWidget(m_allDates, 1, 4);
    grid->addWidget(m_capOnly, 2, 1, 1, 3);
    grid->addWidget(m_go, 1, 5);
    grid->setColumnStretch(1, 1);

    m_progress = new QProgressBar;
    m_progress->setVisible(false);

    m_results = new QTableWidget(0, 5);
    m_results->setHorizontalHeaderLabels(
        { tr("Time"), tr("Source"), tr("Sev"), tr("Message"), tr("File") });
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setAlternatingRowColors(true);
    m_results->verticalHeader()->setVisible(false);
    m_results->horizontalHeader()->setStretchLastSection(false);
    m_results->setColumnWidth(0, 160);
    m_results->setColumnWidth(1,  70);
    m_results->setColumnWidth(2,  50);
    m_results->setColumnWidth(3, 460);
    m_results->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    m_status = new StatusLine;
    m_status->state(tr("Enter a query and press Search."));

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_progress, 1);
    buttons->addWidget(m_cancel);

    auto *root = new QVBoxLayout(this);
    root->addLayout(grid);
    root->addLayout(buttons);
    root->addWidget(m_results, 1);
    root->addWidget(m_status);

    connect(m_go,     &QPushButton::clicked,     this, &ArchiveSearchWindow::startSearch);
    connect(m_query,  &QLineEdit::returnPressed, this, &ArchiveSearchWindow::startSearch);
    connect(m_cancel, &QPushButton::clicked,     this, &ArchiveSearchWindow::cancelSearch);
    connect(m_results, &QTableWidget::cellDoubleClicked,
            this,      &ArchiveSearchWindow::onRowActivated);
    connect(m_results, &QTableWidget::cellActivated,
            this,      &ArchiveSearchWindow::onRowActivated);
}

ArchiveSearchWindow::~ArchiveSearchWindow()
{
    // The worker holds no pointer back into this window (it owns its rules
    // copy), but it must still finish before we go: a running QThread whose
    // parent is destroyed is a fatal Qt error.
    if (m_worker) {
        m_worker->cancel();
        m_worker->wait(5000);
    }
}

void ArchiveSearchWindow::setBusy(bool busy)
{
    m_go->setEnabled(!busy);
    m_cancel->setEnabled(busy);
    m_query->setEnabled(!busy);
    m_progress->setVisible(busy);
}

void ArchiveSearchWindow::startSearch()
{
    if (m_worker) return;                     // already running

    QString q = LogQuery::andConstraint(
        m_query->text().trimmed(),
        m_capOnly->isChecked() ? QStringLiteral("(sev:error OR sev:warn)")
                               : QString());
    if (q.isEmpty()) {
        m_status->warn(tr("Enter a query first."));
        return;
    }

    const QString root = Settings::diskLogRoot();
    const QDate from = m_allDates->isChecked() ? QDate() : m_from->date();
    const QDate to   = m_allDates->isChecked() ? QDate() : m_to->date();

    const QStringList files = findSessionFiles(root, from, to);
    if (files.isEmpty()) {
        m_status->warn(
            tr("No recorded sessions found under %1 for that date range. "
               "Raw capture must be enabled for .dlr files to exist.").arg(root));
        return;
    }

    m_query->rememberCurrent();

    m_results->setRowCount(0);
    m_hits.clear();
    m_progress->setRange(0, files.size());
    m_progress->setValue(0);
    m_status->say(tr("Searching %1 file(s)…").arg(files.size()));
    setBusy(true);

    // The rules are COPIED into the worker by its constructor; nothing in
    // this window is read from the other thread.
    m_worker = new ArchiveSearcher(files, q,
                                   m_rules ? *m_rules : ColorRules(),
                                   kMaxHits, this);
    connect(m_worker, &ArchiveSearcher::progress,
            this,     &ArchiveSearchWindow::onProgress);
    connect(m_worker, &ArchiveSearcher::finished,
            this,     &ArchiveSearchWindow::onFinished);
    connect(m_worker, &QThread::finished, this, [this]() {
        m_worker->deleteLater();
        m_worker = nullptr;
        setBusy(false);
    });
    m_worker->start();
}

void ArchiveSearchWindow::cancelSearch()
{
    if (!m_worker) return;
    m_worker->cancel();
    m_status->say(tr("Cancelling…"));
}

void ArchiveSearchWindow::onProgress(int done, int total)
{
    m_progress->setRange(0, total);
    m_progress->setValue(done);
}

void ArchiveSearchWindow::onFinished(ArchiveScanResult result)
{
    m_hits = result.hits;
    m_results->setRowCount(m_hits.size());

    for (int i = 0; i < m_hits.size(); ++i) {
        const ArchiveHit &h = m_hits.at(i);
        const QString friendly = m_names ? m_names->lookupByKey(h.tabKey)
                                         : h.tabKey;
        // Same glyphs as the log table: severity must not be encoded by
        // colour alone anywhere it is shown.
        const QString sev = h.severity == Severity::Error ? QStringLiteral("✕ ERR")
                          : h.severity == Severity::Warn  ? QStringLiteral("▲ WARN")
                                                  : QStringLiteral("· info");
        const QStringList cells = {
            QDateTime::fromMSecsSinceEpoch(h.epochMs)
                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
            friendly, sev, h.text, QFileInfo(h.filePath).fileName()
        };
        for (int c = 0; c < cells.size(); ++c) {
            auto *item = new QTableWidgetItem(cells.at(c));
            if (h.severity == Severity::Error)     item->setForeground(UiColor::error());
            else if (h.severity == Severity::Warn) item->setForeground(UiColor::warning());
            if (c == 4) item->setToolTip(h.filePath);
            m_results->setItem(i, c, item);
        }
    }

    QString msg;
    if (result.cancelled) {
        msg = tr("Cancelled after %1 record(s) in %2 file(s) — %3 hit(s) so far.")
                  .arg(result.recordsScanned).arg(result.filesScanned)
                  .arg(m_hits.size());
    } else if (m_hits.isEmpty()) {
        msg = result.recordsScanned == 0
                  ? tr("The selected sessions contain no records. Raw capture "
                       "must be enabled for .dlr files to hold anything.")
                  : tr("No matches among %1 record(s) in %2 file(s). Widen the "
                       "date range, or check the query.")
                        .arg(result.recordsScanned).arg(result.filesScanned);
    } else {
        msg = tr("%1 hit(s) from %2 record(s) across %3 file(s).")
                  .arg(m_hits.size()).arg(result.recordsScanned)
                  .arg(result.filesScanned);
    }
    if (result.hitCap) {
        msg += tr("  Stopped at the %1-result cap — narrow the query or the "
                  "date range.").arg(kMaxHits);
    }
    if (result.filesUnreadable > 0) {
        msg += tr("  %1 file(s) could not be read.").arg(result.filesUnreadable);
    }
    if (!m_hits.isEmpty()) {
        msg += tr("  Double-click a row to open its session.");
    }

    // A capped, cancelled or partly-unreadable scan is a qualified answer,
    // and it sticks: "184 hits" means something different when it is really
    // "the first 184, and two files could not be read".
    if (result.hitCap || result.filesUnreadable > 0 || result.cancelled) {
        m_status->warn(msg);
    } else {
        m_status->ok(msg);
    }
    if (!result.warnings.isEmpty()) {
        m_status->setToolTip(result.warnings.join('\n'));
    } else {
        m_status->setToolTip(QString());
    }
}

void ArchiveSearchWindow::onRowActivated(int row, int /*column*/)
{
    if (row < 0 || row >= m_hits.size()) return;
    emit openSessionRequested(m_hits.at(row).filePath, m_hits.at(row).epochMs);
}

void ArchiveSearchWindow::closeEvent(QCloseEvent *event)
{
    // These windows are WA_DeleteOnClose, so this is the last
    // point at which the geometry still exists to be read.
    WindowGeometry::save(this, QStringLiteral("archiveSearch"));
    QWidget::closeEvent(event);
}
