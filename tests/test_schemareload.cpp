#include "testutil.h"
#include "capturedecoder.h"
#include "schema/schemadecoder.h"

#include <QFile>
#include <QTemporaryDir>

// Hot-reload of the shared schema. The interesting cases are not the happy
// path but what a bad reload leaves behind: load() clears before parsing,
// so a failed reload leaves an EMPTY decoder, not the previous one. Code
// and UI both have to be honest about that.
TEST_SUITE(schemareload)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");

    // Baseline: the built-in resource.
    QString err;
    CHECK(reloadKavachSchema(QString(), &err), "built-in schema loads");
    const Schema::Decoder &d = kavachSchema();
    CHECK(d.isLoaded(), "decoder reports loaded");
    const int packets = d.packetCount();
    const int structs = d.structCount();
    CHECK(packets > 0, "built-in schema defines packets");
    CHECK(structs > 0, "built-in schema defines structs");
    CHECK(d.packetNames().size() == packets, "packetNames matches packetCount");
    CHECK(d.packetNames().contains("SLRP"), "a known packet is present");

    // Reload is idempotent — no accumulation across repeated loads.
    reloadKavachSchema(QString(), &err);
    reloadKavachSchema(QString(), &err);
    CHECK(d.packetCount() == packets, "repeat reload does not duplicate packets");
    CHECK(d.structCount() == structs, "repeat reload does not duplicate structs");

    // Registered CRC/meaning hooks must survive a reload: they are C++
    // functions, not schema data. If load() cleared them, every CRC row
    // would silently start reporting failure after the first reload.
    {
        QByteArray f(96, '\0');
        for (int i = 0; i < f.size(); ++i) f[i] = char(i * 13 + 5);
        const QVector<FieldRow> before = d.decode(f, {}, "slrp");
        reloadKavachSchema(QString(), &err);
        const QVector<FieldRow> after = d.decode(f, {}, "slrp");
        CHECK(before.size() == after.size(), "same row count after reload");
        bool identical = (before.size() == after.size());
        for (int i = 0; identical && i < before.size(); ++i) {
            if (before[i].field != after[i].field
                || before[i].value != after[i].value) identical = false;
        }
        CHECK(identical, "decode is byte-identical after reload (hooks survived)");
    }

    // Missing file: fails, and leaves the decoder empty rather than stale.
    {
        err.clear();
        const bool ok = reloadKavachSchema(tmp.path() + "/does-not-exist.xml", &err);
        CHECK(!ok, "missing file reports failure");
        CHECK(!err.isEmpty(), "failure carries a message");
        CHECK(d.packetCount() == 0, "failed reload leaves an EMPTY decoder");
        CHECK(!d.isLoaded(), "isLoaded() is false after a failed reload");
    }

    // Recovery from the built-in copy is the escape hatch the UI offers.
    CHECK(reloadKavachSchema(QString(), &err), "recover from built-in");
    CHECK(d.packetCount() == packets, "recovered to the original packet count");

    // Malformed XML.
    {
        const QString bad = tmp.path() + "/bad.xml";
        QFile f(bad); f.open(QIODevice::WriteOnly);
        f.write("<kavach><packet name=\"X\"</kavach>"); f.close();
        err.clear();
        CHECK(!reloadKavachSchema(bad, &err), "malformed XML reports failure");
        CHECK(err.contains("XML"), "error mentions XML");
        CHECK(d.packetCount() == 0, "malformed load leaves nothing behind");
    }

    // Well-formed but packet-less. load() rejects this outright rather
    // than reporting success and then decoding nothing — which is the
    // right call, and worth pinning down: the alternative would look like
    // a working reload right up until every frame came back unhandled.
    {
        const QString empty = tmp.path() + "/empty.xml";
        QFile f(empty); f.open(QIODevice::WriteOnly);
        f.write("<kavach></kavach>"); f.close();
        err.clear();
        CHECK(!reloadKavachSchema(empty, &err), "packet-less schema is rejected");
        CHECK(err.contains("packet"), "error names the missing element");
        CHECK(!d.isLoaded(), "and leaves the decoder unloaded");
    }

    // A minimal hand-written schema loads and is usable, which is what
    // makes external-file iteration worth having.
    {
        const QString mini = tmp.path() + "/mini.xml";
        QFile f(mini); f.open(QIODevice::WriteOnly);
        f.write("<kavach>"
                "<struct name=\"S\"><field name=\"a\" bits=\"8\"/></struct>"
                "<packet name=\"MINI\" match=\"captype==mini\" wire=\"lsb-first\" "
                "trailer_bytes=\"0\"><field name=\"a\" bits=\"8\"/></packet>"
                "</kavach>"); f.close();
        err.clear();
        CHECK(reloadKavachSchema(mini, &err), "minimal external schema loads");
        CHECK(d.packetCount() == 1, "one packet");
        CHECK(d.structCount() == 1, "one struct");
        CHECK(d.packetNames().contains("MINI"), "packet is named MINI");
        QByteArray frame(4, '\x42');
        CHECK(d.handles(frame, "mini"), "external schema handles its own captype");
        CHECK(!d.decode(frame, {}, "mini").isEmpty(), "and decodes a frame");
    }

    // Leave the shared instance in its shipping state — later suites in the
    // same binary decode against it.
    CHECK(reloadKavachSchema(QString(), &err), "restored built-in schema");
    CHECK(kavachSchema().packetCount() == packets, "restored packet count");
    // ---- conditions are checked at load (session 65) -----------------------
    // Refuse, don't guess: before 65 a when= naming a misspelt field decoded
    // as "that field is 0" on every frame, and one with no operator held.
    auto schemaWith = [&](const QString &file, const QString &cond) {
        const QString path = tmp.path() + "/" + file;
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(QStringLiteral(
            "<schema><packet name=\"P\" match=\"captype==p\">"
            "<field name=\"A\" bits=\"4\"/>"
            "<field name=\"B\" bits=\"4\" when=\"%1\"/>"
            "</packet></schema>").arg(cond.toHtmlEscaped()).toUtf8());
        f.close();
        return path;
    };
    {
        err.clear();
        CHECK(reloadKavachSchema(schemaWith("okcond.xml", "A & 0x7 == 3"), &err),
              "a well-formed mask condition on a declared field loads");
    }
    {
        err.clear();
        CHECK(!reloadKavachSchema(schemaWith("typo.xml", "AA == 1"), &err),
              "a condition naming an undeclared field is refused");
        CHECK(err.contains("'AA'") && err.contains("no packet or struct declares"),
              "and the error names the field");
        CHECK(d.packetCount() == 0 && !d.isLoaded(),
              "a refused schema leaves an EMPTY decoder");
    }
    {
        err.clear();
        CHECK(!reloadKavachSchema(schemaWith("noop.xml", "A"), &err),
              "a condition with no operator is refused");
        CHECK(err.contains("no operator"), "and says why");
    }
    {
        err.clear();
        CHECK(!reloadKavachSchema(schemaWith("range.xml", "A in 5..3"), &err),
              "an inverted range is refused");
    }
    CHECK(reloadKavachSchema(QString(), &err), "recover from built-in again");
    CHECK(d.packetCount() == packets, "and the full schema is back");

    // ---- the grammar itself --------------------------------------------------
    using Schema::Decoder;
    QString nm;
    CHECK(Decoder::checkCondition("FRAME_NUM & 7 == 1", &nm).isEmpty() && nm == "FRAME_NUM",
          "mask form parses and reports its field");
    CHECK(Decoder::checkCondition("A == 0x0F").isEmpty(), "hex literal is accepted");
    CHECK(!Decoder::checkCondition("A == 0x").isEmpty(), "bare 0x is not a number");
    CHECK(!Decoder::checkCondition("A == one").isEmpty(), "a word is not a number");
    CHECK(!Decoder::checkCondition("A && B == 1").isEmpty(), "&& is refused, not misread");
    CHECK(!Decoder::checkCondition("bad name == 1").isEmpty(), "spaces in a name are refused");
    CHECK(Decoder::checkCondition("").isEmpty(), "an empty condition is fine (always holds)");

    const QHash<QString, qint64> v{ { "A", 15 } };
    CHECK(Decoder::conditionHolds("A == 0x0F", v),
          "hex on the plain path means 15 (it used to read as 0)");
    CHECK(Decoder::conditionHolds("A == 015", v),
          "a leading zero is decimal, never octal");
    CHECK(Decoder::conditionHolds("A & 0x3 == 3", v) && !Decoder::conditionHolds("A & 0x10 != 0", v),
          "mask with hex literals");
    CHECK(Decoder::conditionHolds("A in 0x0..0xF", v), "hex range bounds");
    CHECK(Decoder::conditionHolds("MISSING == 0", v),
          "at run time an absent field still reads as 0 (reject rules rely on it)");
}

