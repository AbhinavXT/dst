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
#include <QCoreApplication>
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

namespace {
// "1 file" / "3 files" (session 140: the status read "15 hit(s) from 400
// record(s) across 1 file(s)").
QString countOf(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QCoreApplication::translate("ArchiveSearchWindow", n == 1 ? one : many));
}
}  // namespace

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

    // Query and Search on one row, the dates and filters compact on the
    // next (session 140). In one grid the From box shared the query's
    // stretching column and ran across half the window.
    auto *queryRow = new QHBoxLayout;
    queryRow->addWidget(new QLabel(tr("Query:")));
    queryRow->addWidget(m_query, 1);
    queryRow->addWidget(m_go);
    queryRow->addWidget(m_cancel);
    auto *dateRow = new QHBoxLayout;
    dateRow->setSpacing(8);
    dateRow->addWidget(new QLabel(tr("From:")));
    dateRow->addWidget(m_from);
    dateRow->addWidget(new QLabel(tr("To:")));
    dateRow->addWidget(m_to);
    dateRow->addSpacing(8);
    dateRow->addWidget(m_allDates);
    dateRow->addSpacing(8);
    dateRow->addWidget(m_capOnly);
    dateRow->addStretch(1);
    // Cancel replaces Search while a search runs, rather than a disabled
    // full-width bar under the form the rest of the time.
    m_cancel->setVisible(false);

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
    // Measured to what they hold (session 140): at 160 px the time read
    // "2026-10-02 …" and the header "Source" was cut.
    {
        m_results->setObjectName(QStringLiteral("archiveResults"));
        m_results->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        const QFontMetrics fm(m_results->font());
        QFont hf = m_results->horizontalHeader()->font();
        hf.setWeight(QFont::DemiBold);
        const QFontMetrics hm(hf);
        auto fit = [&](int col, const QString &sample) {
            const QString head = m_results->horizontalHeaderItem(col)->text();
            m_results->setColumnWidth(col, qMax(fm.horizontalAdvance(sample) + 20, hm.horizontalAdvance(head) + 28));
        };
        fit(0, QStringLiteral("8888-88-88 88:88:88.888"));
        fit(1, QStringLiteral("888_888"));
        fit(2, QStringLiteral("\u25B2 WARN"));
        fit(4, QStringLiteral("888_8888.dlr"));
    }
    m_results->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    m_status = new StatusLine;
    m_status->state(tr("Enter a query and press Search."));

    auto *root = new QVBoxLayout(this);
    root->addLayout(queryRow);
    root->addLayout(dateRow);
    root->addWidget(m_progress);   // shown only while searching
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
    m_go->setVisible(!busy);
    m_cancel->setVisible(busy);
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
    m_status->say(tr("Searching %1…").arg(countOf(files.size(), "file", "files")));
    setBusy(true);

    // The rules are COPIED into the worker by its constructor; nothing in
    // this window is read from the other thread.
    m_worker = new ArchiveSearcher(files, q,
                                   m_rules ? *m_rules : ColorRules(),
                                   kMaxHits, this, Settings::showUtc());
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
        msg = tr("Cancelled after %1 in %2 — %3 so far.")
                  .arg(countOf(result.recordsScanned, "record", "records"),
                       countOf(result.filesScanned, "file", "files"),
                       countOf(m_hits.size(), "hit", "hits"));
    } else if (m_hits.isEmpty()) {
        msg = result.recordsScanned == 0
                  ? tr("The selected sessions contain no records. Raw capture "
                       "must be enabled for .dlr files to hold anything.")
                  : tr("No matches among %1 in %2. Widen the "
                       "date range, or check the query.")
                        .arg(countOf(result.recordsScanned, "record", "records"),
                             countOf(result.filesScanned, "file", "files"));
    } else {
        msg = tr("%1 from %2 across %3.")
                  .arg(countOf(m_hits.size(), "hit", "hits"),
                       countOf(result.recordsScanned, "record", "records"),
                       countOf(result.filesScanned, "file", "files"));
    }
    if (result.hitCap) {
        msg += tr("  Stopped at the %1-result cap — narrow the query or the "
                  "date range.").arg(kMaxHits);
    }
    if (result.filesUnreadable > 0) {
        msg += tr("  %1 could not be read.").arg(countOf(result.filesUnreadable, "file", "files"));
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
