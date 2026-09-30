#include "testutil.h"

#include "capturedecoder.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

// =============================================================================
//  A profile is issued once and held; a frame is not a profile.
//
//  The station sends the look-ahead profile when it changes and then sends
//  movement authorities against it. In the captures under replay/, 3322 of
//  3340 SLRP frames carry NOTHING but the MA sub-packet — the full profile
//  arrives eighteen times in the whole session.
//
//  The recorded-session view decoded whichever single SLRP frame was latest,
//  so the look-ahead lanes were empty for 99% of the timeline and appeared
//  only for the instant a profile-bearing frame was current. That is the bug
//  this suite exists for.
//
//  A loco holds what it was issued until REF_PROF_ID changes to a different
//  NON-ZERO value. Zero means "route ahead not known", and the firmware keeps
//  the current profile deliberately in that case.
// =============================================================================

namespace {

QVector<CaptureLine> slrpFrames(int limit)
{
    QVector<CaptureLine> out;
    QDir dir(QStringLiteral(DL_SRC_DIR "/replay"));
    const QStringList caps = dir.entryList({ QStringLiteral("*.cap") }, QDir::Files, QDir::Name);
    for (const QString &c : caps) {
        QFile f(dir.filePath(c));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { continue; }
        QTextStream ts(&f);
        while (!ts.atEnd() && out.size() < limit) {
            const QString ln = ts.readLine();
            if (!ln.startsWith(QLatin1String("@slrp"))) { continue; }
            const CaptureLine cl = CaptureDecoder::parseLine(ln);
            if (cl.valid) { out.push_back(cl); }
        }
        if (out.size() >= limit) { break; }
    }
    return out;
}

}  // namespace

TEST_SUITE(slrpcarry)
{
    const QVector<CaptureLine> frames = slrpFrames(400);
    CHECK(frames.size() > 50, "captured SLRP frames are available to test against");
    if (frames.size() < 50) { return; }

    // ---- the premise: almost every frame is MA-only ------------------------
    int maOnly = 0;
    int withLanes = 0;
    for (const CaptureLine &c : frames) {
        const SlrpProfile p = CaptureDecoder::profileOf(c);
        if (!p.valid) { continue; }
        if (p.carriesLanes()) { ++withLanes; } else if (p.haveMA) { ++maOnly; }
    }
    CHECK(maOnly > withLanes * 10,
          "the overwhelming majority of SLRP frames carry only a movement "
          "authority — which is why one frame cannot be the profile");
    CHECK(withLanes >= 1, "and at least one issues the lanes");

    // ---- carrying forward --------------------------------------------------
    SlrpProfile issued;
    int issuedAt = -1;
    for (int i = 0; i < frames.size(); ++i) {
        const SlrpProfile p = CaptureDecoder::profileOf(frames.at(i));
        if (p.carriesLanes()) { issued = p; issuedAt = i; break; }
    }
    CHECK(issuedAt >= 0, "found the frame that issued the profile");
    if (issuedAt < 0) { return; }

    CHECK(!issued.ssp.isEmpty() || !issued.grad.isEmpty(),
          "and it carries speed or gradient lanes");

    // The next frame after it is MA-only, and that is exactly the case that
    // used to blank the display.
    if (issuedAt + 1 < frames.size()) {
        const SlrpProfile next = CaptureDecoder::profileOf(frames.at(issuedAt + 1));
        CHECK(!next.carriesLanes(), "the following frame is movement authority only");

        const SlrpProfile shown = CaptureDecoder::carryProfile(issued, next);
        CHECK(shown.ssp.size() == issued.ssp.size(),
              "carrying forward keeps the speed lane");
        CHECK(shown.grad.size() == issued.grad.size(), "and the gradient lane");
        CHECK(shown.cond.size() == issued.cond.size(), "and the track conditions");
        CHECK(shown.tags.size() == issued.tags.size(), "and the tag list");
        CHECK(shown.haveMA == next.haveMA && shown.maWrtSig == next.maWrtSig,
              "while the movement authority comes from the NEW frame, since "
              "that is the field which changes every packet");
        CHECK(shown.refRfid == issued.refRfid && shown.distPktStart == issued.distPktStart,
              "and the lanes keep the reference they were issued against, so "
              "the look-ahead does not slide every time a balise is passed");
    }

    // ---- a different profile discards the old one --------------------------
    {
        SlrpProfile older = issued;
        older.refProfId = 1;
        SlrpProfile newer;
        newer.valid = true;
        newer.haveMA = true;
        newer.refProfId = 2;                 // a different NON-ZERO id

        const SlrpProfile shown = CaptureDecoder::carryProfile(older, newer);
        CHECK(shown.ssp.isEmpty() && shown.grad.isEmpty(),
              "a new REF_PROF_ID means the old profile was discarded, and "
              "showing it would be showing a route the loco is no longer on");
    }

    // ---- REF_PROF_ID zero keeps what is held -------------------------------
    //
    // Zero is not a profile id: it is the station saying it does not know the
    // route ahead. The firmware keeps the current profile, deliberately.
    {
        SlrpProfile older = issued;
        older.refProfId = 3;
        SlrpProfile newer;
        newer.valid = true;
        newer.haveMA = true;
        newer.refProfId = 0;

        const SlrpProfile shown = CaptureDecoder::carryProfile(older, newer);
        CHECK(shown.ssp.size() == older.ssp.size(),
              "an unknown route ahead does not blank the profile in force");
        CHECK(shown.refProfId == 3,
              "and the lanes keep the id they were issued under, so the next "
              "real issue is not mistaken for a continuation of this one");
    }

    // ---- a frame that reissues lanes replaces them -------------------------
    {
        SlrpProfile older = issued;
        SlrpProfile newer = issued;          // carries lanes of its own
        newer.refProfId = older.refProfId;
        newer.ssp.clear();
        newer.ssp.push_back({ 500, 60, 60, 60, false });

        const SlrpProfile shown = CaptureDecoder::carryProfile(older, newer);
        CHECK(shown.ssp.size() == 1 && shown.ssp.first().d == 500,
              "a frame that issues its own lanes replaces the held ones "
              "rather than being merged with them");
    }

    // ---- the payload ends at PKT_LENGTH - 8 --------------------------------
    //
    // CRC and MAC. Three different boundaries were in use here — a decode
    // limit of n-4, a loop bound of n-8 and a guard of n-4 — which let a
    // sub-packet read into the MAC.
    {
        const CaptureLine &c = frames.first();
        SlrpProfile p = CaptureDecoder::profileOf(c);
        CHECK(p.valid, "a captured frame decodes");
        CHECK(p.refProfId >= 0, "and reports the profile id it belongs to");

        // Trailing padding must not be mistaken for another sub-packet.
        CaptureLine padded = c;
        padded.bytes.append(QByteArray(6, '\0'));
        const SlrpProfile q = CaptureDecoder::profileOf(padded);
        CHECK(q.ssp.size() == p.ssp.size() && q.grad.size() == p.grad.size()
                  && q.cond.size() == p.cond.size() && q.tags.size() == p.tags.size(),
              "padding after the frame decodes to the same profile, because "
              "the walk is bounded by PKT_LENGTH and not by what arrived");
    }
}
