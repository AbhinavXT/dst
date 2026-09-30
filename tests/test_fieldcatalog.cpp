#include "testutil.h"
#include "fieldcatalog.h"
#include "logquery.h"
#include "logentry.h"

#include <QSharedPointer>

namespace {
const char *kMap = R"({
  "fields": [
    {"name":"loco_mode","field":"LOCO_MODE","packet":"lsrp","desc":"mode",
     "values":{"stand_by":1,"staff_responsible":2,"full_supervision":4}},
    {"name":"cab1_active","field":"24 cab1_active","packet":"dip1",
     "values":{"inactive":0,"active":1}},
    {"name":"train_speed","field":"TRAIN_SPEED","packet":"lsrp"},
    {"name":"bare_name","packet":"lsrp"}
  ]
})";

LogEntryPtr lsrp(qint64 ms, quint32 mode, quint32 speed = 0)
{
    QByteArray f(64, '\0');
    // The frame must agree with itself: the decoder places the body from
    // message_len - PKT_LENGTH, so a fixture leaving both at zero is a frame
    // no loco would send and no longer decodes.
    const int kPktLen = 29;
    f[4] = char(10 + kPktLen);   // message_len 39, LE
    f[5] = 0;
    int p = 80;
    auto put = [&](quint32 v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            if ((v >> i) & 1) f[p >> 3] = f[p >> 3] | char(1 << (7 - (p & 7)));
            ++p;
        }
    };
    put(0,4); put(kPktLen,7); put(1,17); put(0,20); put(0,3); put(0,23);
    put(0,9); put(0,9); put(0,2); put(0,11); put(speed,9); put(0,2); put(0,3);
    put(mode,4); put(0,10); put(0,1); put(0,3); put(0,9); put(0,3); put(0,2);
    put(0,4); put(0,1); put(0,4); put(0,2); put(0,6); put(0,32);
    auto e = QSharedPointer<LogEntry>::create();
    e->epochMs = ms; e->header.source_id = 21; e->header.kvchId = 1;
    e->text = QStringLiteral("@lsrp_1_1 2026-08-19T10:00:00 1 %1")
                  .arg(QString::fromLatin1(f.toHex()));
    e->cacheDerived();
    return e;
}
bool q(const char *query, const LogEntryPtr &e)
{
    LogQuery lq;
    if (!lq.parse(QString::fromLatin1(query))) return false;
    return lq.match(*e, nullptr);
}
bool bad(const char *query) { LogQuery lq; return !lq.parse(QString::fromLatin1(query)); }
}  // namespace

// The catalogue is what lets a test case be written in the language of the
// specification rather than the language of the decoder.
TEST_SUITE(fieldcatalog)
{
    FieldCatalog &c = FieldCatalog::instance();
    const bool hadOne = c.isLoaded();

    // ---- loading ----------------------------------------------------------
    {
        QString err;
        FieldCatalog t;
        CHECK(!t.loadFromJson("not json", &err), "malformed JSON rejected");
        CHECK(!t.loadFromJson("[]", &err), "an array is rejected — object expected");
        CHECK(!t.loadFromJson(R"({"fields":[]})", &err), "empty field list rejected");
        CHECK(!t.isLoaded(), "nothing loaded from a bad file");
    }

    CHECK(c.loadFromJson(kMap), "test catalogue loads");
    CHECK(c.fieldCount() == 4, "four definitions");

    // ---- resolution -------------------------------------------------------
    CHECK(c.resolveField("loco_mode") == "LOCO_MODE", "alias to decoder name");
    CHECK(c.resolveField("LOCO_MODE") == "LOCO_MODE", "already-real name passes through");
    CHECK(c.resolveField("LoCo_MoDe") == "LOCO_MODE", "case-insensitive");
    CHECK(c.resolveField("unknown_thing") == "unknown_thing",
          "an uncatalogued name passes through UNCHANGED — the catalogue is "
          "additive, never a gate");
    CHECK(c.resolveField("bare_name") == "bare_name",
          "omitting 'field' means the alias is the decoder name");
    CHECK(c.resolvePacket("loco_mode") == "lsrp", "packet recorded");
    CHECK(c.resolvePacket("cab1_active") == "dip1", "…per field");
    CHECK(c.resolvePacket("unknown_thing").isEmpty(), "unknown means any packet");

    // ---- symbolic values --------------------------------------------------
    {
        bool ok = false;
        CHECK(c.resolveValue("loco_mode", "staff_responsible", &ok) == 2 && ok,
              "symbol resolves to its number");
        CHECK(c.resolveValue("loco_mode", "STAFF_RESPONSIBLE", &ok) == 2 && ok,
              "case-insensitive");
        c.resolveValue("loco_mode", "no_such_mode", &ok);
        CHECK(!ok, "an unknown symbol reports failure");
        c.resolveValue("train_speed", "fast", &ok);
        CHECK(!ok, "a field with no value table reports failure");
        CHECK(c.symbolFor("loco_mode", 4) == "full_supervision",
              "reverse lookup for display");
        CHECK(c.symbolFor("loco_mode", 99).isEmpty(), "unmapped value has no symbol");
    }

    // ---- queries written in specification language ------------------------
    {
        const LogEntryPtr sr = lsrp(1000, 2, 30);       // Staff Responsible
        const LogEntryPtr fs = lsrp(2000, 4, 60);       // Full Supervision

        CHECK(q("field:loco_mode=staff_responsible", sr),
              "a case can name the mode instead of the number");
        CHECK(!q("field:loco_mode=staff_responsible", fs), "and it discriminates");
        CHECK(q("field:loco_mode=full_supervision", fs), "the other mode");
        CHECK(q("field:loco_mode=2", sr), "the numeric form still works");
        CHECK(q("field:LOCO_MODE=2", sr), "and so does the raw field name");
        CHECK(q("field:train_speed>50", fs), "aliases work for comparisons too");
        CHECK(!q("field:train_speed>50", sr), "and discriminate");

        // A mistyped symbol is a PARSE error, not a query that quietly
        // matches nothing. This is the failure the catalogue exists to
        // prevent: `field:LOCO_MODE=2` with a wrong digit runs happily.
        CHECK(bad("field:loco_mode=staff_responsable"),
              "a misspelt symbol is refused at parse time");
        LogQuery lq;
        lq.parse("field:loco_mode=staff_responsable");
        CHECK(lq.errorString().contains("not a value"), "and says so");
        CHECK(lq.errorString().contains("staff_responsible"),
              "and lists what it could have meant");
    }

    // ---- packet scoping ---------------------------------------------------
    // cab1_active belongs to dip1. An LSRP frame must never satisfy it,
    // even if a field of that name were somehow decodable there.
    {
        const LogEntryPtr e = lsrp(1000, 2);
        CHECK(!q("field:cab1_active=active", e),
              "a field scoped to dip1 does not match an lsrp frame");
        CHECK(!q("field:cab1_active=inactive", e),
              "not even for the zero value — wrong packet is not 'absent'");
    }

    // ---- completion -------------------------------------------------------
    {
        CHECK(c.completionsFor("loco").contains("loco_mode"), "alias completion");
        CHECK(c.completionsFor("").size() == 4, "empty prefix offers everything");
        const QStringList vals = c.completionsFor("loco_mode=");
        CHECK(vals.contains("loco_mode=staff_responsible"),
              "values are offered once the field is typed — the numbers are "
              "exactly what nobody remembers");
        CHECK(vals.size() == 3, "all three symbols");
        CHECK(c.completionsFor("nosuch=").isEmpty(), "unknown field offers nothing");
    }

    c.clear();
    CHECK(!c.isLoaded(), "cleared");
    CHECK(c.resolveField("loco_mode") == "loco_mode",
          "with no catalogue, names pass through and raw queries still work");
    Q_UNUSED(hadOne);
}
