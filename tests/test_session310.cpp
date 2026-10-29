#include "testutil.h"

#include "capturedecoder.h"
#include "schemacontainerdialog.h"
#include "schemadecoder.h"
#include "schemadoc.h"
#include "schemaedit.h"
#include "schemaeditorwindow.h"
#include "statusline.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

// =============================================================================
//  Session 310 — Schema Editor, phase 3: whole packets, structs, enums, flag
//  and event tables and CRC definitions: new, duplicate, move, delete; a
//  struct registered as a sub-packet case.
//
//  On the repository's real schema/kavach.xml, with real frames from replay/.
// =============================================================================

using Place = SchemaDoc::Document::Place;

namespace {

// LF, as the repository stores it: a Windows checkout (core.autocrlf) has
// CRLF, and the checks below spell lines with "\n". CRLF files are
// checked on purpose where it matters (session 309, session 311).
QByteArray schema310()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/schema/kavach.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll().replace("\r\n", "\n") : QByteArray();
}

QVector<QByteArray> frames310(const QString &token, int max)
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

int named310(const SchemaDoc::Document &doc, const QString &tag, const QString &name)
{
    for (int i : doc.all(tag)) {
        if (SchemaEdit::isContainer(doc, i) && doc.node(i).value(QStringLiteral("name")) == name) return i;
    }
    return -1;
}

QStringList packets310(const SchemaDoc::Document &doc)
{
    QStringList out;
    for (int p : doc.all(QStringLiteral("packet"))) out << doc.node(p).value(QStringLiteral("name"));
    return out;
}

bool sameRows310(const QByteArray &a, const QByteArray &b, const QVector<QByteArray> &frames, const QString &captype)
{
    Schema::Decoder da, db;
    if (!da.loadData(a) || !db.loadData(b) || frames.isEmpty()) return false;
    for (const QByteArray &f : frames) {
        const QVector<FieldRow> ra = da.decode(f, {}, captype), rb = db.decode(f, {}, captype);
        if (ra.size() != rb.size()) return false;
        for (int i = 0; i < ra.size(); ++i) {
            if (ra.at(i).field != rb.at(i).field || ra.at(i).value != rb.at(i).value) return false;
        }
    }
    return true;
}

}  // namespace

TEST_SUITE(session310)
{
    const QByteArray xml = schema310();
    SchemaDoc::Document doc;
    CHECK(doc.parse(xml), "kavach.xml parses");
    CHECK(SchemaEdit::clashes(doc).isEmpty(), "the shipped schema has no clashes");
    const int aap = named310(doc, QStringLiteral("packet"), QStringLiteral("AAP"));
    const int aep = named310(doc, QStringLiteral("packet"), QStringLiteral("AEP"));
    CHECK(SchemaEdit::isContainer(doc, aap) && !SchemaEdit::isContainer(doc, doc.node(aap).children.first())
              && !SchemaEdit::isContainer(doc, doc.child(0, QStringLiteral("enums"))),
          "a packet is a container; its field is not; <enums> is a section, not one");

    // ---- blocks: a container with the comment above it ---------------------------
    int from = 0, to = 0;
    CHECK(doc.blockSpan(aap, &from, &to) && xml.mid(from, to - from).startsWith("  <!-- Access/Allotment Packet (AAP)")
              && xml.mid(from, to - from).trimmed().endsWith("</packet>"),
          "AAP's block starts at its comment and ends at </packet>");
    QString why;
    const QByteArray noAap = doc.withoutBlock(aap, &why);
    SchemaDoc::Document d1;
    CHECK(d1.parse(noAap) && named310(d1, QStringLiteral("packet"), QStringLiteral("AAP")) < 0 && !noAap.contains("Access/Allotment Packet"),
          "deleting the block: AAP and its comment gone");
    CHECK(noAap.count("\n\n\n") == xml.count("\n\n\n") && noAap.count("\n\n") == xml.count("\n\n") - 1,
          "and the blank line before it goes too: no doubled blank line left behind");
    int at = -1;
    const QByteArray swapped = doc.withMovedBlock(aep, aap, false, &at, &why);
    SchemaDoc::Document d2;
    d2.parse(swapped);
    CHECK(packets310(d2).mid(1, 2) == QStringList({ QStringLiteral("AEP"), QStringLiteral("AAP") })
              && d2.node(d2.nodeAt(at)).value(QStringLiteral("name")) == QLatin1String("AEP")
              && swapped.indexOf("Additional Emergency Packet") < swapped.indexOf("Access/Allotment Packet"),
          "AEP moved before AAP, its comment with it");
    CHECK(d2.withMovedBlock(d2.nodeAt(at), named310(d2, QStringLiteral("packet"), QStringLiteral("AAP")), true, &at, &why) == xml,
          "and back: the file, byte for byte");

    // ---- the session ----------------------------------------------------------------
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("kavach.xml"));
    {
        QFile out(path);
        CHECK(out.open(QIODevice::WriteOnly) && out.write(xml) == xml.size(), "fixture: a writable copy");
    }
    SchemaEdit::Session s;
    CHECK(s.open(path, &why), "the copy opens");

    // A new struct, decoded as SLRP sub-packet type 8.
    CHECK(SchemaEdit::freeCaseType(s.doc(), QStringLiteral("SLRP")) == 8, "SLRP's next free sub-packet type is 8");
    SchemaEdit::NewContainer st;
    st.tag = QStringLiteral("struct");
    st.name = QStringLiteral("WeatherProfile");
    st.casePacket = QStringLiteral("SLRP");
    st.caseType = 8;
    int node = -1;
    CHECK(s.addContainer(st, &node, &why), QByteArray("WeatherProfile added: ") + why.toUtf8());
    const SchemaDoc::Document &sd = s.doc();
    CHECK(node > 0 && sd.node(node).tag == QLatin1String("struct") && sd.node(node).value(QStringLiteral("name")) == QLatin1String("WeatherProfile")
              && sd.node(node).children.isEmpty(),
          "the new, empty struct is selected by its index");
    CHECK(sd.node(sd.node(node).parent).children.indexOf(node) == sd.node(sd.node(node).parent).children.indexOf(named310(sd, QStringLiteral("struct"), QStringLiteral("TSR"))) + 1,
          "right after the last struct, TSR");
    CHECK(SchemaEdit::caseUses(sd, QStringLiteral("WeatherProfile")) == QStringList{ QStringLiteral("SLRP type 8") }, "SLRP decodes type 8 with it");
    CHECK(s.text().contains("\n\n  <struct name=\"WeatherProfile\">\n  </struct>\n"), "a blank line, the struct, its end tag, at the structs' indent");
    CHECK(s.changeCount() == 1, "one step for the struct and its case");
    CHECK(sameRows310(xml, s.text(), frames310(QStringLiteral("slrp"), 200), QStringLiteral("slrp")),
          "200 real @slrp frames decode the same (none carries a type 8)");
    // Its first field goes inside, one level in.
    SchemaEdit::NewElement f;
    f.tag = QStringLiteral("field");
    f.attrs = { qMakePair(QStringLiteral("name"), QStringLiteral("WEATHER")), qMakePair(QStringLiteral("bits"), QStringLiteral("4")) };
    CHECK(s.insert(node, Place::LastChild, f, &node, &why) && s.text().contains("\n    <field name=\"WEATHER\" bits=\"4\"/>\n  </struct>"),
          "its first field, indented inside it");

    // Refused.
    SchemaEdit::NewContainer bad = st;
    CHECK(!s.previewContainer(bad, nullptr, nullptr, &why) && why.contains(QLatin1String("already")), "the same struct name again");
    bad.name = QStringLiteral("Other");
    bad.caseType = 5;
    CHECK(!s.previewContainer(bad, nullptr, nullptr, &why) && why.contains(QLatin1String("type 5")),
          QByteArray("SLRP type 5 is TagLinking's: refused: ") + why.toUtf8());
    bad.name = QString();
    CHECK(!s.previewContainer(bad, nullptr, nullptr, &why) && why.contains(QLatin1String("name")), "no name");

    // A new packet decodes real bytes.
    SchemaEdit::NewContainer pk;
    pk.tag = QStringLiteral("packet");
    pk.name = QStringLiteral("WEATHER");
    pk.attrs = { qMakePair(QStringLiteral("match"), QStringLiteral("captype==weather")),
                 qMakePair(QStringLiteral("wire"), QStringLiteral("lsb-first")),
                 qMakePair(QStringLiteral("trailer_bytes"), QStringLiteral("0")) };
    CHECK(s.addContainer(pk, &node, &why), QByteArray("WEATHER added: ") + why.toUtf8());
    SchemaEdit::NewElement first;
    first.tag = QStringLiteral("field");
    first.attrs = { qMakePair(QStringLiteral("name"), QStringLiteral("first_byte")), qMakePair(QStringLiteral("bits"), QStringLiteral("8")) };
    CHECK(s.insert(node, Place::LastChild, first, &node, &why), "its field");
    const QVector<QByteArray> aapFrames = frames310(QStringLiteral("aap"), 1);
    Schema::Decoder dw;
    CHECK(dw.loadData(s.text()) && dw.packetCount() == 32, "32 packets now");
    const QVector<FieldRow> rows = aapFrames.isEmpty() ? QVector<FieldRow>() : dw.decode(aapFrames.first(), {}, QStringLiteral("weather"));
    bool found = false;
    for (const FieldRow &r : rows) {
        if (r.field.trimmed() == QLatin1String("first_byte")) found = r.value.trimmed() == QString::number(quint8(aapFrames.first().at(0)));
    }
    CHECK(found, "a real frame's bytes under captype weather: first_byte is its first byte");
    SchemaEdit::NewContainer clash = pk;
    clash.name = QStringLiteral("WEATHER2");
    clash.attrs[0].second = QStringLiteral("captype==slrp");
    CHECK(!s.previewContainer(clash, nullptr, nullptr, &why) && why.contains(QLatin1String("captype slrp")),
          QByteArray("a second packet on captype slrp: refused: ") + why.toUtf8());
    clash.attrs[0].second = QStringLiteral("captype==");
    CHECK(!s.previewContainer(clash, nullptr, nullptr, &why), "no capture token: refused");

    // An enum, a map in it, a field using it.
    SchemaEdit::NewContainer en;
    en.tag = QStringLiteral("enum");
    en.name = QStringLiteral("weatherCode");
    CHECK(s.addContainer(en, &node, &why) && s.doc().node(s.doc().node(node).parent).tag == QLatin1String("enums"),
          "weatherCode added, inside <enums>");
    SchemaEdit::NewElement m;
    m.tag = QStringLiteral("map");
    m.attrs = { qMakePair(QStringLiteral("v"), QStringLiteral("0")), qMakePair(QStringLiteral("label"), QStringLiteral("%v (clear)")) };
    CHECK(s.insert(node, Place::LastChild, m, &node, &why), "a value in it");
    const int weatherField = [&]() {
        for (int i : s.doc().all(QStringLiteral("field"))) {
            if (s.doc().node(i).value(QStringLiteral("name")) == QLatin1String("WEATHER")) return i;
        }
        return -1;
    }();
    CHECK(s.setAttribute(weatherField, QStringLiteral("enum"), QStringLiteral("weatherCode"), &why), "WEATHER uses it");
    const int enumNode = named310(s.doc(), QStringLiteral("enum"), QStringLiteral("weatherCode"));
    CHECK(!s.remove(enumNode, &why) && why.contains(QLatin1String("no such enum")), QByteArray("deleting a used enum: refused: ") + why.toUtf8());

    // A CRC definition: one line, in the others' columns.
    SchemaEdit::NewContainer crc;
    crc.tag = QStringLiteral("crc");
    crc.name = QStringLiteral("anxx_crc32");
    crc.attrs = { qMakePair(QStringLiteral("type"), QStringLiteral("crc32")), qMakePair(QStringLiteral("poly"), QStringLiteral("0x04C11DB7")),
                  qMakePair(QStringLiteral("init"), QStringLiteral("0xFFFFFFFF")), qMakePair(QStringLiteral("reflect_in"), QStringLiteral("true")),
                  qMakePair(QStringLiteral("reflect_out"), QStringLiteral("true")), qMakePair(QStringLiteral("xor_out"), QStringLiteral("0xFFFFFFFF")),
                  qMakePair(QStringLiteral("trailer_bytes"), QStringLiteral("4")) };
    CHECK(s.addContainer(crc, &node, &why) && s.doc().node(s.doc().node(node).parent).tag == QLatin1String("crcs")
              && s.doc().node(node).selfClosing,
          "the CRC definition, one empty tag in <crcs>");
    const int weatherPacket = named310(s.doc(), QStringLiteral("packet"), QStringLiteral("WEATHER"));
    CHECK(s.setAttribute(weatherPacket, QStringLiteral("crc"), QStringLiteral("anxx_crc32"), &why), "WEATHER's crc= can name it");

    // Delete: refused while referenced, allowed once not.
    const int wp = named310(s.doc(), QStringLiteral("struct"), QStringLiteral("WeatherProfile"));
    CHECK(!s.remove(wp, &why) && why.contains(QLatin1String("no such struct")), QByteArray("a struct a case still names: refused: ") + why.toUtf8());
    int theCase = -1;
    for (int c : s.doc().all(QStringLiteral("case"))) {
        if (s.doc().node(c).value(QStringLiteral("struct")) == QLatin1String("WeatherProfile")) theCase = c;
    }
    CHECK(s.remove(theCase, &why) && s.remove(named310(s.doc(), QStringLiteral("struct"), QStringLiteral("WeatherProfile")), &why),
          "its case, then the struct");
    CHECK(s.registerCase(QStringLiteral("TSR"), QStringLiteral("SLRP"), 8, &node, &why)
              && SchemaEdit::caseUses(s.doc(), QStringLiteral("TSR")).size() == 2,
          "a second case for an existing struct (type 8 → TSR)");
    CHECK(!s.registerCase(QStringLiteral("TSR"), QStringLiteral("SLRP"), 8, &node, &why), "type 8 again: refused");
    CHECK(!s.registerCase(QStringLiteral("TSR"), QStringLiteral("AAP"), 0, &node, &why) && why.contains(QLatin1String("no sub-packets")),
          "AAP has no sub-packets");
    while (s.canUndo()) s.undo();
    CHECK(s.text() == xml, "all undone");

    // Move a packet over the structs, delete one, duplicate.
    const int locoSos = named310(s.doc(), QStringLiteral("packet"), QStringLiteral("LOCO_SOS"));
    CHECK(s.move(locoSos, -1, &node, &why), "LOCO_SOS (first after the structs) up");
    QStringList order = packets310(s.doc());
    CHECK(order.indexOf(QStringLiteral("LOCO_SOS")) == order.indexOf(QStringLiteral("DMI")) - 1
              && s.doc().node(node).value(QStringLiteral("name")) == QLatin1String("LOCO_SOS"),
          "it jumps the structs to before DMI");
    const int slrp = named310(s.doc(), QStringLiteral("packet"), QStringLiteral("SLRP"));
    CHECK(!s.move(slrp, -1, &node, &why) && why.contains(QLatin1String("first packet")), "SLRP is the first packet");
    s.undo();
    CHECK(s.remove(named310(s.doc(), QStringLiteral("packet"), QStringLiteral("AEP")), &why)
              && packets310(s.doc()).size() == 30
              && s.text().count("Additional Emergency Packet") == xml.count("Additional Emergency Packet") - 1,
          "AEP deleted with its comment");
    s.undo();
    CHECK(s.duplicate(named310(s.doc(), QStringLiteral("packet"), QStringLiteral("AAP")), &node, &why)
              && s.doc().node(node).value(QStringLiteral("name")) == QLatin1String("AAP_2")
              && s.doc().node(node).value(QStringLiteral("match")) == QLatin1String("captype==aap_2"),
          "AAP copied as AAP_2, answering captype aap_2 (not a clash)");
    CHECK(sameRows310(xml, s.text(), frames310(QStringLiteral("aap"), 50), QStringLiteral("aap")), "real @aap frames: still AAP's rows");
    CHECK(s.duplicate(named310(s.doc(), QStringLiteral("struct"), QStringLiteral("TagLinking")), &node, &why)
              && s.doc().node(node).value(QStringLiteral("name")) == QLatin1String("TagLinking_2")
              && SchemaEdit::caseUses(s.doc(), QStringLiteral("TagLinking_2")).isEmpty(),
          "a struct copied as TagLinking_2, decoding nothing yet");
    while (s.canUndo()) s.undo();
    CHECK(!s.remove(doc.child(0, QStringLiteral("enums")), &why), "<enums> itself stays");

    // ---- the New dialog ---------------------------------------------------------------
    {
        SchemaContainerDialog dlg(s, QStringLiteral("packet"));
        CHECK(!dlg.addButton()->isEnabled(), "Add waits for a name");
        dlg.nameEdit()->setText(QStringLiteral("WEATHER"));
        CHECK(dlg.capturesEdit()->text() == QLatin1String("weather") && dlg.addButton()->isEnabled(), "the captures follow the name");
        CHECK(dlg.previewLabel()->text().startsWith(QLatin1String("<packet name=\"WEATHER\" match=\"captype==weather\" wire=\"lsb-first\" trailer_bytes=\"0\">"))
                  && dlg.previewLabel()->text().endsWith(QLatin1String("</packet>")),
              QByteArray("the block as written: ") + dlg.previewLabel()->text().toUtf8());
        dlg.capturesEdit()->setText(QStringLiteral("slrp"));
        dlg.capturesEdit()->textEdited(QStringLiteral("slrp"));
        CHECK(!dlg.addButton()->isEnabled() && dlg.problem().contains(QLatin1String("captype slrp")), "captures that SLRP answers: refused");
        dlg.nameEdit()->setText(QStringLiteral("WEATHER2"));
        CHECK(dlg.capturesEdit()->text() == QLatin1String("slrp"), "once typed into, the captures stay");

        dlg.kindBox()->setCurrentIndex(dlg.kindBox()->findData(QStringLiteral("struct")));
        dlg.nameEdit()->setText(QStringLiteral("WeatherProfile"));
        dlg.casePacketBox()->setCurrentIndex(1);
        CHECK(dlg.caseTypeSpin()->value() == 8 && dlg.previewLabel()->text().contains(QLatin1String("<case type=\"8\" struct=\"WeatherProfile\"/>")),
              "a struct: SLRP's free type 8 offered, the case line shown");
        dlg.nameEdit()->setText(QStringLiteral("TSR"));
        CHECK(!dlg.addButton()->isEnabled() && dlg.problem().contains(QLatin1String("already")), "a struct name in use: refused");

        dlg.kindBox()->setCurrentIndex(dlg.kindBox()->findData(QStringLiteral("crc")));
        dlg.nameEdit()->setText(QStringLiteral("anxx"));
        CHECK(dlg.addButton()->isEnabled() && dlg.previewLabel()->text().startsWith(QLatin1String("<crc name=\"anxx\""))
                  && dlg.previewLabel()->text().endsWith(QLatin1String("trailer_bytes=\"4\"/>")),
              QByteArray("a CRC definition: one line: ") + dlg.previewLabel()->text().toUtf8());
    }

    // ---- the window ------------------------------------------------------------------
    const QString winPath = tmp.filePath(QStringLiteral("win.xml"));
    {
        QFile out(winPath);
        CHECK(out.open(QIODevice::WriteOnly) && out.write(xml) == xml.size(), "fixture: the window's copy");
    }
    SchemaEditorWindow win(winPath);
    win.show();
    QApplication::processEvents();
    auto category = [&win](const QString &prefix) {
        for (int i = 0; i < win.outline()->topLevelItemCount(); ++i) {
            if (win.outline()->topLevelItem(i)->text(0).startsWith(prefix)) return win.outline()->topLevelItem(i)->text(0);
        }
        return QString();
    };
    win.selectContainer(QStringLiteral("packet"), QStringLiteral("SLRP"));
    QApplication::processEvents();
    CHECK(win.newButton()->isEnabled() && !win.containerUpButton()->isEnabled() && win.containerDownButton()->isEnabled()
              && win.containerDeleteButton()->isEnabled() && !win.subpacketButton()->isVisible(),
          "SLRP: New, Down, Delete; not Up (first); no sub-packet command on a packet");
    CHECK(win.moveContainer(+1), "SLRP down");
    QApplication::processEvents();
    const SchemaDoc::Document &wd = win.session().doc();
    CHECK(packets310(wd).mid(0, 2) == QStringList({ QStringLiteral("AAP"), QStringLiteral("SLRP") })
              && wd.node(win.selectedNode()).value(QStringLiteral("name")) == QLatin1String("SLRP")
              && win.outline()->currentItem() && win.outline()->currentItem()->text(0) == QLatin1String("SLRP"),
          "SLRP second, still selected in the outline");
    SchemaEdit::NewContainer wst;
    wst.tag = QStringLiteral("struct");
    wst.name = QStringLiteral("WeatherProfile");
    CHECK(win.addContainer(wst), "a struct from the window");
    QApplication::processEvents();
    CHECK(category(QStringLiteral("Structs")) == QLatin1String("Structs (9)")
              && win.session().doc().node(win.selectedNode()).value(QStringLiteral("name")) == QLatin1String("WeatherProfile"),
          "Structs (9), the new one selected");
    CHECK(win.subpacketButton()->isVisible() && win.addButton()->isEnabled(), "on a struct: Use as a sub-packet…, and Add for its first field");
    CHECK(win.registerStruct(QStringLiteral("SLRP"), 8), "registered as SLRP type 8");
    QApplication::processEvents();
    CHECK(win.session().doc().node(win.selectedNode()).value(QStringLiteral("name")) == QLatin1String("WeatherProfile"),
          "still on the struct");
    CHECK(win.deleteContainer(false) == false && win.status()->kind() == StatusLine::Kind::Fail,
          "deleting it while SLRP's case names it: refused, said why");
    win.selectContainer(QStringLiteral("packet"), QStringLiteral("AEP"));
    CHECK(win.deleteContainer(false), "AEP deleted");
    QApplication::processEvents();
    CHECK(category(QStringLiteral("Packets")) == QLatin1String("Packets (30)") && win.selectedNode() > 0
              && SchemaEdit::isContainer(win.session().doc(), win.selectedNode()),
          "Packets (30); another packet selected");
    while (win.session().canUndo()) win.undo();
    QApplication::processEvents();
    CHECK(!win.session().dirty() && category(QStringLiteral("Packets")) == QLatin1String("Packets (31)"), "all undone");
    win.close();

    SchemaEditorWindow ro(QStringLiteral(":/schema/kavach.xml"));
    ro.show();
    QApplication::processEvents();
    CHECK(!ro.newButton()->isEnabled() && !ro.containerDeleteButton()->isEnabled() && !ro.containerDuplicateButton()->isEnabled(),
          "read-only: no New, Delete or Duplicate");
    ro.close();
}
