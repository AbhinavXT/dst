#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "stationkavach.h"
#include "stationkavachpanel.h"
#include "stationlayout.h"

#include <QCheckBox>
#include <QFile>
#include <QSpinBox>

// =============================================================================
//  Session 449 — answering as station 527 did where DLConsole did otherwise
//  (Station… ▸ the two "as station 527" controls). Keep the profile into
//  the next section: the loco moving into the next section of its MA keeps
//  the profile it holds (same REF_PROF_ID, the MA alone) while it still
//  covers the new MA. The full profile N answers more after the loco
//  acknowledged it. Checked against station 527's SLRPs to loco 1 in
//  replay/2026-10-08/loco_1_1_08102026_103520.cap: SLRP 38156 at tag 18
//  (A900 Red) kept profile 2; profile 3 went in full in the SLRPs answering
//  LSRPs 38211 (the acknowledgement), 38213 and 38215.
// =============================================================================

namespace {

QStringList capture()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_103520.cap"));
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

QString find(const QStringList &lines, const char *type, qint64 fn)
{
    for (const QString &l : lines)
        if (l.startsWith(QLatin1String(type)) && rawOf(l).value(QStringLiteral("FRAME_NUM")) == fn) return l;
    return {};
}

qint64 ma(const StationKavach::Answer &a, const char *field)
{
    for (const Schema::SubEntry &x : a.subs) if (x.type == 0) return x.values.value(QLatin1String(field), -1);
    return -1;
}

} // namespace

TEST_SUITE(session449)
{
    PacketBuilder b;
    const Schema::Encoder &enc = b.encoder();
    StationLayout::Layout lay;
    StationKavach::Station s;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &lay)
              && StationKavach::fromJson(lay.kavach, &s),
          "station 527's data");
    const QStringList lines = capture();
    auto real = [&](qint64 fn) { return enc.parseBody(QStringLiteral("slrp"), CaptureDecoder::parseLine(find(lines, "@slrp_", fn)).bytes); };
    CHECK(real(38156).ok && real(38156).header.value("REF_PROF_ID") == 2 && real(38156).subs.size() == 1,
          "the real SLRP 38156: profile 2 kept, the MA alone");

    // As the run had it: A900 Red, then Yellow at 38209; S48-S43 under FS. Profile 1 came before the capture.
    auto run = [&](bool keep, int extra, QVector<StationKavach::Answer> *out) {
        StationKavach::State st = StationKavach::State::initial(s);
        st.aspect.insert(QStringLiteral("A900"), 1);
        st.authority.insert(QStringLiteral("S48-S43 DN Main"), 2);
        st.keepProfile = keep;
        st.extraFull = extra;
        StationKavach::Loco loco;
        loco.profId = 1;
        for (qint64 fn : { 38121, 38133, 38155, 38209, 38211, 38213, 38215, 38217 }) {
            if (fn == 38209) st.aspect.insert(QStringLiteral("A900"), 2);
            *out << StationKavach::answer(s, st, &loco, rawOf(find(lines, "@lsrp_", fn)), enc);
        }
    };
    QVector<StationKavach::Answer> as527, before, noExtra;
    run(true, 3, &as527);
    run(false, 0, &before);
    run(true, 0, &noExtra);
    CHECK(as527.size() == 8 && as527[0].ok && as527[0].header.value("REF_PROF_ID") == 2 && !as527[0].fullProfile,
          "tag 12 (LSRP 38121, it acknowledges 2): profile 2, the MA alone, as SLRP 38120");
    const StationKavach::Answer &at18 = as527[2];
    CHECK(at18.ok && at18.header.value("REF_PROF_ID") == 2 && !at18.fullProfile && at18.subs.size() == 1
              && ma(at18, "MA_W_R_T_SIG") == 1116 && at18.header.value("DIST_PKT_START") == 0
              && at18.note.contains(QStringLiteral("kept (it still covers the MA)")),
          "tag 18, into S43-A900, A900 Red: profile 2 kept (1650 m from S48 covers 534 + 1116), the MA alone: as SLRP 38156");
    CHECK(before[2].ok && before[2].header.value("REF_PROF_ID") == 3 && before[2].fullProfile,
          "off (as before): a new profile 3 in full");
    CHECK(as527[3].ok && as527[3].header.value("REF_PROF_ID") == 3 && as527[3].fullProfile && ma(as527[3], "MA_W_R_T_SIG") == 1956,
          "A900 Yellow (38209): 534 + 1956 m is past profile 2's end: profile 3 in full, as SLRP 38209");
    CHECK(as527[4].fullProfile && as527[5].fullProfile && as527[6].fullProfile && !as527[7].fullProfile,
          "acknowledged at 38211: in full answering 38211, 38213, 38215, then the MA alone (3 more, as 527)");
    CHECK(real(38211).subs.size() > 1 && real(38213).subs.size() > 1 && real(38215).subs.size() > 1, "the real 38211, 38213, 38215 in full");
    CHECK(noExtra[3].fullProfile && !noExtra[4].fullProfile, "0 more: the MA alone once acknowledged");

    // ---- Station… ----
    StationKavachPanel panel({});
    StationKavach::State ps = StationKavach::State::initial(s);
    panel.setStation(s, &ps);
    auto *keep = panel.findChild<QCheckBox *>(QStringLiteral("skKeepProfile"));
    auto *extra = panel.findChild<QSpinBox *>(QStringLiteral("skExtraFull"));
    CHECK(keep && extra && !keep->isChecked() && extra->value() == 0, "Station…: keep the profile (off), full after the ack (0)");
    if (!(keep && extra)) return;
    keep->setChecked(true);
    extra->setValue(3);
    CHECK(ps.keepProfile && ps.extraFull == 3, "set: as station 527");
}
