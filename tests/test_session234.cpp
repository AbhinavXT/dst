#include "testutil.h"

#include "packetbuilder.h"
#include "packetmakerdialog.h"
#include "packetpreset.h"
#include "packetvariation.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

// =============================================================================
//  Session 234 — variation rules on sub-packet fields. A rule names
//  "MovementAuthority.CUR_SIG_ASPECT" and sets that field in every
//  MovementAuthority of the frame, per send; the Packet Maker, a preset's
//  build() (Packet Sequence) and the matrix all apply rules through one
//  function, applyVary(). The frame is the bufferload suite's real SLRP
//  reference frame 6 (from replay/).
// =============================================================================

namespace {

const char kSlrpRef6[] =
    "91 B9 AA E6 04 1E 80 00 08 40 6F 8E 68 01 80 00 00 00 00 01 7C AA 08 20 "
    "00 11 63 0D 0C 58 10 B1 00 F4 84 00 22 05 09 BA 20 14 00 00 34 C6 08 5A "
    "40 F4 93 00 54 E8 60 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 11 "
    "30 38 22 41 08 7A 04 83 23 8A 29 0E 08 41 38 40 95 0A 60 61 64 E0 00 03 "
    "43 38 68 60 42 C0 3E A4 18 46 67 50 A7 6E";

qint64 maValue(const QVector<Schema::SubEntry> &subs, const char *field)
{
    for (const Schema::SubEntry &s : subs)
        if (s.type == 0) return s.values.value(QLatin1String(field), -1);
    return -1;
}

} // namespace

TEST_SUITE(session234)
{
    PacketBuilder pb;
    CHECK(pb.ready(), "schema loaded");
    const QByteArray frame = QByteArray::fromHex(QByteArray(kSlrpRef6).replace(' ', ""));
    const Schema::ParsedPacket pp = pb.encoder().parseBody(QStringLiteral("slrp"), frame);
    CHECK(pp.ok, "reference frame 6 parses");
    int maCount = 0;
    for (const Schema::SubEntry &s : pp.subs) maCount += s.type == 0 ? 1 : 0;
    CHECK(maCount >= 1, "it carries a MovementAuthority");

    PacketVary::Rule aspect;
    aspect.field = QStringLiteral("CUR_SIG_ASPECT");
    aspect.sub = QStringLiteral("MovementAuthority");
    aspect.mode = PacketVary::Rule::Increment;
    aspect.start = 1;
    aspect.step = 1;
    aspect.max = 4;
    CHECK(aspect.target() == QLatin1String("MovementAuthority.CUR_SIG_ASPECT")
              && aspect.describe(6).startsWith(QStringLiteral("MovementAuthority.CUR_SIG_ASPECT: 1 then +1")),
          "a sub-packet rule names its struct");

    // ---- JSON: the struct goes with the rule; an old rule is a header rule ----------------
    const QVector<PacketVary::Rule> back = PacketVary::fromJson(PacketVary::toJson({ aspect }));
    CHECK(back.size() == 1 && back.first().sub == aspect.sub && back.first().field == aspect.field, "saved and read back");
    QJsonObject old;
    old.insert(QStringLiteral("field"), QStringLiteral("FRAME_NUM"));
    old.insert(QStringLiteral("mode"), QStringLiteral("increment"));
    CHECK(PacketVary::fromJson(QJsonArray { old }).first().sub.isEmpty()
              && !PacketVary::toJson({ PacketVary::fromJson(QJsonArray { old }).first() }).first().toObject().contains(QStringLiteral("sub")),
          "a rule saved before has no struct: a header rule, written as before");

    // ---- applyVary ------------------------------------------------------------------------
    QVector<qint64> seen;
    for (int i = 0; i < 5; ++i) {
        QHash<QString, qint64> h = pp.header;
        QVector<Schema::SubEntry> subs = pp.subs;
        applyVary({ aspect }, i, pb.encoder(), QStringLiteral("slrp"), &h, &subs);
        seen << maValue(subs, "CUR_SIG_ASPECT");
        if (i == 0) {
            bool others = h == pp.header;
            for (int k = 0; k < subs.size(); ++k)
                if (subs.at(k).type != 0) others = others && subs.at(k).values == pp.subs.at(k).values;
            CHECK(others, "only the MovementAuthority's CUR_SIG_ASPECT changes");
        }
    }
    CHECK(seen == QVector<qint64>({ 1, 2, 3, 4, 1 }), "sends 0-4: 1, 2, 3, 4, then back to 1");
    {
        QHash<QString, qint64> h = pp.header;
        QVector<Schema::SubEntry> subs = pp.subs;
        applyVary({ aspect }, -1, pb.encoder(), QStringLiteral("slrp"), &h, &subs);
        CHECK(maValue(subs, "CUR_SIG_ASPECT") == maValue(pp.subs, "CUR_SIG_ASPECT"), "send -1 (Build): the form's own value");
        PacketVary::Rule wrap = aspect;
        wrap.start = 62;
        wrap.max = 0;       // the field's width: 63
        QVector<qint64> w;
        for (int i = 0; i < 3; ++i) {
            QVector<Schema::SubEntry> s2 = pp.subs;
            applyVary({ wrap }, i, pb.encoder(), QStringLiteral("slrp"), nullptr, &s2);
            w << maValue(s2, "CUR_SIG_ASPECT");
        }
        CHECK(w == QVector<qint64>({ 62, 63, 62 }), "held to the field's 6 bits");
        PacketVary::Rule none = aspect;
        none.sub = QStringLiteral("TSR");          // not in this frame
        QVector<Schema::SubEntry> s3 = pp.subs;
        applyVary({ none }, 2, pb.encoder(), QStringLiteral("slrp"), nullptr, &s3);
        bool same = true;
        for (int k = 0; k < s3.size(); ++k) same = same && s3.at(k).values == pp.subs.at(k).values;
        CHECK(same, "a struct the frame does not carry: nothing changes");
    }

    // ---- to the wire: a preset (Packet Sequence's path) ---------------------------------------
    PacketPreset preset;
    preset.captype = QStringLiteral("slrp");
    preset.header = pp.header;
    preset.subs = pp.subs;
    preset.vary = { aspect };
    bool wire = true;
    for (int i = 0; i < 4; ++i) {
        const PacketBuilder::Result r = preset.build(pb, i, QByteArray());
        const Schema::ParsedPacket got = r.ok ? pb.encoder().parseBody(QStringLiteral("slrp"), r.frame) : Schema::ParsedPacket();
        wire = wire && r.ok && got.ok && maValue(got.subs, "CUR_SIG_ASPECT") == i + 1;
    }
    CHECK(wire, "a preset's sends 0-3 decode with CUR_SIG_ASPECT 1-4: the rule reaches the frame");
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("aspect.packet.json"));
    PacketPreset loaded;
    CHECK(preset.save(path) && PacketPreset::load(path, &loaded) && loaded.vary.size() == 1
              && loaded.vary.first().sub == QLatin1String("MovementAuthority"),
          "the preset keeps its sub-packet rule");

    // ---- the Packet Maker ---------------------------------------------------------------------
    PacketMakerDialog d;
    d.loadBuffer(QString::fromLatin1(kSlrpRef6));
    d.setVaryRulesForTest({ aspect });
    const QVector<PacketVary::Rule> rules = d.varyRules();
    CHECK(rules.size() == 1 && rules.first().sub == QLatin1String("MovementAuthority")
              && rules.first().field == QLatin1String("CUR_SIG_ASPECT"),
          "the rule table offers MovementAuthority.CUR_SIG_ASPECT and reads it back");
    const PacketBuilder::Result s2 = d.buildSendForTest(2);
    const Schema::ParsedPacket g2 = s2.ok ? pb.encoder().parseBody(QStringLiteral("slrp"), s2.frame) : Schema::ParsedPacket();
    CHECK(s2.ok && g2.ok && maValue(g2.subs, "CUR_SIG_ASPECT") == 3, "interval send 2 from the Packet Maker carries 3");
    const PacketBuilder::Result once = d.buildSendForTest(-1);
    const Schema::ParsedPacket g1 = once.ok ? pb.encoder().parseBody(QStringLiteral("slrp"), once.frame) : Schema::ParsedPacket();
    CHECK(once.ok && maValue(g1.subs, "CUR_SIG_ASPECT") == maValue(pp.subs, "CUR_SIG_ASPECT"), "Build / Send once: the form's value");
}
