#include "testutil.h"
#include "layoutaudit.h"

#include "rfidcheck.h"
#include "rfidtag.h"
#include "routestrip.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"
#include "undolog.h"

#include <cstdio>

#include <QFile>
#include <QTabWidget>
#include <QTableWidget>

// =============================================================================
//  Session 196 — RFID Tag Builder, phase B: route checks, the route strip,
//  signals, add duplicate, shift, undo.
//
//  Routes are built here from field values through the schema (the same
//  encoder the window uses), so the suite runs anywhere. With the local,
//  git-ignored real KAV_CONFIG routes (tests/fixtures/tags_sim/) present, it
//  also checks the real Hafizpet DN_MAIN route; without them it says so.
// =============================================================================

namespace {

QByteArray tag(int unique, qint64 loc, bool dup = false, int placement = 0, int tinNom = 84, int tinRev = 84, int type = 9)
{
    return RfidTag::build({ { QStringLiteral("type"), type },       { QStringLiteral("version"), 1 },
                            { QStringLiteral("unique"), unique },   { QStringLiteral("abs_loc"), loc },
                            { QStringLiteral("abs_loc_1"), loc },   { QStringLiteral("tin_nom"), tinNom },
                            { QStringLiteral("tin_rev"), tinRev },  { QStringLiteral("placement"), placement },
                            { QStringLiteral("duplication"), dup ? 1 : 0 } });
}

// A clean nominal route: five main tags 300 m apart, each with its duplicate
// 4 m on; tag 102 is a nominal signal foot with its signal.
RfidTag::Route clean()
{
    RfidTag::Route r;
    r.name = QStringLiteral("clean");
    r.dir = RfidTag::DirNominal;
    for (int i = 0; i < 5; ++i) {
        const int place = i == 2 ? 1 : 0;
        r.tags.append({ QString(), tag(100 + i, 160000 + 300 * i, false, place) });
        r.tags.append({ QString(), tag(100 + i, 160004 + 300 * i, true, place) });
    }
    r.signalList.append({ QStringLiteral("102"), QStringLiteral("S1"), QStringLiteral("4") });
    return r;
}

int count(const QVector<RfidCheck::Finding> &f, RfidCheck::Level level, const QString &needle = QString(), int row = -2)
{
    int n = 0;
    for (const RfidCheck::Finding &x : f)
        if (x.level == level && (needle.isEmpty() || x.text.contains(needle)) && (row == -2 || x.row == row)) ++n;
    return n;
}

QByteArray dump(const QVector<RfidCheck::Finding> &f)
{
    QByteArray out;
    for (const RfidCheck::Finding &x : f)
        out += QByteArray("\n      row ") + QByteArray::number(x.row + 1) + " " + x.tag.toUtf8() + ": " + x.text.toUtf8();
    return out;
}

}  // namespace

TEST_SUITE(session196)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    using RfidCheck::Level;

    // ---- 1. the checks ------------------------------------------------------------------------
    {
        const RfidTag::Route r = clean();
        const QVector<RfidCheck::Finding> f = RfidCheck::check(r);
        CHECK(count(f, Level::Attention) == 0, QByteArray("a clean route: nothing to look at") + dump(f));
        CHECK(RfidCheck::deltaText(r, 1) == QLatin1String("+4") && RfidCheck::deltaText(r, 2) == QLatin1String("+296")
                  && RfidCheck::deltaText(r, 0).isEmpty(),
              "Δ: metres on from the tag before");

        RfidTag::Route u = r;
        u.dir = RfidTag::DirUnset;
        const QVector<RfidCheck::Finding> fu = RfidCheck::check(u);
        CHECK(count(fu, Level::Info, QStringLiteral("Direction not set"), -1) == 1, "no direction: says what is not checked");

        RfidTag::Route rev = r;
        rev.dir = RfidTag::DirReverse;
        const QVector<RfidCheck::Finding> fr = RfidCheck::check(rev);
        CHECK(count(fr, Level::Attention, QStringLiteral("back from")) == 4
                  && count(fr, Level::Attention, QStringLiteral("not further along")) == 5,
              QByteArray("the same tags read as a reverse route: every step goes back, every duplicate is behind its main")
                  + dump(fr));
        CHECK(RfidCheck::deltaText(rev, 2) == QLatin1String("-296"), "and Δ is negative");

        RfidTag::Route bad = r;
        bad.tags[4].bytes[3] = char(bad.tags[4].bytes.at(3) ^ 0x10);          // corrupt 102
        const QVector<RfidCheck::Finding> fb = RfidCheck::check(bad);
        CHECK(count(fb, Level::Attention, QStringLiteral("a loco would not process this tag"), 4) == 1,
              QByteArray("a corrupted tag: its CRC-30 fails, said as what a loco would do") + dump(fb));

        RfidTag::Route swapped = r;
        std::swap(swapped.tags[2], swapped.tags[4]);                          // 101 and 102 out of order
        const QVector<RfidCheck::Finding> fs = RfidCheck::check(swapped);
        CHECK(count(fs, Level::Attention, QStringLiteral("Not right after its main tag 101")) == 1
                  && count(fs, Level::Attention, QStringLiteral("back from")) >= 1,
              QByteArray("two main tags swapped: the order and the pairing are both named") + dump(fs));

        RfidTag::Route lone = r;
        lone.tags.remove(0);                                                  // 100 gone, 100D stays
        lone.tags.remove(lone.tags.size() - 1);                               // 104D gone
        const QVector<RfidCheck::Finding> fl = RfidCheck::check(lone);
        CHECK(count(fl, Level::Attention, QStringLiteral("No main tag 100")) == 1
                  && count(fl, Level::Info, QStringLiteral("1 main tag with no duplicate: 104")) == 1,
              QByteArray("a duplicate with no main; a main with no duplicate (info)") + dump(fl));

        RfidTag::Route differ = r;
        differ.tags[3].bytes = tag(101, 160304, true, 0, 85);                  // 101D on another TIN
        const QVector<RfidCheck::Finding> fd = RfidCheck::check(differ);
        CHECK(count(fd, Level::Attention, QStringLiteral("Differs from its main tag in tin_nom"), 3) == 1,
              QByteArray("a duplicate that differs from its main tag says in what") + dump(fd));

        RfidTag::Route tin = r;
        for (int i = 6; i < tin.tags.size(); ++i)
            tin.tags[i].bytes = tag(103 + (i - 6) / 2, 160900 + 300 * ((i - 6) / 2) + ((i % 2) ? 4 : 0), i % 2, 0, 2, 84);
        const QVector<RfidCheck::Finding> ft = RfidCheck::check(tin);
        CHECK(count(ft, Level::Info, QStringLiteral("changes 84 → 2"), 6) == 1 && count(ft, Level::Attention) == 0,
              QByteArray("the TIN for this direction changing is noted, once, as information") + dump(ft));

        RfidTag::Route sig = r;
        sig.signalList.append({ QStringLiteral("999"), QStringLiteral("S9"), QStringLiteral("7") });
        sig.signalList.append({ QStringLiteral("101"), QStringLiteral("S8"), QStringLiteral("6") });
        sig.signalList.removeFirst();
        const QVector<RfidCheck::Finding> fg = RfidCheck::check(sig);
        CHECK(count(fg, Level::Attention, QStringLiteral("foot tag 999, which is not a main tag")) == 1
                  && count(fg, Level::Attention, QStringLiteral("Signal S8 is at this tag"), 2) == 1
                  && count(fg, Level::Info, QStringLiteral("with no signal"), 4) == 1,
              QByteArray("signals: an unknown foot tag, a signal on a non-foot tag, a foot tag with no signal") + dump(fg));

        RfidTag::Route adj = r;
        QHash<QString, qint64> a12{ { QStringLiteral("type"), 12 },      { QStringLiteral("version"), 1 },
                                    { QStringLiteral("unique"), 660 },   { QStringLiteral("abs_loc_1"), 160700 },
                                    { QStringLiteral("abs_loc_2"), 5000 }, { QStringLiteral("dir_corr_1"), 1 },
                                    { QStringLiteral("tin_nom"), 84 },   { QStringLiteral("tin_rev"), 84 } };
        adj.tags.insert(6, { QString(), RfidTag::build(a12) });
        const QVector<RfidCheck::Finding> fa = RfidCheck::check(adj);
        CHECK(count(fa, Level::Info, QStringLiteral("location-2 5000 m; for this direction dir_corr_1 = 1 (Loco Nominal"), 6) == 1
                  && count(fa, Level::Attention, QStringLiteral("back from")) == 0 && RfidCheck::deltaText(adj, 7).isEmpty(),
              QByteArray("an adjustment tag: what it tells a nominal loco; no order finding across it") + dump(fa));

        RfidTag::Route lc = r;
        lc.tags.append({ QString(), tag(672, 161600, false, 0, 84, 84, 10) });
        CHECK(count(RfidCheck::check(lc), Level::Info, QStringLiteral("no LC gate tag has been seen in a capture")) == 1,
              "an LC gate tag is noted as unchecked against a real one");
    }

    // ---- 2. a real route (local fixtures) -----------------------------------------------------
    const QString real = QStringLiteral(DL_SRC_DIR "/tests/fixtures/tags_sim/DN_MAIN.tagroute.xml");
    if (!QFile::exists(real)) {
        fprintf(stderr, "  NOTE [session196] tests/fixtures/tags_sim/ is not here (kept local, not in git): "
                        "the check of the real Hafizpet DN_MAIN route is skipped\n");
    } else {
        RfidTag::Route r = RfidTag::readFile(real).value(0);
        r.dir = RfidTag::DirNominal;
        const QVector<RfidCheck::Finding> f = RfidCheck::check(r);
        CHECK(count(f, Level::Attention) == 2
                  && count(f, Level::Attention, QStringLiteral("Signal S1637_H (id 4) names foot tag 904, which is not a main tag"), -1) == 1
                  && count(f, Level::Attention, QStringLiteral("a loco would not process this tag"), 23) == 1
                  && count(f, Level::Info, QStringLiteral("TIN for this direction changes")) == 5,
              QByteArray("the real Hafizpet DN_MAIN, nominal: signal S1637_H names tag 904 (the route starts at 910), "
                         "871D fails its CRC-30, the TIN changes five times") + dump(f));
    }

    // ---- 3. the window ------------------------------------------------------------------------
    {
        TagBuilderWindow w;
        w.resize(1100, 680);
        w.show();
        w.setRoute(clean());
        CHECK(w.tabs()->count() == 3 && w.tabs()->tabText(1) == QLatin1String("Signals (1)")
                  && w.tabs()->tabText(2) == QLatin1String("Checks") && w.strip()->drawnTags() == 10,
              QByteArray("Tags, Signals (1), Checks; the strip draws all 10 tags: ") + w.tabs()->tabText(2).toUtf8());
        CHECK(w.routeTable()->item(2, 5)->text() == QLatin1String("+296"), "the Δ column");

        // The strip: a click near a tag selects it.
        const QPoint at(int(w.strip()->width() / 2.0), 50);       // 160600 m is the middle: tag 102
        const int hit = w.strip()->rowAt(at);
        CHECK(hit == 4, QByteArray("the strip finds the tag under the mouse (row ") + QByteArray::number(hit + 1) + ")");

        // Add duplicate.
        w.selectRow(9);                                            // 104D
        w.deleteTag();
        w.selectRow(8);                                            // 104
        w.addDuplicate();
        const RfidTag::Summary d = RfidTag::summary(w.route().tags.value(9).bytes);
        CHECK(w.route().tags.size() == 10 && d.duplicate && d.unique == 104 && d.absLoc == 161204 && d.crcOk,
              "Add duplicate: 104D right after 104, 4 m on, CRC computed");
        w.selectRow(8);
        w.addDuplicate();
        CHECK(w.route().tags.size() == 10 && w.status()->kind() == StatusLine::Kind::Warn,
              "and not twice");

        // Shift, then undo it.
        const RfidTag::Route beforeShift = w.route();
        CHECK(w.shiftAll(-1000) && RfidTag::summary(w.route().tags.at(0).bytes).absLoc == 159000
                  && RfidTag::summary(w.route().tags.at(9).bytes).absLoc == 160204
                  && RfidTag::summary(w.route().tags.at(9).bytes).crcOk,
              "Shift -1000 m: every tag moved, CRCs recomputed");
        CHECK(!w.shiftAll(-200000) && RfidTag::summary(w.route().tags.at(0).bytes).absLoc == 159000
                  && w.status()->kind() == StatusLine::Kind::Fail,
              "a shift that would take a tag below 0 m is refused, nothing moved");
        CHECK(w.undo() && w.route().tags.at(0).bytes == beforeShift.tags.at(0).bytes
                  && w.route().tags.at(9).bytes == beforeShift.tags.at(9).bytes,
              "Undo puts the shift back");
        const int depth = w.undoLog()->depth();
        CHECK(depth == 2, QByteArray("each edit is one undo step (delete, add duplicate; the shift taken back off): ")
                              + QByteArray::number(depth));

        // Direction: reverse reads every step as going back.
        w.setDirection(RfidTag::DirReverse);
        CHECK(w.tabs()->tabText(2).startsWith(QLatin1String("Checks (")) && w.checkTable()->rowCount() > 5,
              QByteArray("the Checks tab counts what to look at: ") + w.tabs()->tabText(2).toUtf8());
        int r9 = -1;
        for (int i = 0; i < w.checkTable()->rowCount(); ++i)
            if (w.checkTable()->item(i, 0)->text() == QLatin1String("3")) { r9 = i; break; }
        w.tabs()->setCurrentIndex(2);
        if (r9 >= 0) emit w.checkTable()->cellDoubleClicked(r9, 2);
        CHECK(r9 >= 0 && w.tabs()->currentIndex() == 0 && w.routeTable()->currentRow() == 2,
              "double-clicking a finding selects its tag");
        CHECK(w.undo() && w.route().dir == RfidTag::DirNominal, "and the direction change undoes too");

        // Signals.
        w.selectRow(0);
        w.addSignal();
        CHECK(w.route().signalList.size() == 2 && w.route().signalList.at(1).footTag == QLatin1String("100")
                  && w.tabs()->currentIndex() == 1,
              "Add signal: at the selected tag, Signals tab shown");
        w.signalTable()->item(1, 1)->setText(QStringLiteral("S0"));
        CHECK(w.route().signalList.at(1).name == QLatin1String("S0"), "editing a cell edits the signal");
        int noted = 0;
        for (int i = 0; i < w.checkTable()->rowCount(); ++i)
            noted += w.checkTable()->item(i, 2)->text().contains(QLatin1String("Signal S0 is at this tag")) ? 1 : 0;
        CHECK(noted == 1, "and the checks follow: S0 is on a tag that is not a signal foot");
        w.signalTable()->selectRow(1);
        w.deleteSignal();
        CHECK(w.route().signalList.size() == 1, "Delete signal");

        CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
              QByteArray("fits a laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
                  + QByteArray::number(w.minimumSizeHint().height()) + ")");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("no widget outside a layout: ") + loose.join(QLatin1Char(' ')).toUtf8());
        w.setRoute(RfidTag::Route());
    }
}
