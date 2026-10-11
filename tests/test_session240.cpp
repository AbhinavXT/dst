#include "testutil.h"

#include "packetbuilder.h"
#include "packetmakerdialog.h"

#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>

// =============================================================================
//  Session 240 — the byte <-> field map in Packet Maker's Output. A click on
//  a byte of the built frame lights up every decoded field with a bit in it,
//  and their bytes; a click on a decoded field lights up its bytes. Spans
//  are the decoder's FieldRow bit offsets. The frame is the bufferload
//  suite's real SLRP reference frame 6 (from replay/).
// =============================================================================

namespace {

const char kSlrpRef6[] =
    "91 B9 AA E6 04 1E 80 00 08 40 6F 8E 68 01 80 00 00 00 00 01 7C AA 08 20 "
    "00 11 63 0D 0C 58 10 B1 00 F4 84 00 22 05 09 BA 20 14 00 00 34 C6 08 5A "
    "40 F4 93 00 54 E8 60 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 11 "
    "30 38 22 41 08 7A 04 83 23 8A 29 0E 08 41 38 40 95 0A 60 61 64 E0 00 03 "
    "43 38 68 60 42 C0 3E A4 18 46 67 50 A7 6E";

// The marked spans as text, so a check can say what it saw.
QStringList marked(QPlainTextEdit *e)
{
    QStringList out;
    for (const QTextEdit::ExtraSelection &s : e->extraSelections()) out << s.cursor.selectedText();
    return out;
}

} // namespace

TEST_SUITE(session240)
{
    PacketMakerDialog d;
    d.loadBuffer(QString::fromLatin1(kSlrpRef6));
    const PacketBuilder::Result r = d.buildSendForTest(-1);
    CHECK(r.ok, "fixture: reference frame 6 builds");
    FieldRow fn;
    int spanned = 0, subSpanned = 0;
    bool inSub = false;
    for (const FieldRow &fr : r.decoded) {
        if (fr.field.trimmed() == QLatin1String("FRAME_NUM")) fn = fr;
        if (fr.field.startsWith(QLatin1String("subpkt["))) inSub = true;
        if (fr.hasSpan()) { ++spanned; if (inSub) ++subSpanned; }
    }
    CHECK(fn.hasSpan(), "FRAME_NUM has a bit span");
    CHECK(subSpanned > 0, QByteArray("sub-packet fields have spans too (") + QByteArray::number(subSpanned) + ")");

    QPushButton *build = nullptr;
    for (QPushButton *b : d.findChildren<QPushButton *>())
        if (b->text() == QLatin1String("Build && Verify")) build = b;
    if (build) build->click();
    auto *out = d.findChild<QPlainTextEdit *>(QStringLiteral("pmOutput"));
    CHECK(out, "the Output pane");
    if (!out) return;
    const QString text = out->toPlainText();
    const QString hex = QString::fromLatin1(r.frame.toHex(' '));
    const int hexStart = text.indexOf(hex);
    CHECK(hexStart > 0, "the frame's hex is in Output");

    // ---- a field -> its bytes ---------------------------------------------------------------
    const int fnLine = text.indexOf(QRegularExpression(QStringLiteral("\\n\\s+FRAME_NUM ")), text.indexOf(QLatin1String("decoded back:")));
    CHECK(fnLine > 0, "FRAME_NUM's decoded line");
    QTextCursor c = out->textCursor();
    c.setPosition(fnLine + 1 + text.mid(fnLine + 1).indexOf(QLatin1String("FRAME_NUM")));
    out->setTextCursor(c);
    QStringList m = marked(out);
    QStringList want;
    for (int b = fn.byteStart(); b <= fn.byteEnd(); ++b) want << hex.mid(b * 3, 2);
    bool allBytes = true;
    for (const QString &w : want) allBytes &= m.contains(w);
    CHECK(m.size() == want.size() + 1 && allBytes,
          "a click on FRAME_NUM marks its line and its " + QByteArray::number(want.size()) + " bytes (" + m.join('|').toUtf8() + ")");

    // ---- a byte -> its fields ---------------------------------------------------------------
    c.setPosition(hexStart + fn.byteStart() * 3);
    out->setTextCursor(c);
    m = marked(out);
    bool fnMarked = false;
    for (const QString &s : m) fnMarked |= s.trimmed().startsWith(QLatin1String("FRAME_NUM"));
    CHECK(fnMarked, "a click on FRAME_NUM's first byte marks FRAME_NUM's line");
    int lines = 0;
    for (const QString &s : m) lines += s.size() > 2 ? 1 : 0;
    CHECK(lines >= 2, "that byte is shared: more than one field is marked (bit-packed)");

    // ---- elsewhere: nothing -----------------------------------------------------------------
    c.setPosition(0);
    out->setTextCursor(c);
    CHECK(out->extraSelections().isEmpty(), "a click on the packet line clears the marks");
}
