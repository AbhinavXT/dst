#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "packetmakerdialog.h"
#include "packetpreset.h"
#include "sessionkeystore.h"
#include "stationkavach.h"
#include "stationkavachpanel.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QUdpSocket>

// =============================================================================
//  Session 451 — the live drawing follows every LSRP. Packet Maker answering
//  as the station redrew the loco only after an LSRP it answered, and drew
//  it on the line of the ref tag (which only an answer moves): an LSRP not
//  answered left the loco where it was until Show on the drawing was clicked
//  again. Now each LSRP heard moves it, on the line of its LAST_RFID_TAG.
//  Unanswered here: station 527's routes made UP, the loco running DN. Real frames: replay/loco_1_1_27062026_140226.cap, station 527's KAVACH
//  data (tests/fixtures/station_kavach/section30_527.json) on the built-in
//  Section_30.xlsx.
// =============================================================================

namespace {

QStringList linesOf(const char *path)
{
    QFile f(QString::fromLatin1(path));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList out;
    for (const QString &l : QString::fromLatin1(f.readAll()).split(QLatin1Char('\n'))) out << l.trimmed();
    return out;
}

QHash<QString, qint64> rawOf(const QString &line)
{
    QHash<QString, qint64> raw;
    CaptureDecoder::describe(CaptureDecoder::parseLine(line), nullptr, 0, &raw);
    return raw;
}

// The datagrams Packet Maker sent (its answers), read off.
int drain(QUdpSocket &rx)
{
    QElapsedTimer t; t.start();
    while (!rx.hasPendingDatagrams() && t.elapsed() < 20) { QCoreApplication::processEvents(); rx.waitForReadyRead(5); }
    int n = 0;
    while (rx.hasPendingDatagrams()) { rx.receiveDatagram(); ++n; }
    return n;
}

} // namespace

TEST_SUITE(session451)
{
    StationLayout::Layout s30, fixture;
    CHECK(StationLayout::load(QStringLiteral(":/station_layouts/Section_30.xlsx"), &s30), "Section_30.xlsx loads");
    StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &fixture);
    StationKavach::Station file;
    CHECK(StationKavach::fromJson(fixture.kavach, &file) && !file.routes.isEmpty(), "station 527's KAVACH data");
    StationLayout::Layout lay = s30;
    lay.kavach = fixture.kavach;
    file.tagLocs = StationLayout::tagLocationsByNumber(s30);
    const StationKavach::State state = StationKavach::State::initial(file);
    const QStringList lines = linesOf(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap");

    // ---- the lane: the tag last read, not the ref tag ----
    {
        // Two tags of the station's first route that are on the layout and on different lines.
        StationKavach::Loco at;
        at.id = 1;
        const StationKavach::Route &r = file.routes.first();
        int a = -1, bTag = -1;
        QString laneA, laneB;
        for (const StationKavach::RouteTag &t : r.tags) {
            if (!file.tagLocs.contains(t.tag)) continue;
            StationKavach::Loco o = at;
            o.refTag = t.tag;
            o.absLoc = file.tagLocs.value(t.tag).first();
            const StationLayoutCanvas::LiveView v = liveViewOf(lay, file, state, { o });
            if (v.locos.isEmpty()) continue;
            if (a < 0) { a = t.tag; laneA = v.locos.first().laneTag; }
            else if (v.locos.first().laneTag != laneA) { bTag = t.tag; laneB = v.locos.first().laneTag; break; }
        }
        CHECK(a >= 0 && bTag >= 0, "two tags of the route, drawn on different tags of the layout");
        StationKavach::Loco o = at;
        o.refTag = a;                                   // the station's ref, from the last answer
        o.lastTag = bTag;                               // the tag the loco has read since
        o.absLoc = file.tagLocs.value(bTag).first();
        const StationLayoutCanvas::LiveView v = liveViewOf(lay, file, state, { o });
        CHECK(v.locos.size() == 1 && v.locos.first().laneTag == laneB,
              "drawn at the tag it last read (LAST_RFID_TAG), not at the ref tag of the last answer");
        o.lastTag = 9999;   // a tag the layout lacks: the ref tag's
        CHECK(liveViewOf(lay, file, state, { o }).locos.value(0).laneTag == laneA, "a tag the layout lacks: the ref tag's place");
    }

    // ---- Packet Maker: an LSRP the station does not answer still moves the loco ----
    {
        QStringList lsrps;
        for (const QString &l : lines) if (l.startsWith(QLatin1String("@lsrp_"))) lsrps << l;
        CHECK(lsrps.size() >= 2, "the capture's LSRPs");
        if (lsrps.size() < 2) return;
        // Station 527 with its routes (all DN) made the other way: none is for a loco running DN.
        const int dir = int(rawOf(lsrps.first()).value(QStringLiteral("MOVEMENT_DIR")));
        StationKavach::Station other = file;
        for (StationKavach::Route &r : other.routes) r.pktDir = dir == 1 ? 2 : 1;
        CHECK((dir == 1 || dir == 2) && file.routes.first().pktDir == dir, "the loco runs DN, as station 527's routes do");
        while (lsrps.size() > 150) lsrps.removeLast();   // two and a half minutes of it
        StationLayout::Layout away = lay;
        away.kavach = StationKavach::toJson(other);
        QTemporaryDir tmp;
        const QString layPath = tmp.filePath(QStringLiteral("s30_527.json"));
        CHECK(StationLayout::save(layPath, away), "Section_30 with them saved");
        QString aapLine, arpLine;
        for (const QString &l : lines) {
            if (l.startsWith(QLatin1String("@aap_")) && aapLine.isEmpty()) aapLine = l;
            if (l.startsWith(QLatin1String("@arp_")) && arpLine.isEmpty()) arpLine = l;
        }
        PacketPreset preset;
        preset.captype = QStringLiteral("aap");
        preset.header = rawOf(aapLine);
        const QString presetPath = tmp.filePath(QStringLiteral("aap.json"));
        CHECK(!arpLine.isEmpty() && preset.save(presetPath), "an ARP and the AAP preset from the capture");


        QUdpSocket rx;
        CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), 0), "loopback receiver binds");
        SessionKeyStore keys;
        PacketMakerDialog dlg(nullptr, nullptr, &keys);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmDestHost"))->setText(QStringLiteral("127.0.0.1"));
        if (QSpinBox *port = dlg.findChild<QSpinBox *>(QStringLiteral("pmDestPort"))) port->setValue(rx.localPort());
        QCheckBox *arm = nullptr;
        for (QCheckBox *cb : dlg.findChildren<QCheckBox *>())
            if (cb->text().contains(QStringLiteral("Arm"), Qt::CaseInsensitive)) arm = cb;
        auto *answer = dlg.findChild<QCheckBox *>(QStringLiteral("pmAnswerLoco"));
        auto *from = dlg.findChild<QComboBox *>(QStringLiteral("pmAnswerFrom"));
        auto *stationBtn = dlg.findChild<QPushButton *>(QStringLiteral("pmStationPanel"));
        CHECK(arm && answer && from && stationBtn, "the arm switch, the answer box, its source, Station… are found");
        if (!(arm && answer && from && stationBtn)) return;
        from->setCurrentIndex(1);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerAap"))->setText(presetPath);
        dlg.findChild<QLineEdit *>(QStringLiteral("pmAnswerLayout"))->setText(layPath);
        answer->setChecked(true);
        stationBtn->click();
        StationKavachPanel *panel = dlg.stationPanel();
        if (!panel || !panel->drawingButton()) { CHECK(false, "Station… has Show on the drawing"); return; }
        panel->drawingButton()->click();
        StationLayoutWindow *w = dlg.stationDrawing();
        CHECK(w && w->isVisible(), "the drawing opens");
        if (!w) return;

        auto entry = [](const QString &line) {
            auto e = LogEntryPtr::create();
            e->text = line;
            e->epochMs = CaptureDecoder::parseLine(line).rtc.toMSecsSinceEpoch();
            e->header.source_id = 21;
            return e;
        };
        dlg.setStationRandomForTest(quint16(preset.header.value("STN_RND_NUM_RS")));
        arm->setChecked(true);
        dlg.observe(entry(arpLine));
        drain(rx);

        // Each LSRP of the capture in turn: the drawn loco is where that LSRP says, answered or not.
        int wrong = 0, unansweredMoved = 0, answered = 0, laneWrong = 0;
        qint64 before = rawOf(arpLine).value(QStringLiteral("ABS_LOCO_LOC"), -1);
        for (const QString &l : lsrps) {
            const qint64 abs = rawOf(l).value(QStringLiteral("ABS_LOCO_LOC"));
            dlg.observe(entry(l));
            const bool sent = drain(rx) > 0;
            const StationLayoutCanvas::LiveView v = w->canvas()->liveView();
            if (v.locos.size() != 1 || v.locos.first().absLoc != abs) { ++wrong; continue; }
            // Its lane: the tag it read, as liveViewOf places a loco whose ref is that tag.
            StationKavach::Loco o;
            o.id = 1;
            o.absLoc = abs;
            o.refTag = int(rawOf(l).value(QStringLiteral("LAST_RFID_TAG")));
            const StationLayoutCanvas::LiveView want = liveViewOf(away, other, StationKavach::State::initial(other), { o });
            if (want.locos.value(0).laneTag != v.locos.first().laneTag) ++laneWrong;
            if (sent) ++answered;
            else if (abs != before) ++unansweredMoved;
            before = abs;
        }
        CHECK(wrong == 0, qPrintable(QStringLiteral("every LSRP: the loco drawn at its ABS_LOCO_LOC (%1 not)").arg(wrong)));
        CHECK(answered == 0 && unansweredMoved > 10,
              qPrintable(QStringLiteral("none answered (%1), the loco moved with them (%2 moves)").arg(answered).arg(unansweredMoved)));
        CHECK(laneWrong == 0, qPrintable(QStringLiteral("each on the line of the tag it read (%1 not)").arg(laneWrong)));
        CHECK(w->liveState()->text().contains(QStringLiteral("loco 1 at %1").arg(QString::number(before / 1000.0, 'f', 3))),
              qPrintable(QStringLiteral("the line under the toolbar: where the last LSRP put it: ") + w->liveState()->text()));
    }
}
