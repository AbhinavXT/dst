#include "testutil.h"

#include "logentry.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableView>
#include <QTextStream>

// =============================================================================
//  Session 254 — NMS tables: Hide unchanging (the header and event columns
//  every packet so far reads the same in) and Columns… (any column by hand).
//  Real frames: replay/loco_1_1_29062026_134509.cap (2081 health packets).
//  What is "unchanging" is worked out here again from the cells.
// =============================================================================

namespace {

QVector<LogEntryPtr> healthOf(const QString &path)
{
    QVector<LogEntryPtr> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    qint64 ms = 1782465000000;
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (!line.startsWith(QLatin1String("@nmshlth_"))) continue;
        auto e = LogEntryPtr::create();
        e->header.source_id = 21;
        e->header.kvchId = 1;
        e->epochMs = ms += 1000;
        e->severity = Severity::Info;
        e->text = line;
        e->cacheDerived();
        out << e;
    }
    return out;
}

// Columns after CRC (the header and the events) whose non-empty cells all
// read the same, by name.
QStringList sameEverywhere(const NmsLog::Table &t)
{
    QStringList out;
    const int firstAfterLead = t.index.value(QStringLiteral("CRC")) + 1;
    for (int c = firstAfterLead; c < t.columnCount() - 1; ++c) {
        QString first;
        bool same = true;
        for (int r = 0; r < t.rows.size() && same; ++r) {
            const QString v = t.cell(r, c);
            if (v.isEmpty()) continue;
            if (first.isEmpty()) first = v;
            else if (v != first) same = false;
        }
        if (same) out << t.header(c);
    }
    return out;
}

// Every cell of every row fits its column (the width the view would give it).
bool allFit(QTableView *v, QString *firstCut)
{
    QAbstractItemModel *m = v->model();
    for (int r = 0; r < m->rowCount(); ++r)
        for (int c = 0; c < m->columnCount(); ++c)
            if (!v->isColumnHidden(c) && v->columnWidth(c) < v->sizeHintForIndex(m->index(r, c)).width()) {
                if (firstCut) *firstCut = QStringLiteral("row %1 col %2 '%3'").arg(r + 1).arg(c).arg(m->index(r, c).data().toString().left(40));
                return false;
            }
    return true;
}

void settle()
{
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
}

}  // namespace

TEST_SUITE(session254)
{
    const QVector<LogEntryPtr> hl = healthOf(QStringLiteral("../replay/loco_1_1_29062026_134509.cap"));
    CHECK(hl.size() == 2081, "fixture: 2081 @nmshlth packets");
    NmsLog::Table all;
    for (const LogEntryPtr &e : hl) NmsLog::append(all, NmsLog::Kind::Health, *e);
    const QStringList want = sameEverywhere(all);
    CHECK(want.contains(QStringLiteral("nms.start_of_frame")) && want.contains(QStringLiteral("RADIO_1_HEALTH"))
              && !want.contains(QStringLiteral("DMI_1_LINK_STATUS")) && !want.contains(QStringLiteral("nms.time")),
          qPrintable(QStringLiteral("fixture: %1 columns read the same in every packet (start_of_frame, the radios; "
                                    "not the DMI link or the time)").arg(want.size())));
    {
        QStringList got;
        for (int c = 0; c < all.columnCount(); ++c)
            if (all.unchanging(c) && all.groupAt(c) != NmsLog::Group::Lead) got << all.header(c);
        CHECK(got == want, "Table::unchanging() names exactly those");
        CHECK(!all.unchanging(all.columnCount() - 1), "the frame column is never 'unchanging'");
    }

    LogModel model(nullptr, 100000);
    model.appendEntries(hl.mid(0, 2000));
    NmsLogWindow w(NmsLog::Kind::Health, &model, QStringLiteral("1_1"), QStringLiteral("L1"));
    w.resize(1000, 500);
    w.show();
    settle();
    CHECK(w.hiddenColumns().isEmpty(), "at open: every column shown");
    auto *box = w.findChild<QCheckBox *>(QStringLiteral("nmsHideUnchanging"));
    CHECK(box && !box->isChecked(), "Hide unchanging: off at open");
    box->setChecked(true);
    settle();
    {
        NmsLog::Table part;
        for (const LogEntryPtr &e : hl.mid(0, 2000)) NmsLog::append(part, NmsLog::Kind::Health, *e);
        const QStringList wantPart = sameEverywhere(part);
        CHECK(w.hiddenColumns() == wantPart,
              qPrintable(QStringLiteral("ticked: exactly the %1 unchanging columns hidden").arg(wantPart.size())));
        bool leadShown = true;
        for (const char *c : { "PC time", "rtc", "seq", "loco / ctrl", "frame len", "CRC", "Frame (hex)" })
            leadShown = leadShown && !w.hiddenColumns().contains(QLatin1String(c));
        CHECK(leadShown, "time, rtc, seq, loco / ctrl, frame len, CRC and the frame stay");
        QLabel *count = nullptr;
        for (QLabel *l : w.findChildren<QLabel *>()) if (l->text().contains(QLatin1String("packets"))) count = l;
        CHECK(count && count->text().contains(QStringLiteral("%1 of %2 columns hidden").arg(wantPart.size()).arg(part.columnCount())),
              qPrintable("the count says how many: " + (count ? count->text() : QString())));
        // Live: a column that was the same so far and changes in the rest comes back.
        QStringList comeBack;
        for (const QString &c : wantPart) if (!want.contains(c)) comeBack << c;
        model.appendEntries(hl.mid(2000));
        settle();
        CHECK(w.hiddenColumns() == want, "the rest arrives: hidden is now the whole capture's unchanging columns");
        bool back = true;
        for (const QString &c : comeBack) back = back && !w.hiddenColumns().contains(c);
        CHECK(back, qPrintable(QStringLiteral("a column that changed later is shown again (%1)").arg(comeBack.join(", "))));
    }

    // Frozen columns and the pinned strip follow.
    w.freezeColumns(12);
    w.setPinned(5, true);
    settle();
    {
        auto *frozen = w.findChild<QTableView *>(QStringLiteral("frozenColumns"));
        bool same = true;
        for (int c = 0; c < w.view()->model()->columnCount(); ++c)
            same = same && w.pinnedView()->isColumnHidden(c) == w.view()->isColumnHidden(c)
                   && (c >= 12 || frozen->isColumnHidden(c) == w.view()->isColumnHidden(c));
        CHECK(same, "the frozen columns and the pinned strip hide the same columns");
    }

    // Columns…: untick one, tick it back, Show all.
    {
        QDialog *d = w.columnChooser();
        d->show();
        settle();
        auto *list = d->findChild<QListWidget *>(QStringLiteral("nmsColumnList"));
        CHECK(list && list->count() == w.view()->model()->columnCount(), "Columns…: one line per column");
        QListWidgetItem *seq = nullptr, *radio = nullptr;
        for (int i = 0; list && i < list->count(); ++i) {
            if (list->item(i)->data(Qt::UserRole).toString() == QLatin1String("seq")) seq = list->item(i);
            if (list->item(i)->data(Qt::UserRole).toString() == QLatin1String("RADIO_1_HEALTH")) radio = list->item(i);
        }
        CHECK(radio && radio->text().contains(QLatin1String("same in every packet")) && seq
                  && !seq->text().contains(QLatin1String("same")),
              "the list marks the unchanging columns");
        if (seq) seq->setCheckState(Qt::Unchecked);
        settle();
        CHECK(w.hiddenColumns().contains(QStringLiteral("seq")), "untick seq: hidden (even though Lead)");
        for (int i = 0; list && i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toString() == QLatin1String("seq")) seq = list->item(i);
        CHECK(seq && seq->checkState() == Qt::Unchecked, "the list, refilled, still shows seq unticked");
        if (seq) seq->setCheckState(Qt::Checked);
        settle();
        CHECK(!w.hiddenColumns().contains(QStringLiteral("seq")), "tick it again: shown");
        w.setColumnShown(QStringLiteral("DMI_1_LINK_STATUS"), false);
        CHECK(w.hiddenColumns().contains(QStringLiteral("DMI_1_LINK_STATUS")), "hide a changing column by hand");
        auto *showAll = d->findChild<QPushButton *>(QStringLiteral("nmsShowAll"));
        if (showAll) showAll->click();
        settle();
        CHECK(showAll && w.hiddenColumns().isEmpty() && !box->isChecked(), "Show all: every column, Hide unchanging off");
    }

    // A column hidden by hand stays hidden when a new column arrives (the
    // positions move; it is kept by name).
    {
        NmsLog::Table t;
        int split = -1, cols = 0;
        for (int i = 0; i < hl.size(); ++i) {
            NmsLog::append(t, NmsLog::Kind::Health, *hl.at(i));
            if (t.columnCount() > cols) { cols = t.columnCount(); split = i; }
        }
        LogModel m2(nullptr, 100000);
        m2.appendEntries(hl.mid(0, split));
        NmsLogWindow w2(NmsLog::Kind::Health, &m2, QStringLiteral("1_1"), QStringLiteral("L1"));
        w2.setColumnShown(QStringLiteral("SKAVACH_REGULAR_PKT_OFFSET"), false);
        const int before = w2.view()->model()->columnCount();
        m2.appendEntries(hl.mid(split));
        settle();
        CHECK(w2.view()->model()->columnCount() > before
                  && w2.hiddenColumns() == QStringList({ QStringLiteral("SKAVACH_REGULAR_PKT_OFFSET") }),
              "a new column arrives: the one hidden by hand is still the only one hidden");
        // The column-fit after that reset: Qt's resize-to-contents measures only
        // the rows near the view; the kept widths cover every row (the shot
        // showed packet 2081's seq as "348…").
        w2.setColumnShown(QStringLiteral("SKAVACH_REGULAR_PKT_OFFSET"), true);
        QString cut;
        CHECK(allFit(w2.view(), &cut), qPrintable("after the reset, every cell of all 2081 rows fits " + cut));
        // With the app's style sheet item padding (UiStyle: "padding:2px 4px";
        // wider here): measured off a real cell, not guessed (the shot did not
        // fit until it was).
        {
            LogModel m4(nullptr, 100000);
            m4.appendEntries(hl.mid(0, 10));
            NmsLogWindow w4(NmsLog::Kind::Health, &m4, QStringLiteral("1_1"), QStringLiteral("L1"));
            w4.view()->setStyleSheet(QStringLiteral("QTableView::item { padding: 2px 9px; }"));
            m4.appendEntries(hl.mid(10));
            settle();
            QString cut4;
            CHECK(allFit(w4.view(), &cut4), qPrintable("with style sheet item padding, every cell fits " + cut4));
        }
        // Rows hidden by Changes only when they arrived fit when shown again.
        LogModel m3(nullptr, 100000);
        m3.appendEntries(hl.mid(0, 10));
        NmsLogWindow w3(NmsLog::Kind::Health, &m3, QStringLiteral("1_1"), QStringLiteral("L1"));
        w3.findChild<QCheckBox *>(QStringLiteral("nmsChangesOnly"))->setChecked(true);
        m3.appendEntries(hl.mid(10));
        settle();
        w3.findChild<QCheckBox *>(QStringLiteral("nmsChangesOnly"))->setChecked(false);
        settle();
        cut.clear();
        CHECK(allFit(w3.view(), &cut), qPrintable("rows that arrived under Changes only fit when shown " + cut));
    }
}
