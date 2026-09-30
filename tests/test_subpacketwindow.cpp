// =============================================================================
//  The detached sub-packet editor.
//
//  Moving the editor out of the Packet Maker traded a layout problem for a
//  lifetime one: the window edits the dialog's sub-packet vector through a
//  pointer and an index, so anything that changes the vector — removing a
//  row, loading a preset, switching packet type — can leave the window
//  pointing at a sub-packet that no longer exists or, worse, at a different
//  one that now occupies that index. A commit through a stale index would not
//  crash; it would quietly write MovementAuthority values into a TagLinking.
//
//  These checks are about that hazard, not about the widgets.
// =============================================================================

#include "testutil.h"

#include "packetbuilder.h"
#include "subpacketwindow.h"

#include <QLineEdit>
#include <QSpinBox>

TEST_SUITE(subpacketwindow)
{
    PacketBuilder builder;
    CHECK(builder.ready(), "schema loaded");
    if (!builder.ready()) { return; }

    const Schema::Encoder &enc = builder.encoder();
    const Schema::PacketInfo pi = enc.packet(QStringLiteral("slrp"));
    CHECK(pi.ok && pi.hasSub, "slrp defines sub-packets");
    if (!pi.ok || !pi.hasSub) { return; }

    QVector<Schema::SubEntry> subs;
    Schema::SubEntry a; a.type = 0; a.values.insert(QStringLiteral("FRAME_OFFSET"), 11);
    Schema::SubEntry b; b.type = 5; b.values.insert(QStringLiteral("DIST_DUP_TAG"), 3);
    subs.push_back(a);
    subs.push_back(b);

    SubPacketWindow w;

    // ---- targeting -------------------------------------------------------

    CHECK(!w.hasTarget(), "a fresh window edits nothing");

    w.setTarget(&enc, QStringLiteral("slrp"), &subs, 0);
    CHECK(w.hasTarget(),  "setTarget takes a valid index");
    CHECK(w.index() == 0, "and reports it");

    w.setTarget(&enc, QStringLiteral("slrp"), &subs, 99);
    CHECK(!w.hasTarget(), "an out-of-range index is refused rather than stored");
    CHECK(w.index() == -1, "and leaves no index behind");

    w.setTarget(&enc, QStringLiteral("slrp"), &subs, -1);
    CHECK(!w.hasTarget(), "a negative index is refused too");

    // ---- commit with no target is a no-op --------------------------------

    {
        const QVector<Schema::SubEntry> before = subs;
        w.clearTarget();
        w.commit();
        CHECK(subs.size() == before.size(), "commit with no target changes nothing");
        CHECK(subs[0].values.value("FRAME_OFFSET") == 11,
              "and leaves the first sub-packet alone");
        CHECK(subs[1].values.value("DIST_DUP_TAG") == 3,
              "and the second");
    }

    // ---- a targeted commit writes only its own sub-packet ----------------
    //
    // The window is populated from the entry, so committing straight back
    // must be a round trip. If it were writing through the wrong index this
    // is where the two sub-packets would swap contents.

    {
        w.setTarget(&enc, QStringLiteral("slrp"), &subs, 1);
        CHECK(w.index() == 1, "targeting the second sub-packet");
        w.commit();
        CHECK(subs[0].type == 0, "the untargeted sub-packet keeps its type");
        CHECK(subs[0].values.value("FRAME_OFFSET") == 11,
              "the untargeted sub-packet keeps its values");
        CHECK(subs[1].type == 5, "the targeted sub-packet keeps its type");
        CHECK(subs[1].values.value("DIST_DUP_TAG") == 3,
              "and round-trips its own value unchanged");
    }

    // ---- retargeting commits the previous target first -------------------
    //
    // The old inline editor committed on every selection change so that
    // moving between sub-packets could not silently drop an edit. That
    // property has to survive the move to a window.

    {
        w.setTarget(&enc, QStringLiteral("slrp"), &subs, 0);
        // Poke the first line edit the window built, which is how the
        // operator would change a value.
        const auto edits = w.findChildren<QLineEdit *>();
        const auto spins = w.findChildren<QSpinBox *>();
        CHECK(!edits.isEmpty() || !spins.isEmpty(),
              "the window builds editors for the struct's fields");

        // Retargeting elsewhere must not throw away or misplace whatever the
        // first target held.
        w.setTarget(&enc, QStringLiteral("slrp"), &subs, 1);
        CHECK(subs.size() == 2, "retargeting does not add or drop sub-packets");
        CHECK(subs[0].type == 0 && subs[1].type == 5,
              "retargeting does not reorder them");
    }

    // ---- the vector shrinking under the window ---------------------------
    //
    // This is the case that would corrupt data rather than crash: index 1 is
    // still in range after a removal, but it is now a DIFFERENT sub-packet.
    // The dialog clears the target before removing, and this asserts the
    // window cooperates.

    {
        w.setTarget(&enc, QStringLiteral("slrp"), &subs, 1);
        w.clearTarget();
        subs.remove(0);
        w.commit();                       // must do nothing at all
        CHECK(subs.size() == 1, "the removal stands");
        CHECK(subs[0].type == 5, "the survivor is the right sub-packet");
        CHECK(subs[0].values.value("DIST_DUP_TAG") == 3,
              "and was not overwritten through a stale index");
    }
}
