#include "testutil.h"

#include "layoutaudit.h"
#include "packetbuilder.h"
#include "packetmatrix.h"
#include "packetmatrixdialog.h"
#include "packetsequencedialog.h"
#include "settings.h"
#include "statusline.h"
#include "testcatalog.h"
#include "testcatalogwindow.h"
#include "theme.h"
#include "uistyle.h"
#include "undolog.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 232 — the Packet Matrix: one preset per SIF operation, from a
//  Test Case Catalog case. The base preset is a real SLRP frame (the
//  bufferload suite's reference frame 6, from replay/), parsed back into
//  header and sub-packets as "Selected row → Packet Maker" does.
// =============================================================================

namespace {

const char kSlrpRef6[] =
    "91 B9 AA E6 04 1E 80 00 08 40 6F 8E 68 01 80 00 00 00 00 01 7C AA 08 20 "
    "00 11 63 0D 0C 58 10 B1 00 F4 84 00 22 05 09 BA 20 14 00 00 34 C6 08 5A "
    "40 F4 93 00 54 E8 60 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 11 "
    "30 38 22 41 08 7A 04 83 23 8A 29 0E 08 41 38 40 95 0A 60 61 64 E0 00 03 "
    "43 38 68 60 42 C0 3E A4 18 46 67 50 A7 6E";

QVector<TestCatalog::Case> opsOf(const QString &id)
{
    const TestCatalog::Catalog &b = TestCatalog::builtin();
    QVector<TestCatalog::Case> ops;
    for (const QString &k : TestCatalog::childrenOf(b, id)) ops << b.cases.at(TestCatalog::indexOf(b, k));
    return ops;
}

int rowOf(QTableWidget *t, const QString &clause)
{
    for (int r = 0; r < t->rowCount(); ++r)
        if (t->item(r, PacketMatrixDialog::ColClause)->text() == clause) return r;
    return -1;
}

} // namespace

TEST_SUITE(session232)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    using namespace PacketMatrix;

    // ---- reading the SIF's values ------------------------------------------------------------
    QString field, printed;
    CHECK(findValue(QStringLiteral("Stationary KAVACH should send a packet with PKT_TYPE as 1000 (Reserved) followed by"),
                    &field, &printed) && field == QLatin1String("PKT_TYPE") && printed == QLatin1String("1000"),
          "31.6.1: PKT_TYPE as 1000");
    CHECK(findValue(QStringLiteral("send a packet with CUR_SIG_ASPECT as ‘0’ followed"), &field, &printed)
              && field == QLatin1String("CUR_SIG_ASPECT") && printed == QLatin1String("0"),
          "31.23.1: CUR_SIG_ASPECT as ‘0’ (quoted)");
    CHECK(!findValue(QStringLiteral("Stationary KAVACH should send MA packet"), &field, &printed), "no value, no match");

    Reading r = read(QStringLiteral("1000"), 4, {});
    CHECK(r.ok && r.sure && r.value == 8 && r.how == QLatin1String("binary 1000 = 8"), "1000 in a 4-bit field: binary, 8");
    r = read(QStringLiteral("100"), 6, { QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("10"), QStringLiteral("100") });
    CHECK(r.ok && !r.sure && r.value == 4, "100 in a 6-bit field among 0, 1, 10: binary 4, flagged to check");
    r = read(QStringLiteral("100"), 15, { QStringLiteral("100"), QStringLiteral("32800") });
    CHECK(r.ok && !r.sure && r.value == 100, "100 among 32800: decimal 100, flagged to check");
    r = read(QStringLiteral("32800"), 15, {});
    CHECK(r.ok && r.sure && r.value == 32800, "32800: decimal");
    r = read(QStringLiteral("0x1001"), 16, {});
    CHECK(r.ok && r.sure && r.value == 0x1001, "0x1001: hex");
    qint64 v = 0;
    CHECK(parseTyped(QStringLiteral("0b1010"), &v) && v == 10 && parseTyped(QStringLiteral("0xA"), &v) && v == 10
              && parseTyped(QStringLiteral(" 10 "), &v) && v == 10 && !parseTyped(QStringLiteral("ten"), &v),
          "typed values: 0b1010, 0xA, 10; not \"ten\"");
    CHECK(senderOf(QStringLiteral("Stationary KAVACH should send")) == QLatin1String("Station")
              && senderOf(QStringLiteral("Onboard KAVACH should send")) == QLatin1String("Onboard")
              && senderOf(QStringLiteral("xviii. Stationary KAVACH should send")) == QLatin1String("Station")
              && senderOfPacket(QStringLiteral("slrp")) == QLatin1String("Station")
              && senderOfPacket(QStringLiteral("lsrp")) == QLatin1String("Onboard"),
          "who sends: the text, and the packet type");

    // ---- the fields of SLRP --------------------------------------------------------------------
    PacketBuilder pb;
    CHECK(pb.ready(), "schema loaded");
    const QVector<FieldRef> fields = fieldsOf(pb.encoder(), QStringLiteral("slrp"));
    const FieldRef *pktType = nullptr, *aspect = nullptr;
    bool noLength = true;
    for (const FieldRef &f : fields) {
        if (f.label() == QLatin1String("PKT_TYPE")) pktType = &f;
        if (f.label() == QLatin1String("MovementAuthority.CUR_SIG_ASPECT")) aspect = &f;
        noLength = noLength && f.name != QLatin1String("PKT_LENGTH");
    }
    CHECK(pktType && pktType->bits == 4 && aspect && aspect->bits == 6 && aspect->subType == 0 && noLength,
          "SLRP offers PKT_TYPE (4 bits) and MovementAuthority.CUR_SIG_ASPECT (6), not PKT_LENGTH (the builder sets it)");
    if (!pktType || !aspect) return;
    CHECK(fits(*pktType, 15) && !fits(*pktType, 16) && !fits(*pktType, -1), "a 4-bit field takes 0-15");

    // the base: a real frame, parsed
    const QByteArray frame = QByteArray::fromHex(QByteArray(kSlrpRef6).replace(' ', ""));
    const Schema::ParsedPacket pp = pb.encoder().parseBody(QStringLiteral("slrp"), frame);
    CHECK(pp.ok, "reference frame 6 parses");
    PacketPreset base;
    base.captype = QStringLiteral("slrp");
    base.header = pp.header;
    base.subs = pp.subs;
    base.dest = QStringLiteral("192.168.1.50");
    base.port = 50002;
    PacketVary::Rule rule;
    rule.field = QStringLiteral("PKT_TYPE");
    base.vary << rule;
    CHECK(base.build(pb, -1, QByteArray()).ok, "the base builds and verifies");

    PacketPreset out;
    QString why;
    CHECK(apply(base, *pktType, 8, &out, &why) && out.header.value(QStringLiteral("PKT_TYPE")) == 8
              && !out.vary.first().enabled && out.build(pb, -1, QByteArray()).ok,
          "PKT_TYPE 8: set, its variation rule off, and it still builds");
    PacketPreset noSubs = base;
    noSubs.subs.clear();
    CHECK(!apply(noSubs, *aspect, 2, &out, &why) && why.contains(QStringLiteral("MovementAuthority")),
          "a sub-packet field with no such sub-packet in the base: refused, named");

    QTemporaryDir tmp;
    const QString basePath = tmp.filePath(QStringLiteral("slrp_base.packet.json"));
    CHECK(base.save(basePath), "the base saved as a preset");

    // ---- the dialog on 31.6 --------------------------------------------------------------------
    const TestCatalog::Catalog &b = TestCatalog::builtin();
    const TestCatalog::Case c316 = b.cases.at(TestCatalog::indexOf(b, QStringLiteral("31.6")));
    Settings::setMatrixLastBase(QString());
    PacketMatrixDialog d(c316, opsOf(QStringLiteral("31.6")));
    d.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(d.setBasePreset(basePath) && d.field() == QLatin1String("PKT_TYPE"), "the base picked: PKT_TYPE, as the SIF names it");
    QTableWidget *t = d.table();
    auto ticked = [t](int row) { return t->item(row, PacketMatrixDialog::ColOn)->checkState() == Qt::Checked; };
    auto cell = [t](int row, int col) { return t->item(row, col)->text(); };
    CHECK(t->rowCount() == 10, "a row per operation");
    const int r1 = rowOf(t, QStringLiteral("31.6.1")), r4 = rowOf(t, QStringLiteral("31.6.4")),
              r5 = rowOf(t, QStringLiteral("31.6.5"));
    CHECK(ticked(r1) && cell(r1, PacketMatrixDialog::ColValue) == QLatin1String("8")
              && cell(r1, PacketMatrixDialog::ColReading) == QLatin1String("binary 1000 = 8")
              && cell(r4, PacketMatrixDialog::ColValue) == QLatin1String("12"),
          "31.6.1: 1000 read as 8; 31.6.4: 1100 as 12");
    CHECK(!ticked(r5) && cell(r5, PacketMatrixDialog::ColReading).contains(QStringLiteral("sent by the Onboard")),
          "31.6.5 is the Onboard's packet: unticked, and it says why");
    int on = 0;
    for (int i = 0; i < t->rowCount(); ++i) on += ticked(i) ? 1 : 0;
    CHECK(on == 4, QByteArray("the four station rows ticked (") + QByteArray::number(on) + ")");

    // a value typed that does not fit, then one that does
    t->item(r4, PacketMatrixDialog::ColValue)->setText(QStringLiteral("16"));
    CHECK(!ticked(r4) && cell(r4, PacketMatrixDialog::ColReading).contains(QStringLiteral("does not fit")),
          "16 in PKT_TYPE: unticked, refused");
    t->item(r4, PacketMatrixDialog::ColValue)->setText(QStringLiteral("0b1011"));
    CHECK(ticked(r4) && cell(r4, PacketMatrixDialog::ColReading) == QLatin1String("typed: 11 (the SIF reads 12)"),
          "typed 0b1011: ticked again, the SIF's reading beside it");
    t->item(r4, PacketMatrixDialog::ColValue)->setText(QStringLiteral("12"));
    CHECK(cell(r4, PacketMatrixDialog::ColReading) == QLatin1String("binary 1100 = 12"), "back to the SIF's value: its reading");

    const QString outDir = tmp.filePath(QStringLiteral("presets"));
    QDir().mkpath(outDir);
    d.setFolder(outDir);
    const QHash<QString, QString> made = d.makePresets();
    CHECK(made.size() == 4 && QFile::exists(QDir(outDir).filePath(QStringLiteral("31.6.1.packet.json")))
              && d.sequencePath() == QDir(outDir).absoluteFilePath(QStringLiteral("31.6.seq.json")),
          "four presets and 31.6.seq.json written");
    PacketPreset p1;
    CHECK(PacketPreset::load(made.value(QStringLiteral("31.6.3")), &p1) && p1.header.value(QStringLiteral("PKT_TYPE")) == 11
              && p1.header.value(QStringLiteral("FRAME_NUM")) == base.header.value(QStringLiteral("FRAME_NUM"))
              && p1.subs.size() == base.subs.size() && p1.dest == base.dest,
          "31.6.3: PKT_TYPE 11, everything else the base's");
    {
        QFile f(d.sequencePath());
        f.open(QIODevice::ReadOnly);
        const QJsonObject seq = QJsonDocument::fromJson(f.readAll()).object();
        CHECK(seq.value(QStringLiteral("format")).toString() == QLatin1String("dlconsole.sequence")
                  && seq.value(QStringLiteral("steps")).toArray().size() == 4
                  && seq.value(QStringLiteral("steps")).toArray().first().toObject().value(QStringLiteral("preset")).toString()
                         == made.value(QStringLiteral("31.6.1"))
                  && seq.value(QStringLiteral("steps")).toArray().first().toObject().value(QStringLiteral("delay_ms")).toInt() == 1000,
              "the sequence: 4 steps in clause order, 1 s apart");
        PacketSequenceDialog sq;
        CHECK(sq.loadSequence(d.sequencePath()) && sq.stepCount() == 4, "Packet Sequence loads it");
    }
    CHECK(d.makePresets().isEmpty() && d.outcomes().first().why.contains(QStringLiteral("already there")),
          "a second run does not replace files unless asked");
    d.setOverwrite(true);
    CHECK(d.makePresets().size() == 4, "and does when asked");

    // ---- 31.23: a sub-packet field, counting in binary ----------------------------------------
    const TestCatalog::Case c3123 = b.cases.at(TestCatalog::indexOf(b, QStringLiteral("31.23")));
    PacketMatrixDialog d2(c3123, opsOf(QStringLiteral("31.23")));
    d2.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(d2.setBasePreset(basePath) && d2.field() == QLatin1String("MovementAuthority.CUR_SIG_ASPECT"),
          "31.23: the field found in the MA sub-packet");
    QTableWidget *t2 = d2.table();
    const int a3 = rowOf(t2, QStringLiteral("31.23.3")), a17 = rowOf(t2, QStringLiteral("31.23.17"));
    CHECK(t2->item(a3, PacketMatrixDialog::ColValue)->text() == QLatin1String("2")
              && t2->item(a3, PacketMatrixDialog::ColReading)->text().startsWith(QStringLiteral("check: binary 10 = 2?"))
              && t2->item(a17, PacketMatrixDialog::ColValue)->text() == QLatin1String("16")
              && t2->item(a17, PacketMatrixDialog::ColReading)->text() == QLatin1String("binary 010000 = 16"),
          "31.23.3 '10' read as 2 and flagged to check; 31.23.17 '010000' (6 digits) as 16");
    const QString outDir2 = tmp.filePath(QStringLiteral("aspects"));
    QDir().mkpath(outDir2);
    d2.setFolder(outDir2);
    const QHash<QString, QString> made2 = d2.makePresets();
    PacketPreset p17;
    bool aspectSet = PacketPreset::load(made2.value(QStringLiteral("31.23.17")), &p17);
    for (const Schema::SubEntry &s : p17.subs)
        if (s.type == 0) aspectSet = aspectSet && s.values.value(QStringLiteral("CUR_SIG_ASPECT")) == 16;
    CHECK(made2.size() >= 20 && aspectSet, QByteArray("31.23: presets with CUR_SIG_ASPECT set in the MA (")
                                                + QByteArray::number(made2.size()) + " written)");
    for (const Outcome &o : d2.outcomes())
        if (!o.ok) std::printf("        refused %s: %s\n", qPrintable(o.clause), qPrintable(o.why));

    // ---- from the catalog ----------------------------------------------------------------------
    Settings::setTestCatalogLastFile(QString());
    TestCatalogWindow w;
    w.reopenLast();
    CHECK(w.select(QStringLiteral("31.6.3")) && w.matrixCase() == QLatin1String("31.6"), "an operation: its case");
    CHECK(w.select(QStringLiteral("31.6")) && w.matrixCase() == QLatin1String("31.6"), "a case with operations: itself");
    CHECK(w.select(QStringLiteral("4.93")) && w.matrixCase().isEmpty(), "4.93 has none: no matrix");
    w.select(QStringLiteral("31.6.1"));
    w.notePresets(made);
    const TestCatalog::Case noted = w.catalog().cases.at(TestCatalog::indexOf(w.catalog(), QStringLiteral("31.6.1")));
    CHECK(noted.notes == QStringLiteral("Preset: %1").arg(QDir::toNativeSeparators(made.value(QStringLiteral("31.6.1"))))
              && w.isModified() && w.editor(TestCatalogWindow::Notes)->toPlainText() == noted.notes,
          "the presets noted in their clauses, shown at once");
    w.notePresets(made);
    CHECK(w.undoLog()->depth() == 1, "noting the same presets again changes nothing");
    const bool undone = w.undo();
    const QString after = w.catalog().cases.at(TestCatalog::indexOf(w.catalog(), QStringLiteral("31.6.1"))).notes;
    CHECK(undone && after.isEmpty(), QByteArray("one Undo takes the notes back (") + after.toUtf8() + ")");

    d.show();
    CHECK(LayoutAudit::orphans(&d).isEmpty(), "no widget outside a layout");
    CHECK(d.minimumSizeHint().width() <= 1100 && d.minimumSizeHint().height() <= 700, "fits a laptop");
    Settings::setMatrixLastBase(QString());
    Settings::setMatrixLastFolder(QString());
}
