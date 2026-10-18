#include "testutil.h"

#include "capturedecoder.h"
#include "schemadecoder.h"
#include "schemadoc.h"
#include "schemaedit.h"
#include "schemaeditorwindow.h"
#include "schemapastedialog.h"
#include "statusline.h"

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

#include <functional>

// =============================================================================
//  Session 311 — Schema Editor, phase 4: enum values, flags and events typed
//  into a grid or pasted; a member's own comment goes when it is deleted; a
//  member moves into or out of a condition, repeat or group; a missing
//  <enums> (<crcs>, ...) section is made.
//
//  On the repository's real schema/kavach.xml, with real frames from replay/.
// =============================================================================

using Place = SchemaDoc::Document::Place;

namespace {

// LF, as the repository stores it: a Windows checkout (core.autocrlf) has
// CRLF, and the checks below spell lines with "\n". CRLF files are
// checked on purpose where it matters (session 309, session 311).
QByteArray schema311()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/schema/kavach.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll().replace("\r\n", "\n") : QByteArray();
}

QVector<QByteArray> frames311(const QString &token, int max)
{
    QVector<QByteArray> out;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd() && out.size() < max) {
        const CaptureLine cl = CaptureDecoder::parseLine(QString::fromUtf8(f.readLine()).trimmed());
        if (cl.valid && cl.typeToken == token) out << cl.bytes;
    }
    return out;
}

int named311(const SchemaDoc::Document &doc, const QString &tag, const QString &name)
{
    for (int i : doc.all(tag)) {
        if (doc.node(i).value(QStringLiteral("name")) == name) return i;
    }
    return -1;
}

// The member `name` inside the container `owner`.
int member311(const SchemaDoc::Document &doc, const QString &ownerTag, const QString &owner, const QString &name)
{
    const int c = named311(doc, ownerTag, owner);
    for (int i = c + 1; c >= 0 && i < doc.count() && doc.container(i) == c; ++i) {
        if (doc.node(i).value(QStringLiteral("name")) == name) return i;
    }
    return -1;
}

// The enum value `v` of `enumName`.
int map311(const SchemaDoc::Document &doc, const QString &enumName, const QString &v)
{
    const int e = named311(doc, QStringLiteral("enum"), enumName);
    if (e < 0) return -1;
    for (int c : doc.node(e).children) {
        if (doc.node(c).value(QStringLiteral("v")) == v) return c;
    }
    return -1;
}

// A row's value in a decode, or a null string.
QString row311(const QVector<FieldRow> &rows, const QString &field)
{
    for (const FieldRow &r : rows) {
        if (r.field.trimmed() == field) return r.value.trimmed();
    }
    return QString();
}

QVector<FieldRow> decode311(const QByteArray &schema, const QByteArray &frame, const QString &captype)
{
    Schema::Decoder d;
    return d.loadData(schema) ? d.decode(frame, {}, captype) : QVector<FieldRow>();
}

bool writeFile311(const QString &path, const QByteArray &bytes)
{
    QFile out(path);
    return out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size();
}

}  // namespace

TEST_SUITE(session311)
{
    const QByteArray xml = schema311();
    SchemaDoc::Document doc;
    CHECK(doc.parse(xml), "kavach.xml parses");
    CHECK(SchemaEdit::entryProblems(doc).isEmpty(),
          QByteArray("the shipped schema: no value named twice, no range never shown, no event id twice: ")
              + SchemaEdit::entryProblems(doc).join(QStringLiteral("; ")).toUtf8());
    QTemporaryDir tmp;
    QString why;

    // ---- a member's own comment goes with it; a section label stays ------------------
    {
        const int threats = member311(doc, QStringLiteral("packet"), QStringLiteral("SOS_SNAPSHOT"), QStringLiteral("src_threats"));
        const int whistle = member311(doc, QStringLiteral("packet"), QStringLiteral("DMI"), QStringLiteral("lc.auto_whistle"));
        CHECK(threats > 0 && doc.ownCommentStart(threats) >= 0, "src_threats: the two comment lines above it are its own (</repeat> follows)");
        CHECK(whistle > 0 && doc.ownCommentStart(whistle) < 0, "lc.auto_whistle: the comment above heads LC_GATE_DISP_INFO's run of fields");
        const QString path = tmp.filePath(QStringLiteral("comment.xml"));
        CHECK(writeFile311(path, xml), "fixture");
        SchemaEdit::Session s;
        CHECK(s.open(path, &why), "opens");
        CHECK(s.remove(threats, &why) && !s.text().contains("a plain byte: <flags> is not walked")
                  && s.undoText().endsWith(QLatin1String("and the comment above it")),
              QByteArray("src_threats deleted with its comment: ") + s.undoText().toUtf8());
        s.undo();
        CHECK(s.text() == xml, "undone: the file back");
        CHECK(s.remove(whistle, &why) && s.text().contains("LC_GATE_DISP_INFO (body bytes 0-9)")
                  && !s.undoText().contains(QLatin1String("comment")),
              "lc.auto_whistle deleted, the section label above it stays");
        s.undo();
    }

    // ---- moving between levels ---------------------------------------------------------
    {
        const QString path = tmp.filePath(QStringLiteral("move.xml"));
        CHECK(writeFile311(path, xml), "fixture");
        SchemaEdit::Session s;
        CHECK(s.open(path, &why), "opens");
        int mac = member311(s.doc(), QStringLiteral("packet"), QStringLiteral("LSRP"), QStringLiteral("MAC_CODE"));
        int anchor = -1;
        Place place = Place::After;
        CHECK(SchemaEdit::planMoveIn(s.doc(), mac, &anchor, &place, &why) && place == Place::LastChild
                  && s.doc().node(anchor).value(QStringLiteral("test")) == QLatin1String("FRAME_NUM & 1 == 0"),
              "LSRP MAC_CODE: In goes into the even-frame condition above it, last");
        CHECK(!SchemaEdit::planMoveOut(s.doc(), mac, nullptr, nullptr, &why) && why.contains(QLatin1String("not inside")),
              "and it is not inside anything to move out of");
        int moved = -1;
        CHECK(s.moveIn(mac, &moved, &why) && s.doc().node(s.doc().node(moved).parent).tag == QLatin1String("when")
                  && s.text().contains("      <note name=\"Health_Note\" text=\"not updated on even frames (stale)\"/>\n"
                                       "      <field name=\"MAC_CODE\"            bits=\"32\" hex=\"8\"/>\n    </when>"),
              QByteArray("moved in, one level deeper: ") + why.toUtf8());
        // Real @lsrp frames: MAC_CODE is now read only on even frame numbers.
        const QVector<QByteArray> lsrp = frames311(QStringLiteral("lsrp"), 11);
        int even = 0, odd = 0, right = 0;
        for (const QByteArray &f : lsrp) {
            const QVector<FieldRow> before = decode311(xml, f, QStringLiteral("lsrp"));
            const QVector<FieldRow> after = decode311(s.text(), f, QStringLiteral("lsrp"));
            const qint64 fn = row311(before, QStringLiteral("FRAME_NUM")).section(QLatin1Char(' '), 0, 0).toLongLong();
            const bool isEven = (fn & 1) == 0;
            (isEven ? even : odd)++;
            if (isEven ? row311(after, QStringLiteral("MAC_CODE")) == row311(before, QStringLiteral("MAC_CODE"))
                       : row311(after, QStringLiteral("MAC_CODE")).isNull()) {
                ++right;
            }
        }
        CHECK(!lsrp.isEmpty() && right == lsrp.size(),
              QByteArray("11 real @lsrp frames: MAC_CODE shown on the even ones only (")
                  + QByteArray::number(even) + " even, " + QByteArray::number(odd) + " odd, " + QByteArray::number(right) + " right)");
        CHECK(SchemaEdit::planMoveOut(s.doc(), moved, &anchor, &place, &why) && place == Place::After, "Out: after the condition (it was last in it)");
        CHECK(s.moveOut(moved, &moved, &why) && s.text() == xml, "moved out again: the file, byte for byte");
        s.discard();

        // A repeat's first field out (above the repeat), then in again (first).
        int a = member311(s.doc(), QStringLiteral("packet"), QStringLiteral("DOP1"), QStringLiteral("a"));
        CHECK(SchemaEdit::planMoveOut(s.doc(), a, &anchor, &place, &why) && place == Place::BeforeBlock, "DOP1 pin's first field: Out goes above the repeat");
        CHECK(s.moveOut(a, &moved, &why) && s.doc().node(s.doc().node(moved).parent).tag == QLatin1String("packet")
                  && s.text().contains("<packet name=\"DOP1\" match=\"captype==dop1\" wire=\"lsb-first\" trailer_bytes=\"0\">\n"
                                       "    <field name=\"a\"      bits=\"1\"/>\n    <repeat name=\"pin\""),
              QByteArray("out of the repeat, at the packet's indent: ") + why.toUtf8());
        const QVector<QByteArray> dop = frames311(QStringLiteral("dop1"), 1);
        const QVector<FieldRow> once = dop.isEmpty() ? QVector<FieldRow>() : decode311(s.text(), dop.first(), QStringLiteral("dop1"));
        CHECK(!dop.isEmpty() && !row311(once, QStringLiteral("a")).isNull() && row311(once, QStringLiteral("a")).toInt() == (quint8(dop.first().at(0)) & 1),
              "a real @dop1 frame: a is read once, before the pins, from bit 0");
        CHECK(SchemaEdit::planMoveIn(s.doc(), moved, &anchor, &place, &why) && place == Place::FirstChild, "In: into the repeat below it, first");
        CHECK(s.moveIn(moved, &moved, &why) && s.text() == xml, "and back: byte for byte");
        CHECK(!SchemaEdit::movableInto(QStringLiteral("note"), QStringLiteral("repeat"))
                  && !SchemaEdit::movableInto(QStringLiteral("when"), QStringLiteral("group"))
                  && SchemaEdit::movableInto(QStringLiteral("flags"), QStringLiteral("when")),
              "a repeat or group reads only fields and pads; a condition reads any member");
        const int note = member311(s.doc(), QStringLiteral("packet"), QStringLiteral("LSRP"), QStringLiteral("Health_Note"));
        CHECK(!s.moveIn(note, &moved, &why) && s.text() == xml, QByteArray("the note has no condition next to it inside its condition: ") + why.toUtf8());
    }

    // ---- the grid: cells of real enums, flags and events --------------------------------
    const QString gridPath = tmp.filePath(QStringLiteral("grid.xml"));
    CHECK(writeFile311(gridPath, xml), "fixture");
    SchemaEdit::Session s;
    CHECK(s.open(gridPath, &why), "opens");
    CHECK(SchemaEdit::entryColumns(QStringLiteral("enum")).size() == 6 && SchemaEdit::entryColumns(QStringLiteral("flagtable")).first() == QLatin1String("bit")
              && !SchemaEdit::entryEditable(QStringLiteral("bit")) && SchemaEdit::entryEditable(QStringLiteral("label")),
          "columns: an enum's six; a flag's bit, which cannot be typed");

    // The mode in a real ARP frame, relabelled.
    const QVector<QByteArray> arp = frames311(QStringLiteral("arp"), 1);
    const QVector<FieldRow> arpRows = arp.isEmpty() ? QVector<FieldRow>() : decode311(xml, arp.first(), QStringLiteral("arp"));
    const QString mode = row311(arpRows, QStringLiteral("LOCO_MODE"));
    const QString modeValue = mode.section(QLatin1Char(' '), 0, 0);
    const int modeMap = map311(s.doc(), QStringLiteral("locoMode"), modeValue);
    CHECK(modeMap > 0, QByteArray("a real @arp frame's LOCO_MODE: ") + mode.toUtf8());
    CHECK(s.setEntryCell(modeMap, QStringLiteral("label"), QStringLiteral("%v (Bench)"), &why)
              && row311(decode311(s.text(), arp.first(), QStringLiteral("arp")), QStringLiteral("LOCO_MODE")) == modeValue + QStringLiteral(" (Bench)"),
          QByteArray("its label typed: the frame now reads ") + modeValue.toUtf8() + " (Bench)");
    CHECK(s.undoText().startsWith(QStringLiteral("locoMode › %1: label").arg(modeValue)), QByteArray("undo says which: ") + s.undoText().toUtf8());
    s.undo();

    // pktDir: 3 into a range, back, "otherwise", a repeat refused.
    int spare = map311(s.doc(), QStringLiteral("pktDir"), QStringLiteral("3"));
    CHECK(s.setEntryCell(spare, QStringLiteral("to"), QStringLiteral("5"), &why)
              && s.text().contains("<map from=\"3\" to=\"5\" label=\"%v (Spare)\"/>"),
          QByteArray("pktDir 3: To 5 makes it the range 3–5, written from/to first: ") + why.toUtf8());
    CHECK(s.doc().node(spare).has(QStringLiteral("from")) && SchemaEdit::entryText(s.doc(), spare, QStringLiteral("value")).isEmpty(),
          "the same element, now a range");
    CHECK(!s.setEntryCell(spare, QStringLiteral("from"), QStringLiteral("9"), &why) && why.contains(QLatin1String("after")),
          "From 9 after To 5: refused");
    CHECK(s.setEntryCell(spare, QStringLiteral("value"), QStringLiteral("3"), &why) && s.text().contains("<map v=\"3\" label=\"%v (Spare)\"/>"),
          "Value 3: one value again");
    CHECK(s.setEntryCell(spare, QStringLiteral("value"), QStringLiteral("otherwise"), &why)
              && s.doc().node(spare).value(QStringLiteral("default")) == QLatin1String("true")
              && SchemaEdit::entryText(s.doc(), spare, QStringLiteral("value")) == QLatin1String("otherwise"),
          "\"otherwise\": the catch-all");
    while (s.canUndo()) s.undo();
    CHECK(!s.setEntryCell(spare, QStringLiteral("value"), QStringLiteral("2"), &why) && why.contains(QLatin1String("value 2 named twice")),
          QByteArray("3 retyped as 2, which 2 (Reverse) names: refused: ") + why.toUtf8());
    CHECK(!s.setEntryCell(spare, QStringLiteral("value"), QString(), &why), "an empty value: refused");

    // New rows: what is missing is said; the next value is offered.
    const int pktDir = named311(s.doc(), QStringLiteral("enum"), QStringLiteral("pktDir"));
    CHECK(SchemaEdit::nextValue(s.doc(), pktDir) == QLatin1String("4"), "pktDir's next value is 4");
    QHash<QString, QString> cells;
    cells.insert(QStringLiteral("value"), QStringLiteral("4"));
    CHECK(!s.previewEntry(pktDir, cells, nullptr, &why) && why.contains(QLatin1String("label")), "a value with no label: \"Type its label\"");
    cells.clear();
    cells.insert(QStringLiteral("label"), QStringLiteral("%v (Test)"));
    int node = -1;
    CHECK(s.addEntry(pktDir, cells, &node, &why) && s.text().contains("<map v=\"3\" label=\"%v (Spare)\"/>\n      <map v=\"4\" label=\"%v (Test)\"/>\n    </enum>"),
          QByteArray("a label alone: value 4, after 3, in its columns: ") + why.toUtf8());
    CHECK(s.undoText() == QLatin1String("pktDir: + 4 %v (Test)"), QByteArray("undo text: ") + s.undoText().toUtf8());
    // An enum that ends with "otherwise": a new value goes above it.
    int withDefault = -1;
    for (int e : s.doc().all(QStringLiteral("enum"))) {
        const QVector<int> &k = s.doc().node(e).children;
        if (!k.isEmpty() && s.doc().node(k.last()).value(QStringLiteral("default")) == QLatin1String("true")) {
            withDefault = e;
            break;
        }
    }
    cells.clear();
    cells.insert(QStringLiteral("label"), QStringLiteral("added"));
    CHECK(withDefault > 0 && s.addEntry(withDefault, cells, &node, &why)
              && s.doc().node(withDefault).children.at(s.doc().node(withDefault).children.size() - 2) == node,
          "an enum ending in \"otherwise\": the new value goes just above it");
    s.undo();

    // A flag and an event.
    const int g1 = named311(s.doc(), QStringLiteral("flagtable"), QStringLiteral("locoHealthG1"));
    int fewest = -1;
    CHECK(SchemaEdit::flagReaders(s.doc(), QStringLiteral("locoHealthG1"), &fewest).size() == 1 && fewest == 6,
          "locoHealthG1 is read by LSRP's 6-bit flags");
    cells.clear();
    CHECK(!s.previewEntry(g1, cells, nullptr, &why) && why.contains(QLatin1String("name")), "a flag with no name: refused");
    cells.insert(QStringLiteral("name"), QStringLiteral("B6  SPARE_FAULT"));
    CHECK(s.addEntry(g1, cells, &node, &why) && SchemaEdit::entryText(s.doc(), node, QStringLiteral("bit")) == QLatin1String("6"),
          "a flag added: bit 6");
    CHECK(!s.setEntryCell(node, QStringLiteral("name"), QStringLiteral("  "), &why), "a flag's name cannot be emptied");
    s.undo();
    const int nms = named311(s.doc(), QStringLiteral("eventtable"), QStringLiteral("nmsHealth"));
    const QString nextId = SchemaEdit::nextValue(s.doc(), nms);
    cells.clear();
    cells.insert(QStringLiteral("name"), QStringLiteral("RADIO_3_HEALTH"));
    CHECK(s.addEntry(nms, cells, &node, &why) && s.doc().node(node).value(QStringLiteral("id")) == nextId
              && s.doc().node(node).value(QStringLiteral("bytes")) == QLatin1String("1"),
          QByteArray("an event with a name alone: id ") + nextId.toUtf8() + ", 1 byte");
    CHECK(!s.setEntryCell(node, QStringLiteral("id"), QStringLiteral("1"), &why) && why.contains(QLatin1String("id 1 twice")),
          QByteArray("its id retyped as 1 (RADIO_1_HEALTH's): refused: ") + why.toUtf8());
    CHECK(s.setEntryCell(node, QStringLiteral("signed"), QStringLiteral("true"), &why) && s.doc().node(node).value(QStringLiteral("signed")) == QLatin1String("true"),
          "signed ticked");
    while (s.canUndo()) s.undo();
    CHECK(s.text() == xml, "all undone");
    // Indices as they are now (the pktDir value added above is undone too).
    const int nmsNow = named311(s.doc(), QStringLiteral("eventtable"), QStringLiteral("nmsHealth"));
    const int g1Now = named311(s.doc(), QStringLiteral("flagtable"), QStringLiteral("locoHealthG1"));
    const int pktDirNow = named311(s.doc(), QStringLiteral("enum"), QStringLiteral("pktDir"));
    // A real @nmshlth frame's event renamed.
    const QVector<QByteArray> hl = frames311(QStringLiteral("nmshlth"), 1);
    const int radio1 = [&]() {
        for (int c : s.doc().node(nmsNow).children) {
            if (s.doc().node(c).value(QStringLiteral("name")) == QLatin1String("RADIO_1_HEALTH")) return c;
        }
        return -1;
    }();
    const QVector<FieldRow> hlBefore = hl.isEmpty() ? QVector<FieldRow>() : decode311(xml, hl.first(), QStringLiteral("nmshlth"));
    CHECK(!row311(hlBefore, QStringLiteral("RADIO_1_HEALTH")).isNull(), "a real @nmshlth frame reports RADIO_1_HEALTH");
    CHECK(s.setEntryCell(radio1, QStringLiteral("name"), QStringLiteral("RADIO_A_HEALTH"), &why)
              && !row311(decode311(s.text(), hl.first(), QStringLiteral("nmshlth")), QStringLiteral("RADIO_A_HEALTH")).isNull(),
          QByteArray("renamed in the grid: the frame reports RADIO_A_HEALTH: ") + why.toUtf8());
    s.undo();

    // ---- paste --------------------------------------------------------------------------
    QStringList problems;
    QVector<SchemaEdit::TableRow> rows = SchemaEdit::parseRows(s.doc(), pktDirNow,
        QStringLiteral("0\t%v (Unidentified)\n1 - %v (Forward)\n4  %v (Test)\nnext one\n6..9 reserved\n4 again\n7\notherwise rsvd(%v)\n"),
        false, &problems);
    CHECK(rows.size() == 6, QByteArray("6 rows understood: ") + QByteArray::number(rows.size()));
    CHECK(rows.at(0).skip && rows.at(0).existing > 0, "0 (Unidentified): the same as now, skipped");
    CHECK(rows.at(1).existing > 0 && rows.at(1).note.contains(QString::fromUtf8("%v (Nominal) → %v (Forward)")), QByteArray("1: label changed: ") + rows.at(1).note.toUtf8() + " existing " + QByteArray::number(rows.at(1).existing));
    CHECK(rows.at(2).existing < 0 && rows.at(3).cells.value(QStringLiteral("value")) == QLatin1String("5")
              && rows.at(3).cells.value(QStringLiteral("label")) == QLatin1String("next one"),
          "4 new; a line with no number takes 5");
    CHECK(rows.at(4).cells.value(QStringLiteral("from")) == QLatin1String("6") && rows.at(4).cells.value(QStringLiteral("to")) == QLatin1String("9"), "6..9 a range");
    CHECK(rows.at(5).cells.value(QStringLiteral("value")) == QLatin1String("otherwise"), "otherwise");
    CHECK(problems.size() == 2 && problems.at(0).contains(QLatin1String("value 4 again")) && problems.at(1).contains(QLatin1String("no label")),
          QByteArray("problems: ") + problems.join(QStringLiteral("; ")).toUtf8());
    CHECK(s.applyRows(pktDirNow, rows, false, &why) && s.changeCount() == 1 && s.undoText() == QLatin1String("pktDir: pasted (4 new, 1 changed)"),
          QByteArray("one step: ") + s.undoText().toUtf8() + " " + why.toUtf8());
    CHECK(s.text().contains("<map v=\"1\" label=\"%v (Forward)\"/>") && s.text().contains("<map from=\"6\" to=\"9\" label=\"reserved\"/>")
              && s.text().contains("<map default=\"true\" label=\"rsvd(%v)\"/>\n    </enum>"),
          "written: the label, the range, the catch-all last");
    Schema::Decoder pd;
    CHECK(pd.loadData(s.text()) && pd.enumLabel(QStringLiteral("pktDir"), 7) == QLatin1String("reserved")
              && pd.enumLabel(QStringLiteral("pktDir"), 12) == QLatin1String("rsvd(12)"),
          "the decoder: 7 reserved, 12 rsvd(12)");
    s.undo();
    rows = SchemaEdit::parseRows(s.doc(), pktDirNow, QStringLiteral("Left\nRight\n"), true, &problems);
    CHECK(s.applyRows(pktDirNow, rows, true, &why) && s.doc().node(pktDirNow).children.size() == 2
              && s.text().contains("<enum name=\"pktDir\">\n      <map v=\"0\" label=\"Left\"/>\n      <map v=\"1\" label=\"Right\"/>\n    </enum>"),
          QByteArray("replace: two lines become 0 and 1: ") + why.toUtf8());
    s.undo();
    rows = SchemaEdit::parseRows(s.doc(), g1Now, QStringLiteral("B0  SYSTEM_INTERNAL_FAULT\n6\tB6  NEW\n9\tB7  OTHER\n"), false, &problems);
    CHECK(rows.size() == 3 && rows.at(0).skip && rows.at(1).note == QLatin1String("new, bit 6") && problems.size() == 1
              && problems.first().contains(QLatin1String("says bit 9; it would be bit 7")),
          QByteArray("flags: one already there, two new; a stated bit that disagrees: ") + problems.join(QStringLiteral("; ")).toUtf8()
              + " rows " + QByteArray::number(rows.size()) + " " + (rows.size() > 1 ? rows.at(1).note.toUtf8() : QByteArray()));
    rows = SchemaEdit::parseRows(s.doc(), nmsNow, QStringLiteral("1 RADIO_1_HEALTH 2\n90 TEMP SENSOR 2 signed\n"), false, &problems);
    CHECK(rows.size() == 2 && rows.at(0).note.contains(QString::fromUtf8("bytes 1 → 2")) && rows.at(1).cells.value(QStringLiteral("name")) == QLatin1String("TEMP SENSOR")
              && rows.at(1).cells.value(QStringLiteral("bytes")) == QLatin1String("2") && rows.at(1).cells.value(QStringLiteral("signed")) == QLatin1String("true"),
          QByteArray("events: id 1's bytes changed; 90 TEMP SENSOR, 2 bytes, signed: ") + QByteArray::number(rows.size())
              + (rows.size() > 1 ? " | " + rows.at(0).note.toUtf8() + " | " + rows.at(1).cells.value(QStringLiteral("name")).toUtf8() + " " + rows.at(1).cells.value(QStringLiteral("bytes")).toUtf8() + " " + rows.at(1).cells.value(QStringLiteral("signed")).toUtf8() : QByteArray()));

    // ---- a missing section is made --------------------------------------------------------
    {
        const int from = xml.indexOf("  <eventtables>"), to = xml.indexOf("</eventtables>") + 15;
        const QByteArray noEvents = xml.left(from) + xml.mid(to);
        SchemaEdit::Session ns;
        CHECK(ns.openText(noEvents, tmp.filePath(QStringLiteral("noevents.xml")), &why) && ns.doc().child(0, QStringLiteral("eventtables")) < 0,
              "a schema with no <eventtables>");
        writeFile311(tmp.filePath(QStringLiteral("noevents.xml")), noEvents);
        SchemaEdit::NewContainer et;
        et.tag = QStringLiteral("eventtable");
        et.name = QStringLiteral("nmsHealth");
        CHECK(ns.addContainer(et, &node, &why), QByteArray("an event table added: ") + why.toUtf8());
        const SchemaDoc::Document &nd = ns.doc();
        const int sec = nd.node(node).parent;
        const QVector<int> &top = nd.node(0).children;
        CHECK(nd.node(sec).tag == QLatin1String("eventtables") && top.indexOf(sec) == top.indexOf(nd.child(0, QStringLiteral("crcs"))) + 1,
              "in a new <eventtables>, right after <crcs> (the file's order)");
        CHECK(ns.text().contains("  </crcs>\n\n  <eventtables>\n    <eventtable name=\"nmsHealth\">\n    </eventtable>\n  </eventtables>\n"),
              "written at the sections' indent");
        CHECK(ns.undoText().contains(QLatin1String("new <eventtables> section")), QByteArray("undo says so: ") + ns.undoText().toUtf8());
        CHECK(ns.text().size() > 0 && SchemaEdit::loadProblem(ns.text()).isEmpty(), "the decoder loads it");

        const int ef = xml.indexOf("  <enums>"), et2 = xml.indexOf("</enums>") + 9;
        SchemaEdit::Session ne;
        writeFile311(tmp.filePath(QStringLiteral("noenums.xml")), xml.left(ef) + xml.mid(et2));
        CHECK(ne.open(tmp.filePath(QStringLiteral("noenums.xml")), &why), "a schema with no <enums>");
        SchemaEdit::NewContainer en;
        en.tag = QStringLiteral("enum");
        en.name = QStringLiteral("speed6");
        CHECK(ne.addContainer(en, &node, &why) && ne.doc().node(ne.doc().node(node).parent).tag == QLatin1String("enums")
                  && ne.doc().node(0).children.first() == ne.doc().node(node).parent,
              QByteArray("an enum: <enums> made first in the file: ") + why.toUtf8());
    }

    // ---- a CRLF file (a Windows copy of kavach.xml) stays CRLF -------------------------
    {
        QByteArray crlf = xml;
        crlf.replace("\n", "\r\n");
        SchemaEdit::Session lf, cr;
        CHECK(lf.openText(xml, tmp.filePath(QStringLiteral("lf.xml")), &why) && cr.openText(crlf, tmp.filePath(QStringLiteral("cr.xml")), &why),
              "the same schema, LF and CRLF");
        auto both = [&](const std::function<bool(SchemaEdit::Session &)> &edit) {
            return edit(lf) && edit(cr);
        };
        auto at = [](SchemaEdit::Session &x, const QString &tag, const QString &name) { return named311(x.doc(), tag, name); };
        bool ok = both([&](SchemaEdit::Session &x) {
            int m = -1;
            return x.moveIn(member311(x.doc(), QStringLiteral("packet"), QStringLiteral("LSRP"), QStringLiteral("MAC_CODE")), &m, &why);
        });
        ok = ok && both([&](SchemaEdit::Session &x) {
            return x.remove(member311(x.doc(), QStringLiteral("packet"), QStringLiteral("SOS_SNAPSHOT"), QStringLiteral("src_threats")), &why);
        });
        ok = ok && both([&](SchemaEdit::Session &x) {
            QHash<QString, QString> c;
            c.insert(QStringLiteral("label"), QStringLiteral("%v (Test)"));
            int n = -1;
            return x.addEntry(at(x, QStringLiteral("enum"), QStringLiteral("pktDir")), c, &n, &why);
        });
        ok = ok && both([&](SchemaEdit::Session &x) {
            return x.setEntryCell(map311(x.doc(), QStringLiteral("pktDir"), QStringLiteral("3")), QStringLiteral("value"), QStringLiteral("otherwise"), &why);
        });
        ok = ok && both([&](SchemaEdit::Session &x) {
            const int t = at(x, QStringLiteral("flagtable"), QStringLiteral("locoHealthG1"));
            return x.applyRows(t, SchemaEdit::parseRows(x.doc(), t, QStringLiteral("A\nB\n"), true, nullptr), true, &why);
        });
        CHECK(ok, QByteArray("move in, delete with comment, add, reshape, replace: made on both: ") + why.toUtf8());
        QByteArray back = cr.text();
        CHECK(back.count('\n') == back.count("\r\n") && back.replace("\r\n", "\n") == lf.text(),
              "the CRLF result has only CRLF line ends, and is the LF result line for line");
    }

    // ---- the window --------------------------------------------------------------------
    const QString winPath = tmp.filePath(QStringLiteral("win.xml"));
    CHECK(writeFile311(winPath, xml), "fixture: the window's copy");
    SchemaEditorWindow win(winPath);
    win.show();
    QApplication::processEvents();
    win.selectContainer(QStringLiteral("packet"), QStringLiteral("LSRP"));
    win.selectNode(member311(win.session().doc(), QStringLiteral("packet"), QStringLiteral("LSRP"), QStringLiteral("MAC_CODE")));
    QApplication::processEvents();
    CHECK(win.inButton()->isVisible() && win.inButton()->isEnabled() && win.outButton()->isVisible() && !win.outButton()->isEnabled()
              && !win.rowButton()->isVisible() && win.addButton()->isVisible(),
          "a packet's member: In and Out (In possible, Out not); no Row / Paste");
    CHECK(win.moveInSelected() && win.session().doc().node(win.session().doc().node(win.selectedNode()).parent).tag == QLatin1String("when"),
          "In from the window: MAC_CODE selected inside the condition");
    QApplication::processEvents();
    CHECK(win.outButton()->isEnabled() && win.moveOutSelected() && !win.session().dirty(), "Out again: nothing left to save");

    win.selectContainer(QStringLiteral("enum"), QStringLiteral("pktDir"));
    QApplication::processEvents();
    QTreeWidget *grid = win.members();
    CHECK(grid->columnCount() == 6 && grid->headerItem()->text(3) == QLatin1String("Label") && grid->topLevelItemCount() == 5,
          "pktDir: six columns, four values and the blank row");
    QTreeWidgetItem *blank = grid->topLevelItem(4);
    CHECK(blank->text(0) == QLatin1String("4") && blank->data(0, Qt::UserRole + 1).toString().isEmpty() && (blank->flags() & Qt::ItemIsEditable),
          "the blank row offers 4, which is not its value until typed");
    CHECK(win.rowButton()->isVisible() && win.pasteButton()->isVisible() && !win.addButton()->isVisible() && !win.inButton()->isVisible(),
          "a table: ＋ Row and Paste…, not Add, In, Out");
    CHECK(!win.typeNewRow(QStringLiteral("value"), QStringLiteral("7")) && win.pendingRow().value(QStringLiteral("value")) == QLatin1String("7")
              && win.status()->kind() == StatusLine::Kind::Warn,
          "Value 7 typed: kept pending, \"Type its label\" said");
    QApplication::processEvents();
    CHECK(grid->topLevelItem(4)->text(0) == QLatin1String("7"), "the blank row shows the 7 typed");
    CHECK(win.typeNewRow(QStringLiteral("label"), QStringLiteral("%v (Seven)")) && win.pendingRow().isEmpty()
              && win.session().doc().node(win.selectedNode()).value(QStringLiteral("v")) == QLatin1String("7"),
          "then its label: staged as value 7, selected");
    QApplication::processEvents();
    CHECK(grid->topLevelItemCount() == 6, "five values and the blank row");
    // An edit through the grid's own cell (the delegate's commit), then undone.
    QTreeWidgetItem *first = grid->topLevelItem(0);
    first->setText(3, QStringLiteral("%v (Unknown)"));
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(win.session().text().contains("<map v=\"0\" label=\"%v (Unknown)\"/>"), "a label typed into its cell: staged");
    CHECK(win.editEntry(win.session().doc().node(named311(win.session().doc(), QStringLiteral("enum"), QStringLiteral("pktDir"))).children.at(1),
                        QStringLiteral("value"), QStringLiteral("0")) == false
              && win.status()->kind() == StatusLine::Kind::Fail,
          "a value named twice: refused, said why");
    CHECK(win.pasteRows(QStringLiteral("8\tEight\n9\tNine\n"), false) && win.session().undoText() == QLatin1String("pktDir: pasted (2 new)"),
          "pasted from the window: one step");
    while (win.session().canUndo()) win.undo();
    QApplication::processEvents();

    // dmiAlarm: 71 flags, read as 71 bits; a 72nd is never shown.
    win.selectContainer(QStringLiteral("flagtable"), QStringLiteral("dmiAlarm"));
    QApplication::processEvents();
    CHECK(grid->headerItem()->text(0) == QLatin1String("Bit") && grid->topLevelItemCount() == 72, "dmiAlarm: bit and name, 71 flags and the blank row");
    CHECK(win.typeNewRow(QStringLiteral("name"), QStringLiteral("Extra alarm")) && win.status()->kind() == StatusLine::Kind::Warn
              && win.status()->message().contains(QLatin1String("reads 71 bits")),
          QByteArray("a 72nd flag: staged, and said that no <flags> shows it: ") + win.status()->message().toUtf8());
    win.undo();
    QApplication::processEvents();
    win.selectNode(win.session().doc().node(named311(win.session().doc(), QStringLiteral("flagtable"), QStringLiteral("dmiAlarm"))).children.first());
    CHECK(win.deleteSelected() && win.status()->message().contains(QLatin1String("bit below")), "the first flag deleted: the ones after it shift, said");
    win.undo();

    // The paste dialog.
    {
        const int g1w = named311(win.session().doc(), QStringLiteral("flagtable"), QStringLiteral("locoHealthG1"));
        SchemaPasteDialog dlg(win.session(), g1w);
        CHECK(!dlg.okButton()->isEnabled(), "nothing pasted: OK waits");
        dlg.textEdit()->setPlainText(QStringLiteral("B6  A\nB7  B\nB0  SYSTEM_INTERNAL_FAULT\n"));
        CHECK(dlg.previewTree()->topLevelItemCount() == 3 && dlg.okButton()->isEnabled() && dlg.okButton()->text() == QLatin1String("add 2"),
              QByteArray("three rows previewed, \"add 2\": ") + dlg.okButton()->text().toUtf8());
        dlg.replaceBox()->setChecked(true);
        CHECK(dlg.okButton()->text() == QLatin1String("Replace with 3"), "replace: all three new");
    }
    win.close();

    SchemaEditorWindow ro(QStringLiteral(":/schema/kavach.xml"));
    ro.show();
    ro.selectContainer(QStringLiteral("enum"), QStringLiteral("pktDir"));
    QApplication::processEvents();
    CHECK(ro.members()->topLevelItemCount() == 4 && !(ro.members()->topLevelItem(0)->flags() & Qt::ItemIsEditable) && !ro.rowButton()->isEnabled(),
          "read-only: no blank row, no editable cell, no ＋ Row");
    ro.close();
}
