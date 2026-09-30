#include "watchpanel.h"
#include "undolog.h"

#include <QPointer>

#include "querylineedit.h"
#include "settings.h"
#include "statusline.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QDateTime>
#include <QMenu>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

enum Col { ColState = 0, ColWhat, ColWhen, ColTimes, ColFrom, ColCount };

QString sinceText(qint64 ms)
{
    if (ms < 0)    { return QString(); }
    const qint64 s = ms / 1000;
    if (s < 60)    { return QStringLiteral("%1 s ago").arg(s); }
    if (s < 3600)  { return QStringLiteral("%1 m ago").arg(s / 60); }
    return QStringLiteral("%1 h ago").arg(s / 3600);
}

}  // namespace

WatchPanel::WatchPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(UiStyle::margin(), UiStyle::margin(),
                             UiStyle::margin(), UiStyle::margin());
    root->setSpacing(UiStyle::gap());

    auto *row = new QHBoxLayout;
    row->setSpacing(UiStyle::gap());

    // The same editor the filter bar uses: completion, and an error marked at
    // the offset that caused it. A watch is a query, so it gets the query box.
    m_expr = new QueryLineEdit(this);
    m_expr->setObjectName(QStringLiteral("watchExpr"));
    m_expr->setPlaceholderText(
        tr("A condition, as in the filter bar:  LOCO_MODE == 2"));
    m_expr->setToolTip(
        tr("The same language as the filter bar and the find window.\\n\\n"
           "A watch that does not fire can be pasted into the filter to find\\n"
           "out why — which is the reason it is not a language of its own."));
    row->addWidget(m_expr, 1);

    m_label = new QLineEdit(this);
    m_label->setObjectName(QStringLiteral("watchLabel"));
    m_label->setPlaceholderText(tr("name (optional)"));
    m_label->setMaximumWidth(160);
    m_label->setToolTip(tr("What to call it in a report. The condition is\\n"
                           "exact; a name is what a person reads."));
    row->addWidget(m_label);

    m_addBtn = new QPushButton(tr("Watch"), this);
    m_addBtn->setObjectName(QStringLiteral("watchAddBtn"));
    row->addWidget(m_addBtn);
    root->addLayout(row);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setObjectName(QStringLiteral("watchTable"));
    m_table->setHorizontalHeaderLabels({ QString(), tr("Watching for"),
                                         tr("Fired"), tr("Times"), tr("From") });
    m_table->horizontalHeaderItem(ColTimes)->setToolTip(
        tr("Separate occurrences: matches more than %1 s apart count as a new one.")
            .arg(WatchList::kOccurrenceGapMs / 1000));
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(ColState, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColWhat,  QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColWhen,  QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColFrom,  QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColTimes, QHeaderView::ResizeToContents);
    // Right-click a watch: what it does when it fires (session 82).
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = m_table->rowAt(pos.y());
        if (row < 0 || row >= m_list.count()) return;
        const WatchList::Watch &w = m_list.watches().at(row);
        QMenu menu(this);
        QAction *beep = menu.addAction(tr("Beep when it fires"));
        QAction *mark = menu.addAction(tr("Bookmark the frame that fired it"));
        QAction *every = menu.addAction(tr("Announce every occurrence (not just the first)"));
        for (QAction *a : { beep, mark, every }) a->setCheckable(true);
        beep->setChecked(w.beep);
        mark->setChecked(w.bookmark);
        every->setChecked(w.everyTime);
        QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        setActions(row, beep->isChecked(), mark->isChecked(), every->isChecked());
    });
    root->addWidget(m_table, 1);

    auto *btns = new QHBoxLayout;
    btns->setSpacing(UiStyle::gap());
    m_armBtn = new QPushButton(tr("Re-arm"), this);
    m_armBtn->setObjectName(QStringLiteral("watchRearmBtn"));
    m_armBtn->setToolTip(tr("Forget the hit and wait for it again."));
    m_delBtn = new QPushButton(tr("Remove"), this);
    m_delBtn->setObjectName(QStringLiteral("watchRemoveBtn"));
    btns->addWidget(m_armBtn);
    btns->addWidget(m_delBtn);
    btns->addStretch(1);
    root->addLayout(btns);

    m_status = new StatusLine(this);
    m_status->state(tr("A watch waits for a condition and holds what it saw."));
    root->addWidget(m_status);

    connect(m_addBtn, &QPushButton::clicked, this, &WatchPanel::addWatch);
    connect(m_expr, &QLineEdit::returnPressed, this, &WatchPanel::addWatch);
    connect(m_delBtn, &QPushButton::clicked, this, &WatchPanel::removeSelected);
    connect(m_armBtn, &QPushButton::clicked, this, &WatchPanel::rearmSelected);

    connect(m_table, &QTableWidget::itemDoubleClicked,
            this, [this](QTableWidgetItem *it) {
        if (!it || it->row() >= m_list.count()) { return; }
        const WatchList::Watch &w = m_list.watches().at(it->row());
        if (w.fired && !w.firedEntry.isNull()) {
            emit revealRequested(w.firedEntry, w.firedBy);
        }
    });

    // "Fired 40 s ago" has to keep counting while nothing arrives — a watch
    // that fired is most interesting in the minutes after it did.
    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &WatchPanel::refresh);
    m_tick->start();
}

void WatchPanel::addWatch()
{
    const QString expr = m_expr->text().trimmed();
    if (expr.isEmpty()) {
        m_status->warn(tr("Write a condition first."));
        return;
    }
    const int i = m_list.add(expr, m_label->text());
    if (i < 0) { return; }

    const WatchList::Watch &w = m_list.watches().at(i);
    if (!w.valid()) {
        // Marked in the box where it was typed, at the offset that caused it.
        m_expr->setQueryError(w.parseError, w.errorOffset);
        m_status->fail(tr("That condition does not parse — the watch is "
                          "listed, but it cannot fire until it does."));
    } else {
        m_expr->clearQueryError();
        m_expr->rememberCurrent();
        m_status->ok(tr("Watching."));
        m_expr->clear();
        m_label->clear();
    }
    persist();
    refresh();
}

void WatchPanel::removeSelected()
{
    const int r = m_table->currentRow();
    if (r < 0) { m_status->warn(tr("Select a watch first.")); return; }
    const QStringList before = m_list.toStrings();
    QString what = m_list.watches().at(r).label;
    if (what.isEmpty()) { what = m_list.watches().at(r).expr; }
    m_list.remove(r);
    if (m_undo) {
        QPointer<WatchPanel> self(this);
        m_undo->push(tr("Remove watch %1").arg(what), [self, before]() {
            if (!self) { return false; }
            self->m_list.fromStrings(before);
            self->persist();
            self->refresh();
            return true;
        });
        m_status->say(tr("Removed. Ctrl+Z puts it back."));
    } else {
        m_status->say(tr("Removed."));
    }
    persist();
    refresh();
}

void WatchPanel::rearmSelected()
{
    const int r = m_table->currentRow();
    if (r < 0) {
        // No selection: re-arm everything, which is what is wanted between
        // runs and saves clicking down a list of them.
        m_list.rearmAll();
        m_status->say(tr("All watches re-armed."));
    } else {
        m_list.rearm(r);
        m_status->say(tr("Re-armed."));
    }
    refresh();
}

void WatchPanel::observe(const LogEntryPtr &entry, const QString &sourceKey)
{
    const QVector<int> fired = m_list.observe(entry, sourceKey, m_names);
    if (fired.isEmpty()) { return; }

    for (int i : fired) {
        const WatchList::Watch &w = m_list.watches().at(i);
        emit watchFired(w.label.isEmpty() ? w.expr : w.label, w.expr, w.firedBy);
        // Its actions (session 82).
        if (w.beep) QApplication::beep();
        if (w.bookmark && !w.firedEntry.isNull()) emit bookmarkRequested(w.firedEntry, w.firedBy,
                                                                         w.label.isEmpty() ? w.expr : w.label);
    }
    refresh();
}

void WatchPanel::refresh()
{
    const auto &ws = m_list.watches();
    if (m_table->rowCount() != ws.size()) { m_table->setRowCount(ws.size()); }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (int i = 0; i < ws.size(); ++i) {
        const WatchList::Watch &w = ws.at(i);

        auto set = [this, i](int col, const QString &text, const QString &tip = QString()) {
            QTableWidgetItem *it = m_table->item(i, col);
            if (!it) { it = new QTableWidgetItem; m_table->setItem(i, col, it); }
            if (it->text() != text) { it->setText(text); }
            it->setToolTip(tip);
            return it;
        };

        // A glyph as well as a colour: a fired watch has to be findable by
        // someone who cannot tell the two colours apart.
        QTableWidgetItem *state = set(ColState,
            !w.valid()  ? QStringLiteral("!")
            : w.fired   ? QStringLiteral("\u25cf")
                        : QStringLiteral("\u25cb"),
            !w.valid()  ? w.parseError
            : w.fired   ? tr("Fired. Double-click to go to the frame.")
                        : tr("Waiting."));
        state->setForeground(!w.valid() ? UiColor::error()
                             : w.fired  ? UiColor::error()
                                        : UiColor::muted());

        QString actions;
        if (w.beep) actions += QStringLiteral(" \u266A");        // ♪
        if (w.bookmark) actions += QStringLiteral(" \u2691");    // ⚑
        if (w.everyTime) actions += QStringLiteral(" \u21BB");   // ↻
        set(ColWhat, (w.label.isEmpty() ? w.expr
                                        : QStringLiteral("%1  —  %2").arg(w.label, w.expr)) + actions,
            w.expr + (actions.isEmpty() ? QString()
                                        : tr("\n\n♪ beeps · ⚑ bookmarks the frame · ↻ announces every occurrence"
                                             "\nRight-click to change.")));
        set(ColTimes, w.occurrences > 0 ? QString::number(w.occurrences) : QString(),
            w.occurrences > 0 ? tr("%1 occurrence(s), %2 matching frame(s)").arg(w.occurrences).arg(w.matches)
                              : QString());

        if (!w.valid()) {
            set(ColWhen, tr("cannot fire"), w.parseError);
            set(ColFrom, QString());
        } else if (!w.fired) {
            set(ColWhen, tr("waiting"));
            set(ColFrom, QString());
        } else {
            // Both facts: when it fired, and whether it is still true. A
            // count on its own would not say the first; a time on its own
            // would not say the second.
            const QString when = QDateTime::fromMSecsSinceEpoch(w.firedAtMs)
                                     .toString(QStringLiteral("HH:mm:ss"));
            set(ColWhen, QStringLiteral("%1  (%2)").arg(when, sinceText(now - w.firedAtMs)),
                w.matches > 1
                    ? tr("Still matching — %1 frames since.\\n\\n%2")
                          .arg(w.matches).arg(w.evidence.left(300))
                    : w.evidence.left(300));
            set(ColFrom, w.firedBy);
        }
    }

    if (ws.isEmpty()) {
        m_status->state(tr("A watch waits for a condition and holds what it saw."));
    }
}

void WatchPanel::restore()
{
    m_list.fromStrings(Settings::watches());
    refresh();
}

void WatchPanel::persist() const
{
    Settings::setWatches(m_list.toStrings());
}

bool WatchPanel::setActions(int index, bool beep, bool bookmark, bool everyTime)
{
    if (!m_list.setActions(index, beep, bookmark, everyTime)) return false;
    persist();
    refresh();
    return true;
}
