#include "searchwindow.h"

#include <QMap>
#include "statusline.h"
#include "emptystate.h"
#include "uicolors.h"
#include "uistyle.h"
#include "windowgeometry.h"

#include "logmodel.h"
#include "logquery.h"
#include "messagedispatcher.h"
#include "namemap.h"

#include <QCloseEvent>
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLabel>
#include "querylineedit.h"

#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

SearchWindow::SearchWindow(MessageDispatcher *dispatcher,
                           const NameMap     *names,
                           QWidget           *parent)
    : QDialog(parent)
    , m_dispatcher(dispatcher)
    , m_names(names)
{
    setWindowTitle(tr("Search all sources"));
    WindowGeometry::makeResizableWindow(this);
    // Not modal: the whole point is to keep it open beside the live tabs
    // and jump back and forth.
    setModal(false);
    setWindowFlag(Qt::Window, true);
    resize(1000, 600);
    // Default above; a remembered size/position wins over it.
    WindowGeometry::restore(this, QStringLiteral("searchWindow"));

    m_edit = new QueryLineEdit;
    m_edit->setPlaceholderText(
        tr("sev:error RAD NOT \"No Error\"     ·     "
           "(src:33_1 OR src:21_1) after:-15m"));
    m_edit->setClearButtonEnabled(true);

    m_runBtn = new QPushButton(tr("Search"));
    m_runBtn->setDefault(true);
    UiStyle::makePrimary(m_runBtn);           // session 123: the one primary action

    m_capOnly = new QCheckBox(tr("Errors and warnings only"));
    m_capOnly->setToolTip(
        tr("Shorthand for ANDing sev:error OR sev:warn onto the query."));

    // Session 123: the query as one panel — icon, field, its two switches,
    // and Search — rather than a bare row of controls.
    auto *queryPanel = new QWidget;
    queryPanel->setObjectName(QStringLiteral("searchQueryPanel"));
    UiStyle::makePanel(queryPanel);
    auto *top = new QHBoxLayout(queryPanel);
    top->setContentsMargins(UiStyle::space(3), UiStyle::space(2), UiStyle::space(2), UiStyle::space(2));
    top->setSpacing(UiStyle::space(2));
    auto *icon = new QLabel;
    UiIcons::bind(icon, QStringLiteral("search"), 16, []() { return UiColor::muted(); });
    icon->setAccessibleName(tr("Query"));
    top->addWidget(icon);
    top->addWidget(m_edit, 1);
    top->addWidget(m_capOnly);
    // Session 82: the hits source by source, each under a heading with its
    // count, instead of interleaved by time.
    m_group = new QCheckBox(tr("Group by tab"));
    m_group->setObjectName(QStringLiteral("searchGroupByTab"));
    top->addWidget(m_group);
    top->addWidget(m_runBtn);

    m_status = new StatusLine;
    m_status->state(tr("Enter a query and press Search."));

    // Shows how the query was parsed. Precedence surprises ("why did my OR
    // swallow the rest of the line?") are the most common way a query does
    // something other than what was intended, and this makes that visible
    // instead of leaving people to guess.
    m_explain = new QLabel;
    // The real mono face: "monospace" as a family name does not resolve on
    // Windows (session 123).
    m_explain->setStyleSheet(UiColor::mutedStyle());
    UiStyle::makeMono(m_explain);
    m_explain->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_explain->hide();

    m_results = new QTableWidget(0, 6);
    EmptyState::attach(m_results, [this]() -> QString {
        if (m_lastQuery.isEmpty()) {
            return tr("Type a query above and press Search.");
        }
        return tr("No messages match\n%1").arg(m_lastQuery);
    });
    m_results->setHorizontalHeaderLabels(
        { tr("Time"), tr("Source"), tr("Name"),
          tr("Dir"), tr("Sev"), tr("Message") });
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setAlternatingRowColors(true);
    m_results->verticalHeader()->setVisible(false);
    m_results->horizontalHeader()->setStretchLastSection(true);
    // Measured to the fonts they are drawn in (session 123; fixed pixel
    // widths clipped "2026-06-27 14:02:27.512" as they did in the main log).
    {
        QFont hf = m_results->horizontalHeader()->font();
        hf.setWeight(QFont::DemiBold);
        const QFontMetrics head(hf), cell(m_results->font()), mono(UiStyle::monoFont());
        auto fit = [&](int col, int sample) {
            const QString h = m_results->horizontalHeaderItem(col)->text();
            m_results->setColumnWidth(col, qMax(sample + 18, head.horizontalAdvance(h) + 28));
        };
        fit(0, mono.horizontalAdvance(QStringLiteral("8888-88-88 88:88:88.888")));
        fit(1, cell.horizontalAdvance(QStringLiteral("888_888")));
        fit(2, cell.horizontalAdvance(QStringLiteral("Fault_L1V1W")));
        fit(3, cell.horizontalAdvance(QStringLiteral("OUT")));
        fit(4, cell.horizontalAdvance(QStringLiteral("\u25B2 WARN")));
    }

    auto *root = new QVBoxLayout(this);
    root->addWidget(queryPanel);
    root->addWidget(m_explain);
    root->addWidget(m_results, 1);
    root->addWidget(m_status);

    connect(m_runBtn,  &QPushButton::clicked,   this, &SearchWindow::runSearch);
    connect(m_edit,    &QLineEdit::returnPressed, this, &SearchWindow::runSearch);
    connect(m_capOnly, &QCheckBox::toggled,     this, &SearchWindow::runSearch);
    connect(m_group,   &QCheckBox::toggled,     this, &SearchWindow::runSearch);
    connect(m_results, &QTableWidget::cellActivated,
            this,      &SearchWindow::onCellActivated);
    connect(m_results, &QTableWidget::cellDoubleClicked,
            this,      &SearchWindow::onCellActivated);
}

void SearchWindow::setQueryText(const QString &text, bool runNow)
{
    m_edit->setText(text);
    if (runNow) runSearch();
}

void SearchWindow::setStatus(const QString &text, bool isError)
{
    // Kept as a two-state helper for the call sites that really are binary,
    // but the boolean used to carry three different meanings — a prompt, a
    // query error, and a results cap were all "not error" or "error". The
    // call sites now pick the verb themselves.
    if (isError) { m_status->fail(text); } else { m_status->state(text); }
}

void SearchWindow::runSearch()
{
    if (!m_dispatcher) return;

    QString text = m_edit->text().trimmed();
    if (m_capOnly->isChecked()) {
        text = LogQuery::andConstraint(
            text, QStringLiteral("(sev:error OR sev:warn)"));
    }

    m_lastQuery = text;
    if (text.isEmpty()) {
        m_results->setRowCount(0);
        m_hits.clear();
        m_explain->hide();
        // A caption, not news: it stays until a search replaces it.
        m_status->state(tr("Enter a query and press Search."));
        return;
    }

    // Time read the same way the per-tab filter reads it: in the zone the
    // tables show, and last: counted back from the newest row of any tab.
    LogQuery query;
    {
        qint64 newest = 0;
        bool   utc    = false;
        for (const QString &key : m_dispatcher->knownKeys()) {
            LogModel *model = m_dispatcher->modelForKey(key);
            if (!model) continue;
            utc = model->showUtc();
            if (model->count() == 0) continue;
            const LogEntryPtr last = model->entryAt(model->count() - 1);
            if (last && last->epochMs > newest) newest = last->epochMs;
        }
        query.setUtc(utc);
        query.setDataEnd(newest);
    }
    if (!query.parse(text)) {
        m_results->setRowCount(0);
        m_hits.clear();
        m_explain->hide();
        const int off = query.errorOffset();
        setStatus(off >= 0 ? tr("Query error at column %1: %2")
                                 .arg(off + 1).arg(query.errorString())
                           : tr("Query error: %1").arg(query.errorString()),
                  true);
        m_edit->setQueryError(query.errorString(), off);
        m_edit->selectErrorRange();
        return;
    }

    m_explain->setText(query.explain().trimmed());
    m_explain->show();
    m_edit->rememberCurrent();      // it parsed and is about to run

    QApplication::setOverrideCursor(Qt::WaitCursor);

    m_hits.clear();
    QVector<QPair<Hit, LogEntryPtr>> found;
    qint64 scanned = 0;
    bool   capped  = false;

    const QStringList keys = m_dispatcher->knownKeys();
    for (const QString &key : keys) {
        LogModel *model = m_dispatcher->modelForKey(key);
        if (!model) continue;

        const int n = model->count();
        for (int row = 0; row < n; ++row) {
            LogEntryPtr e = model->entryAt(row);
            if (!e) continue;
            ++scanned;
            if (!query.match(*e, m_names)) continue;

            found.append({ Hit{ key, e->epochMs }, e });
            if (found.size() >= kMaxResults) { capped = true; break; }
        }
        if (capped) break;
    }

    // Interleave by time. Each model is already time-ordered, so this is a
    // merge of sorted runs — but std::sort is simpler and, at the 5000-row
    // cap, indistinguishable in cost.
    std::stable_sort(found.begin(), found.end(),
                     [](const QPair<Hit, LogEntryPtr> &a,
                        const QPair<Hit, LogEntryPtr> &b) {
                         return a.first.epochMs < b.first.epochMs;
                     });

    // Grouped: by source (in the order the sources were searched), each
    // group still in time order, under a heading row. The heading is not a
    // hit: its Hit has an empty key and double-clicking it does nothing.
    QMap<QString, int> perSource;
    for (const auto &f : found) perSource[f.first.tabKey] += 1;
    const bool grouped = m_group && m_group->isChecked();
    if (grouped) {
        QHash<QString, int> order;
        for (int k = 0; k < keys.size(); ++k) order.insert(keys.at(k), k);
        std::stable_sort(found.begin(), found.end(),
                         [&order](const QPair<Hit, LogEntryPtr> &a, const QPair<Hit, LogEntryPtr> &b) {
                             return order.value(a.first.tabKey) < order.value(b.first.tabKey);
                         });
    }

    m_results->setSortingEnabled(false);
    m_results->setRowCount(found.size() + (grouped ? perSource.size() : 0));

    int row = -1;
    QString lastKey;
    for (int i = 0; i < found.size(); ++i) {
        const Hit         &h = found.at(i).first;
        const LogEntryPtr &e = found.at(i).second;
        if (grouped && h.tabKey != lastKey) {
            lastKey = h.tabKey;
            ++row;
            m_hits.append(Hit{ QString(), 0 });
            const QString friendly = m_names ? m_names->lookupByKey(h.tabKey) : h.tabKey;
            auto *head = new QTableWidgetItem(tr("%1  ·  %2  —  %3 hit(s)")
                                                  .arg(h.tabKey, friendly).arg(perSource.value(h.tabKey)));
            QFont bold = head->font();
            bold.setBold(true);
            head->setFont(bold);
            m_results->setItem(row, 0, head);
            m_results->setSpan(row, 0, 1, m_results->columnCount());
        }
        ++row;
        m_hits.append(h);

        const QString friendly = m_names ? m_names->lookupByKey(h.tabKey)
                                         : h.tabKey;
        const QString dir = e->direction == LogDirection::In  ? QStringLiteral("IN")
                          : e->direction == LogDirection::Out ? QStringLiteral("OUT")
                                                           : QString();
        // Same glyphs as the log table: severity must not be encoded by
        // colour alone anywhere it is shown.
        const QString sev = e->severity == Severity::Error ? QStringLiteral("✕ ERR")
                          : e->severity == Severity::Warn  ? QStringLiteral("▲ WARN")
                                                  : QStringLiteral("· info");

        const QStringList cells = {
            QDateTime::fromMSecsSinceEpoch(e->epochMs)
                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
            h.tabKey, friendly, dir, sev, e->text
        };
        for (int c = 0; c < cells.size(); ++c) {
            auto *item = new QTableWidgetItem(cells.at(c));
            if (c == 0) item->setFont(UiStyle::monoFont());   // digit under digit
            if (e->severity == Severity::Error) {
                item->setForeground(UiColor::error());
            } else if (e->severity == Severity::Warn) {
                item->setForeground(UiColor::warning());
            }
            m_results->setItem(row, c, item);
        }
    }

    QApplication::restoreOverrideCursor();

    const QString when = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString msg = tr("%1 hit(s) across %2 source(s), %3 rows scanned  ·  "
                     "snapshot at %4  ·  double-click to jump")
                      .arg(found.size()).arg(keys.size()).arg(scanned).arg(when);
    if (!found.isEmpty()) {
        msg += tr("  ·  in %1 of them").arg(perSource.size());
    }

    if (found.isEmpty()) {
        // Distinguish the two reasons for zero hits. "Nothing matched" and
        // "there was nothing to match against" call for completely
        // different next steps, and a bare count says neither.
        msg = scanned == 0
                  ? tr("No messages have been received yet, so there is "
                       "nothing to search. Recorded sessions can be searched "
                       "from Edit → Search recorded sessions.")
                  : tr("No matches among %1 message(s). Check the spelling, "
                       "or widen the query — a bare word matches the message "
                       "text, source and name.").arg(scanned);
    }
    if (capped) {
        msg = tr("Stopped at the %1-result cap (%2 rows scanned). "
                 "Narrow the query — add a source, a severity, or a time "
                 "window such as after:-15m.")
                  .arg(kMaxResults).arg(scanned);
    }
    // Hitting the cap is a qualification, not a failure: the results shown
    // are real, there are simply more of them. It sticks, because a result
    // count that silently means "the first 500 of who knows how many" is
    // exactly the thing an operator must not miss.
    if (capped) { m_status->warn(msg); } else { m_status->ok(msg); }
}

void SearchWindow::onCellActivated(int row, int /*column*/)
{
    if (row < 0 || row >= m_hits.size()) return;
    const Hit &h = m_hits.at(row);
    if (h.tabKey.isEmpty()) return;   // a group heading
    emit jumpRequested(h.tabKey, h.epochMs);
}

void SearchWindow::closeEvent(QCloseEvent *event)
{
    // These windows are WA_DeleteOnClose, so this is the last
    // point at which the geometry still exists to be read.
    WindowGeometry::save(this, QStringLiteral("searchWindow"));
    QDialog::closeEvent(event);
}

void SearchWindow::setGroupByTab(bool on)
{
    const QSignalBlocker block(m_group);
    m_group->setChecked(on);
}

int SearchWindow::resultRowCount() const { return m_results->rowCount(); }

int SearchWindow::hitCount() const
{
    int n = 0;
    for (const Hit &h : m_hits) if (!h.tabKey.isEmpty()) ++n;
    return n;
}
