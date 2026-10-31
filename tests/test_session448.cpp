#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "logmodel.h"
#include "packetmakerdialog.h"
#include "packetpreset.h"
#include "sessionkeystore.h"
#include "stationinterlock.h"
#include "stationkavach.h"
#include "stationkavachpanel.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "statusline.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include <algorithm>

// =============================================================================
//  Session 448 — the train-length times. When a loco's front and then its
//  rear cross a track-circuit joint of its line, the times (interpolated
//  between its reports either side) go out in its next SLRPs: Start
//  (TRN_LEN_INFO_TYPE 0), then End (1), REF_FRAME_NUM_TLM + REF_OFFSET_INT_TLM
//  (10 ms). Off by default (station 527 never sent them).
//  Station 527's KAVACH data (the session 422 fixture) given the circuits
//  of station_527.xml that lie on its routes (S43-A900: 43T … 43CT;
//  A900-A904: 1629T … 1629BT) and the June route S50-S48 over DMT; the real
//  ARPs and LSRPs of loco 1 (DN) and loco 2 (UP) in
//  replay/loco_{1,2}_1_27062026_170217.cap, in frame order.
// =============================================================================

namespace {

QStringList linesOf(const QString &path)
{
    QFile f(path);
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

struct In { qint64 fn; int loco; QString line; QHash<QString, qint64> raw; };

QVector<In> heard(const QStringList &one, const QStringList &two)
{
    QVector<In> ins;
    for (int k = 0; k < 2; ++k)
        for (const QString &l : (k ? two : one))
            if (l.startsWith(QLatin1String("@arp_")) || l.startsWith(QLatin1String("@lsrp_"))) {
                const QHash<QString, qint64> r = rawOf(l);
                ins.append({ r.value(QStringLiteral("FRAME_NUM")), k + 1, l, r });
            }
    std::stable_sort(ins.begin(), ins.end(), [](const In &x, const In &y) { return x.fn != y.fn ? x.fn < y.fn : x.loco < y.loco; });
    return ins;
}

// Station 527's KAVACH data with station_527.xml's circuits on its routes, S50-S48 over DMT,
// and two routes from S48 (main: 19 Normal; loop: 19 and P18 Reverse) for the locking.
StationKavach::Station station527()
{
    StationLayout::Layout fixture;
    StationKavach::Station s;
    StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &fixture);
    StationKavach::fromJson(fixture.kavach, &s);
    for (StationKavach::Route &r : s.routes) {
        if (r.entry == QLatin1String("S43")) r.trackCircuits = QStringList{ "43T", "43AT", "43BT", "43CT" };
        if (r.entry == QLatin1String("A900")) r.trackCircuits = QStringList{ "1629T", "1629AT", "1629BT" };
    }
    StationKavach::Signal s50;
    s50.name = QStringLiteral("S50");
    s50.footTag = 2;
    s50.aspect = 1;
    s.signalList.prepend(s50);
    StationKavach::Route r;
    r.name = QStringLiteral("S50-S48 DN");
    r.entry = QStringLiteral("S50");
    r.exit = QStringLiteral("S48");
    r.tags << StationKavach::RouteTag{ 0, 2, 0 } << StationKavach::RouteTag{ 1650, 12, 0 };
    r.trackCircuits = QStringList{ QStringLiteral("DMT") };
    s.routes << r;
    return s;
}

} // namespace

namespace {

qint64 maField(const StationKavach::Answer &a, const char *field)
{
    for (const Schema::SubEntry &x : a.subs) if (x.type == 0) return x.values.value(QLatin1String(field), -1);
    return -1;
}

// Loco 1's frames (FRAME_NUM, ABS_LOCO_LOC of the frame's first report), in order.
QVector<QPair<qint64, qint64>> track1(const QVector<In> &ins)
{
    QVector<QPair<qint64, qint64>> out;
    for (const In &in : ins) {
        if (in.loco != 1 || in.raw.value(QStringLiteral("ABS_LOCO_LOC")) <= 0) continue;
        const qint64 f = in.raw.value(QStringLiteral("FRAME_NUM")), x = in.raw.value(QStringLiteral("ABS_LOCO_LOC"));
        if (out.isEmpty() || out.last().first != f) out.append({ f, x });
    }
    return out;
}

// When the point x was passed (front: shift 0; rear: shift = train length), in frames.
double passed(const QVector<QPair<qint64, qint64>> &t, qint64 x, qint64 shift)
{
    for (int i = 1; i < t.size(); ++i) {
        const qint64 a = t[i - 1].second - shift, b = t[i].second - shift;
        if (a < x && b >= x) return t[i - 1].first + double(x - a) / double(b - a) * double(t[i].first - t[i - 1].first);
    }
    return -1;
}

double frontAt(const QVector<QPair<qint64, qint64>> &t, double when)
{
    for (int i = 1; i < t.size(); ++i)
        if (t[i - 1].first <= when && t[i].first >= when)
            return t[i - 1].second + (when - t[i - 1].first) / double(t[i].first - t[i - 1].first) * double(t[i].second - t[i - 1].second);
    return -1;
}

} // namespace

TEST_SUITE(session448)
{
    PacketBuilder b;
    const Schema::Encoder &enc = b.encoder();
    StationLayout::Layout l527;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml"), &l527), "station_527.xml");
    const StationInterlock::Track track = StationInterlock::trackOf(l527);
    StationKavach::Station s = station527();
    const QVector<In> ins = heard(linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap")),
                                  linesOf(QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_27062026_170217.cap")));
    const QVector<QPair<qint64, qint64>> t1 = track1(ins);
    CHECK(t1.size() > 20 && t1.first().second < 161330, "loco 1's reports, from before DMT's far joint (161330 m)");

    int frame = 0, offset = 0;
    StationInterlock::frameAndOffset(61330.257, &frame, &offset);
    CHECK(frame == 61330 && offset == 26, "frame 61330.257: frame 61330 + 26 x 10 ms");

    // ---- loco 1's real reports: the joint at 161330 m (DMT's end) ----
    StationKavach::Loco loco;
    QStringList found;
    StationKavach::Loco atStart, atEnd;
    for (const In &in : ins) {
        if (in.loco != 1) continue;
        StationKavach::note(&loco, in.raw);
        const QString f = StationInterlock::measure(track, &loco);
        if (f.isEmpty()) continue;
        found << f;
        if (found.size() == 1) atStart = loco;
        if (found.size() == 2) { atEnd = loco; break; }
    }
    CHECK(found.size() == 2 && atStart.tlmJoint == 161330, QByteArray("the first joint ahead of it: 161330 m (") + found.join(" | ").toUtf8() + ")");
    const double start = passed(t1, 161330, 0), end = passed(t1, 161330, 250);
    int sf = 0, so = 0, ef = 0, eo = 0;
    StationInterlock::frameAndOffset(start, &sf, &so);
    StationInterlock::frameAndOffset(end, &ef, &eo);
    CHECK(start > 0 && atStart.tlmStartFrame == sf && atStart.tlmStartOffset == so,
          QByteArray("Start: its front over 161330 m at frame ") + QByteArray::number(sf) + " + " + QByteArray::number(so * 10) + " ms");
    CHECK(end > start && atEnd.tlmEndFrame == ef && atEnd.tlmEndOffset == eo,
          QByteArray("End: its rear (250 m back) over it at frame ") + QByteArray::number(ef) + " + " + QByteArray::number(eo * 10) + " ms");
    const double run = frontAt(t1, ef + eo / 100.0) - frontAt(t1, sf + so / 100.0);
    CHECK(run > 247 && run < 253, QByteArray("the loco runs ") + QByteArray::number(run, 'f', 1) + " m between them: its 250 m train");

    // ---- sent: Start, then End, one SLRP each; off: not at all ----
    QHash<QString, qint64> lsrp;
    for (const In &in : ins)
        if (in.loco == 1 && in.line.startsWith(QLatin1String("@lsrp_")) && in.raw.value(QStringLiteral("LAST_RFID_TAG")) == 14) { lsrp = in.raw; break; }
    CHECK(!lsrp.isEmpty(), "loco 1's real LSRP at tag 14");
    StationKavach::State st = StationKavach::State::initial(s);
    StationKavach::Loco off = atStart;
    StationKavach::Answer a = StationKavach::answer(s, st, &off, lsrp, enc);
    CHECK(a.ok && maField(a, "TRN_LEN_INFO_STS") == 0, "Send train-length times off: TRN_LEN_INFO_STS 0 (as station 527)");
    st.trainLength = true;
    StationKavach::Loco on = atStart;
    on.frame = lsrp.value(QStringLiteral("FRAME_NUM"));
    a = StationKavach::answer(s, st, &on, lsrp, enc);
    CHECK(a.ok && maField(a, "TRN_LEN_INFO_STS") == 1 && maField(a, "TRN_LEN_INFO_TYPE") == 0 && maField(a, "REF_FRAME_NUM_TLM") == sf
              && maField(a, "REF_OFFSET_INT_TLM") == so && a.note.contains(QStringLiteral("train length Start at the joint at 161330 m")),
          "on: Start sent (TYPE 0, its frame and offset)");
    QString err;
    const Schema::ParsedPacket pp = enc.parseBody(QStringLiteral("slrp"), enc.encodeBody(QStringLiteral("slrp"), a.header, a.subs, &err));
    CHECK(pp.ok && pp.subs.value(0).values.value(QStringLiteral("REF_FRAME_NUM_TLM")) == sf, QByteArray("encodes, reads back ") + err.toUtf8());
    a = StationKavach::answer(s, st, &on, lsrp, enc);
    CHECK(a.ok && maField(a, "TRN_LEN_INFO_STS") == 0, "the next SLRP: nothing (no End yet)");
    on.tlmEndFrame = atEnd.tlmEndFrame;
    on.tlmEndOffset = atEnd.tlmEndOffset;
    a = StationKavach::answer(s, st, &on, lsrp, enc);
    CHECK(a.ok && maField(a, "TRN_LEN_INFO_STS") == 1 && maField(a, "TRN_LEN_INFO_TYPE") == 1 && maField(a, "REF_FRAME_NUM_TLM") == ef
              && maField(a, "REF_OFFSET_INT_TLM") == eo,
          "End measured: sent (TYPE 1)");
    CHECK(on.tlmJoint < 0 && on.tlmStartFrame < 0 && on.tlmEndFrame < 0, "then the next joint is measured afresh");

    // ---- Station… ----
    StationKavachPanel panel({});
    StationKavach::State ps = StationKavach::State::initial(s);
    panel.setStation(s, &ps);
    auto *box = panel.findChild<QCheckBox *>(QStringLiteral("skTrainLength"));
    CHECK(box && !box->isChecked(), "Station…: Send train-length times, unticked");
    if (box) box->setChecked(true);
    CHECK(ps.trainLength, "ticked: sent");
}
