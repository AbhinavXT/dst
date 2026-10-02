#include "testutil.h"

#include "pinboard.h"

#include "fieldinspector.h"
#include "fieldplot.h"
#include "pinpanel.h"
#include "schema/schemadecoder.h"
#include "logmodel.h"

#include <QAction>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QPushButton>
#include <QToolButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>

#include <QSharedPointer>
#include <QString>

// =============================================================================
//  Pinned fields.
//
//  A pin holds one decoded field still while traffic runs. The parts worth
//  testing are the ones that are wrong quietly:
//
//    – narrowing to a source, so another loco's value for the same field does
//      not overwrite it
//    – "was", which must not appear on the first sighting: there was nothing
//      to change from, and "changed 0 s ago" is a claim an operator acts on
//    – never seen, which has to be distinguishable from a value of zero
// =============================================================================

namespace {

// Two real frames from the captures, one per loco, differing in LOCO_MODE.
const QString kLsrpLoco1 = QStringLiteral(
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");
const QString kArpLoco1 = QStringLiteral(
    "@arp_1_1 2026-06-27T14:02:26 21436 02 07 0D 00 27 00 00 00 0F 02 D3 AC 57 30 "
    "00 01 40 9F FB 47 D0 01 0E 04 1F C3 15 00 00 00 00 00 08 32 00 C9 5E DE 2F");

LogEntryPtr entryFor(const QString &line)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->text     = line;
    e->epochMs  = 1000;
    e->severity = Severity::Info;
    e->cacheDerived();
    return e;
}

QString valueOf(const PinBoard &b, int i) { return b.pins().at(i).value; }

}  // namespace

TEST_SUITE(pinboard)
{
    // ---- adding ------------------------------------------------------------
    {
        PinBoard b;
        CHECK(b.add(QStringLiteral("PKT_TYPE")), "a field can be pinned");
        CHECK(!b.add(QStringLiteral("PKT_TYPE")),
              "and pinning it twice is refused — two identical pins would "
              "update in step and read as a bug");
        CHECK(b.add(QStringLiteral("pkt_type"), QStringLiteral("1_1")),
              "but the same field narrowed to a source is a different pin");
        CHECK(b.count() == 2, "two pins");
        CHECK(!b.add(QStringLiteral("   ")), "an empty name is not a pin");

        CHECK(b.remove(0) && b.count() == 1, "a pin can be removed");
        CHECK(!b.remove(7), "and removing one that is not there is refused");
    }

    // ---- a pin that has seen nothing ---------------------------------------
    {
        PinBoard b;
        b.add(QStringLiteral("NO_SUCH_FIELD"));
        b.observe(entryFor(kArpLoco1), QStringLiteral("1_1"), 1000);
        CHECK(b.pins().at(0).seen == 0,
              "a field no frame carries is never seen");
        CHECK(b.pins().at(0).value.isEmpty(),
              "and holds no value — which must not be confused with a value "
              "of zero");
    }

    // ---- first sighting is not a change ------------------------------------
    {
        PinBoard b;
        b.add(QStringLiteral("PKT_TYPE"));
        const bool changed = b.observe(entryFor(kArpLoco1),
                                       QStringLiteral("1_1"), 5000);
        CHECK(changed, "the first value is news");
        CHECK(!valueOf(b, 0).isEmpty(), "and is held");
        CHECK(!b.pins().at(0).everChanged(),
              "but nothing 'was' anything yet — reporting a change on the "
              "first frame would date a transition that never happened");
        CHECK(b.pins().at(0).seen == 1, "one frame carried it");
    }

    // ---- a repeat is not a change ------------------------------------------
    {
        PinBoard b;
        b.add(QStringLiteral("PKT_TYPE"));
        b.observe(entryFor(kArpLoco1), QStringLiteral("1_1"), 5000);
        const QString first = valueOf(b, 0);
        const bool changed = b.observe(entryFor(kArpLoco1),
                                       QStringLiteral("1_1"), 6000);
        CHECK(!changed, "the same value again is not a change");
        CHECK(valueOf(b, 0) == first, "and the value stands");
        CHECK(b.pins().at(0).seen == 2, "though it was seen twice");
        CHECK(b.pins().at(0).atMs == 6000,
              "and the last-seen time moves, which is what tells a live value "
              "from one that stopped arriving");
    }

    // ---- a real change keeps the old value ---------------------------------
    {
        PinBoard b;
        b.add(QStringLiteral("PKT_TYPE"));
        b.observe(entryFor(kArpLoco1),  QStringLiteral("1_1"), 5000);
        const QString was = valueOf(b, 0);
        b.observe(entryFor(kLsrpLoco1), QStringLiteral("1_1"), 9000);

        CHECK(valueOf(b, 0) != was, "a different frame gives a different type");
        CHECK(b.pins().at(0).prevValue == was, "the old value is kept");
        CHECK(b.pins().at(0).changedMs == 9000, "with the time it changed");
    }

    // ---- narrowing to a source ---------------------------------------------
    //
    // The reason narrowing exists: with two locos on air, an unnarrowed pin
    // shows whichever spoke last, which is not a reading of anything.
    {
        PinBoard b;
        b.add(QStringLiteral("PKT_TYPE"), QStringLiteral("1_1"));
        b.observe(entryFor(kArpLoco1), QStringLiteral("1_1"), 1000);
        const QString mine = valueOf(b, 0);
        CHECK(!mine.isEmpty(), "the narrowed source sets it");

        const bool changed = b.observe(entryFor(kLsrpLoco1),
                                       QStringLiteral("2_1"), 2000);
        CHECK(!changed, "another source does not");
        CHECK(valueOf(b, 0) == mine, "and cannot overwrite it");
        CHECK(b.pins().at(0).seen == 1, "nor is it counted as a sighting");
    }

    // ---- which source a value came from ------------------------------------
    {
        PinBoard b;
        b.add(QStringLiteral("PKT_TYPE"));
        b.observe(entryFor(kArpLoco1), QStringLiteral("2_1"), 1000);
        CHECK(b.pins().at(0).fromSource == QStringLiteral("2_1"),
              "an unnarrowed pin records where its value came from, so a "
              "reading can be traced back");
    }

    // ---- which frame established the value ----------------------------------
    //
    // Double-clicking a pin goes to the frame where the value BECAME what it
    // is. That needs the log timestamp of that frame, which is not the same as
    // when this program saw it — a replayed session makes the difference
    // obvious, and jumping by the wrong one lands nowhere.
    {
        auto first  = entryFor(kArpLoco1);
        auto second = entryFor(kLsrpLoco1);
        first->epochMs  = 1750000000000LL;
        second->epochMs = 1750000004000LL;

        PinBoard b;
        b.add(QStringLiteral("PKT_TYPE"));

        // Wall-clock times deliberately unrelated to the log times, so a
        // confusion between the two cannot pass unnoticed.
        b.observe(first, QStringLiteral("1_1"), 90000);
        CHECK(b.pins().at(0).atEpochMs == 1750000000000LL,
              "the log time of the frame is recorded, not the arrival time");
        CHECK(b.pins().at(0).originEpochMs() == 1750000000000LL,
              "and before anything changes, that first frame is where the "
              "value came from");

        b.observe(second, QStringLiteral("1_1"), 95000);
        CHECK(b.pins().at(0).changedEpochMs == 1750000004000LL,
              "a change records the frame that caused it");
        CHECK(b.pins().at(0).originEpochMs() == 1750000004000LL,
              "which is then the frame worth going to — where it BECAME this, "
              "not where it was first seen");

        // A repeat must not move it: the value became what it is once, and
        // landing on the newest frame carrying it would answer a different
        // question every second.
        auto third = entryFor(kLsrpLoco1);
        third->epochMs = 1750000009000LL;
        b.observe(third, QStringLiteral("1_1"), 99000);
        CHECK(b.pins().at(0).originEpochMs() == 1750000004000LL,
              "a frame repeating the same value does not move it");
        CHECK(b.pins().at(0).atEpochMs == 1750000009000LL,
              "though last-seen does move, which is what says it is still live");
    }

    {
        // Nothing seen: no frame to go to, and no timestamp pretending there
        // is one.
        PinBoard b;
        b.add(QStringLiteral("NO_SUCH_FIELD"));
        b.observe(entryFor(kArpLoco1), QStringLiteral("1_1"), 1000);
        CHECK(b.pins().at(0).originEpochMs() == 0,
              "a pin that has seen nothing offers no frame");
    }

    // ---- narrowing to a packet ----------------------------------------------
    //
    // Thirty-five field names in the schema live in more than one packet.
    // FRAME_NUM is in five of them — ARP, LSRP, SLRP, AAP, AEP — so an
    // unnarrowed pin on it shows whichever arrived last, which is a reading of
    // nothing even with a single loco on air. The two ends' counters are not
    // the same clock and must not share a row.
    {
        PinBoard b;
        b.add(QStringLiteral("FRAME_NUM"), QString(), QStringLiteral("arp"));
        b.add(QStringLiteral("FRAME_NUM"), QString(), QStringLiteral("lsrp"));
        CHECK(b.count() == 2,
              "the same field in two packets is two pins, not a duplicate");

        b.observe(entryFor(kArpLoco1),  QStringLiteral("1_1"), 1000);
        b.observe(entryFor(kLsrpLoco1), QStringLiteral("1_1"), 2000);

        CHECK(b.pins().at(0).seen == 1, "the arp pin saw only the arp");
        CHECK(b.pins().at(1).seen == 1, "the lsrp pin only the lsrp");
        CHECK(!valueOf(b, 0).isEmpty() && !valueOf(b, 1).isEmpty(),
              "and both hold a value");
        CHECK(valueOf(b, 0) != valueOf(b, 1),
              "which are different numbers — the point of separating them");
    }

    {
        // Unnarrowed, the same field takes whatever came last. Pinned here so
        // the behaviour is a choice on record rather than a surprise.
        PinBoard b;
        b.add(QStringLiteral("FRAME_NUM"));
        b.observe(entryFor(kArpLoco1),  QStringLiteral("1_1"), 1000);
        b.observe(entryFor(kLsrpLoco1), QStringLiteral("1_1"), 2000);
        CHECK(b.pins().at(0).seen == 2,
              "an unnarrowed pin counts both packets, which is why narrowing "
              "exists");
    }

    // ---- freezing ------------------------------------------------------------
    //
    // The board answers "what is it now", which is the wrong tense the moment
    // something happens: by the time the operator looks up from the DMI, now
    // has moved on and the state they wanted is hundreds of frames back.
    {
        PinBoard b;
        b.add(QStringLiteral("PKT_TYPE"));
        b.observe(entryFor(kArpLoco1), QStringLiteral("1_1"), 1000);
        const QString held = valueOf(b, 0);
        CHECK(!b.isFrozen(), "a board starts live");

        b.freeze(5000, QStringLiteral("watch: mode left Stand By"));
        CHECK(b.isFrozen(), "and can be frozen");
        CHECK(b.frozenAtMs() == 5000, "at a stated instant");
        CHECK(b.frozenWhy().contains(QStringLiteral("Stand By")),
              "with what caused it, so a frozen board explains itself");

        const bool changed = b.observe(entryFor(kLsrpLoco1),
                                       QStringLiteral("1_1"), 6000);
        CHECK(!changed, "a frozen board takes nothing in");
        CHECK(valueOf(b, 0) == held, "and holds the value it had");
        CHECK(b.pins().at(0).seen == 1, "not even the sighting count moves");

        // Values are HELD, not cleared: everything the board knew at the
        // freeze is still readable, which is the whole point.
        CHECK(b.pins().at(0).atEpochMs > 0, "the origin frame is still there");

        b.thaw();
        CHECK(!b.isFrozen(), "and it thaws");
        CHECK(b.observe(entryFor(kLsrpLoco1), QStringLiteral("1_1"), 7000),
              "taking the next frame that carries the field");
        CHECK(valueOf(b, 0) != held, "which updates it again");
    }

    {
        // The FIRST freeze is the interesting one. A second watch firing must
        // not overwrite the instant the first one captured.
        PinBoard b;
        b.add(QStringLiteral("PKT_TYPE"));
        b.freeze(1000, QStringLiteral("first"));
        b.freeze(2000, QStringLiteral("second"));
        CHECK(b.frozenAtMs() == 1000 && b.frozenWhy() == QStringLiteral("first"),
              "a second freeze does not move the first");
    }

    // ---- round trip ---------------------------------------------------------
    {
        PinBoard b;
        b.add(QStringLiteral("LOCO_MODE"), QStringLiteral("1_1"));
        b.add(QStringLiteral("ABS_LOCO_LOC"));

        PinBoard back;
        back.fromStrings(b.toStrings());
        CHECK(back.count() == 2, "pins survive a save and load");
        CHECK(back.pins().at(0).field == QStringLiteral("LOCO_MODE"), "field");
        CHECK(back.pins().at(0).sourceKey == QStringLiteral("1_1"), "and source");
        CHECK(back.pins().at(1).sourceKey.isEmpty(), "an unnarrowed pin too");

        // The captype survives too, and a line written by the earlier build —
        // field and source only — still loads, meaning "any packet", which is
        // what it meant when it was written.
        PinBoard withType;
        withType.add(QStringLiteral("FRAME_NUM"), QString(), QStringLiteral("slrp"));
        PinBoard rt;
        rt.fromStrings(withType.toStrings());
        CHECK(rt.pins().at(0).captype == QStringLiteral("slrp"), "captype survives");

        PinBoard old;
        // Split literal (session 116): "\x1F1_1" is ONE character, U+01F1 —
        // \x takes every hex digit after it. Clang took it in a 16-bit
        // literal, so this "two-part line" was one part and the check below
        // passed without testing anything; MSVC refused it.
        old.fromStrings({ QStringLiteral("LOCO_MODE\x1F" "1_1") });
        CHECK(old.count() == 1, "a two-part line from an earlier build loads");
        CHECK(old.pins().at(0).field == QLatin1String("LOCO_MODE")
                  && old.pins().at(0).sourceKey == QLatin1String("1_1"),
              "both parts read: the field and its source");
        CHECK(old.pins().at(0).captype.isEmpty(), "as any packet");

        // A hand-edited ini must not produce half a pin.
        PinBoard junk;
        junk.fromStrings({ QStringLiteral(""), QStringLiteral("   ") });
        CHECK(junk.count() == 0, "blank lines are not pins");
    }
}

// =============================================================================
//  Pinning and plotting from where the operator is already pointing.
//
//  The field chooser in either window asks which field, when the operator has
//  just right-clicked the field. These check the two paths that make that
//  work: the inspector raising a named request, and the plot window opening
//  onto a field rather than onto its chooser.
// =============================================================================

// =============================================================================
//  What the chooser can offer.
//
//  The panel shipped with an EMPTY chooser: setAvailableFields existed and
//  nothing called it, so every field had to be typed from memory. These pin
//  both halves of the fix — that the schema can enumerate its fields at all,
//  and that the panel lists them.
// =============================================================================

TEST_SUITE(pinchoices)
{
    Schema::Decoder dec;
    QString err;
    CHECK(dec.load(QStringLiteral(":/schema/kavach.xml"), &err), "the schema loads");

    const QStringList all = dec.allFieldNames();
    CHECK(all.size() > 200,
          "the schema can name hundreds of fields — the point of offering them "
          "is that a session carries a few dozen");
    CHECK(all.contains(QStringLiteral("LOCO_MODE")), "including LOCO_MODE");
    CHECK(all.contains(QStringLiteral("FRAME_NUM")), "and FRAME_NUM");

    // Fields nested inside a repeat are reachable. A repeated entry's fields
    // are exactly the ones worth watching, and a walk that only looked at the
    // top level would miss every one of them.
    CHECK(all.contains(QStringLiteral("speed")),
          "and fields inside a <repeat>, which a top-level walk would miss");

    // Sorted, checked pairwise rather than against a re-sort: the schema has
    // names that differ only in case ("speed" and "SPEED"), so a
    // case-insensitive sort has no single right answer for their order and
    // comparing two runs of it is comparing coin flips.
    bool ordered = true;
    for (int i = 1; i < all.size(); ++i) {
        if (all.at(i - 1).compare(all.at(i), Qt::CaseInsensitive) > 0) {
            ordered = false;
        }
    }
    CHECK(ordered, "the list is sorted");
    CHECK(QSet<QString>(all.begin(), all.end()).size() == all.size(),
          "and free of duplicates, though the same name occurs in several "
          "packets");

    // ---- the schema splits by packet ---------------------------------------
    //
    // The flat list above is the right answer to "what can the equipment
    // send" and the wrong shape for browsing. These check the split that
    // makes the nested chooser possible.
    const QVector<Schema::Decoder::PacketFields> byPkt = dec.fieldsByCaptype();
    CHECK(byPkt.size() >= dec.packetCount(),
          "every packet is offered, and one of them twice — ARP answers to "
          "two captype tokens");

    auto groupFor = [&byPkt](const QString &captype) {
        for (const Schema::Decoder::PacketFields &pf : byPkt) {
            if (pf.captype == captype) { return pf; }
        }
        return Schema::Decoder::PacketFields{};
    };

    // Keyed on the TOKEN, not the packet name. This is the whole reason the
    // narrowing combo was wrong: PinBoard::observe matches a pin's captype
    // against the capture line's token, so a combo offering packet NAMES
    // produced narrowings that could never fire.
    CHECK(!groupFor(QStringLiteral("lsos")).packet.isEmpty(),
          "LOCO_SOS is reachable by the token it actually arrives as");
    CHECK(groupFor(QStringLiteral("lsos")).packet == QStringLiteral("LOCO_SOS"),
          "and still names the packet, so the label can say both");
    CHECK(!groupFor(QStringLiteral("arprecv")).fields.isEmpty(),
          "ARP received is its own group — one packet, two tokens, and the "
          "second one was silently unreachable before");
    CHECK(groupFor(QStringLiteral("arp")).fields
              == groupFor(QStringLiteral("arprecv")).fields,
          "carrying the same fields, because it is the same packet");

    // Sub-packet fields are reachable. SLRP decodes its eight sub-packets
    // through <case struct=>, and a walk that stopped at the case element
    // would offer an empty SLRP — which is where most of the fields worth
    // pinning live.
    const QStringList slrp = groupFor(QStringLiteral("slrp")).fields;
    CHECK(slrp.size() > 40,
          "SLRP offers its sub-packet fields, not just its header");
    CHECK(slrp.contains(QStringLiteral("speed")),
          "including one from inside a sub-packet struct");

    // Narrowing is the point, so the groups must actually differ.
    CHECK(!groupFor(QStringLiteral("dmi")).fields.contains(QStringLiteral("SUB_PKT_TYPE")),
          "and a field of one packet is not offered under another");

    int frameNumIn = 0;
    for (const Schema::Decoder::PacketFields &pf : byPkt) {
        if (pf.fields.contains(QStringLiteral("FRAME_NUM"))) { ++frameNumIn; }
    }
    CHECK(frameNumIn >= 4,
          "FRAME_NUM is offered under every packet that carries it — the "
          "flat list could not say which of them was meant");

    // ---- the panel nests them ----------------------------------------------
    {
        QVector<PinPacketFields> panelPkts;
        for (const Schema::Decoder::PacketFields &pf : byPkt) {
            panelPkts.push_back({ pf.packet, pf.captype, pf.fields });
        }

        PinPanel panel;
        auto *edit = panel.findChild<QLineEdit *>(QStringLiteral("pinFieldBox"));
        auto *browse =
            panel.findChild<QToolButton *>(QStringLiteral("pinFieldBrowseBtn"));
        CHECK(edit != nullptr,   "the panel has a field box to type in");
        CHECK(browse != nullptr, "and a button to browse from");
        CHECK(browse && browse->menu() && browse->menu()->isEmpty(),
              "which starts empty — this is the state it shipped in, and the "
              "reason it needed filling");

        panel.setAvailablePackets(panelPkts);
        const QStringList seen{ QStringLiteral("LOCO_MODE"),
                                QStringLiteral("FRAME_NUM") };
        panel.setAvailableFields(seen, panelPkts);

        QMenu *root = browse->menu();
        QList<QAction *> top = root->actions();

        // What this tab has carried comes first: a few dozen names out of
        // five hundred, and nearly always the one wanted.
        CHECK(!top.isEmpty() && top.first()->menu() != nullptr,
              "the first group is a submenu");
        CHECK(!top.isEmpty()
                  && top.first()->text().contains(QStringLiteral("this tab")),
              "and it is what this tab has carried");
        CHECK(top.first()->menu()->actions().size() == seen.size(),
              "holding exactly what was observed");

        auto groupNamed = [&top](const QString &needle) -> QMenu * {
            for (QAction *a : top) {
                if (a->menu() && a->text().contains(needle)) { return a->menu(); }
            }
            return nullptr;
        };

        CHECK(groupNamed(QStringLiteral("SLRP")) != nullptr,
              "then one group per packet");
        CHECK(groupNamed(QStringLiteral("(lsos)")) != nullptr,
              "labelled with the token where it differs from the name, "
              "because the token is what arrives");
        CHECK(groupNamed(QStringLiteral("(arprecv)")) != nullptr,
              "which is what tells the two ARP groups apart");

        // A group short enough to read is a flat list of fields; a long one
        // is bucketed, because a menu taller than the screen scrolls and a
        // scrolling menu of 167 names is the flat list again.
        QMenu *aep = groupNamed(QStringLiteral("AEP"));
        CHECK(aep && !aep->actions().isEmpty()
                  && aep->actions().first()->menu() == nullptr,
              "a short group lists its fields directly");

        QMenu *linfo = groupNamed(QStringLiteral("LINFO"));
        CHECK(linfo && !linfo->actions().isEmpty()
                  && linfo->actions().first()->menu() != nullptr,
              "a long one is bucketed alphabetically first");
        if (linfo) {
            int leaves = 0;
            for (QAction *b : linfo->actions()) {
                if (b->menu()) { leaves += b->menu()->actions().size(); }
            }
            CHECK(leaves == groupFor(QStringLiteral("linfo")).fields.size(),
                  "and the buckets between them hold every field, none lost "
                  "at a boundary and none listed twice");
        }

        // NMSHLTH decodes entirely through a flag table and names no fields.
        // An empty submenu would read as a bug, so it says so.
        QMenu *hlth = groupNamed(QStringLiteral("NMSHLTH"));
        CHECK(hlth && hlth->actions().size() == 1
                  && !hlth->actions().first()->isEnabled(),
              "a packet with no named fields says so rather than opening "
              "onto nothing");

        // ---- picking answers both questions at once -------------------------
        //
        // An operator who picks FRAME_NUM from under LSRP has already said
        // which of the five they mean. Leaving the narrowing on "any packet"
        // after that would pin the one thing they did not ask for: whichever
        // packet arrived last.
        auto *packetBox =
            panel.findChild<QComboBox *>(QStringLiteral("pinPacketBox"));
        CHECK(packetBox != nullptr, "the panel narrows by packet");

        QMenu *lsrp = groupNamed(QStringLiteral("LSRP"));
        QAction *pickFrameNum = nullptr;
        if (lsrp) {
            for (QAction *a : lsrp->actions()) {
                if (a->text() == QStringLiteral("FRAME_NUM")) { pickFrameNum = a; }
            }
        }
        CHECK(pickFrameNum != nullptr, "LSRP offers FRAME_NUM");
        if (pickFrameNum) {
            pickFrameNum->trigger();
            CHECK(edit->text() == QStringLiteral("FRAME_NUM"),
                  "picking it names the field");
            CHECK(packetBox->currentData().toString() == QStringLiteral("lsrp"),
                  "and narrows the pin to the packet it was picked under");

            panel.board().clear();
            auto *addBtn =
                panel.findChild<QPushButton *>(QStringLiteral("pinAddBtn"));
            CHECK(addBtn != nullptr, "the panel has a Pin button");
            if (addBtn) {
                addBtn->click();
                CHECK(panel.board().count() == 1, "which pins what was picked");
                CHECK(panel.board().pins().first().captype
                          == QStringLiteral("lsrp"),
                      "narrowed to the token a capture line actually carries, "
                      "not to the packet name, which would never match");
            }
        }

        // Choosing from the observed group says nothing about packets, so it
        // must not invent a narrowing.
        packetBox->setCurrentIndex(0);
        QMenu *here = top.first()->menu();
        if (here && !here->actions().isEmpty()) {
            here->actions().first()->trigger();
            CHECK(packetBox->currentData().toString().isEmpty(),
                  "a field picked from what this tab carried leaves the "
                  "narrowing alone — the pick said nothing about packets");
        }

        // With nothing observed there is no group above an empty one.
        PinPanel fresh;
        fresh.setAvailablePackets(panelPkts);
        fresh.setAvailableFields({}, panelPkts);
        auto *browse2 =
            fresh.findChild<QToolButton *>(QStringLiteral("pinFieldBrowseBtn"));
        CHECK(browse2 && browse2->menu()
                  && browse2->menu()->actions().size() == panelPkts.size(),
              "a tab that has carried nothing yet still offers every packet, "
              "with no separator above an empty group");
    }
}

TEST_SUITE(pinfromrow)
{
    // ---- the inspector raises, it does not act ------------------------------
    //
    // It knows which field was clicked and nothing else — not the tab, not
    // where the pin board lives. Opening windows is MainWindow's job
    // everywhere else in this program.
    {
        FieldInspector insp;
        QSignalSpy pinSpy(&insp,  &FieldInspector::pinFieldRequested);
        QSignalSpy plotSpy(&insp, &FieldInspector::plotFieldRequested);
        CHECK(pinSpy.isValid(),  "the inspector offers a pin request");
        CHECK(plotSpy.isValid(), "and a plot request");

        auto *table = insp.findChild<QTableWidget *>();
        CHECK(table != nullptr, "its rows live in a table");
        CHECK(table && table->contextMenuPolicy() == Qt::CustomContextMenu,
              "which answers a right-click — without that the menu never "
              "appears and the whole path is unreachable");
    }

    // ---- the plot opens ON a field -----------------------------------------
    {
        LogModel model;
        model.appendEntries({ entryFor(kLsrpLoco1), entryFor(kArpLoco1) });

        FieldPlotWindow plot(&model, QStringLiteral("1_1"));
        plot.plotField(QStringLiteral("PKT_TYPE"));
        QTest::qWait(30);

        auto *chooser = plot.findChild<QToolButton *>(QStringLiteral("fieldPlotChooser"));
        CHECK(chooser != nullptr, "the plot has a field chooser");
        CHECK(plot.currentField() == QStringLiteral("PKT_TYPE") && !plot.currentType().isEmpty()
                  && chooser && chooser->text().endsWith(QStringLiteral("PKT_TYPE")),
              "and opens on the field it was asked for, in a packet that carries it");
    }

    // ---- a field the sample never saw is still selected ---------------------
    //
    // The chooser lists what the first few hundred rows carried, which is not
    // what the session contains. Refusing an unlisted name would refuse fields
    // that do plot — and silently plotting whatever the chooser was on would
    // draw the wrong field with nothing saying so.
    {
        LogModel model;
        model.appendEntries({ entryFor(kArpLoco1) });

        FieldPlotWindow plot(&model, QStringLiteral("1_1"));
        plot.plotField(QStringLiteral("SOME_UNSAMPLED_FIELD"));
        QTest::qWait(30);

        CHECK(plot.currentField() == QStringLiteral("SOME_UNSAMPLED_FIELD") && plot.currentType().isEmpty(),
              "an unlisted field is selected (from any packet) rather than ignored");
    }
}
