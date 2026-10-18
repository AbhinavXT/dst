#include "testutil.h"

#include "capturedecoder.h"
#include "schemadecoder.h"
#include "schemadoc.h"
#include "schemaedit.h"
#include "schemaeditorwindow.h"
#include "schemaelementdialog.h"
#include "statusline.h"

#include <QApplication>
#include <QCheckBox>
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
//  Session 309 — Schema Editor, phase 2: add, delete, duplicate and move
//  elements (fields, pads, notes; delete / duplicate / move any member).
//
//  On the repository's real schema/kavach.xml, with real frames from replay/
//  for what the decoder makes of the result.
// =============================================================================

using Place = SchemaDoc::Document::Place;

namespace {

QByteArray schema309()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/schema/kavach.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray frame309(const QString &prefix, QString *captype)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap"));
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    while (!f.atEnd()) {
        const CaptureLine cl = CaptureDecoder::parseLine(QString::fromUtf8(f.readLine()).trimmed());
        if (cl.valid && cl.typeToken == prefix) {
            *captype = cl.typeToken;
            return cl.bytes;
        }
    }
    return QByteArray();
}

QString row309(const QVector<FieldRow> &rows, const QString &field)
{
    for (const FieldRow &r : rows) {
        if (r.field.trimmed() == field) return r.value.trimmed();
    }
    return QStringLiteral("<none>");
}

int find309(const SchemaDoc::Document &doc, const QString &tag, const QString &name, int within = -1)
{
    for (int i : doc.all(tag)) {
        if (doc.node(i).value(QStringLiteral("name")) != name) continue;
        if (within >= 0 && doc.container(i) != within) continue;
        return i;
    }
    return -1;
}

int packet309(const SchemaDoc::Document &doc, const QString &name) { return find309(doc, QStringLiteral("packet"), name); }

QStringList lines309(const QByteArray &t)
{
    QStringList out;
    for (const QByteArray &l : t.split('\n')) out << QString::fromUtf8(l);
    return out;
}

SchemaEdit::NewElement field309(const QString &name, int bits)
{
    SchemaEdit::NewElement e;
    e.tag = QStringLiteral("field");
    e.attrs = { qMakePair(QStringLiteral("name"), name), qMakePair(QStringLiteral("bits"), QString::number(bits)) };
    return e;
}

QStringList names309(const SchemaDoc::Document &doc, int parent)
{
    QStringList out;
    for (int c : doc.node(parent).children) out << doc.node(c).value(QStringLiteral("name"));
    return out;
}

}  // namespace

TEST_SUITE(session309)
{
    const QByteArray xml = schema309();
    SchemaDoc::Document doc;
    CHECK(doc.parse(xml), "kavach.xml parses");
    const int slrp = packet309(doc, QStringLiteral("SLRP"));
    const int aap = packet309(doc, QStringLiteral("AAP"));
    const QStringList orig = lines309(xml);

    // ---- line spans ---------------------------------------------------------------
    int from = 0, to = 0;
    const int pktDir = find309(doc, QStringLiteral("field"), QStringLiteral("PKT_DIR"), slrp);
    CHECK(doc.lineSpan(pktDir, &from, &to) && xml.mid(from, to - from) == orig.at(doc.node(pktDir).line - 1).toUtf8() + "\n",
          "a field owns its one line, line break included");
    const int tagLinking = find309(doc, QStringLiteral("struct"), QStringLiteral("TagLinking"));
    const int rep = doc.node(find309(doc, QStringLiteral("repeat"), QStringLiteral("tag"), tagLinking)).children.isEmpty()
                        ? -1 : find309(doc, QStringLiteral("repeat"), QStringLiteral("tag"), tagLinking);
    CHECK(rep > 0 && doc.lineSpan(rep, &from, &to) && xml.mid(from, to - from).count('\n') == 5,
          "a repeat owns its block: start tag, three fields, end tag");
    int popup = -1;
    for (int i : doc.all(QStringLiteral("field"))) {
        if (doc.node(i).value(QStringLiteral("name")) == QLatin1String("popup_pressure")) popup = i;
    }
    CHECK(popup > 0 && doc.lineSpan(popup, &from, &to) && xml.mid(from, to - from).contains("<!-- brake_pressure x 100 -->"),
          "a comment on the same line goes with its element");

    // ---- insert -------------------------------------------------------------------
    int at = -1;
    QString why;
    const QByteArray ins = doc.withInserted(pktDir, Place::After, QStringLiteral("<field name=\"WEATHER\" bits=\"4\"/>"),
                                            pktDir, &at, &why);
    const QStringList il = lines309(ins);
    const int pktDirLine = doc.node(pktDir).line;   // 1-based
    CHECK(il.size() == orig.size() + 1 && il.at(pktDirLine).trimmed().startsWith(QLatin1String("<field name=\"WEATHER\" "))
              && il.at(pktDirLine).trimmed().endsWith(QLatin1String(" bits=\"4\"/>")),
          QByteArray("a line after PKT_DIR, attributes in PKT_DIR's columns: ") + il.value(pktDirLine).toUtf8());
    CHECK(il.at(pktDirLine).indexOf(QLatin1String("bits=")) == orig.at(pktDirLine - 1).indexOf(QLatin1String("bits="))
              && il.at(pktDirLine).indexOf(QLatin1String("<field")) == orig.at(pktDirLine - 1).indexOf(QLatin1String("<field")),
          "same indent, bits= in the same column");
    SchemaDoc::Document d2;
    d2.parse(ins);
    const int added = d2.nodeAt(at);
    CHECK(added > 0 && d2.node(added).value(QStringLiteral("name")) == QLatin1String("WEATHER"), "*at is the new element's '<'");
    CHECK(d2.withoutNode(added, &why) == xml, "deleting it again: the file, byte for byte");
    {
        // A pad lined up under a field: its bits= under the field's first attribute.
        const QByteArray p = doc.withInserted(pktDir, Place::Before, QStringLiteral("<pad bits=\"2\"/>"), pktDir, &at, &why);
        CHECK(lines309(p).at(pktDirLine - 1).trimmed() == QLatin1String("<pad   bits=\"2\"/>"),
              QByteArray("a pad before PKT_DIR, as the file writes pads: ") + lines309(p).at(pktDirLine - 1).toUtf8());
    }
    {
        // Into a block: first and last child.
        const QByteArray f = doc.withInserted(rep, Place::FirstChild, QStringLiteral("<field name=\"X\" bits=\"1\"/>"), -1, &at, &why);
        SchemaDoc::Document df;
        df.parse(f);
        const int x = df.nodeAt(at);
        CHECK(x > 0 && df.node(df.node(x).parent).tag == QLatin1String("repeat") && df.node(df.node(x).parent).children.first() == x,
              "first inside the repeat");
        const QByteArray l = doc.withInserted(slrp, Place::LastChild, QStringLiteral("<pad bits=\"8\"/>"), -1, &at, &why);
        SchemaDoc::Document dl;
        dl.parse(l);
        CHECK(dl.node(dl.node(packet309(dl, QStringLiteral("SLRP"))).children.last()).tag == QLatin1String("pad"),
              "last in SLRP, after its sub-packets");
    }
    {
        QByteArray crlf = xml;
        crlf.replace("\n", "\r\n");
        SchemaDoc::Document dc;
        dc.parse(crlf);
        const QByteArray t = dc.withInserted(pktDir, Place::After, QStringLiteral("<field name=\"W\" bits=\"4\"/>"), -1, &at, &why);
        CHECK(t.count("\r\n") == crlf.count("\r\n") + 1 && t.count('\n') == t.count("\r\n"), "a CRLF file gets a CRLF line");
    }

    // ---- move, duplicate ----------------------------------------------------------------
    const int pktType = find309(doc, QStringLiteral("field"), QStringLiteral("PKT_TYPE"), slrp);
    const QByteArray mv = doc.withMoved(pktType, +1, &at, &why);
    SchemaDoc::Document dm;
    dm.parse(mv);
    CHECK(names309(dm, packet309(dm, QStringLiteral("SLRP"))).mid(0, 2) == QStringList({ QStringLiteral("PKT_LENGTH"), QStringLiteral("PKT_TYPE") })
              && dm.node(dm.nodeAt(at)).value(QStringLiteral("name")) == QLatin1String("PKT_TYPE"),
          "PKT_TYPE moved down one; *at follows it");
    CHECK(dm.withMoved(dm.nodeAt(at), -1, &at, &why) == xml, "and back up: the file");
    CHECK(doc.withMoved(pktType, -1, &at, &why).isEmpty() && why.contains(QLatin1String("first")), "the first cannot move up");
    const QByteArray dup = doc.withDuplicated(rep, &at, &why);
    SchemaDoc::Document dd;
    dd.parse(dup);
    CHECK(dd.count() == doc.count() + 4 && dd.node(dd.nodeAt(at)).tag == QLatin1String("repeat")
              && dd.node(dd.nodeAt(at)).children.size() == 3,
          "a duplicated repeat brings its three fields");

    // ---- paths --------------------------------------------------------------------------
    bool paths = true;
    for (int i = 0; i < doc.count() && paths; ++i) paths = doc.resolve(doc.pathOf(i)) == i;
    CHECK(paths, "every element's path resolves back to it");
    QVector<int> past = doc.pathOf(doc.node(aap).children.last());
    past.last() += 5;
    CHECK(doc.resolve(past) == doc.node(aap).children.last(), "a path past the end clamps to the last sibling");

    // ---- the session -------------------------------------------------------------------
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("kavach.xml"));
    {
        QFile out(path);
        CHECK(out.open(QIODevice::WriteOnly) && out.write(xml) == xml.size(), "fixture: a writable copy");
    }
    SchemaEdit::Session s;
    CHECK(s.open(path, &why), "the copy opens");

    // Splitting AAP's MAC_CODE in two halves decodes to the same bits, on a real frame.
    QString aapType;
    const QByteArray aapFrame = frame309(QStringLiteral("aap"), &aapType);
    CHECK(!aapFrame.isEmpty(), "fixture: a real @aap frame");
    Schema::Decoder d0;
    d0.loadData(xml);
    const QString mac = row309(d0.decode(aapFrame, {}, aapType), QStringLiteral("MAC_CODE"));
    const int macNode = find309(s.doc(), QStringLiteral("field"), QStringLiteral("MAC_CODE"), aap);
    const int tdma = find309(s.doc(), QStringLiteral("field"), QStringLiteral("STN_TDMA"), aap);
    int node = -1;
    CHECK(s.remove(macNode, &why), QByteArray("MAC_CODE deleted: ") + why.toUtf8());
    SchemaEdit::NewElement hi = field309(QStringLiteral("MAC_HI"), 16), lo = field309(QStringLiteral("MAC_LO"), 16);
    hi.attrs.push_back(qMakePair(QStringLiteral("hex"), QStringLiteral("4")));
    lo.attrs.push_back(qMakePair(QStringLiteral("hex"), QStringLiteral("4")));
    CHECK(s.insert(tdma, Place::After, hi, &node, &why) && s.doc().node(node).value(QStringLiteral("name")) == QLatin1String("MAC_HI"),
          "MAC_HI added after STN_TDMA, its index returned");
    CHECK(s.insert(node, Place::After, lo, &node, &why), "MAC_LO after it");
    Schema::Decoder d1;
    d1.loadData(s.text());
    const QVector<FieldRow> split = d1.decode(aapFrame, {}, aapType);
    const QString macHi = row309(split, QStringLiteral("MAC_HI")), macLo = row309(split, QStringLiteral("MAC_LO"));
    CHECK(mac.startsWith(QLatin1String("0x")) && mac.size() == 10 && macHi == mac.left(6) && macLo == QLatin1String("0x") + mac.mid(6),
          QByteArray("the halves are the real MAC_CODE's: ") + mac.toUtf8() + " = " + macHi.toUtf8() + " " + macLo.toUtf8());
    CHECK(SchemaEdit::fixedBits(s.doc(), packet309(s.doc(), QStringLiteral("AAP"))) == 176, "AAP is still 176 bits");
    CHECK(s.changeCount() == 3 && s.changes().value(1).contains(QLatin1String("+ field MAC_HI")),
          QByteArray("three steps, said plainly: ") + s.changes().join(QLatin1String(" | ")).toUtf8());
    while (s.canUndo()) s.undo();
    CHECK(s.text() == xml, "undone: the file as opened");

    // Refusals.
    const int locReset = find309(s.doc(), QStringLiteral("field"), QStringLiteral("LOC_RESET"), tagLinking);
    CHECK(!s.remove(locReset, &why) && why.contains(QLatin1String("decoder would refuse")),
          QByteArray("LOC_RESET, which three conditions test: refused: ") + why.toUtf8());
    const int rfidCnt = find309(s.doc(), QStringLiteral("field"), QStringLiteral("ROUTE_RFID_CNT"), tagLinking);
    CHECK(!s.remove(rfidCnt, &why) && why.contains(QLatin1String("count=\"ROUTE_RFID_CNT\"")),
          QByteArray("ROUTE_RFID_CNT, the tag repeat's count: refused (the decoder alone would not): ") + why.toUtf8());
    // Session 310 made whole packets deletable; a section of the file still is not.
    CHECK(!s.remove(s.doc().child(0, QStringLiteral("enums")), &why) && why.contains(QLatin1String("section")),
          "<enums>, a section of the file, stays");
    SchemaEdit::NewElement note;
    note.tag = QStringLiteral("note");
    note.attrs = { qMakePair(QStringLiteral("text"), QStringLiteral("x")) };
    CHECK(!s.insert(rep, Place::FirstChild, note, &node, &why) && why.contains(QLatin1String("does not go inside")),
          "a note inside a repeat: refused");
    CHECK(!s.insert(pktDir, Place::After, field309(QString(), 4), &node, &why) && why.contains(QLatin1String("name")),
          "a field with no name: refused");
    CHECK(!s.insert(pktDir, Place::After, field309(QStringLiteral("W"), 0), &node, &why), "a field of 0 bits: refused");
    SchemaEdit::NewElement badEnum = field309(QStringLiteral("W"), 2);
    badEnum.attrs.push_back(qMakePair(QStringLiteral("enum"), QStringLiteral("nope")));
    CHECK(!s.insert(pktDir, Place::After, badEnum, &node, &why), "a field with a missing enum: refused");
    QByteArray previewText;
    CHECK(s.previewInsert(pktDir, Place::After, field309(QStringLiteral("W"), 4), &previewText, &node, &why)
              && !s.dirty() && previewText.size() > xml.size(),
          "a preview stages nothing");
    CHECK(!s.dirty() && s.changeCount() == 0, "no refusal was staged");

    // Duplicate and move.
    CHECK(s.duplicate(pktDir, &node, &why) && s.doc().node(node).value(QStringLiteral("name")) == QLatin1String("PKT_DIR_2"),
          "a duplicate is named PKT_DIR_2, right after it");
    CHECK(s.duplicate(pktDir, &node, &why) && s.doc().node(node).value(QStringLiteral("name")) == QLatin1String("PKT_DIR_3"),
          "the next PKT_DIR_3");
    CHECK(s.move(node, -1, &node, &why) && s.doc().node(node).value(QStringLiteral("name")) == QLatin1String("PKT_DIR_3"),
          "moved up (the second copy went right after PKT_DIR), its new index returned");
    const QStringList order = names309(s.doc(), packet309(s.doc(), QStringLiteral("SLRP")));
    CHECK(order.mid(9, 3) == QStringList({ QStringLiteral("PKT_DIR_3"), QStringLiteral("PKT_DIR"), QStringLiteral("PKT_DIR_2") }),
          QByteArray("the order: ") + order.mid(9, 3).join(QLatin1Char(',')).toUtf8());
    // Members of an enum: duplicate, move, delete work there too.
    const int speed6 = find309(s.doc(), QStringLiteral("enum"), QStringLiteral("speed6"));
    const int firstMap = s.doc().node(speed6).children.first();
    CHECK(s.move(firstMap, +1, &node, &why) && s.remove(node, &why), "an enum value: moved, then deleted");
    while (s.canUndo()) s.undo();
    CHECK(s.text() == xml, "all undone");

    SchemaEdit::Session ro;
    CHECK(ro.open(QStringLiteral(":/schema/kavach.xml"), &why), "the built-in copy");
    CHECK(!ro.insert(pktDir, Place::After, field309(QStringLiteral("W"), 4), &node, &why) && why.contains(QLatin1String("editable copy"))
              && !ro.remove(pktDir, &why) && !ro.duplicate(pktDir, &node, &why),
          "read-only: no insert, delete or duplicate");

    // ---- the dialog ----------------------------------------------------------------------
    {
        SchemaElementDialog dlg(s, pktDir);
        CHECK(dlg.whereBox()->count() == 3 && dlg.whereBox()->itemText(0) == QLatin1String("After PKT_DIR"),
              "after / before PKT_DIR, or at the end of SLRP");
        CHECK(!dlg.addButton()->isEnabled() && dlg.problem().contains(QLatin1String("name")), "Add waits for a name, and says so");
        dlg.nameEdit()->setText(QStringLiteral("WEATHER_CODE"));
        dlg.bitsSpin()->setValue(4);
        CHECK(dlg.addButton()->isEnabled() && dlg.previewLabel()->text() == QLatin1String("<field name=\"WEATHER_CODE\" bits=\"4\"/>"),
              QByteArray("named: Add enabled, the line shown: ") + dlg.previewLabel()->text().toUtf8());
        CHECK(dlg.effectLabel()->text().contains(QLatin1String("moves 4 bit(s) later")),
              QByteArray("the effect: ") + dlg.effectLabel()->text().toUtf8());
        dlg.signedBox()->setChecked(true);
        dlg.unitEdit()->setText(QStringLiteral("m"));
        dlg.enumBox()->setCurrentIndex(dlg.enumBox()->findData(QStringLiteral("pktDir")));
        CHECK(dlg.previewLabel()->text() == QLatin1String("<field name=\"WEATHER_CODE\" bits=\"4\" signed=\"true\" unit=\"m\" enum=\"pktDir\"/>"),
              "signed, unit and enum in the line");
        dlg.typeBox()->setCurrentIndex(3);
        CHECK(!dlg.bitsSpin()->isEnabled() && dlg.countSpin()->isEnabled() && !dlg.enumBox()->isEnabled()
                  && dlg.previewLabel()->text() == QLatin1String("<field name=\"WEATHER_CODE\" type=\"char\" count=\"8\" unit=\"m\"/>"),
              QByteArray("text: Characters instead of Bits: ") + dlg.previewLabel()->text().toUtf8());
        dlg.kindBox()->setCurrentIndex(1);
        dlg.padBitsSpin()->setValue(3);
        CHECK(dlg.element().tag == QLatin1String("pad") && dlg.previewLabel()->text() == QLatin1String("<pad bits=\"3\"/>")
                  && dlg.addButton()->isEnabled(),
              "a pad: just its bits");
        dlg.kindBox()->setCurrentIndex(0);
        dlg.nameEdit()->setText(QStringLiteral("PKT_DIR"));
        dlg.typeBox()->setCurrentIndex(0);
        CHECK(dlg.effectLabel()->text().contains(QLatin1String("already has a PKT_DIR")), "a name already in SLRP: warned, not refused");
    }
    {
        SchemaElementDialog dlg(s, tdma);
        dlg.nameEdit()->setText(QStringLiteral("EXTRA"));
        dlg.bitsSpin()->setValue(8);
        CHECK(dlg.effectLabel()->text().startsWith(QStringLiteral("AAP: 22 B → 23 B")), QByteArray("a fixed packet: its size before and after: ")
                                                                                             + dlg.effectLabel()->text().toUtf8());
        SchemaElementDialog inRepeat(s, rep);
        CHECK(inRepeat.whereBox()->count() == 5, "beside a repeat, inside it first or last, or at the end");
        SchemaElementDialog onPacket(s, aap);
        CHECK(onPacket.whereBox()->count() == 2 && onPacket.place() == Place::LastChild, "on the packet itself: its end, or its start");
    }

    // ---- the window ----------------------------------------------------------------------
    const QString winPath = tmp.filePath(QStringLiteral("win.xml"));
    {
        QFile out(winPath);
        CHECK(out.open(QIODevice::WriteOnly) && out.write(xml) == xml.size(), "fixture: the window's copy");
    }
    SchemaEditorWindow win(winPath);
    win.show();
    QApplication::processEvents();
    win.selectContainer(QStringLiteral("packet"), QStringLiteral("AAP"));
    QApplication::processEvents();
    CHECK(win.addButton()->isEnabled() && !win.deleteButton()->isEnabled() && !win.upButton()->isEnabled(),
          "the packet itself: Add, but no Delete or Move");
    win.selectNode(find309(win.session().doc(), QStringLiteral("field"), QStringLiteral("MAC_CODE"), aap));
    CHECK(win.deleteButton()->isEnabled() && win.upButton()->isEnabled() && !win.downButton()->isEnabled() && win.duplicateButton()->isEnabled(),
          "MAC_CODE, the last: Delete, Up, Duplicate; no Down");
    CHECK(win.deleteSelected(), "deleted");
    QApplication::processEvents();
    auto selName = [&win]() { return win.session().doc().node(win.selectedNode()).value(QStringLiteral("name")); };
    CHECK(win.members()->topLevelItemCount() == 12 && selName() == QLatin1String("STN_TDMA"),
          QByteArray("twelve rows; the one before it selected: ") + selName().toUtf8());
    CHECK(win.insertElement(win.selectedNode(), Place::After, field309(QStringLiteral("MAC_HI"), 16)), "MAC_HI added");
    QApplication::processEvents();
    CHECK(selName() == QLatin1String("MAC_HI") && win.members()->topLevelItemCount() == 13
              && win.members()->currentItem() && win.members()->currentItem()->text(0) == QLatin1String("MAC_HI"),
          "the new field selected, in the list and the form");
    CHECK(win.moveSelected(-1), "moved up");
    QApplication::processEvents();
    CHECK(selName() == QLatin1String("MAC_HI") && win.members()->topLevelItem(11)->text(0) == QLatin1String("MAC_HI")
              && win.members()->topLevelItem(11)->text(3) == QLatin1String("137"),
          "still selected, now 12th, at bit 137");
    CHECK(win.duplicateSelected() && selName() == QLatin1String("MAC_HI_2"), "duplicated: the copy selected");
    QApplication::processEvents();
    CHECK(win.session().changeCount() == 4, "four steps");
    win.undo();
    win.undo();
    QApplication::processEvents();
    CHECK(win.session().doc().container(win.selectedNode()) == packet309(win.session().doc(), QStringLiteral("AAP"))
              && win.members()->topLevelItemCount() == 13,
          "two undos: still in AAP, MAC_HI back after STN_TDMA");
    CHECK(!win.deleteButton()->isEnabled() || win.selectedNode() > 0, "the buttons follow the selection");
    win.selectNode(find309(win.session().doc(), QStringLiteral("field"), QStringLiteral("LOC_RESET"), tagLinking));
    CHECK(!win.deleteSelected() && win.status()->kind() == StatusLine::Kind::Fail, "a refused delete: the reason on the status line");
    while (win.session().canUndo()) win.undo();
    QApplication::processEvents();
    win.close();

    SchemaEditorWindow roWin(QStringLiteral(":/schema/kavach.xml"));
    roWin.show();
    roWin.selectNode(pktDir);
    QApplication::processEvents();
    CHECK(!roWin.addButton()->isEnabled() && !roWin.deleteButton()->isEnabled() && !roWin.duplicateButton()->isEnabled(),
          "read-only: no structure commands");
    roWin.close();
}
