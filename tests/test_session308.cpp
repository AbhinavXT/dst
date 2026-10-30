#include "testutil.h"

#include "capturedecoder.h"
#include "schemadecoder.h"
#include "schemadoc.h"
#include "schemaedit.h"
#include "schemaeditorwindow.h"
#include "statusline.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTreeWidget>

// =============================================================================
//  Session 308 — Tools ▸ Schema ▸ Schema Editor, phase 1: every element of
//  kavach.xml browsed, and its attributes changed, as staged edits.
//
//  All on the repository's real schema/kavach.xml (a temporary copy where it
//  is written), and real frames from replay/ for what the decoder makes of
//  an edit.
// =============================================================================

namespace {

// LF, as the repository stores it: a Windows checkout (core.autocrlf) has
// CRLF, and the checks below spell lines with "\n". CRLF files are
// checked on purpose where it matters (session 309, session 311).
QByteArray schema308()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/schema/kavach.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll().replace("\r\n", "\n") : QByteArray();
}

QByteArray frame308(const QString &prefix, QString *captype)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap"));
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (!line.startsWith(prefix)) continue;
        const CaptureLine cl = CaptureDecoder::parseLine(line);
        if (!cl.valid) continue;
        *captype = cl.typeToken;
        return cl.bytes;
    }
    return QByteArray();
}

QString row308(const QVector<FieldRow> &rows, const QString &field)
{
    for (const FieldRow &r : rows) {
        if (r.field.trimmed() == field) return r.value.trimmed();
    }
    return QStringLiteral("<none>");
}

// The node index of <tag name="name"> (the first), or -1.
int find308(const SchemaDoc::Document &doc, const QString &tag, const QString &name, int within = -1)
{
    for (int i : doc.all(tag)) {
        if (doc.node(i).value(QStringLiteral("name")) != name) continue;
        if (within >= 0 && doc.container(i) != within) continue;
        return i;
    }
    return -1;
}

// The lines that differ (equal line counts), as "old|new".
QStringList changed308(const QByteArray &a, const QByteArray &b)
{
    const QList<QByteArray> x = a.split('\n'), y = b.split('\n');
    QStringList out;
    if (x.size() != y.size()) return { QStringLiteral("line count %1 → %2").arg(x.size()).arg(y.size()) };
    for (int i = 0; i < x.size(); ++i) {
        if (x.at(i) != y.at(i)) out << QString::fromUtf8(x.at(i)) + QLatin1Char('|') + QString::fromUtf8(y.at(i));
    }
    return out;
}

void walk308(const QDomElement &e, QVector<QDomElement> &out)
{
    out.push_back(e);
    for (QDomElement k = e.firstChildElement(); !k.isNull(); k = k.nextSiblingElement()) walk308(k, out);
}

}  // namespace

TEST_SUITE(session308)
{
    const QByteArray xml = schema308();
    CHECK(!xml.isEmpty(), "the repository's kavach.xml reads");

    // ---- SchemaDoc: the same elements and values as Qt's own parser ----------------
    SchemaDoc::Document doc;
    QString err;
    CHECK(doc.parse(xml, &err), QByteArray("kavach.xml parses: ") + err.toUtf8());
    QDomDocument dom;
    dom.setContent(xml);
    QVector<QDomElement> elements;
    walk308(dom.documentElement(), elements);
    CHECK(doc.count() == elements.size(),
          QByteArray("every element, none extra: ") + QByteArray::number(doc.count()) + " vs " + QByteArray::number(elements.size()));
    bool same = doc.count() == elements.size();
    for (int i = 0; same && i < doc.count(); ++i) {
        const SchemaDoc::Node &n = doc.node(i);
        const QDomNamedNodeMap attrs = elements.at(i).attributes();
        same = n.tag == elements.at(i).tagName() && n.attrs.size() == attrs.count();
        for (const SchemaDoc::Attr &a : n.attrs) {
            if (same && elements.at(i).attribute(a.name) != a.value) {
                same = false;
                std::printf("    differs: line %d <%s %s>: \"%s\" vs \"%s\"\n", n.line, qPrintable(n.tag), qPrintable(a.name),
                            qPrintable(a.value), qPrintable(elements.at(i).attribute(a.name)));
            }
        }
    }
    CHECK(same, "each element's tag, attribute count and every decoded value match QDomDocument's, in order");
    CHECK(doc.all(QStringLiteral("packet")).size() == 38 && doc.all(QStringLiteral("enum")).size() == 65,
          "38 packets and 65 enums (session 420: the seven BIU_* and biuNmtState / biuNmtCs)");
    const int slrp = find308(doc, QStringLiteral("packet"), QStringLiteral("SLRP"));
    CHECK(slrp > 0 && doc.node(slrp).line == 963, "SLRP is at line 963, as in the file");
    const int aap = find308(doc, QStringLiteral("packet"), QStringLiteral("AAP"));
    CHECK(doc.node(aap).comment.contains(QLatin1String("Access/Allotment Packet")), "AAP carries the comment above it");
    const int map0 = doc.node(find308(doc, QStringLiteral("enum"), QStringLiteral("speed6"))).children.first();
    CHECK(doc.container(map0) == find308(doc, QStringLiteral("enum"), QStringLiteral("speed6")),
          "a <map>'s container is its enum, not <enums>");

    // ---- loadData == load ----------------------------------------------------------
    Schema::Decoder fromBytes, fromFile;
    CHECK(fromBytes.loadData(xml) && fromFile.load(QStringLiteral(DL_SRC_DIR "/schema/kavach.xml")),
          "the decoder loads the bytes and the file");
    CHECK(fromBytes.packetCount() == fromFile.packetCount() && fromBytes.structCount() == fromFile.structCount()
              && fromBytes.enumCount() == fromFile.enumCount(),
          "the same packets, structs and enums either way");
    CHECK(!SchemaEdit::loadProblem(xml + "<").isEmpty(), "broken XML: the decoder refuses it");

    // ---- an edit changes those bytes only ------------------------------------------
    const int pktType = find308(doc, QStringLiteral("field"), QStringLiteral("PKT_TYPE"), slrp);
    CHECK(doc.withAttribute(pktType, QStringLiteral("bits"), QStringLiteral("4")) == xml, "the same value: the same file");
    {
        // <field name="PKT_TYPE"        bits="4"/>: the name grows, bits= stays in its column.
        const QStringList d = changed308(xml, doc.withAttribute(pktType, QStringLiteral("name"), QStringLiteral("PKT_TYPE_XY")));
        CHECK(d.size() == 1, "a rename changes one line");
        const QStringList pair = d.value(0).split(QLatin1Char('|'));
        CHECK(pair.size() == 2 && pair.at(1).indexOf(QLatin1String("bits=")) == pair.at(0).indexOf(QLatin1String("bits="))
                  && pair.at(1).contains(QLatin1String("name=\"PKT_TYPE_XY\"")),
              QByteArray("the next attribute keeps its column: ") + d.value(0).toUtf8());
        const QStringList shrink = changed308(xml, doc.withAttribute(pktType, QStringLiteral("name"), QStringLiteral("PT")));
        const QStringList p2 = shrink.value(0).split(QLatin1Char('|'));
        CHECK(p2.size() == 2 && p2.at(1).indexOf(QLatin1String("bits=")) == p2.at(0).indexOf(QLatin1String("bits=")),
              "a shorter name: still the same column");
    }
    {
        const int pad = doc.node(slrp).children.at(10);   // <pad   bits="3"/>
        CHECK(doc.node(pad).tag == QLatin1String("pad"), "fixture: SLRP's header pad");
        const QStringList d = changed308(xml, doc.withAttribute(pad, QStringLiteral("fill"), QStringLiteral("0")));
        CHECK(d.size() == 1 && d.value(0).endsWith(QLatin1String("<pad   bits=\"3\" fill=\"0\"/>")),
              QByteArray("an added attribute goes at the end of the tag: ") + d.value(0).toUtf8());
        SchemaDoc::Document added;
        added.parse(doc.withAttribute(pad, QStringLiteral("fill"), QStringLiteral("0")));
        CHECK(added.withoutAttribute(pad, QStringLiteral("fill")) == xml, "removing it again gives the file back, byte for byte");
    }
    {
        const int pktDir = find308(doc, QStringLiteral("field"), QStringLiteral("PKT_DIR"), slrp);
        SchemaDoc::Document e;
        e.parse(doc.withAttribute(pktDir, QStringLiteral("when"), QStringLiteral("FRAME_NUM & 7 == 1")));
        CHECK(e.text().contains("when=\"FRAME_NUM &amp; 7 == 1\""), "& is written &amp;");
        CHECK(e.node(pktDir).value(QStringLiteral("when")) == QLatin1String("FRAME_NUM & 7 == 1"), "and read back as &");
        e.parse(doc.withAttribute(pktDir, QStringLiteral("unit"), QStringLiteral("say \"x\" <y>")));
        CHECK(e.node(pktDir).value(QStringLiteral("unit")) == QLatin1String("say \"x\" <y>")
                  && e.text().contains("unit=\"say &quot;x&quot; &lt;y&gt;\""),
              "quotes and angle brackets escaped, read back as typed");
    }
    {
        // A CRLF file stays CRLF.
        QByteArray crlf = xml;
        crlf.replace("\r\n", "\n").replace("\n", "\r\n");
        SchemaDoc::Document c;
        CHECK(c.parse(crlf), "the CRLF copy parses");
        const QByteArray edited = c.withAttribute(pktType, QStringLiteral("bits"), QStringLiteral("5"));
        CHECK(edited.count("\r\n") == crlf.count("\r\n") && edited.count('\n') == crlf.count('\n')
                  && changed308(crlf, edited).size() == 1,
              "one line changed, every line ending kept");
    }
    {
        const QByteArray edited = doc.withAttribute(pktType, QStringLiteral("bits"), QStringLiteral("5"));
        const QStringList diff = SchemaDoc::lineDiff(xml, edited, 1);
        CHECK(diff.size() == 5 && diff.at(0) == QStringLiteral("@@ line %1").arg(doc.node(pktType).line - 1)
                  && diff.at(2).startsWith(QLatin1String("- ")) && diff.at(3).startsWith(QLatin1String("+ "))
                  && diff.at(3).contains(QLatin1String("bits=\"5\"")),
              QByteArray("the diff: a hunk header, context, - old, + new, context: ") + diff.join(QLatin1String(" / ")).toUtf8());
        CHECK(SchemaDoc::lineDiff(xml, xml).isEmpty(), "no change, no diff");
    }

    // ---- sizes ----------------------------------------------------------------------
    CHECK(SchemaEdit::fixedBits(doc, aap) == 4 + 7 + 17 + 16 + 3 + 23 + 20 + 12 + 12 + 7 + 16 + 7 + 32,
          "AAP is 176 fixed bits, the sum of its fields");
    {
        const QVector<int> offs = SchemaEdit::childOffsets(doc, aap);
        CHECK(offs.value(0) == 0 && offs.value(1) == 4 && offs.value(2) == 11 && offs.last() == 144,
              "each AAP field's bit offset; MAC_CODE starts at bit 144");
        CHECK(SchemaEdit::fixedBits(doc, slrp) < 0, "SLRP's sub-packets make it variable");
        const QVector<int> so = SchemaEdit::childOffsets(doc, slrp);
        CHECK(so.value(10) == 101 && so.value(11) == 104, "SLRP's header pad at bit 101; the sub-packets at 104");
        const int linfo = find308(doc, QStringLiteral("packet"), QStringLiteral("LINFO"));
        CHECK(SchemaEdit::fixedBits(doc, linfo) > 0 && SchemaEdit::fixedBits(doc, linfo) % 8 == 0,
              "LINFO (typed float and char fields) has a fixed byte size");
    }

    // ---- references a rename rewrites ----------------------------------------------
    const int tagLinking = find308(doc, QStringLiteral("struct"), QStringLiteral("TagLinking"));
    const int locReset = find308(doc, QStringLiteral("field"), QStringLiteral("LOC_RESET"), tagLinking);
    {
        const SchemaEdit::RenamePlan plan = SchemaEdit::planRename(doc, locReset, QStringLiteral("LOC_RST"));
        CHECK(plan.changes.size() == 3 && plan.elsewhere.isEmpty(), "LOC_RESET: three when= in TagLinking, none elsewhere");
        bool all = plan.changes.size() == 3;
        for (const SchemaEdit::Reference &r : plan.changes) {
            all = all && r.attr == QLatin1String("when") && r.before == QLatin1String("LOC_RESET==1")
                  && r.after == QLatin1String("LOC_RST==1");
        }
        CHECK(all, "each becomes LOC_RST==1");
        const int lsrp = find308(doc, QStringLiteral("packet"), QStringLiteral("LSRP"));
        const int frameNum = find308(doc, QStringLiteral("field"), QStringLiteral("FRAME_NUM"), lsrp);
        const SchemaEdit::RenamePlan fn = SchemaEdit::planRename(doc, frameNum, QStringLiteral("FN"));
        CHECK(fn.changes.size() == 5 && fn.changes.first().after == QLatin1String("FN & 7 == 1"),
              "LSRP's FRAME_NUM: its five <when test=> conditions, the & kept");
        const SchemaEdit::RenamePlan en = SchemaEdit::planRename(doc, find308(doc, QStringLiteral("enum"), QStringLiteral("pktDir")),
                                                                 QStringLiteral("packetDirection"));
        int users = 0;
        for (int f : doc.all(QStringLiteral("field"))) users += doc.node(f).value(QStringLiteral("enum")) == QLatin1String("pktDir");
        CHECK(en.changes.size() == users && users > 0, "an enum: every field's enum= that names it");
        const int jam = find308(doc, QStringLiteral("crc"), QStringLiteral("anxc_jamcrc"));
        CHECK(SchemaEdit::planRename(doc, jam, QStringLiteral("jam")).changes.size() == 5, "a CRC definition: the packets' crc=");
        CHECK(!SchemaEdit::planRename(doc, slrp, QStringLiteral("S")).warning.isEmpty(), "a packet: warned that code looks it up by name");
    }

    // ---- the session: checked, staged, undone, saved ---------------------------------
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("kavach.xml"));
    {
        QFile out(path);
        CHECK(out.open(QIODevice::WriteOnly), "fixture: out opens");
        out.write(xml);
    }
    SchemaEdit::Session s;
    CHECK(s.open(path, &err) && !s.readOnly() && !s.dirty(), "the copy opens, editable, clean");
    QString why;
    const int stnLoc = find308(s.doc(), QStringLiteral("field"), QStringLiteral("STN_ILC_IBS_LOC"), aap);
    CHECK(!s.setAttribute(stnLoc, QStringLiteral("bits"), QStringLiteral("0"), &why) && why.contains(QLatin1String("1 to 4096")),
          QByteArray("bits 0 refused: ") + why.toUtf8());
    CHECK(!s.setAttribute(stnLoc, QStringLiteral("bits"), QStringLiteral("abc"), &why), "bits abc refused");
    CHECK(!s.setAttribute(stnLoc, QStringLiteral("enum"), QStringLiteral("noSuchEnum"), &why) && why.contains(QLatin1String("noSuchEnum")),
          "an enum that does not exist refused");
    CHECK(!s.setAttribute(stnLoc, QStringLiteral("when"), QStringLiteral("STN_ILC_IBS =="), &why), "a malformed condition refused");
    CHECK(!s.setAttribute(stnLoc, QStringLiteral("when"), QStringLiteral("NO_SUCH_FIELD==1"), &why)
              && why.contains(QLatin1String("decoder would refuse")),
          QByteArray("a condition on a field nobody declares: the decoder's own refusal: ") + why.toUtf8());
    CHECK(!s.setAttribute(stnLoc, QStringLiteral("signed"), QStringLiteral("yes"), &why), "signed=yes refused");
    CHECK(!s.dirty() && s.changeCount() == 0 && s.text() == xml, "nothing refused was staged");

    // The decoder's view of a unit change, on a real @aap frame.
    QString aapType;
    const QByteArray aapFrame = frame308(QStringLiteral("@aap_1_1"), &aapType);
    CHECK(!aapFrame.isEmpty(), "fixture: a real @aap frame");
    Schema::Decoder before;
    before.loadData(xml);
    const QString locBefore = row308(before.decode(aapFrame, {}, aapType), QStringLiteral("STN_ILC_IBS_LOC"));
    CHECK(locBefore.endsWith(QLatin1String(" m")), QByteArray("as shipped: ") + locBefore.toUtf8());
    CHECK(s.setAttribute(stnLoc, QStringLiteral("unit"), QStringLiteral("metre"), &why), why.toUtf8());
    Schema::Decoder after;
    after.loadData(s.text());
    const QString locAfter = row308(after.decode(aapFrame, {}, aapType), QStringLiteral("STN_ILC_IBS_LOC"));
    CHECK(locAfter == locBefore.chopped(1) + QStringLiteral("metre"), QByteArray("decoded with the staged unit: ") + locAfter.toUtf8());
    CHECK(s.dirty() && s.changeCount() == 1 && s.changes().value(0).contains(QStringLiteral("unit m → metre")),
          QByteArray("one staged change, said plainly: ") + s.changes().value(0).toUtf8());

    // A run of edits to one attribute is one step; back to the start is none.
    const int tdma = find308(s.doc(), QStringLiteral("field"), QStringLiteral("STN_TDMA"), aap);
    CHECK(s.setAttribute(tdma, QStringLiteral("bits"), QStringLiteral("8"), &why)
              && s.setAttribute(tdma, QStringLiteral("bits"), QStringLiteral("9"), &why) && s.changeCount() == 2,
          "two clicks on STN_TDMA's bits: one more step");
    CHECK(s.changes().value(1).contains(QStringLiteral("bits 7 → 9")), "the step says where the run started");
    CHECK(s.setAttribute(tdma, QStringLiteral("bits"), QStringLiteral("7"), &why) && s.changeCount() == 1,
          "back to 7: the step is gone");

    // Undo / redo.
    CHECK(s.undo() && !s.dirty() && s.text() == xml, "undo: the file as opened");
    CHECK(s.redo() && s.changeCount() == 1 && s.doc().node(stnLoc).value(QStringLiteral("unit")) == QLatin1String("metre"),
          "redo: the unit again");

    // Removing an attribute; a required one is the form's business, name= is not removed.
    const int pktDirS = find308(s.doc(), QStringLiteral("field"), QStringLiteral("PKT_DIR"), slrp);
    CHECK(s.removeAttribute(pktDirS, QStringLiteral("enum"), &why) && !s.doc().node(pktDirS).has(QStringLiteral("enum")),
          "PKT_DIR's enum= removed");
    CHECK(!s.removeAttribute(pktDirS, QStringLiteral("name"), &why), "name= is renamed, never removed");
    CHECK(SchemaEdit::isRequired(s.doc(), pktDirS, QStringLiteral("bits")) && !SchemaEdit::isRequired(s.doc(), pktDirS, QStringLiteral("unit")),
          "bits is required on a plain field, unit is not");
    s.undo();

    // Renames.
    CHECK(!s.rename(locReset, QStringLiteral("LOC_RST"), false, &why) && why.contains(QLatin1String("decoder would refuse")),
          QByteArray("LOC_RESET renamed alone: its conditions name nothing, refused: ") + why.toUtf8());
    CHECK(s.rename(locReset, QStringLiteral("LOC_RST"), true, &why), QByteArray("with its references: staged ") + why.toUtf8());
    CHECK(s.text().count("LOC_RST==1") == 3 && !s.text().contains("\"LOC_RESET") && !s.text().contains("LOC_RESET=="),
          "all three when= follow (START_DIST_TO_LOC_RESET is another field, left alone)");
    CHECK(changed308(xml, s.text()).size() == 5, "five lines: the unit, the name, three conditions");
    {
        QString slrpType;
        const QByteArray slrpFrame = frame308(QStringLiteral("@slrp_1_1"), &slrpType);
        Schema::Decoder a, b;
        a.loadData(xml);
        b.loadData(s.text());
        const QVector<FieldRow> ra = a.decode(slrpFrame, {}, slrpType), rb = b.decode(slrpFrame, {}, slrpType);
        bool sameRows = !slrpFrame.isEmpty() && ra.size() == rb.size() && ra.size() > 5;
        for (int i = 0; sameRows && i < ra.size(); ++i) {
            sameRows = ra.at(i).value == rb.at(i).value
                       && (ra.at(i).field == rb.at(i).field || ra.at(i).field.contains(QLatin1String("LOC_RESET")));
        }
        CHECK(sameRows, "a real @slrp decodes to the same rows and values after the rename");
    }
    const int pktDirEnum = find308(s.doc(), QStringLiteral("enum"), QStringLiteral("pktDir"));
    CHECK(!s.rename(pktDirEnum, QStringLiteral("speed6"), true, &why) && why.contains(QLatin1String("already")),
          "an enum cannot take another enum's name");
    CHECK(!s.rename(pktDirEnum, QStringLiteral(" x "), true, &why) && !s.rename(pktDirEnum, QString(), true, &why),
          "nor an empty or padded one");

    // Saving.
    QFile::remove(path + QStringLiteral(".probe"));
    QString backup;
    CHECK(s.save(&backup, &err), QByteArray("saved: ") + err.toUtf8());
    QFile written(path), kept(backup);
    CHECK(written.open(QIODevice::ReadOnly), "fixture: written opens");
    CHECK(kept.open(QIODevice::ReadOnly), "fixture: kept opens");
    const QByteArray onDisk = written.readAll();
    CHECK(onDisk == s.text() && kept.readAll() == xml && !s.dirty() && s.changeCount() == 0,
          "the file is the staged text, the backup the file as it was, the session clean");
    CHECK(SchemaEdit::loadProblem(onDisk).isEmpty(), "the saved file loads");
    {
        // Changed on disk behind the editor's back: refused.
        CHECK(s.setAttribute(stnLoc, QStringLiteral("unit"), QStringLiteral("m"), &why), "another edit");
        QFile hand(path);
        CHECK(hand.open(QIODevice::WriteOnly), "fixture: hand opens");
        hand.write(xml);
        hand.close();
        CHECK(!s.save(&backup, &err) && err.contains(QLatin1String("changed on disk")), "the file changed on disk: not written");
        QFile again(path);
        CHECK(again.open(QIODevice::ReadOnly), "fixture: again opens");
        CHECK(again.readAll() == xml, "and left as the hand edit wrote it");
    }
    SchemaEdit::Session builtin;
    CHECK(builtin.open(QStringLiteral(":/schema/kavach.xml"), &err) && builtin.readOnly(), "the built-in copy opens read-only");
    CHECK(!builtin.setAttribute(stnLoc, QStringLiteral("unit"), QStringLiteral("x"), &why) && why.contains(QLatin1String("editable copy")),
          "and refuses edits");

    // ---- the window ------------------------------------------------------------------
    const QString winPath = tmp.filePath(QStringLiteral("win.xml"));
    {
        QFile out(winPath);
        CHECK(out.open(QIODevice::WriteOnly), "fixture: out opens");
        out.write(xml);
    }
    SchemaEditorWindow win(winPath);
    win.show();
    QApplication::processEvents();
    QStringList cats;
    for (int i = 0; i < win.outline()->topLevelItemCount(); ++i) cats << win.outline()->topLevelItem(i)->text(0);
    CHECK(cats.value(0) == QLatin1String("Packets (38)") && cats.contains(QStringLiteral("Enums (65)"))
              && cats.contains(QStringLiteral("CRCs (3)")),
          QByteArray("the outline: ") + cats.join(QLatin1String(", ")).toUtf8());
    CHECK(win.selectContainer(QStringLiteral("packet"), QStringLiteral("AAP")), "AAP selected");
    CHECK(win.members()->topLevelItemCount() == 13, "its thirteen fields");
    QTreeWidgetItem *mac = win.members()->topLevelItem(12);
    CHECK(mac && mac->text(0) == QLatin1String("MAC_CODE") && mac->text(2) == QLatin1String("32") && mac->text(3) == QLatin1String("144")
              && mac->text(4).contains(QLatin1String("hex 8")),
          "MAC_CODE: 32 bits at bit 144, hex 8 in the details");
    CHECK(win.outline()->currentItem() && win.outline()->currentItem()->text(1) == QLatin1String("22 B"), "the outline: AAP, 22 B");

    const int tdmaW = find308(win.session().doc(), QStringLiteral("field"), QStringLiteral("STN_TDMA"), aap);
    win.selectNode(tdmaW);
    auto *bits = qobject_cast<QSpinBox *>(win.editorFor(QStringLiteral("bits")));
    CHECK(bits && bits->value() == 7 && bits->isEnabled(), "STN_TDMA's form: bits in a spin box, 7");
    CHECK(win.editorFor(QStringLiteral("unit")) == nullptr, "no unit row: it has none (offered under Add a property)");
    bits->setValue(8);
    QApplication::processEvents();
    CHECK(win.session().changeCount() == 1 && win.saveButton()->isEnabled(), "a click on the spin box: staged, Save enabled");
    CHECK(win.status()->kind() == StatusLine::Kind::Ok, "the status line says it was staged");
    CHECK(win.sizeChanges() == QStringList{ QStringLiteral("AAP: 22 B → 177 bits") },
          QByteArray("the size change: ") + win.sizeChanges().join(QLatin1Char(';')).toUtf8());
    bits = qobject_cast<QSpinBox *>(win.editorFor(QStringLiteral("bits")));
    CHECK(bits && bits->value() == 8 && win.selectedNode() == tdmaW, "the form rebuilt on the same field, 8");
    CHECK(win.members()->topLevelItem(12)->text(3) == QLatin1String("145"), "MAC_CODE moved to bit 145");

    // A refused edit: the reason, nothing staged, the box put back.
    auto *nameEdit = qobject_cast<QLineEdit *>(win.editorFor(QStringLiteral("name")));
    CHECK(nameEdit != nullptr, "the name in a line edit");
    CHECK(!win.editAttribute(tdmaW, QStringLiteral("bits"), QStringLiteral("0")), "bits 0 through the window: refused");
    QApplication::processEvents();
    CHECK(win.status()->kind() == StatusLine::Kind::Fail && win.session().changeCount() == 1, "the reason shown, still one change");

    // Add a property, then remove it.
    CHECK(win.editAttribute(tdmaW, QStringLiteral("signed"), QStringLiteral("true")), "signed added");
    QApplication::processEvents();
    auto *signedBox = qobject_cast<QCheckBox *>(win.editorFor(QStringLiteral("signed")));
    CHECK(signedBox && signedBox->isChecked(), "a checked box");
    signedBox->setChecked(false);
    QApplication::processEvents();
    CHECK(!win.session().doc().node(tdmaW).has(QStringLiteral("signed")) && win.editorFor(QStringLiteral("signed")) == nullptr,
          "unchecked: the attribute goes");

    // A reference as a choice of what exists.
    const int pktDirW = find308(win.session().doc(), QStringLiteral("field"), QStringLiteral("PKT_DIR"), slrp);
    win.selectNode(pktDirW);
    auto *enumBox = qobject_cast<QComboBox *>(win.editorFor(QStringLiteral("enum")));
    CHECK(enumBox && enumBox->currentText() == QLatin1String("pktDir") && enumBox->count() == 66,
          "PKT_DIR's enum: a list of the 65 enums and (none)");

    // Review lines.
    const QStringList review = win.reviewLines();
    CHECK(review.value(0).contains(QLatin1String("change(s) to")) && review.join(QLatin1Char('\n')).contains(QStringLiteral("AAP: 22 B → 177 bits"))
              && review.join(QLatin1Char('\n')).contains(QLatin1String("+ ")),
          "the review: the changes, the size change, the diff");

    // Undo through the window.
    CHECK(win.session().changeCount() == 3, "three steps: bits, signed added, signed removed");
    while (win.session().canUndo()) win.undo();
    CHECK(!win.session().dirty(), "undone to the start: clean");
    QApplication::processEvents();
    CHECK(!win.saveButton()->isEnabled(), "nothing to save");
    win.editAttribute(tdmaW, QStringLiteral("bits"), QStringLiteral("8"));

    // Save.
    QSignalSpy saved(&win, &SchemaEditorWindow::schemaSaved);
    QString summary;
    CHECK(win.save(&summary, &err) && saved.count() == 1 && saved.at(0).at(0).toString() == winPath,
          QByteArray("saved, and said so: ") + summary.toUtf8());
    CHECK(summary.contains(QLatin1String("Backup: win.xml.bak-")) && summary.contains(QStringLiteral("AAP: 22 B → 177 bits")),
          "the summary names the backup and the size change");

    // Find.
    win.findEdit()->setText(QStringLiteral("MAC_CODE"));
    QApplication::processEvents();
    QStringList shown;
    QTreeWidgetItem *packets = win.outline()->topLevelItem(0);
    for (int i = 0; i < packets->childCount(); ++i) {
        if (!packets->child(i)->isHidden()) shown << packets->child(i)->text(0);
    }
    CHECK(shown.contains(QStringLiteral("AAP")) && !shown.contains(QStringLiteral("DIP1")) && win.outline()->topLevelItem(2)->isHidden(),
          QByteArray("find MAC_CODE: the packets that have one, no enum: ") + shown.join(QLatin1Char(',')).toUtf8());
    win.findEdit()->clear();
    win.close();

    // Read-only on the built-in schema.
    SchemaEditorWindow ro(QStringLiteral(":/schema/kavach.xml"));
    ro.show();
    QApplication::processEvents();
    ro.selectContainer(QStringLiteral("packet"), QStringLiteral("AAP"));
    QWidget *roBits = ro.editorFor(QStringLiteral("name"));
    CHECK(ro.session().readOnly() && roBits && !roBits->isEnabled() && !ro.saveButton()->isEnabled(),
          "the built-in schema: browsable, every editor disabled, no Save");
    ro.close();
}
