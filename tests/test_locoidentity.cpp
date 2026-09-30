#include "testutil.h"

#include "locoidentity.h"
#include "rejectrules.h"

// =============================================================================
//  Which loco is "ours".
//
//  Several reject rules turn on it — an SLRP addressed to another loco is one
//  the onboard would not process — and there is no way to say so without
//  knowing which loco this console is watching.
//
//  The dangerous state is not "unknown". It is "unknown, and reporting
//  anyway": a console that reads an absent identity as zero would flag every
//  frame as addressed elsewhere, and an operator would learn to ignore the
//  feature within a minute.
// =============================================================================

namespace {
QHash<QString, qint64> loco(qint64 id)
{
    return { { QStringLiteral("SOURCE_LOCO_ID"), id } };
}
}  // namespace

TEST_SUITE(locoidentity)
{
    const QString src = QStringLiteral("21_1");

    // ---- nothing seen yet ---------------------------------------------------
    {
        LocoIdentity ident;
        CHECK(ident.idFor(src).state == LocoIdentity::State::Unknown,
              "a console that has seen nothing knows nothing");
        CHECK(ident.contextFor(src).isEmpty(),
              "and supplies no OWN_LOCO_ID — absence is what keeps the "
              "identity rules from firing, so there is no value for a rule "
              "to compare against by accident");
        CHECK(!ident.explain(src).isEmpty(),
              "and says so, because silence reads exactly like \"checked, "
              "and fine\"");
    }

    // ---- learned from what the loco sends -----------------------------------
    {
        LocoIdentity ident;
        ident.observe(src, QStringLiteral("lsrp"), loco(4711));
        const auto id = ident.idFor(src);
        CHECK(id.state == LocoIdentity::State::Known, "LSRP identifies the loco");
        CHECK(id.value == 4711, "as the one it names");
        CHECK(ident.explain(src).isEmpty(),
              "and there is then nothing to explain");
        CHECK(ident.contextFor(src).value(QStringLiteral("OWN_LOCO_ID")) == 4711,
              "and the rules are given it");

        ident.observe(src, QStringLiteral("arp"), loco(4711));
        CHECK(ident.idFor(src).state == LocoIdentity::State::Known,
              "seeing the same loco again changes nothing");
    }

    // ---- ARP RECEIVED is another loco --------------------------------------
    //
    // `arprecv` is another loco's ARP arriving at ours. Learning from it
    // would teach the wrong identity on every loco-to-loco approach, which
    // is exactly when these rules matter most.
    {
        LocoIdentity ident;
        ident.observe(src, QStringLiteral("arprecv"), loco(9999));
        CHECK(ident.idFor(src).state == LocoIdentity::State::Unknown,
              "an ARP received from another loco teaches nothing");

        ident.observe(src, QStringLiteral("arp"), loco(4711));
        CHECK(ident.idFor(src).value == 4711,
              "and does not contaminate the one the loco sends");
        ident.observe(src, QStringLiteral("arprecv"), loco(9999));
        CHECK(ident.idFor(src).state == LocoIdentity::State::Known,
              "nor turn a settled identity ambiguous afterwards");
    }

    // ---- two locos on one source -------------------------------------------
    {
        LocoIdentity ident;
        ident.observe(src, QStringLiteral("lsrp"), loco(4711));
        ident.observe(src, QStringLiteral("lsrp"), loco(4712));
        const auto id = ident.idFor(src);
        CHECK(id.state == LocoIdentity::State::Ambiguous,
              "two locos on one source is undecidable, not a coin toss");
        CHECK(id.seen == 2, "and it says how many");
        CHECK(ident.contextFor(src).isEmpty(),
              "so the rules stay quiet");
        CHECK(ident.explain(src).contains(QStringLiteral("2")),
              "and the reason given is the right one — a station-side capture "
              "or two runs merged into a tab, not an absent one");
    }

    // ---- sources are independent -------------------------------------------
    {
        LocoIdentity ident;
        ident.observe(QStringLiteral("21_1"), QStringLiteral("lsrp"), loco(1));
        ident.observe(QStringLiteral("22_1"), QStringLiteral("lsrp"), loco(2));
        CHECK(ident.idFor(QStringLiteral("21_1")).value == 1
                  && ident.idFor(QStringLiteral("22_1")).value == 2,
              "two tabs are two locos, not one ambiguity");
    }

    // ---- unassigned is not an identity --------------------------------------
    {
        LocoIdentity ident;
        ident.observe(src, QStringLiteral("lsrp"), loco(0));
        CHECK(ident.idFor(src).state == LocoIdentity::State::Unknown,
              "loco id 0 is the unassigned value, not a loco — recording it "
              "would make a source carrying only unassigned frames look "
              "identified");
    }

    // ---- and the rule that depends on all of it -----------------------------
    {
        RejectRules rules;
        CHECK(rules.load(QStringLiteral(":/schema/rejectrules.xml")), "rules load");

        LocoIdentity ident;
        QHash<QString, qint64> frame{ { QStringLiteral("DEST_LOCO_ID"), 4711 } };

        // Before anything is known, an SLRP for another loco must NOT be
        // reported: there is nothing to compare it against.
        QHash<QString, qint64> v = frame;
        for (auto it = ident.contextFor(src).cbegin();
             it != ident.contextFor(src).cend(); ++it) { v.insert(it.key(), it.value()); }
        CHECK(rules.evaluate(v).isEmpty(),
              "with no identity, nothing is claimed about who a frame is for");

        // Once identified, a frame for us is unremarkable and one for
        // another loco is reported.
        ident.observe(src, QStringLiteral("arp"), loco(4711));
        auto with = [&](qint64 dest) {
            QHash<QString, qint64> m{ { QStringLiteral("DEST_LOCO_ID"), dest } };
            const auto ctx = ident.contextFor(src);
            for (auto it = ctx.cbegin(); it != ctx.cend(); ++it) {
                m.insert(it.key(), it.value());
            }
            return rules.evaluate(m);
        };
        CHECK(with(4711).isEmpty(), "a frame addressed to us passes");
        const auto other = with(4712);
        CHECK(other.size() == 1, "one addressed elsewhere is reported");
        CHECK(!other.isEmpty()
                  && other.first().text.contains(QStringLiteral("OWN_LOCO_ID")),
              "and the reason names what it was compared against, so the "
              "claim can be checked rather than taken on trust");
    }
}
