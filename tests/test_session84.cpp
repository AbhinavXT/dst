#include "testutil.h"

#include "capturedecoder.h"
#include "dmipanel.h"
#include "dmitimetravel.h"
#include "logentry.h"
#include "logmodel.h"
#include "replaywindow.h"
#include "settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTest>
#include <QKeyEvent>
#include <QToolButton>

// =============================================================================
//  Session 84: DMI time travel. The DMI window, following the cursor, shows
//  each loco's latest @dmi at or before the row / replay record picked
//  elsewhere. Fed from REAL @dmi frames in replay/.
// =============================================================================

namespace {

QStringList realDmiOf(const QString &key, int limit)
{
    QStringList out;
    const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
    const QString prefix = QStringLiteral("@dmi_") + key + QLatin1Char(' ');
    for (const QString &f : dir.entryList({ QStringLiteral("loco_*.cap") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        while (!file.atEnd() && out.size() < limit) {
            const QString l = QString::fromLatin1(file.readLine()).trimmed();
            if (l.startsWith(prefix)) out << l;
        }
    }
    return out;
}

LogEntryPtr entry(qint64 ms, const QString &text)
{
    LogEntryPtr e(new LogEntry);
    e->epochMs = ms;
    e->text = text;
    return e;
}

// A frame whose speed differs from `other`'s, so the view proves which it drew.
QString differentSpeed(const QStringList &frames, const QString &other)
{
    const int s = dmiStateFromLine(other).speed;
    for (const QString &f : frames)
        if (dmiStateFromLine(f).valid && dmiStateFromLine(f).speed != s) return f;
    return QString();
}

class Counter : public QObject { public: int runs = 0; };

}  // namespace

TEST_SUITE(session84)
{
    const qint64 T0 = QDateTime(QDate(2026, 6, 27), QTime(12, 58, 40)).toMSecsSinceEpoch();
    const QStringList l1 = realDmiOf(QStringLiteral("1_1"), 4000);
    const QStringList l2 = realDmiOf(QStringLiteral("2_1"), 400);
    CHECK(l1.size() > 100 && l2.size() > 100, "real @dmi frames from two locos in replay/");
    if (l1.size() < 2 || l2.size() < 2) return;
    const QString f1 = l1.first();
    const QString f2 = differentSpeed(l1, f1);
    const QString g1 = l2.first();
    const QString g2 = l2.last();
    CHECK(!f2.isEmpty(), "two loco-1 frames with different speeds");

    // ---- pure helpers -----------------------------------------------------------------
    CHECK(dmiKeyOfText(f1) == QLatin1String("1_1") && dmiKeyOfText(g1) == QLatin1String("2_1"),
          "the loco of an @dmi line, from its token");
    CHECK(dmiKeyOfText(QStringLiteral("@arp_1_1 x")).isEmpty() && dmiKeyOfText(QStringLiteral("hello")).isEmpty(),
          "anything else has none");

    // Tab A (loco 1) and tab B (loco 2): frames and other traffic interleaved.
    LogModel a, b;
    a.appendEntry(entry(T0 + 1000, f1));
    a.appendEntry(entry(T0 + 1500, QStringLiteral("some text")));
    a.appendEntry(entry(T0 + 2000, f2));
    a.appendEntry(entry(T0 + 2500, QStringLiteral("SLRP from station")));
    a.appendEntry(entry(T0 + 9000, QStringLiteral("seven seconds after the last frame")));
    b.appendEntry(entry(T0 + 1200, g1));
    b.appendEntry(entry(T0 + 2600, g2));
    const QVector<const LogModel *> both{ &a, &b };

    CHECK(logModelRowAfter(&a, T0 + 1500) == 2 && logModelRowAfter(&a, T0) == 0 && logModelRowAfter(&a, T0 + 99999) == 5,
          "the first row after a time, by binary search");

    {
        const DmiMoment m = dmiMomentFromModels(both, &a, 3, QStringLiteral("tab A"));
        CHECK(m.valid && m.atMs == T0 + 2500 && m.origin == QLatin1String("tab A"), "the moment is the clicked row's time");
        const DmiFrameAt *x = m.frameFor(QStringLiteral("1_1"));
        const DmiFrameAt *y = m.frameFor(QStringLiteral("2_1"));
        CHECK(x && x->frameMs == T0 + 2000 && dmiStateFromCapture(x->cap).speed == dmiStateFromLine(f2).speed,
              "loco 1: its latest frame before the row, not the one before that");
        CHECK(y && y->frameMs == T0 + 1200, "loco 2, from the other tab: its frame before the moment, not the one after");
        CHECK(m.keys() == QStringList({ QStringLiteral("1_1"), QStringLiteral("2_1") }), "one frame per loco, in key order");
        CHECK(m.preferredKey.isEmpty(), "a text row names no loco");
    }
    {
        const DmiMoment m = dmiMomentFromModels(both, &a, 0, QStringLiteral("tab A"));
        const DmiFrameAt *x = m.frameFor(QStringLiteral("1_1"));
        CHECK(x && x->frameMs == T0 + 1000 && m.preferredKey == QLatin1String("1_1"),
              "clicking an @dmi row shows exactly that frame, and its loco is preferred");
        CHECK(!m.frameFor(QStringLiteral("2_1")), "loco 2 had sent nothing yet");
    }
    {
        const DmiMoment m = dmiMomentFromModels(both, &a, 4, QStringLiteral("tab A"));
        const DmiFrameAt *x = m.frameFor(QStringLiteral("1_1"));
        CHECK(x && m.atMs - x->frameMs == 7000 && m.atMs - x->frameMs > kDmiStaleMs,
              "seven seconds on, the frame is still what was shown, and old enough to be muted");
        CHECK(m.frameFor(QStringLiteral("2_1")) && m.frameFor(QStringLiteral("2_1"))->frameMs == T0 + 2600,
              "and loco 2's newer frame is picked up");
    }
    {
        LogModel far;
        far.appendEntry(entry(T0, f1));
        far.appendEntry(entry(T0 + kDmiLookbackMs + 1, QStringLiteral("eleven minutes on")));
        const DmiMoment m = dmiMomentFromModels({ &far }, &far, 1, QString());
        CHECK(m.valid && m.frames.isEmpty(), "a frame beyond the look-back is 'none', not an ancient panel");
    }
    {
        // Same millisecond in two tabs: the clicked tab's frame stands.
        LogModel c;
        c.appendEntry(entry(T0 + 2000, f1));
        const DmiMoment m = dmiMomentFromModels({ &a, &c }, &a, 2, QString());
        CHECK(m.frameFor(QStringLiteral("1_1")) && dmiStateFromCapture(m.frameFor(QStringLiteral("1_1"))->cap).speed
                                                        == dmiStateFromLine(f2).speed,
              "a tie in time: the clicked tab's frame wins");
    }
    {
        // A live tab trims its oldest rows after the click: the resolver
        // finds the row again; once it is trimmed away there is no moment.
        LogModel live(nullptr, 10, 0.5);
        for (int i = 0; i < 8; ++i) live.appendEntry(entry(T0 + i * 100, i == 5 ? f1 : QStringLiteral("row %1").arg(i)));
        const LogEntryPtr clicked = live.entryAt(6);
        const DmiResolver r = dmiTabResolver({ &live }, &live, clicked, 6, QStringLiteral("live"));
        CHECK(r().frameFor(QStringLiteral("1_1")) != nullptr, "resolver: the moment at the clicked row");
        for (int i = 8; i < 11; ++i) live.appendEntry(entry(T0 + i * 100, QStringLiteral("row %1").arg(i)));
        CHECK(live.entryPtrAt(6) != clicked.data() && logModelRowOf(&live, clicked.data(), 6) == 1,
              "after a trim the row has moved up, and is found again");
        const DmiMoment again = r();
        CHECK(again.valid && again.atMs == T0 + 600 && again.frameFor(QStringLiteral("1_1")),
              "the same moment, the same frame");
        for (int i = 11; i < 20; ++i) live.appendEntry(entry(T0 + i * 100, QStringLiteral("row %1").arg(i)));
        CHECK(logModelRowOf(&live, clicked.data(), 1) == -1 && !r().valid, "trimmed away: no moment, not a wrong one");
    }

    // ---- the broker ---------------------------------------------------------------------
    auto *tt = DmiTimeTravel::instance();
    tt->reset();
    {
        auto *owner = new Counter;
        tt->offer(owner, [owner]() { ++owner->runs; DmiMoment m; m.valid = true; m.atMs = 42; return m; });
        CHECK(owner->runs == 0, "nobody follows: offering costs nothing");
        QObject follower;
        int seen = 0;
        auto conn = QObject::connect(tt, &DmiTimeTravel::momentChanged, [&seen](const DmiMoment &) { ++seen; });
        tt->follow(&follower);
        CHECK(owner->runs == 1 && seen == 1 && tt->last().atMs == 42, "the first follower gets the last place pointed at");
        tt->offer(owner, [owner]() { ++owner->runs; DmiMoment m; m.valid = true; m.atMs = 43; return m; });
        CHECK(owner->runs == 2 && tt->last().atMs == 43, "while followed, every move is resolved");
        tt->unfollow(&follower);
        tt->offer(owner, [owner]() { ++owner->runs; return DmiMoment(); });
        CHECK(owner->runs == 2, "unfollowed: back to free");
        delete owner;
        {
            QObject f2;
            tt->follow(&f2);
            CHECK(seen == 2 && tt->last().atMs == 43, "the offering window has gone: its resolver is not run");
        }
        CHECK(!tt->hasFollowers(), "a destroyed follower drops out");
        QObject::disconnect(conn);
    }
    tt->reset();

    // ---- the window -----------------------------------------------------------------------
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    const QVariant followWas = settings.value(QStringLiteral("dmi/followCursor"));
    settings.remove(QStringLiteral("dmi/followCursor"));
    {
        DmiWindow w(nullptr);
        w.show();
        auto *follow = w.findChild<QCheckBox *>(QStringLiteral("dmiFollowCursor"));
        CHECK(follow && !follow->isChecked() && !w.followCursor(), "by default the window is live");
        w.observeLine(QStringLiteral("1_1"), f1);
        CHECK(w.view()->state().speed == dmiStateFromLine(f1).speed, "live: the latest frame");

        // The operator clicks a row in tab A (loco 2's tab also open).
        LogModel *pa = &a, *pb = &b;
        QObject tabOwner;
        tt->offer(&tabOwner, dmiTabResolver({ pa, pb }, pa, a.entryAt(3), 3, QStringLiteral("tab A")));
        if (follow) follow->setChecked(true);
        CHECK(w.followCursor() && tt->hasFollowers(), "ticking Follow cursor follows");
        CHECK(w.moment().valid && w.moment().atMs == T0 + 2500, "it picks up the row already selected");
        CHECK(w.selectedSource() == QLatin1String("1_1") && w.view()->state().speed == dmiStateFromLine(f2).speed,
              "and draws loco 1 as it was then (the later frame, not the live one)");
        CHECK(w.statusText().contains(QStringLiteral("tab A")) && w.statusText().contains(QStringLiteral("0.5 s before")),
              QByteArray("the status says when, where from, and how old the frame was: ") + w.statusText().toUtf8());
        CHECK(w.windowTitle().contains(QStringLiteral("12:58:42")), "the title carries the moment");
        CHECK(!w.view()->isStale(), "a fresh frame is not muted");

        w.observeLine(QStringLiteral("1_1"), f1);
        CHECK(w.view()->state().speed == dmiStateFromLine(f2).speed, "live frames do not overwrite the moment");

        // Another loco chosen by hand.
        w.setSelectedSource(QStringLiteral("2_1"));
        CHECK(w.view()->state().valid && w.statusText().contains(QStringLiteral("2_1")), "loco 2 at the same moment");

        // Clicking a loco-1 @dmi row brings loco 1 back.
        tt->offer(&tabOwner, dmiTabResolver({ pa, pb }, pa, a.entryAt(0), 0, QStringLiteral("tab A")));
        CHECK(w.selectedSource() == QLatin1String("1_1") && w.view()->state().speed == dmiStateFromLine(f1).speed,
              "the clicked row's loco is shown");

        // Seven seconds on: the same frame, muted.
        tt->offer(&tabOwner, dmiTabResolver({ pa, pb }, pa, a.entryAt(4), 4, QStringLiteral("tab A")));
        CHECK(w.view()->isStale() && w.statusText().contains(QStringLiteral("stale")), "an old frame is muted, and says so");

        // A loco with nothing: the empty panel and the reason.
        LogModel quiet;
        quiet.appendEntry(entry(T0, QStringLiteral("no dmi here")));
        LogModel *pq = &quiet;
        w.setSelectedSource(QStringLiteral("2_1"));
        tt->offer(&tabOwner, dmiTabResolver({ pq }, pq, quiet.entryAt(0), 0, QStringLiteral("quiet")));
        CHECK(!w.view()->state().valid && w.statusText().contains(QStringLiteral("no @dmi")),
              "nothing before the moment: the panel is empty and says why");

        if (follow) follow->setChecked(false);
        CHECK(!w.followCursor() && !tt->hasFollowers(), "unticked: live again");
        w.setSelectedSource(QStringLiteral("1_1"));
        CHECK(w.view()->state().speed == dmiStateFromLine(f1).speed && !w.windowTitle().contains(QStringLiteral(" at ")),
              "the live frame that arrived meanwhile is shown");
        CHECK(QSettings(Settings::iniPath(), QSettings::IniFormat).value(QStringLiteral("dmi/followCursor")).toBool() == false,
              "the choice is remembered");
    }
    tt->reset();

    // ---- replay -------------------------------------------------------------------------------
    {
        const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
        const QStringList caps{ dir.filePath(QStringLiteral("loco_1_1_27062026_140226.cap")),
                                dir.filePath(QStringLiteral("loco_2_1_27062026_140226.cap")) };
        ReplayWindow replay(caps);
        CHECK(replay.recordCount() > 100, "two real captures load together");
        // Brute force against the index, across the capture.
        int checked = 0, agree = 0, withBoth = -1;
        const int n = replay.recordCount();
        for (int i = 0; i < n; i += qMax(1, n / 97)) {
            const DmiMoment m = replay.dmiMomentAt(i);
            ++checked;
            bool ok = m.valid;
            for (const QString &key : { QStringLiteral("1_1"), QStringLiteral("2_1") }) {
                const DmiFrameAt *f = m.frameFor(key);
                if (f) ok = ok && f->frameMs <= m.atMs && f->cap.type == CapType::Dmi && f->cap.key() == key;
            }
            if (m.frames.size() == 2 && withBoth < 0) withBoth = i;
            if (ok) ++agree;
        }
        CHECK(checked > 50 && agree == checked, "every cursor: each loco's frame is an @dmi of that loco, at or before it");
        CHECK(withBoth >= 0, "somewhere both locos have a panel");

        // Moving on can only move a loco's frame forward in time.
        bool monotonic = true;
        qint64 prev = 0;
        for (int i = 0; i < n; ++i) {
            const DmiFrameAt *f = replay.dmiMomentAt(i).frameFor(QStringLiteral("1_1"));
            if (!f) continue;
            if (f->frameMs < prev) { monotonic = false; break; }
            prev = f->frameMs;
        }
        CHECK(monotonic, "scrubbing forward never shows an older frame");

        auto *btn = replay.findChild<QToolButton *>(QStringLiteral("replayDmi"));
        CHECK(btn && btn->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+D")), "⏱ DMI at cursor… on Ctrl+Alt+D");
        replay.show();
        replay.openDmi();
        auto *w = replay.findChild<DmiWindow *>();
        CHECK(w && w->followCursor(), "it opens a DMI window already following");
        const int at = withBoth >= 0 ? withBoth : 0;
        replay.setActiveKey(1);   // 2_1
        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        // Drive the cursor to `at` with the keys the operator would use.
        while (replay.cursorIndex() < at) QApplication::sendEvent(&replay, &right);
        if (w) {
            const DmiMoment m = replay.dmiMomentAt(at);
            CHECK(w->moment().atMs == m.atMs && w->selectedSource() == QLatin1String("2_1"),
                  "the panel follows the replay cursor, on the replay's active loco");
            const DmiFrameAt *f = m.frameFor(QStringLiteral("2_1"));
            CHECK(f && w->view()->state().speed == dmiStateFromCapture(f->cap).speed,
                  "and draws that loco's frame at the cursor");
            CHECK(w->statusText().contains(QStringLiteral("replay")), "the status names the replay as the source");
            w->close();
        }
    }
    tt->reset();
    if (followWas.isValid()) settings.setValue(QStringLiteral("dmi/followCursor"), followWas);
    else settings.remove(QStringLiteral("dmi/followCursor"));
}
