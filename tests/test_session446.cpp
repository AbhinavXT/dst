#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "stationkavach.h"
#include "stationkavachpanel.h"
#include "stationlayout.h"

#include <QCheckBox>
#include <QFile>

// =============================================================================
//  Session 446 — a shortened MA asked for first (Station… ▸ Ask the loco
//  before shortening its MA). A signal gone back on the loco's route: the
//  SLRP carries REQ_SHORTEN_MA 1 and NEW_MA (the shorter MA) while
//  MA_W_R_T_SIG stays the one the loco holds; the loco's NEW_MA_REPLY 1
//  (granted): the shorter MA sent; 2 (rejected): the one it holds kept, not
//  asked again. Off (the default; "New, Future Use" in Annexure-C, never
//  sent by station 527): the shorter MA at once, as session 422.
//  Station 527's data (the session 422 fixture); loco 1's real LSRP 40380
//  at tag 906 (replay/2026-10-08/loco_1_1_08102026_105200.cap).
// =============================================================================

namespace {

QHash<QString, qint64> lsrpAt(qint64 fn)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    for (const QString &l : QString::fromLatin1(f.readAll()).split(QLatin1Char('\n'))) {
        if (!l.startsWith(QLatin1String("@lsrp_"))) continue;
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(CaptureDecoder::parseLine(l.trimmed()), nullptr, 0, &raw);
        if (raw.value(QStringLiteral("FRAME_NUM")) == fn) return raw;
    }
    return {};
}

qint64 ma(const StationKavach::Answer &a, const char *field)
{
    for (const Schema::SubEntry &x : a.subs) if (x.type == 0) return x.values.value(QLatin1String(field), -1);
    return -1;
}

} // namespace

TEST_SUITE(session446)
{
    PacketBuilder b;
    const Schema::Encoder &enc = b.encoder();
    StationLayout::Layout lay;
    StationKavach::Station s;
    CHECK(StationLayout::load(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_kavach/section30_527.json"), &lay)
              && StationKavach::fromJson(lay.kavach, &s),
          "station 527's data");
    QHash<QString, qint64> lsrp = lsrpAt(40380);
    CHECK(lsrp.value(QStringLiteral("LAST_RFID_TAG")) == 906 && lsrp.value(QStringLiteral("NEW_MA_REPLY")) == 0,
          "the real LSRP 40380: tag 906, NEW_MA_REPLY 0");

    StationKavach::State st = StationKavach::State::initial(s);
    st.askShorten = true;
    StationKavach::Loco loco;
    StationKavach::Answer a = StationKavach::answer(s, st, &loco, lsrp, enc);
    CHECK(a.ok && ma(a, "MA_W_R_T_SIG") == 1956 && ma(a, "REQ_SHORTEN_MA") == 0, "A900 Yellow: MA 1956 m");

    // ---- asked, then granted ----
    st.aspect.insert(QStringLiteral("A900"), 1);
    a = StationKavach::answer(s, st, &loco, lsrp, enc);
    CHECK(a.ok && ma(a, "REQ_SHORTEN_MA") == 1 && ma(a, "NEW_MA") == 1116 && ma(a, "MA_W_R_T_SIG") == 1956,
          "A900 Red: REQ_SHORTEN_MA 1, NEW_MA 1116, MA_W_R_T_SIG still 1956");
    CHECK(a.note.contains(QStringLiteral("asks the loco to shorten its MA from 1956 to 1116 m (REQ_SHORTEN_MA)")), "said");
    QString err;
    CHECK(!enc.encodeBody(QStringLiteral("slrp"), a.header, a.subs, &err).isEmpty(), QByteArray("it encodes (NEW_MA present) ") + err.toUtf8());
    a = StationKavach::answer(s, st, &loco, lsrp, enc);
    CHECK(a.ok && ma(a, "REQ_SHORTEN_MA") == 1, "no reply yet: asked again");
    QHash<QString, qint64> granted = lsrp;
    granted.insert(QStringLiteral("NEW_MA_REPLY"), 1);
    a = StationKavach::answer(s, st, &loco, granted, enc);
    CHECK(a.ok && ma(a, "REQ_SHORTEN_MA") == 0 && ma(a, "MA_W_R_T_SIG") == 1116 && loco.maSent == 1116
              && a.note.contains(QStringLiteral("MA shortened from 1956 m: the loco granted it")),
          "NEW_MA_REPLY 1: MA 1116 m sent");

    // ---- asked, then rejected ----
    st.aspect.insert(QStringLiteral("A900"), 2);
    StationKavach::Loco l2;
    StationKavach::answer(s, st, &l2, lsrp, enc);
    st.aspect.insert(QStringLiteral("A900"), 1);
    StationKavach::answer(s, st, &l2, lsrp, enc);
    QHash<QString, qint64> rejected = lsrp;
    rejected.insert(QStringLiteral("NEW_MA_REPLY"), 2);
    a = StationKavach::answer(s, st, &l2, rejected, enc);
    CHECK(a.ok && ma(a, "REQ_SHORTEN_MA") == 0 && ma(a, "MA_W_R_T_SIG") == 1956 && l2.shortRejected
              && a.checks.contains(QStringLiteral("the loco rejected the shortened MA (1116 m): it keeps 1956 m")),
          "NEW_MA_REPLY 2: MA 1956 m kept, said");
    a = StationKavach::answer(s, st, &l2, lsrp, enc);
    CHECK(a.ok && ma(a, "REQ_SHORTEN_MA") == 0 && ma(a, "MA_W_R_T_SIG") == 1956, "the next LSRP (reply 0): not asked again, 1956 m kept");
    st.aspect.insert(QStringLiteral("A900"), 2);
    a = StationKavach::answer(s, st, &l2, lsrp, enc);
    CHECK(a.ok && ma(a, "MA_W_R_T_SIG") == 1956 && !l2.shortRejected, "A900 Yellow again: 1956 m, nothing pending");

    // ---- off: at once ----
    st.askShorten = false;
    StationKavach::Loco l3;
    StationKavach::answer(s, st, &l3, lsrp, enc);
    st.aspect.insert(QStringLiteral("A900"), 1);
    a = StationKavach::answer(s, st, &l3, lsrp, enc);
    CHECK(a.ok && ma(a, "REQ_SHORTEN_MA") == 0 && ma(a, "MA_W_R_T_SIG") == 1116, "not asking: MA 1116 m at once (session 422)");

    // ---- Station… ----
    StationKavachPanel panel({});
    StationKavach::State ps = StationKavach::State::initial(s);
    panel.setStation(s, &ps);
    auto *box = panel.findChild<QCheckBox *>(QStringLiteral("skAskShorten"));
    CHECK(box && !box->isChecked(), "Station…: Ask the loco before shortening its MA, unticked");
    if (box) box->setChecked(true);
    CHECK(ps.askShorten, "ticked: asked");
}
