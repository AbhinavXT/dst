#include "testutil.h"

#include "colorrules.h"
#include "dmidiff.h"
#include "dmipanel.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "runcomparewindow.h"
#include "sessionwindow.h"
#include "sessionfile.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 374 — two runs played in step, the two DMIs side by side, and
//  what differs between them at each moment (DmiDiff). Real frames: two runs
//  of loco 1_1 on 2026-06-27, replay/loco_1_1_27062026_140226.cap (to 14:06)
//  and replay/loco_1_1_27062026_151052.cap (the rear-end capture of
//  sessions 133 and 343), each written to a .dlr the way Save writes it.
// =============================================================================

namespace {

QString capToDlr374(const QString &cap, const QString &dlr, qint64 untilMs)
{
    MessageDispatcher disp;
    QFile f(cap);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
        if (untilMs && ms >= untilMs) break;
        disp.ingestLocal(21, 1, l, ms, QString());
    }
    disp.drainNow();
    LogModel *m = disp.modelForKey(QStringLiteral("21_1"));
    QFile out(dlr);
    if (!m || !out.open(QIODevice::WriteOnly)) return QString();
    SessionFile::FileHeader h;
    h.createdMs = 1782558000000LL;
    h.sourceId = 21;
    h.kvchId = 1;
    out.write(SessionFile::encodeHeader(h));
    for (int r = 0; r < m->rowCount(); ++r) {
        const LogEntryPtr e = m->entryAt(r);
        if (e && !e->rawBytes.isEmpty()) out.write(SessionFile::encodeRecord(e->epochMs, e->rawBytes));
    }
    return dlr;
}

// The capture's @dmi_1_1 lines, in order, as (time, line).
QVector<QPair<qint64, QString>> dmiLines374(const QString &cap, qint64 untilMs)
{
    QVector<QPair<qint64, QString>> out;
    QFile f(cap);
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) {
        const QString l = QString::fromLatin1(f.readLine().trimmed());
        if (!l.startsWith(QLatin1String("@dmi_1_1 "))) continue;
        const qint64 ms = QDateTime::fromString(l.section(QLatin1Char(' '), 1, 1), Qt::ISODate).toMSecsSinceEpoch();
        if (untilMs && ms >= untilMs) break;
        out.append({ ms, l });
    }
    return out;
}

}  // namespace

TEST_SUITE(session374)
{
    ThemeUtil::apply(Theme::Dark);
    UiStyle::apply();

    // ---- DmiDiff ---------------------------------------------------------------------------------
    {
        DmiState a, b;
        CHECK(DmiDiff::compare(a, b).isEmpty(), "two empty pictures: nothing differs");
        a.valid = true;
        a.speed = 45;
        a.modeText = QStringLiteral("FS");
        a.brakeType = 0;
        b = a;
        CHECK(DmiDiff::compare(a, b).isEmpty(), "the same picture: nothing differs");
        const QVector<DmiDiff::Item> one = DmiDiff::compare(a, DmiState());
        CHECK(one.size() == 1 && one.first().text() == QStringLiteral("DMI a frame → no frame"),
              "one side without a frame: said in one line");
        b.speed = 52;
        b.modeText = QStringLiteral("OS");
        b.brakeType = 4;
        b.hasTargetSpeed = true;
        b.targetSpeed = 30;
        const QVector<DmiDiff::Item> d = DmiDiff::compare(a, b);
        QStringList texts;
        for (const DmiDiff::Item &i : d) texts << i.text();
        CHECK(texts == (QStringList{ QStringLiteral("Mode FS → OS"), QStringLiteral("Brake none → EB"),
                                     QStringLiteral("Speed 45 km/h → 52 km/h"), QStringLiteral("Target speed — → 30 km/h") }),
              "in panel order, in the DMI's words: " + texts.join(QStringLiteral(" | ")).toUtf8());
        CHECK(!d.at(0).continuous && d.at(2).continuous && d.at(2).key == QLatin1String("speed"),
              "speed is marked as a value that moves every frame; mode is not");
        CHECK(DmiDiff::brakeName(3) == QLatin1String("FSB") && DmiDiff::brakeName(5) == QLatin1String("LE")
                  && DmiDiff::brakeName(9) == QLatin1String("9"),
              "brake names as the panel's B8");
    }

    // ---- two real runs ----------------------------------------------------------------------------
    const QString capA = QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap");
    const QString capB = QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap");
    const qint64 untilA = QDateTime::fromString(QStringLiteral("2026-06-27T14:06:00"), Qt::ISODate).toMSecsSinceEpoch();
    QTemporaryDir tmp;
    const QString dlrA = capToDlr374(capA, tmp.filePath(QStringLiteral("runA.dlr")), untilA);
    const QString dlrB = capToDlr374(capB, tmp.filePath(QStringLiteral("runB.dlr")), 0);
    const auto dmiA = dmiLines374(capA, untilA), dmiB = dmiLines374(capB, 0);
    CHECK(!dlrA.isEmpty() && !dlrB.isEmpty() && dmiA.size() > 100 && dmiB.size() > 10, "fixture: two runs with @dmi");
    if (dmiA.isEmpty() || dmiB.isEmpty()) return;

    ColorRules rules;
    RunCompareWindow w(&rules);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.show();
    QString err;
    CHECK(w.loadRun(RunCompareWindow::A, { dlrA }, &err) && w.loadRun(RunCompareWindow::B, { dlrB }, &err),
          "both runs load: " + err.toUtf8());
    CHECK(w.locos(RunCompareWindow::A) == QStringList{ QStringLiteral("1_1") } && w.loco(RunCompareWindow::B) == QLatin1String("1_1"),
          "loco 1_1 in each, chosen");
    CHECK(w.start(RunCompareWindow::A) == dmiA.first().first && w.start(RunCompareWindow::B) == dmiB.first().first,
          "each run's T+0 is its first DMI frame");
    CHECK(w.length() == qMax(dmiA.last().first - dmiA.first().first, dmiB.last().first - dmiB.first().first),
          "the clock runs to the end of the longer run");

    // ---- T+0, then T+20 s: each panel the frame at its own start + T --------------------------------
    w.setT(0);
    CHECK(w.frameMs(RunCompareWindow::A) == dmiA.first().first && w.frameMs(RunCompareWindow::B) == dmiB.first().first
              && w.state(RunCompareWindow::A).valid && w.state(RunCompareWindow::B).valid,
          "T+0: both first frames on show");
    w.setT(20000);
    auto lastAtOrBefore = [](const QVector<QPair<qint64, QString>> &v, qint64 ms) {
        qint64 r = 0;
        for (const auto &p : v)
            if (p.first <= ms) r = p.first;
        return r;
    };
    CHECK(w.frameMs(RunCompareWindow::A) == lastAtOrBefore(dmiA, dmiA.first().first + 20000)
              && w.frameMs(RunCompareWindow::B) == lastAtOrBefore(dmiB, dmiB.first().first + 20000),
          "T+20 s: each run's last @dmi at or before its own start + 20 s");
    {
        const QVector<DmiDiff::Item> d = w.differences();
        const QVector<DmiDiff::Item> want = DmiDiff::compare(w.state(RunCompareWindow::A), w.state(RunCompareWindow::B));
        CHECK(d.size() == want.size() && w.diffTable()->rowCount() == d.size(),
              "the table lists what differs between the two pictures: " + QByteArray::number(d.size()) + " field(s)");
        bool cells = true;
        for (int i = 0; i < d.size(); ++i)
            cells = cells && w.diffTable()->item(i, 0)->text() == d.at(i).label && w.diffTable()->item(i, 1)->text() == d.at(i).a
                    && w.diffTable()->item(i, 2)->text() == d.at(i).b;
        CHECK(cells, "field, Run A, Run B, as DmiDiff says");
    }
    CHECK(w.caption(RunCompareWindow::A)->text().startsWith(QStringLiteral("Loco 1_1 · ")), "each panel says whose and when");

    // ---- moving one start moves only that run ----------------------------------------------------
    w.setStart(RunCompareWindow::B, dmiB.first().first + 5000);
    CHECK(w.frameMs(RunCompareWindow::B) == lastAtOrBefore(dmiB, dmiB.first().first + 25000)
              && w.frameMs(RunCompareWindow::A) == lastAtOrBefore(dmiA, dmiA.first().first + 20000),
          "B's T+0 five seconds later: B shows 25 s in, A still 20 s");

    // ---- play ---------------------------------------------------------------------------------------
    w.setT(0);
    w.setSpeed(4);
    w.play();
    CHECK(w.isPlaying(), "Play");
    w.advance(1000);
    CHECK(w.t() == 4000, "4×: one second plays four on both");
    w.pause();
    w.advance(1000);
    CHECK(!w.isPlaying() && w.t() == 4000, "paused: the clock stands");
    w.play();
    for (int i = 0; i < 200 && w.isPlaying(); ++i) w.advance(10000);
    CHECK(!w.isPlaying() && w.t() == w.length() && w.status()->text().contains(QLatin1String("End")),
          "at the end of the longer run it stops, and says so");

    // ---- align ------------------------------------------------------------------------------------
    {
        // Run A brakes (FSB or EB) once 14:02-14:06; the rear-end capture has
        // no FSB or EB on its DMI.
        qint64 brakeA = 0, brakeB = 0;
        for (const auto &p : dmiA) { const int t = dmiStateFromLine(p.second).brakeType; if (!brakeA && (t == 3 || t == 4)) brakeA = p.first; }
        for (const auto &p : dmiB) { const int t = dmiStateFromLine(p.second).brakeType; if (!brakeB && (t == 3 || t == 4)) brakeB = p.first; }
        CHECK(brakeA > 0 && brakeB == 0, "fixture: run A brakes, run B does not");
        const qint64 startA = w.start(RunCompareWindow::A), startB = w.start(RunCompareWindow::B);
        QString why;
        CHECK(!w.align(RunCompareWindow::AlignAt::FirstBrake, &why)
                  && why == QStringLiteral("Run B (loco 1_1) has no FSB or EB: its start is left as it was."),
              "Align at the first FSB or EB with a run that has none: refused, naming it: " + why.toUtf8());
        CHECK(w.start(RunCompareWindow::A) == startA && w.start(RunCompareWindow::B) == startB, "and neither start moved");

        // The same run on both sides: both T+0 at that brake.
        RunCompareWindow same(&rules);
        same.setAttribute(Qt::WA_DeleteOnClose, false);
        same.loadRun(RunCompareWindow::A, { dlrA });
        same.loadRun(RunCompareWindow::B, { dlrA });
        CHECK(same.align(RunCompareWindow::AlignAt::FirstBrake) && same.start(RunCompareWindow::A) == brakeA
                  && same.start(RunCompareWindow::B) == brakeA && same.t() == 0
                  && same.state(RunCompareWindow::A).brakeType >= 3 && same.differences().isEmpty(),
              "a run beside itself, aligned at its brake: both panels show the brake, nothing differs");
        CHECK(same.diffTable()->rowCount() == 0, "and the table is empty");

        CHECK(w.align(RunCompareWindow::AlignAt::FirstDmi) && w.start(RunCompareWindow::A) == dmiA.first().first
                  && w.start(RunCompareWindow::B) == dmiB.first().first,
              "Align at the first DMI frame puts both back");
    }

    // ---- a file with no @dmi is refused by name ---------------------------------------------------
    {
        QFile empty(tmp.filePath(QStringLiteral("nodmi.dlr")));
        empty.open(QIODevice::WriteOnly);
        SessionFile::FileHeader h;
        h.createdMs = 1782558000000LL;
        h.sourceId = 21;
        h.kvchId = 1;
        empty.write(SessionFile::encodeHeader(h));
        empty.close();
        QString e;
        CHECK(!w.loadRun(RunCompareWindow::B, { empty.fileName() }, &e) && e.contains(QLatin1String("No @dmi frames in nodmi.dlr")),
              "a recording without @dmi: refused, by name: " + e.toUtf8());
        CHECK(w.loco(RunCompareWindow::B) == QLatin1String("1_1"), "and run B is left as it was");
    }

    // ---- from a recording: Tools ▸ Compare with another run… -------------------------------------
    {
        NameMap names;
        SessionWindow sw(&rules, &names, Theme::Dark);
        sw.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(sw.compareWithAnotherRun() == nullptr, "nothing loaded: no compare window");
        sw.loadFiles({ dlrA });
        RunCompareWindow *c = sw.compareWithAnotherRun();
        CHECK(c && c->loco(RunCompareWindow::A) == QLatin1String("1_1") && c->start(RunCompareWindow::A) == dmiA.first().first
                  && c->loco(RunCompareWindow::B).isEmpty(),
              "the recording is run A; run B is chosen there");
        delete c;
    }
}
