#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "packetbuilder.h"
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
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUdpSocket>

#include <algorithm>

// =============================================================================
//  Session 428 — the station live on the Station Layout drawing. Packet
//  Maker answering as the station: Station… ▸ Show on the drawing opens the
//  layout answered from; the signals are drawn at the aspects set in
//  Station…, each loco the station has heard at its ABS_LOCO_LOC (its train
//  TRAIN_LENGTH behind it), with the MA it was last sent, to where that MA
//  ends; an SoS sent is said. Real frames: station 527's two locos of
//  replay/loco_{1,2}_1_27062026_151052.cap (loco 1's MA cut behind loco 2,
//  session 427), and loco 1 past A904 at Red in
//  replay/loco_1_1_27062026_140226.cap (SoS SPAD, session 426) through
//  Packet Maker over loopback. Tag locations: the built-in Section_30.xlsx.
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

Schema::ParsedPacket slrpOf(const Schema::Encoder &enc, const QString &line)
{
    return enc.parseBody(QStringLiteral("slrp"), CaptureDecoder::parseLine(line).bytes);
}

QString slrpLine(const Schema::Encoder &enc, const QStringList &lines, qint64 fn)
{
    for (const QString &l : lines)
        if (l.startsWith(QLatin1String("@slrp_")) && slrpOf(enc, l).header.value(QStringLiteral("FRAME_NUM")) == fn) return l;
    return QString();
}

QString lineOf(const QStringList &lines, const char *prefix, qint64 fn)
{
    for (const QString &l : lines)
        if (l.startsWith(QLatin1String(prefix)) && rawOf(l).value(QStringLiteral("FRAME_NUM"), -1) == fn) return l;
    return QString();
}

// Station 527 as it was in June (session 427): the fixture's, plus S50 and
// route S50-S48 made from the real SLRP's own sub-packets.
StationKavach::Station juneStation(const Schema::Encoder &enc, const QString &realSlrp)
{
    StationLayout::Layout lay;
    StationKavach::Station s;
    StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &lay);
    StationKavach::fromJson(lay.kavach, &s);
    const Schema::ParsedPacket pp = slrpOf(enc, realSlrp);
    StationKavach::Route r;
    r.name = QStringLiteral("S50-S48 DN Main");
    r.entry = QStringLiteral("S50");
    r.exit = QStringLiteral("S48");
    r.pktDir = int(pp.header.value(QStringLiteral("PKT_DIR")));
    for (const Schema::SubEntry &se : pp.subs) {
        auto rows = [&se](const char *rep) {
            QVector<StationKavach::Row> out = se.repeats.value(QLatin1String(rep));
            for (StationKavach::Row &x : out) x.remove(QStringLiteral("class"));
            return out;
        };
        if (se.type == 0) {
            r.sectionType = int(se.values.value(QStringLiteral("TRAIN_SECTION_TYPE")));
            r.authority = int(se.values.value(QStringLiteral("AUTHORITY_TYPE")));
            r.osSpeed = int(se.values.value(QStringLiteral("AUTHORIZED_SPEED")));
        }
        if (se.type == 1) r.ssp = rows("speed");
        if (se.type == 2) r.gradient = rows("grad");
        if (se.type == 6) r.trackConditions = rows("tc");
        if (se.type == 5)
            for (const StationKavach::Row &t : se.repeats.value(QStringLiteral("tag")))
                r.tags << StationKavach::RouteTag{ int(t.value(QStringLiteral("dist"))), int(t.value(QStringLiteral("tag"))),
                                                   int(t.value(QStringLiteral("dup_dir"))) };
    }
    StationKavach::Signal s50;
    s50.name = QStringLiteral("S50");
    s50.footTag = 2;
    s.signalList.prepend(s50);
    s.routes.prepend(r);
    return s;
}

QByteArray receive(QUdpSocket &rx)
{
    QElapsedTimer t; t.start();
    while (!rx.hasPendingDatagrams() && t.elapsed() < 2000) { QCoreApplication::processEvents(); rx.waitForReadyRead(20); }
    return rx.hasPendingDatagrams() ? rx.receiveDatagram().data() : QByteArray();
}

int aspectOn(const StationLayoutCanvas::LiveView &v, const char *signal)
{
    for (const StationLayoutCanvas::KavachSignal &k : v.signalList) if (k.name == QLatin1String(signal)) return k.aspect;
    return -1;
}

bool hitSays(const StationLayoutCanvas *c, const QString &text)
{
    for (const StationLayoutCanvas::Hit &h : c->hits()) if (h.text.contains(text)) return true;
    return false;
}

} // namespace

TEST_SUITE(session428)
{
    PacketBuilder b;
    const Schema::Encoder &enc = b.encoder();
    StationLayout::Layout s30;
    CHECK(StationLayout::load(QStringLiteral(":/station_layouts/Section_30.xlsx"), &s30), "Section_30.xlsx loads");

    // ---- the two locos of 27 June through the station, to loco 1's first answer ----
    const QStringList one = linesOf(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap");
    const QStringList two = linesOf(DL_SRC_DIR "/replay/loco_2_1_27062026_151052.cap");
    StationKavach::Station st = juneStation(enc, slrpLine(enc, one, 54791));
    st.tagLocs = StationLayout::tagLocationsByNumber(s30);
    StationKavach::State state = StationKavach::State::initial(st);
    state.aspect.insert(QStringLiteral("S48"), 1);
    struct In { qint64 fn; int loco; QHash<QString, qint64> lsrp; };
    QVector<In> ins;
    for (const QString &l : one) if (l.startsWith(QLatin1String("@lsrp_"))) { auto r = rawOf(l); ins.append({ r.value(QStringLiteral("FRAME_NUM")), 1, r }); }
    for (const QString &l : two) if (l.startsWith(QLatin1String("@lsrp_"))) { auto r = rawOf(l); ins.append({ r.value(QStringLiteral("FRAME_NUM")), 2, r }); }
    std::stable_sort(ins.begin(), ins.end(), [](const In &x, const In &y) { return x.fn != y.fn ? x.fn < y.fn : x.loco < y.loco; });
    QHash<int, StationKavach::Loco> heard;
    for (const In &in : ins) {
        QList<StationKavach::Loco> others;
        for (auto it = heard.cbegin(); it != heard.cend(); ++it) if (it.key() != in.loco) others << it.value();
        StationKavach::answer(st, state, &heard[in.loco], in.lsrp, enc, others);
        if (in.fn == 54791 && in.loco == 1) break;   // loco 2 as last heard then (54789)
    }
    const StationKavach::Loco l1 = heard.value(1), l2 = heard.value(2);
    CHECK(l1.absLoc == 160661 && l1.maRoutes == (QVector<int>{ 0 }) && l1.maSent == 1236 && l1.sos == 5,
          "the station remembers what loco 1 was sent: S50-S48, MA 1236 m (cut behind loco 2), SoS Rear-end");
    CHECK(l2.absLoc == 161246 && l2.trainLength == 250 && l2.maSent == 1670 && l2.sos == 0,
          "and loco 2, ahead at 161246 m, 250 m long: MA 1670 m to S48, no SoS");

    StationLayout::Layout lay = s30;
    lay.kavach = StationKavach::toJson(st);
    const StationLayoutCanvas::LiveView v = liveViewOf(lay, st, state, { l1, l2 });
    CHECK(v.locos.size() == 2 && v.locos[0].id == 1 && v.locos[1].id == 2, "the live view: both locos");
    CHECK(v.locos[0].maEnd == 160896 && v.locos[0].maText == QStringLiteral("MA 1236 m") && v.locos[0].sos == QStringLiteral("SoS Rear-end"),
          "loco 1's MA ends at 160896 m: 100 m short of loco 2's rear (161246 - 250); SoS Rear-end");
    CHECK(v.locos[1].maEnd == 161330 && v.locos[1].sign == 1 && v.locos[1].trainLength == 250,
          "loco 2's at S48's foot, 1670 m from S50 (159660 m)");
    CHECK(!v.locos[0].laneTag.isEmpty() && aspectOn(v, "S48") == 1 && aspectOn(v, "S50") >= 0,
          "each placed on the lane of the tag it last read; S48 at Red, as set");
    {
        StationKavach::State yellow = state;
        yellow.aspect.insert(QStringLiteral("S48"), 4);
        CHECK(aspectOn(liveViewOf(lay, st, yellow, { l1, l2 }), "S48") == 4, "an aspect set in Station…: the view's");
        StationKavach::Loco quiet;
        CHECK(liveViewOf(lay, st, state, { quiet }).locos.isEmpty(), "a loco not heard yet: not drawn");
    }

    // ---- on the drawing ----
    {
        StationLayoutWindow w;
        w.resize(1400, 900);
        w.setStation(lay);
        w.showLiveStation(st, state, { l1, l2 }, QStringLiteral("Packet Maker"));
        StationLayoutCanvas *c = w.canvas();
        c->grab();
        CHECK(w.liveState()->isVisibleTo(&w)
                  && w.liveState()->text() == QStringLiteral("Live from Packet Maker, station 527: loco 1 at 160.661 km, MA 1236 m, "
                                                             "SoS Rear-end; loco 2 at 161.246 km, MA 1670 m"),
              "under the toolbar, in words");
        const QRectF r1 = c->liveLocoRect(0), r2 = c->liveLocoRect(1);
        CHECK(!r1.isNull() && !r2.isNull() && qAbs(r1.center().x() - c->xOf(160661)) < 0.5 && qAbs(r2.center().x() - c->xOf(161246)) < 0.5,
              "each loco drawn at its ABS_LOCO_LOC");
        const QPolygonF ma = c->liveMaPath(0);
        CHECK(ma.size() >= 2 && ma.first() == r1.center() && qAbs(ma.last().x() - c->xOf(160896)) < 0.5,
              "loco 1's MA from it to 160896 m");
        CHECK(ma.last().x() < c->xOf(161246 - 250), "ending before loco 2's rear: the cut is seen");
        CHECK(hitSays(c, QStringLiteral("End of loco 1's MA: MA 1236 m, at 160.896 km"))
                  && hitSays(c, QStringLiteral("Loco 2 at 161.246 km, 250 m long")) && hitSays(c, QStringLiteral("KAVACH, live: Red")),
              "the tooltips: the MA end, the loco, the live aspect");
        w.clearLiveStation();
        CHECK(c->liveView().isEmpty() && !w.liveState()->isVisibleTo(&w) && c->liveMaPath(0).isEmpty(), "cleared: none of it");
    }

    // ---- Packet Maker: Station… ▸ Show on the drawing, loco 1 past A904 at Red ----
    {
        const QStringList lines = linesOf(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap");
        QTemporaryDir tmp;
        StationLayout::Layout spad = s30;
        StationLayout::Layout fixture;
        StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &fixture);
        StationKavach::Station file;
        StationKavach::fromJson(fixture.kavach, &file);
        for (StationKavach::Signal &g : file.signalList) if (g.name == QLatin1String("A904")) g.aspect = 1;
        spad.kavach = StationKavach::toJson(file);
        const QString layPath = tmp.filePath(QStringLiteral("s30_527.json"));
        CHECK(StationLayout::save(layPath, spad), "Section_30 with station 527's KAVACH data (A904 at Red) saved");
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
        CHECK(panel && panel->drawingButton() && panel->drawingButton()->text() == QStringLiteral("Show on the drawing"),
              "Station… has Show on the drawing");
        if (!panel) return;
        panel->drawingButton()->click();
        StationLayoutWindow *w = dlg.stationDrawing();
        CHECK(w && w->isVisible() && w->station().tags.size() == spad.tags.size(), "it opens the layout answered from");
        if (!w) return;
        CHECK(aspectOn(w->canvas()->liveView(), "A904") == 1 && w->canvas()->liveView().locos.isEmpty()
                  && w->liveState()->text().endsWith(QStringLiteral("no loco heard yet")),
              "live: A904 at Red, no loco heard yet");

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
        receive(rx);   // the AAP
        dlg.observe(entry(lineOf(lines, "@lsrp_", 50548)));
        receive(rx);   // the SLRP
        const StationLayoutCanvas::LiveView lv = w->canvas()->liveView();
        CHECK(lv.locos.size() == 1 && lv.locos[0].id == 1 && lv.locos[0].absLoc == 163821 && lv.locos[0].maText == QStringLiteral("MA 0")
                  && lv.locos[0].sos == QStringLiteral("SoS SPAD") && lv.locos[0].maEnd < 0,
              "LSRP 50548 answered: loco 1 at 163821 m, MA 0, SoS SPAD (no MA drawn)");
        w->canvas()->grab();
        CHECK(!w->canvas()->liveLocoRect(0).isNull() && hitSays(w->canvas(), QStringLiteral("SoS SPAD")), "drawn, the SoS said");

        auto *sigs = panel->findChild<QTableWidget *>(QStringLiteral("skSignals"));
        QComboBox *a904 = nullptr;
        for (int i = 0; sigs && i < sigs->rowCount(); ++i)
            if (sigs->item(i, 0) && sigs->item(i, 0)->text() == QLatin1String("A904")) a904 = qobject_cast<QComboBox *>(sigs->cellWidget(i, 2));
        CHECK(a904 && a904->findData(4) >= 0, "A904's aspect in Station…");
        if (a904) a904->setCurrentIndex(a904->findData(4));
        CHECK(aspectOn(w->canvas()->liveView(), "A904") == 4, "set there: the drawing follows at once");
        answer->setChecked(false);
        CHECK(w->canvas()->liveView().isEmpty() && !w->liveState()->isVisibleTo(w), "answering stopped: no longer live");
    }
}
