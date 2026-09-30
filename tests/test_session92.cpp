#include "testutil.h"

#include "cabpanel.h"
#include "capturedecoder.h"
#include "dmipanel.h"
#include "packetbuilder.h"
#include "rejectrules.h"
#include "schema/schemaencoder.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPushButton>

// =============================================================================
//  Session 92: the DMI always draws its target column and signal distance;
//  a Fields panel beside it; the cab dial runs 0-250; the packet maker writes
//  lsb-first packets (checked frame for frame against replay/); an ARP
//  received from the loco's own ID is flagged.
// =============================================================================

namespace {

QImage render92(const DmiState &s)
{
    DmiView v;
    v.setAnnexureColours(true);
    v.resize(800, 600);
    v.setState(s);
    QImage img(800, 600, QImage::Format_ARGB32);
    img.fill(Qt::black);
    v.render(&img);
    return img;
}

int inked92(const QImage &img, const QRect &r)
{
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() + c.green() + c.blue() > 200) ++n;
        }
    return n;
}

// Up to `limit` real frames of each capture type, from replay/.
QHash<QString, QVector<CaptureLine>> corpus92(const QStringList &types, int limit)
{
    QHash<QString, QVector<CaptureLine>> out;
    const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
    for (const QString &f : dir.entryList({ QStringLiteral("*.cap") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        while (!file.atEnd()) {
            const QString l = QString::fromLatin1(file.readLine()).trimmed();
            if (!l.startsWith(QLatin1Char('@'))) continue;
            const CaptureLine c = CaptureDecoder::parseLine(l);
            if (!c.valid || !types.contains(c.typeToken) || out[c.typeToken].size() >= limit) continue;
            out[c.typeToken].append(c);
        }
    }
    return out;
}

// The same frame as a capture line would carry it, re-parsed, so the
// capture layer's own CRC verdict is recomputed for the new bytes.
CaptureLine reparsed(const CaptureLine &c, const QByteArray &bytes)
{
    QString hex;
    for (char b : bytes) hex += QStringLiteral(" %1").arg(uchar(b), 2, 16, QLatin1Char('0')).toUpper();
    return CaptureDecoder::parseLine(QStringLiteral("@%1_%2 %3 1%4").arg(c.typeToken, c.key(),
                                                                          c.rtc.toString(Qt::ISODate), hex));
}

// Every CRC row of a decoded frame says PASS (and there is at least one).
bool crcPasses(const CaptureLine &c, bool *any = nullptr)
{
    bool pass = true, seen = false;
    for (const FieldRow &r : CaptureDecoder::describe(c)) {
        if (r.value.endsWith(QLatin1String("PASS"))) seen = true;
        if (r.value.endsWith(QLatin1String("FAIL"))) { seen = true; pass = false; }
    }
    if (any) *any = seen;
    return pass;
}

}  // namespace

TEST_SUITE(session92)
{
    // ---- 4. cab dial ---------------------------------------------------------------------
    CHECK(kCabDialMaxKmh == 250.0, "the cab view's dial runs to 250 km/h, the loco's top speed");

    // ---- 1 & 3. DMI: target column and signal distance always drawn ------------------------
    {
        DmiState s;
        s.valid = true;                              // aspect 0, target distance 0, no type
        const QImage img = render92(s);
        CHECK(inked92(img, QRect(0, 352, 90, 26)) > 30, "target column: '0000 m' is drawn with no target");
        CHECK(inked92(img, QRect(0, 28, 90, 44)) > 30, "target column: 'Target Distance' is drawn with no target");
        CHECK(inked92(img, QRect(45, 100, 30, 240)) > 30, "target column: the scale lines are drawn with no target");
        CHECK(inked92(img, QRect(575, 360, 220, 26)) > 30, "signal: the distance is drawn with no aspect");
        CHECK(inked92(img, QRect(640, 40, 90, 230)) > 200, "signal: the post and lamps are drawn with no aspect");
    }

    // ---- 6 & 7. the Fields panel ------------------------------------------------------------
    const QHash<QString, QVector<CaptureLine>> frames =
        corpus92({ "dmi", "rfid", "ccsys", "dlsys" }, 400);
    {
        QString line;
        const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
        QFile file(dir.filePath(QStringLiteral("loco_1_1_26062026_162418.cap")));
        if (file.open(QIODevice::ReadOnly)) {
            while (!file.atEnd() && line.isEmpty()) {
                const QString l = QString::fromLatin1(file.readLine()).trimmed();
                if (l.startsWith(QLatin1String("@dmi_"))) line = l;
            }
        }
        DmiWindow w(nullptr);
        w.show();
        bool hasOldButton = false;
        for (QPushButton *b : w.findChildren<QPushButton *>())
            if (b->text().startsWith(QLatin1String("Field sources"))) hasOldButton = true;
        CHECK(!hasOldButton, "'Field sources…' is gone");
        auto *btn = w.findChild<QPushButton *>(QStringLiteral("dmiFieldsButton"));
        CHECK(btn && btn->isCheckable() && !w.fieldsVisible(), "a Fields button, panel closed by default");
        w.observeLine(QStringLiteral("1_1"), line);
        if (btn) btn->setChecked(true);
        QApplication::processEvents();
        const QStringList names = w.fieldNames();
        CHECK(w.fieldsVisible() && names.size() > 40,
              QByteArray("opening it lists the frame's decoded fields (") + QByteArray::number(names.size()) + ")");
        bool hasTarget = false;
        for (const QString &n : names) if (n.trimmed() == QLatin1String("target_distance")) hasTarget = true;
        CHECK(hasTarget, "including target_distance");
        CHECK(!w.findChild<QWidget *>(QStringLiteral("dmiFieldsPane"))->findChildren<QLabel *>().isEmpty(),
              "the old field-source notes sit under the table");
        if (btn) btn->setChecked(false);
        CHECK(!w.fieldsVisible(), "closing it hides the panel");
    }

    // ---- 5. lsb-first packets, frame for frame --------------------------------------------------
    {
        (void)kavachSchema();                              // registers the CRCs
        Schema::Encoder enc;
        CHECK(enc.load(QStringLiteral(":/schema/kavach.xml")), "encoder loads the schema");
        for (const QString &type : { QStringLiteral("dmi"), QStringLiteral("rfid"), QStringLiteral("ccsys"), QStringLiteral("dlsys") }) {
            const QVector<CaptureLine> &list = frames.value(type);
            const Schema::PacketInfo pi = enc.packet(type);
            CHECK(pi.ok && pi.wire == QLatin1String("lsb-first") && pi.unsupported.isEmpty(),
                  QByteArray("@") + type.toLatin1() + " is lsb-first and now buildable");
            int same = 0, sameIgnoringBadCrc = 0, builtCrcOk = 0, total = 0;
            for (const CaptureLine &c : list) {
                ++total;
                const Schema::ParsedPacket pp = enc.parseBody(type, c.bytes);
                if (!pp.ok) continue;
                QString err;
                const QByteArray out = enc.encodeBody(type, pp.header, pp.subs, &err);
                const CaptureLine rebuilt = reparsed(c, out);
                if (crcPasses(rebuilt)) ++builtCrcOk;
                if (out == c.bytes) { ++same; continue; }
                // A corpus frame whose own CRC fails is rebuilt with a correct
                // one; everything else must match.
                if (!crcPasses(c)) {
                    QHash<QString, qint64> a, b;
                    CaptureDecoder::describe(c, nullptr, 0, &a);
                    CaptureDecoder::describe(rebuilt, nullptr, 0, &b);
                    if (a == b && out.size() == c.bytes.size()) ++sameIgnoringBadCrc;
                }
            }
            CHECK(total > 0 && same + sameIgnoringBadCrc == total,
                  QByteArray("@") + type.toLatin1() + ": every real frame rebuilt (" + QByteArray::number(same)
                      + " byte for byte, " + QByteArray::number(sameIgnoringBadCrc) + " with the corpus's failing CRC corrected, of "
                      + QByteArray::number(total) + ")");
            CHECK(builtCrcOk == total, QByteArray("@") + type.toLatin1() + ": every rebuilt frame's CRC passes");
        }
        const Schema::PacketInfo linfo = enc.packet(QStringLiteral("linfo"));
        CHECK(!linfo.unsupported.isEmpty() && linfo.unsupported.join(QLatin1Char(' ')).contains(QLatin1String("typed")),
              "LINFO (float and text fields) is still refused, and says why");

        // Through the packet maker's builder: a real @dmi rebuilt from its fields.
        if (!frames.value(QStringLiteral("dmi")).isEmpty()) {
            const CaptureLine c = frames.value(QStringLiteral("dmi")).at(5);
            const Schema::ParsedPacket pp = enc.parseBody(QStringLiteral("dmi"), c.bytes);
            PacketBuilder pb;
            const PacketBuilder::Result r = pb.build(QStringLiteral("dmi"), pp.header, pp.subs, QByteArray());
            CHECK(r.ok && r.roundTripped && r.frame == c.bytes,
                  QByteArray("the packet maker builds a real @dmi byte for byte: ") + r.error.toUtf8());
            QHash<QString, qint64> h = pp.header;
            h.insert(QStringLiteral("train_speed"), 87);
            const PacketBuilder::Result r2 = pb.build(QStringLiteral("dmi"), h, pp.subs, QByteArray());
            const CaptureLine built = reparsed(c, r2.frame);
            CHECK(r2.ok && dmiStateFromCapture(built).speed == 87 && crcPasses(built) && r2.frame.startsWith(QByteArray::fromHex("AAAA74"))
                      && r2.frame.endsWith(QByteArray::fromHex("BBBB")),
                  "an edited @dmi: the new speed, AA AA 74 .. BB BB, and a CRC that passes");
        }
    }

    // ---- 9. ARP received from our own loco ID -------------------------------------------------------
    {
        const RejectRules &rules = kavachRejectRules();
        auto fires = [&](const QHash<QString, qint64> &v, const QString &type) {
            for (const RejectRules::Finding &f : rules.evaluate(v, type))
                if (f.rule.field == QLatin1String("SOURCE_LOCO_ID") && f.rule.op == QLatin1String("eq_field")) return f.text;
            return QString();
        };
        const QHash<QString, qint64> own{ { QStringLiteral("SOURCE_LOCO_ID"), 1234 }, { QStringLiteral("OWN_LOCO_ID"), 1234 } };
        const QHash<QString, qint64> other{ { QStringLiteral("SOURCE_LOCO_ID"), 99 }, { QStringLiteral("OWN_LOCO_ID"), 1234 } };
        const QHash<QString, qint64> unknown{ { QStringLiteral("SOURCE_LOCO_ID"), 1234 } };
        const QString text = fires(own, QStringLiteral("arprecv"));
        CHECK(!text.isEmpty() && text.contains(QLatin1String("same as OWN_LOCO_ID")),
              QByteArray("an ARP received from our own ID is flagged: ") + text.toUtf8());
        CHECK(fires(other, QStringLiteral("arprecv")).isEmpty(), "one from another loco is not");
        CHECK(fires(own, QStringLiteral("arp")).isEmpty(), "the ARP we SEND carries our own ID by design: not flagged");
        CHECK(fires(unknown, QStringLiteral("arprecv")).isEmpty(), "before the loco is identified: not checked, not flagged");
    }
}
