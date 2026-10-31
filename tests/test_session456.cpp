#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "stationkavach.h"
#include "stationlayout.h"

#include <QFile>

// =============================================================================
//  Session 456 — a firm's track profile table read onto the Stationary KAVACH
//  routes (scripts/kavach_sheets.py): SSP, gradient, LC gates, turnouts,
//  track conditions. The station: HBL's Lingampalli, station 527, from
//  committed extracts of its tag data (pages 1-3), Table of Control (pages 1
//  and 3) and track profile (pages 1-2):
//  schema/fixtures/kavach_sheets/LPI_routes/station_527.json.
//    S50-S48   (U,110,1670); F 1 in 209 1245 m, Level 320 m, R 1 in 260 105 m;
//              fouling marks 780 and 905 m from S50
//    S48-S43   (U,80,534); R 1 in 221 534 m
//  Station 527 itself answered loco 1 on 26 Jun 2026
//  (replay/loco_1_1_26062026_162418.cap): SLRP 59357 with S50-S48 alone,
//  59544 with S48-S43 after it. Their speed and gradient sub-packets are
//  exactly what DLConsole now sends from the sheets: the average gradient's
//  1000/N truncated (209 -> 4, 260 -> 3, 221 -> 4), Level downhill 0. Their
//  track conditions are not: the station sent "Reversing" over the whole
//  route where the sheet gives fouling marks; that is said, not matched.
// =============================================================================

namespace {

const char *kLpi = DL_SRC_DIR "/schema/fixtures/kavach_sheets/LPI_routes/station_527.json";

int routeNamed(const StationKavach::Station &s, const QString &name)
{
    for (int i = 0; i < s.routes.size(); ++i) if (s.routes[i].name == name) return i;
    return -1;
}

QString realSlrp(qint64 frame)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_26062026_162418.cap"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    for (const QString &l : QString::fromLatin1(f.readAll()).split(QLatin1Char('\n'))) {
        if (!l.startsWith(QLatin1String("@slrp_"))) continue;
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(CaptureDecoder::parseLine(l.trimmed()), nullptr, 0, &raw);
        if (raw.value(QStringLiteral("FRAME_NUM")) == frame) return l.trimmed();
    }
    return {};
}

// The rows of sub-packet `type`'s repeat `name`, as "field=value ..." lines
// over `fields`.
QStringList rowsOf(const QVector<Schema::SubEntry> &subs, int type, const char *name, const QStringList &fields)
{
    QStringList out;
    for (const Schema::SubEntry &s : subs) {
        if (s.type != type) continue;
        for (const QHash<QString, qint64> &r : s.repeats.value(QLatin1String(name))) {
            QStringList x;
            for (const QString &f : fields) x << QStringLiteral("%1=%2").arg(f).arg(r.value(f));
            out << x.join(QLatin1Char(' '));
        }
    }
    return out;
}

} // namespace

TEST_SUITE(session456)
{
    StationLayout::Layout lay;
    QString err;
    CHECK(StationLayout::load(QString::fromLatin1(kLpi), &lay, &err), "LPI's station_527.json opens");
    StationKavach::Station s;
    CHECK(StationKavach::fromJson(lay.kavach, &s, &err), "its KAVACH data reads");
    const int a = routeNamed(s, QStringLiteral("S50-S48 Down Main"));
    const int b = routeNamed(s, QStringLiteral("S48-S43 Down Main"));
    CHECK(a >= 0 && b >= 0, "the routes S50-S48 and S48-S43");
    if (a < 0 || b < 0) return;
    const StationKavach::Route &ra = s.routes[a], &rb = s.routes[b];
    CHECK(ra.length() == 1670 && ra.ssp.size() == 1 && ra.ssp[0].value("d") == 1670 && ra.ssp[0].value("u") == 22,
          "S50-S48: 1670 m at 110 km/h (u 22)");
    CHECK(ra.gradient.size() == 3 && ra.gradient[0].value("value") == 4 && ra.gradient[0].value("dir") == 0
              && ra.gradient[1].value("value") == 0 && ra.gradient[2].value("dir") == 1 && ra.gradient[2].value("value") == 3,
          "its gradient: F 1 in 209 -> downhill 4, Level -> 0, R 1 in 260 -> uphill 3");
    CHECK(ra.trackConditions.size() == 2 && ra.trackConditions[0].value("type") == 8 && ra.trackConditions[0].value("sd") == 780
              && ra.trackConditions[1].value("sd") == 905,
          "fouling marks (type 8) 780 and 905 m from S50");
    CHECK(rb.ssp.size() == 1 && rb.ssp[0].value("u") == 16 && rb.gradient.size() == 1 && rb.gradient[0].value("value") == 4,
          "S48-S43: 534 m at 80 km/h; R 1 in 221 -> uphill 4");
    CHECK(StationKavach::checks(s).filter(QStringLiteral("covers")).isEmpty(), "every SSP and gradient covers its route");
    const int c = routeNamed(s, QStringLiteral("S50A-S48 Down Main"));
    CHECK(c >= 0 && s.routes[c].authority == 1 && s.routes[c].osSpeed == 3 && s.routes[c].ssp == ra.ssp,
          "S50A-S48, calling-on: OS authority at 15 km/h (speed6 3, as 455 should have written it), S50-S48's SSP");

    // ---- against station 527's own answers ----
    PacketBuilder pb;
    const Schema::Encoder &enc = pb.encoder();
    const QStringList speedF{ "d", "class", "u" }, gradF{ "d", "dir", "value" };
    StationKavach::State st = StationKavach::State::initial(s);
    st.aspect.insert(QStringLiteral("S48"), 1);   // Red: one section, S50-S48
    StationKavach::Loco loco;
    const QHash<QString, qint64> lsrp{ { "FRAME_NUM", 59356 }, { "SOURCE_LOCO_ID", 1 }, { "LAST_RFID_TAG", 2 },
                                       { "MOVEMENT_DIR", 1 }, { "ABS_LOCO_LOC", 159700 } };
    StationKavach::Answer ans = StationKavach::answer(s, st, &loco, lsrp, enc);
    const Schema::ParsedPacket r1 = enc.parseBody(QStringLiteral("slrp"), CaptureDecoder::parseLine(realSlrp(59357)).bytes);
    CHECK(ans.ok && r1.ok, QByteArray("our answer and the real SLRP 59357 ") + ans.why.toUtf8() + r1.error.toUtf8());
    CHECK(rowsOf(ans.subs, 1, "speed", speedF) == rowsOf(r1.subs, 1, "speed", speedF)
              && rowsOf(ans.subs, 2, "grad", gradF) == rowsOf(r1.subs, 2, "grad", gradF)
              && rowsOf(r1.subs, 2, "grad", gradF).size() == 3,
          "S48 Red: speed and gradient as station 527 sent them in 59357");
    st.aspect.insert(QStringLiteral("S48"), 2);   // Yellow: two sections
    StationKavach::Loco loco2;
    ans = StationKavach::answer(s, st, &loco2, lsrp, enc);
    const Schema::ParsedPacket r2 = enc.parseBody(QStringLiteral("slrp"), CaptureDecoder::parseLine(realSlrp(59544)).bytes);
    CHECK(ans.ok && r2.ok && rowsOf(ans.subs, 1, "speed", speedF) == rowsOf(r2.subs, 1, "speed", speedF)
              && rowsOf(ans.subs, 2, "grad", gradF) == rowsOf(r2.subs, 2, "grad", gradF)
              && rowsOf(r2.subs, 2, "grad", gradF).size() == 4,
          "S48 Yellow: S50-S48 then S48-S43, speed and gradient as in 59544");
    const QStringList tcF{ "type", "sd", "ln" };
    CHECK(rowsOf(r1.subs, 6, "tc", tcF) == QStringList{ QStringLiteral("type=7 sd=0 ln=1670") }
              && rowsOf(ans.subs, 6, "tc", tcF).size() >= 2,
          "track conditions differ: station 527 sent Reversing over the route; the sheet gives fouling marks");
}
