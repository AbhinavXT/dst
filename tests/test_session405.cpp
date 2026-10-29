#include "testutil.h"

#include "benchrecord.h"
#include "locosim.h"
#include "locosimwindow.h"
#include "rfidtag.h"
#include "routestrip.h"
#include "settings.h"
#include "simpreview.h"

#include <QComboBox>
#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Session 405 — RFID faults per tag in the Loco Simulator's RFID tab: Late
//  (N ms), Sent twice, Bad CRC, for reader 1, 2 or both, each time the loco
//  passes. Every run is the same run (one clock, one speed) on
//  LocoTcasSimulator's own Configuration1.xml, once clean and once with the
//  faults, compared frame by frame.
// =============================================================================

namespace {

struct Out405 { qint64 ms; int reader; QByteArray tag; };

QVector<Out405> run405(LocoSimWindow &window, qint64 &t, int ticks)
{
    QVector<Out405> out;
    window.setTagSink([&out, &t](int, const QByteArray &frame) {
        out.append({ t, quint8(frame.at(3)), frame.mid(4, 16) });
    });
    window.placeAtStart();
    window.motion().setSpeed(72);
    window.start();
    for (int i = 0; i < ticks; ++i) {
        t += 100;
        window.tick();
    }
    window.stop();
    window.setTagSink({});
    return out;
}

QVector<Out405> of405(const QVector<Out405> &all, const QString &name, int reader)
{
    QVector<Out405> out;
    for (const Out405 &o : all)
        if (RfidTag::nameOf(o.tag) == name && (reader == 0 || o.reader == reader)) out.append(o);
    return out;
}

}  // namespace

TEST_SUITE(session405)
{
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
    // The bad CRC itself.
    const QVector<RfidTag::Route> routes
        = RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    CHECK(!routes.isEmpty(), "the simulator's Configuration1.xml");
    if (routes.isEmpty()) return;
    const QByteArray tag = routes.first().tags.first().bytes;
    const QByteArray bad = LocoSim::badCrcTag(tag);
    CHECK(RfidTag::summary(tag).crcOk && !RfidTag::summary(bad).crcOk && bad.left(15) == tag.left(15)
              && (quint8(bad.at(15)) ^ quint8(tag.at(15))) == 1,
          "Bad CRC: one bit of the last byte (inside the CRC-30) flipped, the CRC fails, the rest as it was");
    CHECK(RfidTag::summary(bad).absLoc == RfidTag::summary(tag).absLoc && RfidTag::nameOf(bad) == RfidTag::nameOf(tag),
          "what the loco decodes from it is unchanged");

    LocoSimWindow window(nullptr);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    qint64 t = 1782558147000LL;
    window.setClock([&t]() { return t; });
    window.setSendSink([](const QByteArray &) {});
    CHECK(window.loadRouteFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml")), "the route");
    window.setRoute(0);

    // ---- clean ---------------------------------------------------------------------------------
    const qint64 t0 = t;
    const QVector<Out405> clean = run405(window, t, 600);
    QStringList names;
    for (const Out405 &o : clean)
        if (o.reader == 1 && !names.contains(RfidTag::nameOf(o.tag))) names << RfidTag::nameOf(o.tag);
    CHECK(names.size() >= 4, qPrintable(QStringLiteral("a clean run: reader 1 sends %1 tags").arg(names.size())));
    if (names.size() < 4) return;
    const QString crcTag = names.at(1), twiceTag = names.at(2), lateTag = names.at(3);

    // ---- with faults ---------------------------------------------------------------------------
    window.setTagFault(crcTag, LocoSim::TagFault::BadCrc);
    window.setTagFault(twiceTag, LocoSim::TagFault::Twice, 1);
    window.setTagFault(lateTag, LocoSim::TagFault::Late, 3, 500);
    auto *box = window.findChild<QComboBox *>(QStringLiteral("locoSimFault_%1").arg(lateTag));
    CHECK(box && box->currentText() == QLatin1String("Late"), "the tag's row says Late");

    // The track view: each faulted tag's chip.
    int crcRow = -1;
    const QVector<SimPreview::Row> rows = SimPreview::rowsOf(routes.first());
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).id == crcTag) crcRow = i;
    CHECK(crcRow >= 0 && (window.wholeRoute()->tagState(crcRow) & RouteStrip::Faulted)
              && window.wholeRoute()->tagLabel(crcRow) == QLatin1String("crc"),
          "the track view marks the tag before the loco gets there: \"crc\"");

    QTemporaryDir dir;
    BenchRecord::Recorder recorder;
    window.setRecorder(&recorder);
    t = t0;
    recorder.start(dir.filePath(QStringLiteral("f.benchrec")), t);
    const QVector<Out405> faulty = run405(window, t, 600);
    recorder.stop(t);

    // Bad CRC, on both readers.
    const QVector<Out405> crcClean = of405(clean, crcTag, 0), crcFaulty = of405(faulty, crcTag, 0);
    bool allBad = !crcFaulty.isEmpty() && crcFaulty.size() == crcClean.size();
    for (int i = 0; allBad && i < crcFaulty.size(); ++i)
        allBad = crcFaulty.at(i).tag == LocoSim::badCrcTag(crcClean.at(i).tag) && !RfidTag::summary(crcFaulty.at(i).tag).crcOk;
    CHECK(allBad, qPrintable(QStringLiteral("tag %1: every frame on both readers with a bad CRC (%2)").arg(crcTag).arg(crcFaulty.size())));

    // Sent twice, on reader 1 only.
    CHECK(of405(faulty, twiceTag, 1).size() == 2 * of405(clean, twiceTag, 1).size()
              && of405(faulty, twiceTag, 2).size() == of405(clean, twiceTag, 2).size(),
          qPrintable(QStringLiteral("tag %1: twice on reader 1 (%2 vs %3), once on reader 2")
                         .arg(twiceTag).arg(of405(faulty, twiceTag, 1).size()).arg(of405(clean, twiceTag, 1).size())));
    const QVector<Out405> twice = of405(faulty, twiceTag, 1);
    CHECK(twice.size() >= 2 && twice.at(0).ms == twice.at(1).ms && twice.at(0).tag == twice.at(1).tag, "back to back, the same frame");

    // Late by 500 ms.
    const QVector<Out405> lateClean = of405(clean, lateTag, 1), lateFaulty = of405(faulty, lateTag, 1);
    CHECK(!lateFaulty.isEmpty() && lateFaulty.size() == lateClean.size()
              && lateFaulty.first().ms >= lateClean.first().ms + 500 && lateFaulty.first().ms < lateClean.first().ms + 600,
          qPrintable(QStringLiteral("tag %1: sent 500 ms late (%2 ms)").arg(lateTag).arg(lateFaulty.value(0).ms - lateClean.value(0).ms)));
    CHECK(lateFaulty.first().tag == lateClean.first().tag, "the late frame itself unchanged");

    // The other tags, as in the clean run.
    bool othersSame = true;
    for (const QString &name : names.mid(4))
        othersSame = othersSame && of405(faulty, name, 0).size() == of405(clean, name, 0).size();
    CHECK(othersSame, "every other tag as in the clean run");

    // In the recording, each fault said.
    BenchRecord::File f;
    BenchRecord::read(dir.filePath(QStringLiteral("f.benchrec")), &f);
    bool noteCrc = false, noteTwice = false, noteLate = false;
    for (const BenchRecord::Item &i : f.items) {
        noteCrc = noteCrc || i.note == QLatin1String("bad CRC");
        noteTwice = noteTwice || i.note == QLatin1String("sent twice (2nd)");
        noteLate = noteLate || i.note == QLatin1String("late by 500 ms");
    }
    CHECK(noteCrc && noteTwice && noteLate, "the recording notes each fault on its frame");
    window.setRecorder(nullptr);

    // A late tag still waiting when the loco stops is not sent.
    window.setTagFault(lateTag, LocoSim::TagFault::Late, 3, 60000);
    t = t0;
    const QVector<Out405> cut = run405(window, t, 600);
    CHECK(of405(cut, lateTag, 0).isEmpty() && window.lateQueued() == 0, "a late tag due after Stop: dropped, not sent");

    // None takes the fault off; a route picked afresh has none.
    window.setTagFault(crcTag, LocoSim::TagFault::None);
    CHECK(window.tagFault(crcTag).kind == LocoSim::TagFault::None && !(window.wholeRoute()->tagState(crcRow) & RouteStrip::Faulted),
          "None: the fault and its mark gone");
    window.setRoute(0);
    CHECK(window.tagFault(twiceTag).kind == LocoSim::TagFault::None, "the route picked again: no faults");

    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
}
