#include "testutil.h"

#include "capturedecoder.h"
#include "fielddocs.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QXmlStreamReader>

// =============================================================================
//  Session 301 — what RDSO's annexures say each field is (the data).
//
//  schema/fielddocs.json carries Annexure C's description of every radio
//  packet field (SLRP, LSRP, AAP, AEP, ARP) and Annexure D's of every RFID
//  tag field, keyed by the names DLConsole shows. The windows use it from the
//  next patches on. Checked here:
//    * the file loads, every entry cites its source, clause and page;
//    * every name it describes is a name the schema really has for that
//      packet (a field, group or repeat), so a typo cannot hide;
//    * the reverse: decoding the real frames in replay/, every field row of
//      those packets has a description, except DLConsole's own rows (frame
//      header, health bits) which the annexures do not define;
//    * the lookup rules (index, suffix, sub-packet header, tag type, arprecv).
// =============================================================================

namespace {

// Names the schema gives each packet: <field>, <group>, <repeat>. SLRP's
// sub-packets are <struct>s, outside any <packet>.
QHash<QString, QSet<QString>> schemaNames()
{
    QHash<QString, QSet<QString>> out;
    QFile file(QStringLiteral(DL_SRC_DIR "/schema/kavach.xml"));
    if (!file.open(QIODevice::ReadOnly)) return out;
    QXmlStreamReader xml(&file);
    QStringList tokens;
    bool inStruct = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString element = xml.name().toString();
            if (element == QLatin1String("packet")) {
                const QString match = xml.attributes().value(QStringLiteral("match")).toString();
                tokens = match.mid(match.indexOf(QLatin1String("==")) + 2).split(QLatin1Char(','));
            } else if (element == QLatin1String("struct")) {
                inStruct = true;
                tokens = QStringList{ QStringLiteral("slrp") };
            } else if (element == QLatin1String("field") || element == QLatin1String("group")
                       || element == QLatin1String("repeat")) {
                for (const QString &token : tokens) {
                    out[token.trimmed()].insert(xml.attributes().value(QStringLiteral("name")).toString());
                }
            }
        } else if (xml.isEndElement()) {
            const QString element = xml.name().toString();
            if (element == QLatin1String("packet") || (element == QLatin1String("struct") && inStruct)) {
                tokens.clear();
                inStruct = false;
            }
        }
    }
    return out;
}

}  // namespace

TEST_SUITE(session301)
{
    const FieldDocs &docs = FieldDocs::instance();
    CHECK(docs.size() >= 190, QByteArray("the built-in descriptions load: ") + QByteArray::number(docs.size()));
    CHECK(docs.sourceDocument(QStringLiteral("C")).contains(QLatin1String("Amdt-10"))
              && docs.sourceDocument(QStringLiteral("D")).contains(QLatin1String("Amdt-7")),
          "the two sources, by amendment");

    // ---- every entry cites, and names something real ---------------------------------
    const QHash<QString, QSet<QString>> schema = schemaNames();
    CHECK(schema.value(QStringLiteral("slrp")).contains(QStringLiteral("speed"))
              && schema.value(QStringLiteral("rfid")).contains(QStringLiteral("tin")),
          "the schema's names, sub-packet structs and groups included");
    // DLConsole rows the decoder adds beside the schema's fields.
    const QSet<QString> decoderRows = { QStringLiteral("CRC"), QStringLiteral("CRC-30"), QStringLiteral("subpkt"),
                                        QStringLiteral("Loco_Health"), QStringLiteral("Latitude"),
                                        QStringLiteral("Longitude"), QStringLiteral("MAC_CODE") };
    QStringList unknownNames, badCitations;
    for (const FieldDoc &doc : docs.docs()) {
        const bool fromC = doc.src == QLatin1String("C");
        if ((fromC && (doc.page < 12 || doc.page > 45)) || (!fromC && (doc.page < 4 || doc.page > 12))
            || doc.clause.isEmpty() || (!fromC && doc.tagTypes.isEmpty() && !doc.names.contains(QStringLiteral("CRC-30")))) {
            badCitations << doc.names.join(QLatin1Char('/'));
        }
        for (const FieldDocPart &part : doc.parts) {
            if (part.field.isEmpty() || part.bits <= 0 || part.text.isEmpty()) {
                badCitations << doc.names.join(QLatin1Char('/')) + QStringLiteral(" (part)");
            }
        }
        for (const QString &packet : doc.packets) {
            for (const QString &name : doc.names) {
                if (!schema.value(packet).contains(name) && !decoderRows.contains(name)) {
                    unknownNames << packet + QLatin1Char(':') + name;
                }
            }
        }
    }
    CHECK(badCitations.isEmpty(), QByteArray("every entry has its clause, a page in its annexure, and its parts: ")
                                      + badCitations.join(QStringLiteral(", ")).toUtf8());
    CHECK(unknownNames.isEmpty(), QByteArray("every name is one the schema has for that packet: ")
                                      + unknownNames.join(QStringLiteral(", ")).toUtf8());

    // ---- the reverse: real frames' rows all have one ---------------------------------
    {
        const QSet<QString> want = { QStringLiteral("slrp"), QStringLiteral("lsrp"), QStringLiteral("arp"),
                                     QStringLiteral("aap"), QStringLiteral("rfid") };
        // Rows the annexures do not define: the capture line, the message
        // header (Annexure G), DLConsole's own notes, the health bits
        // (Annexure G 6.1.24), the reader's own id.
        static const QRegularExpression ownRow(QStringLiteral(
            "^(frame len|loco / ctrl|rtc|seq|type|msg\\..*|loco keeps|start of mission|Health_Note|body|reader_id|B\\d+ .*)$"));
        QHash<QString, int> frames;
        QSet<QString> missing;
        const QStringList caps = QDir(QStringLiteral(DL_SRC_DIR "/replay")).entryList({ QStringLiteral("*.cap") });
        for (const QString &name : caps) {
            QFile file(QStringLiteral(DL_SRC_DIR "/replay/") + name);
            if (!file.open(QIODevice::ReadOnly)) continue;
            QTextStream in(&file);
            while (!in.atEnd()) {
                const CaptureLine line = CaptureDecoder::parseLine(in.readLine());
                if (!want.contains(line.typeToken) || frames.value(line.typeToken) >= 300) continue;
                frames[line.typeToken] += 1;
                QHash<QString, qint64> raw;
                const QVector<FieldRow> rows = CaptureDecoder::describe(line, nullptr, 0, &raw);
                const int tagType = line.typeToken == QLatin1String("rfid") ? int(raw.value(QStringLiteral("type"))) : 0;
                for (const FieldRow &row : rows) {
                    const QString shown = row.field.trimmed();
                    if (ownRow.match(shown).hasMatch()) {
                        // The capture's own "type" row; the RFID tag's "type" is indented.
                        if (!(shown == QLatin1String("type") && row.field.startsWith(QLatin1Char(' ')))) continue;
                    }
                    // The reader message's own CRC (not the tag's CRC-30).
                    if (line.typeToken == QLatin1String("rfid") && shown == QLatin1String("CRC")) continue;
                    if (!docs.find(line.typeToken, shown, tagType)) {
                        missing.insert(line.typeToken + QLatin1Char(':') + shown);
                    }
                }
            }
        }
        CHECK(frames.value(QStringLiteral("slrp")) > 100 && frames.value(QStringLiteral("lsrp")) > 100
                  && frames.value(QStringLiteral("arp")) > 100 && frames.value(QStringLiteral("aap")) > 10
                  && frames.value(QStringLiteral("rfid")) > 100,
              "real SLRP, LSRP, ARP, AAP and RFID frames from replay/");
        QStringList list = missing.values();
        list.sort();
        CHECK(list.isEmpty(), QByteArray("every field row of those frames has a description; missing: ")
                                  + list.join(QStringLiteral(", ")).toUtf8());
    }

    // ---- the lookup rules ----------------------------------------------------------
    {
        const FieldDoc *speed = docs.find(QStringLiteral("slrp"), QStringLiteral("  speed[3]"));
        CHECK(speed && speed->parts.size() == 3 && speed->parts.at(0).field == QLatin1String("LM_Static_Speed_Distance")
                  && speed->clause == QLatin1String("C.4.2") && speed->page == 22,
              "a repeat row (index and indent ignored): the SSP item's three spec fields, C.4.2 p.22");
        const FieldDoc *health = docs.find(QStringLiteral("lsrp"), QStringLiteral("  Loco_Health (faults 0-5)"));
        CHECK(health && health->parts.first().field == QLatin1String("Loco_Health_Status"), "a \" (…)\" suffix ignored");
        const FieldDoc *sub = docs.find(QStringLiteral("slrp"), QStringLiteral("subpkt[2] StaticSpeedProfile @byte 25"));
        CHECK(sub && sub->parts.first().field == QLatin1String("SUB_PKT_TYPE"), "the sub-packet header row");
        const FieldDoc *stn = docs.find(QStringLiteral("slrp"), QStringLiteral("SOURCE_STN_ID"));
        CHECK(stn && stn->parts.first().field == QLatin1String("SOURCE_STN_ILC_IBS_ID"),
              "DLConsole's name, the annexure's name in the description");
        const FieldDoc *recv = docs.find(QStringLiteral("arprecv"), QStringLiteral("LOCO_RND_NUM_RL"));
        CHECK(recv && recv->clause == QLatin1String("C.4.6"), "a received ARP is an ARP");
        CHECK(!docs.find(QStringLiteral("dmi"), QStringLiteral("TRAIN_SPEED")), "nothing for a packet the annexures do not cover");
        CHECK(!docs.find(QStringLiteral("slrp"), QStringLiteral("NOT_A_FIELD")), "nor for an unknown name");

        const FieldDoc *normal = docs.find(QStringLiteral("rfid"), QStringLiteral("placement"), 9);
        const FieldDoc *lc = docs.find(QStringLiteral("rfid"), QStringLiteral("placement"), 10);
        CHECK(normal && lc && normal != lc && lc->parts.first().text.contains(QLatin1String("G-tag"))
                  && normal->clause == QLatin1String("D 2. Normal Tag") && lc->clause == QLatin1String("D 3. LC Tag"),
              "an RFID field per tag type: placement on an LC tag mentions the G-tag");
        const FieldDoc *tin1 = docs.find(QStringLiteral("rfid"), QStringLiteral("tin_nom"), 12);
        CHECK(tin1 && tin1->parts.first().field == QLatin1String("TIN -1"), "on a junction tag tin_nom is TIN -1");
        const FieldDoc *anyType = docs.find(QStringLiteral("rfid"), QStringLiteral("unique"));
        CHECK(anyType && anyType->tagTypes.contains(9), "tag type unknown: the Normal tag's");
        CHECK(docs.find(QStringLiteral("rfid"), QStringLiteral("gate_id")) != nullptr
                  && !docs.find(QStringLiteral("rfid"), QStringLiteral("gate_id"), 9),
              "gate_id: found without a type (only LC tags have it), not on a Normal tag");

        const QString tip = docs.tooltip(QStringLiteral("slrp"), QStringLiteral("AUTHORIZED_SPEED"));
        CHECK(tip.contains(QLatin1String("<b>AUTHORIZED_SPEED</b>")) && tip.contains(QLatin1String("6 bits"))
                  && tip.contains(QLatin1String("62: 8 Kmph (Configuarble) for auto signal override during night"))
                  && tip.contains(QLatin1String("C.4.2, page 20"))
                  && tip.contains(QLatin1String("RDSO/SPN/196/2020 Version 4.0, Amdt-10")),
              QByteArray("the hover text: name, size, the annexure's words (its typos too), and where: ") + tip.toUtf8());
        const QString noted = docs.tooltip(QStringLiteral("slrp"), QStringLiteral("lc[1]"));
        CHECK(noted.contains(QLatin1String("DLConsole note:")) && noted.contains(QLatin1String("000 B1")),
              "a DLConsole note is marked as ours, and the annexure's text is left as printed");
        CHECK(docs.tooltip(QStringLiteral("slrp"), QStringLiteral("NOT_A_FIELD")).isEmpty(), "no description: no text");
    }

    // ---- the loader refuses nonsense ----------------------------------------------
    {
        FieldDocs empty;
        QString err;
        CHECK(!empty.loadFromJson("{ not json", &err) && !err.isEmpty(), "bad JSON refused, with a reason");
        CHECK(!empty.loadFromJson("{\"docs\": []}", &err) && err.contains(QLatin1String("no descriptions")),
              "an empty file refused");
    }
}
