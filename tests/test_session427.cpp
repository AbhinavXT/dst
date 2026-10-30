#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "stationkavach.h"
#include "stationlayout.h"

#include <QFile>

#include <algorithm>

// =============================================================================
//  Session 427 — the Stationary KAVACH and a loco ahead (Rear-end). A loco
//  ahead on the MA's routes, the same way, cuts the MA to its ABS_LOCO_LOC
//  less its TRAIN_LENGTH less 100 m; a loco reporting EMERGENCY_STATUS 5
//  is answered DEST_LOCO_SOS 5 (Abhinav: "echo + cut"). Checked against
//  station 527's answers to loco 1 standing behind loco 2 in section
//  S50-S48 in replay/loco_{1,2}_1_27062026_151052.cap: both captures' LSRPs
//  through the station in frame order, loco 1's 11 answers the real ones
//  field for field (MAC aside: no session keys in those captures).
//  S50-S48 (June's data) is built from that real SLRP's own profile; the
//  tag locations are the built-in Section_30.xlsx's.
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

// `maOnly`: the header and the MA sub-packet alone, both sides (station 527
// sent the profile again after the loco acknowledged it; DLConsole does not,
// session 422).
bool sameAsReal(const Schema::Encoder &enc, StationKavach::Answer a, const QString &line, QString *why = nullptr, bool maOnly = false)
{
    Schema::ParsedPacket pp = slrpOf(enc, line);
    if (!pp.ok) { if (why) *why = pp.error; return false; }
    if (maOnly) {
        auto keepMa = [](QVector<Schema::SubEntry> &v) {
            v.erase(std::remove_if(v.begin(), v.end(), [](const Schema::SubEntry &x) { return x.type != 0; }), v.end());
        };
        keepMa(pp.subs);
        keepMa(a.subs);
        pp.header.remove(QStringLiteral("PKT_LENGTH"));
    }
    pp.header.insert(QStringLiteral("FRAME_NUM"), a.header.value(QStringLiteral("FRAME_NUM")));
    QHash<QString, qint64> ours = a.header;
    if (pp.header.contains(QStringLiteral("PKT_LENGTH"))) ours.insert(QStringLiteral("PKT_LENGTH"), pp.header.value(QStringLiteral("PKT_LENGTH")));
    QString e1, e2;
    const QByteArray real = enc.encodeBody(QStringLiteral("slrp"), pp.header, pp.subs, &e1);
    const QByteArray mine = enc.encodeBody(QStringLiteral("slrp"), ours, a.subs, &e2);
    if (why) *why = QStringLiteral("real %1 | ours %2").arg(QString::fromLatin1(real.toHex(' ')), QString::fromLatin1(mine.toHex(' ')));
    return !real.isEmpty() && real == mine;
}

qint64 maField(const StationKavach::Answer &a, const char *field)
{
    for (const Schema::SubEntry &s : a.subs) if (s.type == 0) return s.values.value(QLatin1String(field), -1);
    return -1;
}

// Station 527 as it was in June: the fixture's, plus S50 and route S50-S48
// made from the real SLRP's own sub-packets.
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
            const int i = s.signalIndex(QStringLiteral("S48"));
            if (i >= 0) s.signalList[i].sigInfo = se.values.value(QStringLiteral("CUR_SIG_INFO"));
        }
        if (se.type == 1) r.ssp = rows("speed");
        if (se.type == 2) r.gradient = rows("grad");
        if (se.type == 6) r.trackConditions = rows("tc");
        if (se.type == 5) {
            for (const StationKavach::Row &t : se.repeats.value(QStringLiteral("tag")))
                r.tags << StationKavach::RouteTag{ int(t.value(QStringLiteral("dist"))), int(t.value(QStringLiteral("tag"))),
                                                   int(t.value(QStringLiteral("dup_dir"))) };
            r.distDupTag = int(se.values.value(QStringLiteral("DIST_DUP_TAG")));
            r.tin = int(se.values.value(QStringLiteral("TIN")));
        }
    }
    StationKavach::Signal s50;
    s50.name = QStringLiteral("S50");
    s50.footTag = 2;
    s.signalList.prepend(s50);
    s.routes.prepend(r);
    StationLayout::Layout s30;
    StationLayout::load(QStringLiteral(":/station_layouts/Section_30.xlsx"), &s30);
    s.tagLocs = StationLayout::tagLocationsByNumber(s30);
    return s;
}

} // namespace

TEST_SUITE(session427)
{
    PacketBuilder b;
    const Schema::Encoder &enc = b.encoder();
    const QStringList one = linesOf(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap");
    const QStringList two = linesOf(DL_SRC_DIR "/replay/loco_2_1_27062026_151052.cap");
    const QString first = slrpLine(enc, one, 54791);
    CHECK(!first.isEmpty(), "station 527's SLRP 54791 to loco 1 is there");
    StationKavach::Station st = juneStation(enc, first);
    CHECK(st.routes.size() == 4 && st.routes[0].length() == 1670 && st.routes[0].tags.size() == 6
              && st.routes[0].tags.last().tag == 12 && st.routes[0].authority == 1,
          "June's S50-S48 from that SLRP: 6 tags, 2 to 12 (S48's foot), 1670 m, OS");
    StationKavach::State state = StationKavach::State::initial(st);
    state.aspect.insert(QStringLiteral("S48"), 1);   // as the real MA says: S48 Red

    // ---- both locos' LSRPs through the station, in frame order ----
    struct In { qint64 fn; int loco; QHash<QString, qint64> lsrp; };
    QVector<In> ins;
    for (const QString &l : one) if (l.startsWith(QLatin1String("@lsrp_"))) { auto r = rawOf(l); ins.append({ r.value(QStringLiteral("FRAME_NUM")), 1, r }); }
    for (const QString &l : two) if (l.startsWith(QLatin1String("@lsrp_"))) { auto r = rawOf(l); ins.append({ r.value(QStringLiteral("FRAME_NUM")), 2, r }); }
    // A frame's LSRPs: loco 1's answered before loco 2's of the same frame is heard (as the real MAs show).
    std::stable_sort(ins.begin(), ins.end(), [](const In &x, const In &y) { return x.fn != y.fn ? x.fn < y.fn : x.loco < y.loco; });
    QHash<int, StationKavach::Loco> locos;
    int pairs = 0, same = 0, sos = 0, whole = 0;
    QString firstWhy, firstNote;
    for (const In &in : ins) {
        QList<StationKavach::Loco> others;
        for (auto it = locos.cbegin(); it != locos.cend(); ++it) if (it.key() != in.loco) others << it.value();
        const StationKavach::Answer a = StationKavach::answer(st, state, &locos[in.loco], in.lsrp, enc, others);
        if (in.loco != 1) continue;
        QString real = slrpLine(enc, one, in.fn);
        if (real.isEmpty()) real = slrpLine(enc, one, in.fn + 1);   // answered a frame later
        if (real.isEmpty()) continue;
        ++pairs;
        QString why;
        if (a.fullProfile && sameAsReal(enc, a, real, &why)) ++whole;
        if (sameAsReal(enc, a, real, &why, true)) ++same;
        else if (firstWhy.isEmpty()) firstWhy = QStringLiteral("fn %1: %2").arg(in.fn).arg(why);
        if (maField(a, "DEST_LOCO_SOS") == 5) ++sos;
        if (in.fn == 54791) firstNote = a.note;
    }
    if (!firstWhy.isEmpty()) qWarning("%s", qPrintable(firstWhy));
    const QByteArray msg = QStringLiteral("loco 1's %1 answers: the header and the MA station 527's, field for field (%2 the same)")
                               .arg(pairs).arg(same).toUtf8();
    CHECK(pairs == 10 && same == 10, msg.constData());   // 54811 unanswered before the capture ends; SLRP 54808 answers 54807 a second time
    CHECK(whole == 1, "the one with the profile due (54791, the first) the real one whole: profile and all, MAC aside");
    CHECK(sos == 10, "each with SoS Rear-end: loco 1 reports EMERGENCY_STATUS 5 in each");
    CHECK(firstNote.contains(QStringLiteral("MA 1236 m"))
              && firstNote.contains(QStringLiteral("MA cut to 1236 m: loco 2 ahead, its rear at 1336 m, less 100 m"))
              && firstNote.endsWith(QStringLiteral("SoS Rear-end (the loco reports it)")),
          "the send-log note says the cut and the SoS");

    // ---- the rule's edges, from LSRP 54791 with loco 2 as last heard (54789) ----
    QHash<QString, qint64> l1, l2;
    for (const In &in : ins) {
        if (in.loco == 1 && in.fn == 54791) l1 = in.lsrp;
        if (in.loco == 2 && in.fn == 54789) l2 = in.lsrp;
    }
    CHECK(l1.value(QStringLiteral("ABS_LOCO_LOC")) == 160661 && l2.value(QStringLiteral("ABS_LOCO_LOC")) == 161246
              && l2.value(QStringLiteral("TRAIN_LENGTH")) == 250,
          "the real LSRPs: loco 1 at 160661 m, loco 2 at 161246 m with 250 m of train");
    auto run = [&](const QHash<QString, qint64> &self, const QHash<QString, qint64> &ahead, const StationKavach::Station &stn) {
        StationKavach::Loco o, me;
        StationKavach::answer(stn, state, &o, ahead, enc);
        return StationKavach::answer(stn, state, &me, self, enc, { o });
    };
    {
        StationKavach::Loco me;
        const StationKavach::Answer alone = StationKavach::answer(st, state, &me, l1, enc);
        CHECK(alone.ok && maField(alone, "MA_W_R_T_SIG") == 1670 && maField(alone, "DEST_LOCO_SOS") == 5,
              "no loco ahead: MA to S48 (1670 m); the SoS still echoed");
        QHash<QString, qint64> calm = l1;
        calm.insert(QStringLiteral("EMERGENCY_STATUS"), 0);
        const StationKavach::Answer c = run(calm, l2, st);
        CHECK(c.ok && maField(c, "MA_W_R_T_SIG") == 1236 && maField(c, "DEST_LOCO_SOS") == 0,
              "loco 1 reporting no emergency: the MA cut all the same, no SoS");
        const StationKavach::Answer behind = run(l2, l1, st);
        CHECK(behind.ok && maField(behind, "MA_W_R_T_SIG") == 1670, "loco 2, with loco 1 behind it: not cut");
        QHash<QString, qint64> other = l2;
        other.insert(QStringLiteral("MOVEMENT_DIR"), 2);
        CHECK(maField(run(l1, other, st), "MA_W_R_T_SIG") == 1670, "a loco ahead going the other way: not cut (head-on is not modelled)");
        QHash<QString, qint64> far = l2;
        far.insert(QStringLiteral("LAST_RFID_TAG"), 906);
        far.insert(QStringLiteral("ABS_LOCO_LOC"), 162100);
        CHECK(maField(run(l1, far, st), "MA_W_R_T_SIG") == 1670, "a loco on a route beyond the MA's (S43-A900): not cut");
        QHash<QString, qint64> near = l1, close = l2;
        near.insert(QStringLiteral("ABS_LOCO_LOC"), 159680);    // 20 m past S50
        close.insert(QStringLiteral("ABS_LOCO_LOC"), 159900);   // 240 m past S50: its rear less 100 m is before S50
        CHECK(maField(run(near, close, st), "MA_W_R_T_SIG") == 0, "a loco ahead whose rear less 100 m is behind the start signal: MA 0, never negative");
        StationKavach::Station noLocs = st;
        noLocs.tagLocs.clear();
        CHECK(maField(run(l1, l2, noLocs), "MA_W_R_T_SIG") == 1670, "without the layout's tag locations: not cut");
    }
}
