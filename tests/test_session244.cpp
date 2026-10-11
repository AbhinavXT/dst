#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "packetmakerdialog.h"
#include "roundtrip.h"
#include "schema/schemaencoder.h"

#include <QDirIterator>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QFile>
#include <QHash>
#include <QTextStream>
#include <cstring>

// =============================================================================
//  Session 244 — the encoder writes what it used to refuse: typed float /
//  double / char fields (ANALOG, LINFO, UBA), packet-level <repeat> (DOP1,
//  DOP2, NMSFLT, SOS snapshot, UBA) and NMSHLTH's <eventstream>. The proof
//  is the corpus: every frame of those types in replay/ (up to 2000 a type)
//  is parsed and rebuilt byte for byte by RoundTrip::check. SOS and UBA have
//  no real capture yet, so theirs are the synthetic fixtures in
//  schema/fixtures/ (layout only, not firmware evidence).
// =============================================================================

namespace {

void collect(const QString &root, const QStringList &types, QHash<QString, QVector<QByteArray>> &out)
{
    QDirIterator it(root, { QStringLiteral("*.cap"), QStringLiteral("*.log") }, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile f(it.next());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream ts(&f);
        while (!ts.atEnd()) {
            const QString line = ts.readLine();
            if (!line.startsWith(QLatin1Char('@'))) continue;
            const CaptureLine c = CaptureDecoder::parseLine(line);
            if (!c.valid || !types.contains(c.typeToken)) continue;
            // LINFO: June's captures are 370 B (older firmware), October's 371 B,
            // which is what the schema describes. Kept apart.
            const QString key = c.typeToken == QLatin1String("linfo")
                ? QStringLiteral("linfo/%1").arg(c.bytes.size()) : c.typeToken;
            QVector<QByteArray> &v = out[key];
            if (v.size() < 2000) v << c.bytes;
        }
    }
}

} // namespace

TEST_SUITE(session244)
{
    Schema::Encoder enc;
    QString err;
    CHECK(enc.load(QStringLiteral(":/schema/kavach.xml"), &err), "kavach.xml loads: " + err.toUtf8());

    const QStringList types = { QStringLiteral("dop1"), QStringLiteral("dop2"), QStringLiteral("analog_top"),
                                QStringLiteral("analog_bottom"), QStringLiteral("linfo"), QStringLiteral("nmsflt"),
                                QStringLiteral("nmshlth"), QStringLiteral("sos"), QStringLiteral("uba") };
    for (const QString &t : types)
        CHECK(enc.packet(t).unsupported.isEmpty(),
              QByteArray("buildable now: ") + t.toUtf8() + " (" + enc.packet(t).unsupported.join(", ").toUtf8() + ")");

    QHash<QString, QVector<QByteArray>> frames;
    collect(QStringLiteral("../replay"), types, frames);
    collect(QStringLiteral("../schema/fixtures"), { QStringLiteral("sos"), QStringLiteral("uba") }, frames);

    for (const QString &key : types) {
        const QString t = key == QLatin1String("linfo") ? QStringLiteral("linfo/371") : key;
        const QVector<QByteArray> &v = frames.value(t);
        int same = 0;
        QString first;
        for (const QByteArray &fr : v) {
            const RoundTrip::Result r = RoundTrip::check(enc, key, fr);
            if (r.identical()) ++same;
            else if (first.isEmpty()) first = RoundTrip::outcomeName(r.outcome) + QStringLiteral(": ") + r.detail
                                             + QStringLiteral(" (first diff at body byte %1)").arg(r.firstDiff);
        }
        CHECK(!v.isEmpty() && same == v.size(),
              QByteArray(t.toUtf8()) + ": " + QByteArray::number(same) + " of " + QByteArray::number(v.size())
                  + " frames rebuilt byte for byte" + (first.isEmpty() ? QByteArray() : " — " + first.toUtf8()));
    }

    // The 370-byte LINFOs are an older struct: refused as short, not rebuilt
    // into a 371-byte frame that would say something else.
    const QVector<QByteArray> &old = frames.value(QStringLiteral("linfo/370"));
    bool allShort = !old.isEmpty();
    for (const QByteArray &fr : old) {
        const RoundTrip::Result r = RoundTrip::check(enc, QStringLiteral("linfo"), fr);
        allShort &= r.outcome == RoundTrip::Outcome::ParseFailed && r.detail.contains(QLatin1String("too short"));
    }
    CHECK(allShort, "June's " + QByteArray::number(old.size()) + " LINFOs of 370 B are refused as too short");

    // ---- the shapes the form sees -----------------------------------------------------------
    const Schema::PacketInfo uba = enc.packet(QStringLiteral("uba"));
    int doubles = 0; bool seg8 = false, seg9 = false;
    for (const Schema::FieldInfo &f : uba.header) {
        doubles += f.type == QLatin1String("double") ? 1 : 0;
        seg8 |= f.name == QLatin1String("seg[8].A");
        seg9 |= f.name == QLatin1String("seg[9].A");
    }
    CHECK(doubles == 2 + 9 * 6 && seg8 && !seg9, "UBA: 2 doubles + 9 rows of 6, named seg[0..8].x");

    const Schema::PacketInfo linfo = enc.packet(QStringLiteral("linfo"));
    QStringList apn;
    for (const Schema::FieldInfo &f : linfo.header)
        if (f.name.startsWith(QLatin1String("gsm_1_apn_name"))) apn << QStringLiteral("%1/%2").arg(f.name).arg(f.bits);
    CHECK(apn == QStringList({ QStringLiteral("gsm_1_apn_name/64"), QStringLiteral("gsm_1_apn_name#2/64"),
                               QStringLiteral("gsm_1_apn_name#3/64"), QStringLiteral("gsm_1_apn_name#4/64"),
                               QStringLiteral("gsm_1_apn_name#5/64") }),
          "a 40-char array is five 8-byte pieces: " + apn.join(' ').toUtf8());

    const Schema::PacketInfo flt = enc.packet(QStringLiteral("nmsflt"));
    QString when3;
    for (const Schema::FieldInfo &f : flt.header) if (f.name == QLatin1String("fault[3].module")) when3 = f.when;
    CHECK(when3 == QLatin1String("fault_count > 3"), "a counted row is gated on its count: " + when3.toUtf8());

    // ---- values in, frame out ---------------------------------------------------------------
    // ANALOG: 6 floats; 1.5 must land as IEEE-754 little-endian 00 00 C0 3F.
    float f15 = 1.5f; quint32 raw15; std::memcpy(&raw15, &f15, 4);
    QHash<QString, qint64> an;
    an.insert(QStringLiteral("channel1_data"), raw15);
    QByteArray body = enc.encodeBody(QStringLiteral("analog_top"), an, {}, &err);
    CHECK(body.left(4).toHex() == "0000c03f", "a float is written as IEEE-754 LE: " + body.left(4).toHex());
    const Schema::ParsedPacket anBack = enc.parseBody(QStringLiteral("analog_top"), body);
    CHECK(anBack.ok && anBack.header.value(QStringLiteral("channel1_data")) == raw15, "and read back to the same bits");

    // NMSFLT: count outside 0..63 is refused, as the decoder would read no rows.
    QHash<QString, qint64> nf;
    nf.insert(QStringLiteral("fault_count"), 64);
    err.clear();
    CHECK(enc.encodeBody(QStringLiteral("nmsflt"), nf, {}, &err).isEmpty() && err.contains(QLatin1String("fault_count = 64")),
          "fault_count 64 is refused: " + err.toUtf8());
    nf.insert(QStringLiteral("fault_count"), 2);
    nf.insert(QStringLiteral("fault[1].fault_id"), 0x1234);
    nf.insert(QStringLiteral("fault[2].fault_id"), 0x5678);          // row 2: not on the wire
    body = enc.encodeBody(QStringLiteral("nmsflt"), nf, {}, &err);
    CHECK(body.size() == 2 + 2 * 4 && body.right(2).toHex() == "1234",
          "two rows written, the third left out: " + body.toHex());

    // NMSHLTH: each value as wide as its event's table entry.
    QHash<QString, qint64> nh;
    nh.insert(QStringLiteral("event_count"), 2);
    nh.insert(QStringLiteral("event[0].id"), 1);        // RADIO_1_HEALTH, 1 byte
    nh.insert(QStringLiteral("event[0].value"), 7);
    nh.insert(QStringLiteral("event[1].id"), 9999);     // not in the table: 1 byte, as the decoder
    nh.insert(QStringLiteral("event[1].value"), 0x1FF);
    body = enc.encodeBody(QStringLiteral("nmshlth"), nh, {}, &err);
    CHECK(body.toHex() == "02000107270fff", "count, id, 1-byte value, unknown id, 1-byte value: " + body.toHex());

    // ---- the Packet Maker form --------------------------------------------------------------
    const QVector<QByteArray> &oct = frames.value(QStringLiteral("linfo/371"));
    if (!oct.isEmpty()) {
        const QByteArray real = oct.first();
        PacketMakerDialog d;
        d.loadBuffer(QStringLiteral("@linfo_1_1 2026-10-08T10:13:00 1 ") + QString::fromLatin1(real.toHex(' ')));
        // the editors by their form label
        QHash<QString, QLineEdit *> ed;
        for (QFormLayout *form : d.findChildren<QFormLayout *>())
            for (int r = 0; r < form->rowCount(); ++r) {
                auto *lab = form->itemAt(r, QFormLayout::LabelRole);
                auto *fld = form->itemAt(r, QFormLayout::FieldRole);
                if (!lab || !fld || !qobject_cast<QLabel *>(lab->widget())) continue;
                if (auto *le = qobject_cast<QLineEdit *>(fld->widget()))
                    ed.insert(qobject_cast<QLabel *>(lab->widget())->text(), le);
            }
        QLineEdit *mr = ed.value(QStringLiteral("mr_pressure"));
        QLineEdit *desc = ed.value(QStringLiteral("train_desc"));
        CHECK(mr && mr->property("pmType").toString() == QLatin1String("float"), "a float field gets a number box");
        bool num = false;
        if (mr) mr->text().toDouble(&num);
        CHECK(num, "showing the real frame's value as a number: " + (mr ? mr->text().toUtf8() : QByteArray()));
        CHECK(desc && desc->maxLength() == 8, "a char array's first piece: an 8-character box");
        CHECK(ed.contains(QStringLiteral("speed_margin_warning (2)")), "LINFO's second speed_margin_warning has its own box");

        const PacketBuilder::Result same = d.buildSendForTest(-1);
        CHECK(same.ok && same.frame == real, "the real LINFO, filled into the form, rebuilds byte for byte: " + same.error.toUtf8());
        if (mr) mr->setText(QStringLiteral("2.5"));
        const PacketBuilder::Result edited = d.buildSendForTest(-1);
        QString shown;
        for (const FieldRow &r : edited.decoded) if (r.field.trimmed() == QLatin1String("mr_pressure")) shown = r.value;
        CHECK(edited.ok && shown.startsWith(QLatin1String("2.5")), "mr_pressure typed as 2.5 decodes as " + shown.toUtf8());
    }
    CHECK(!oct.isEmpty(), "fixture: a 371-byte LINFO in replay/");
}
