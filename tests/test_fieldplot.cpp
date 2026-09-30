#include "testutil.h"
#include "fieldplot.h"
#include "logmodel.h"
#include "capturedecoder.h"
#include "schema/schemadecoder.h"

#include <QSet>
#include <QSharedPointer>

static double num(const char *s, bool *ok) { return parseFieldNumber(QString::fromLatin1(s), ok); }
static bool numeric(const char *s) { bool ok=false; num(s,&ok); return ok; }
static double val(const char *s) { bool ok=false; return num(s,&ok); }

TEST_SUITE(fieldplot)
{
    // ---- number extraction ------------------------------------------------
    CHECK(numeric("42") && val("42")==42.0,           "plain integer");
    CHECK(numeric("-3.5") && val("-3.5")==-3.5,       "negative decimal");
    CHECK(numeric("+7") && val("+7")==7.0,            "leading plus");
    CHECK(numeric("  12  ") && val("  12  ")==12.0,   "surrounding whitespace");
    CHECK(numeric("0") && val("0")==0.0,              "zero");
    CHECK(numeric("42 km/h") && val("42 km/h")==42.0, "value with a unit");
    CHECK(numeric("-3.5 m") && val("-3.5 m")==-3.5,   "negative with a unit");
    CHECK(numeric("7 (RESERVED)") && val("7 (RESERVED)")==7.0, "value with annotation");
    CHECK(numeric("0x2A") && val("0x2A")==42.0,       "hex");
    CHECK(numeric("0xff") && val("0xff")==255.0,      "lowercase hex");
    CHECK(numeric("12%") && val("12%")==12.0,         "percentage");

    // Rejections must be rejections, not silent zeros — that distinction is
    // the whole point: a dropped row is reported, a zero is a false reading.
    CHECK(!numeric(""),          "empty string is not numeric");
    CHECK(!numeric("   "),       "whitespace is not numeric");
    CHECK(!numeric("ON"),        "enum label is not numeric");
    CHECK(!numeric("RESERVED"),  "word is not numeric");
    CHECK(!numeric("(none)"),    "'(none)' is not numeric");
    CHECK(!numeric("N/A"),       "'N/A' is not numeric");
    CHECK(!numeric("-"),         "bare sign is not numeric");
    CHECK(!numeric("0x"),        "bare 0x is not numeric");
    CHECK(!numeric("0xZZ"),      "invalid hex is not numeric");
    CHECK(!numeric("ab 12"),     "number not at the start is not numeric");
    // an identifier that merely begins with digits must not become a value
    CHECK(!numeric("12ABC"),     "identifier starting with digits is refused");

    // ---- series extraction over a synthetic model -------------------------
    const Schema::Decoder &d = kavachSchema();
    CHECK(d.isLoaded(), "schema available");

    // Degenerate inputs must not crash.
    CHECK(extractFieldSeries(nullptr, "x", d).isEmpty(),   "null model -> empty");
    CHECK(discoverFieldNames(nullptr, d).isEmpty(),        "null model -> no names");
    {
        LogModel m(nullptr, 100);
        CHECK(extractFieldSeries(&m, "x", d).isEmpty(),    "empty model -> empty");
        CHECK(extractFieldSeries(&m, "", d).isEmpty(),     "empty field name -> empty");
        QVector<LogEntryPtr> v;
        auto e = QSharedPointer<LogEntry>::create();
        e->header.source_id=33; e->header.kvchId=1; e->epochMs=1000;
        e->cacheDerived(); v.append(e);
        m.appendEntries(v);
        CHECK(extractFieldSeries(&m, "anything", d).isEmpty(),
              "entries with no rawBytes yield nothing");
        CHECK(extractFieldSeries(&m, "x", d, 0).isEmpty(), "zero row cap -> empty");
    }

    // Real capture frames. Entries must carry an @-capture LINE in their
    // text: that is where captype comes from, and without it the schema
    // matches nothing. Getting this wrong is exactly the bug this suite
    // caught in the field inspector.
    {
        LogModel m(nullptr, 5000);
        QVector<LogEntryPtr> v;
        for (int k = 0; k < 40; ++k) {
            QByteArray payload(96, '\0');
            for (int i = 0; i < payload.size(); ++i) payload[i] = char(i*13 + 5 + k);
            auto e = QSharedPointer<LogEntry>::create();
            e->header.source_id = 33; e->header.kvchId = 1;
            e->epochMs = 100000 + k * 1000;
            e->text = QStringLiteral("@slrp_33_1 2026-08-08T14:00:%1 %2 %3")
                          .arg(k % 60, 2, 10, QChar('0')).arg(k)
                          .arg(QString::fromLatin1(payload.toHex()));
            e->cacheDerived();
            v.append(e);
        }
        m.appendEntries(v);

        // Sanity: the capture line must actually parse, or everything below
        // is vacuously true.
        const CaptureLine probe = CaptureDecoder::parseLine(m.entryAt(0)->text);
        CHECK(probe.valid, "synthetic @slrp line parses");
        CHECK(probe.typeToken.contains("slrp"), "type token recognised");
        CHECK(!probe.bytes.isEmpty(), "frame bytes recovered from the hex");

        const QStringList names = discoverFieldNames(&m, d);
        CHECK(!names.isEmpty(), "fields discovered from capture lines");

        bool sorted = true;
        for (int i = 1; i < names.size(); ++i)
            if (QString::compare(names[i-1], names[i], Qt::CaseInsensitive) > 0) sorted = false;
        CHECK(sorted, "field names are sorted");
        bool trimmed = true, unique = true;
        QSet<QString> seen;
        for (const QString &n : names) {
            if (n != n.trimmed()) trimmed = false;
            if (seen.contains(n)) unique = false;
            seen.insert(n);
        }
        CHECK(trimmed, "field names are trimmed");
        CHECK(unique, "field names are de-duplicated");

        int plotted = 0;
        for (const QString &n : names) {
            const FieldSeries s2 = extractFieldSeries(&m, n, d);
            if (s2.points.isEmpty()) continue;
            ++plotted;
            CHECK(s2.rowsScanned == 40, "every row scanned");
            CHECK(s2.rowsDecoded > 0, "rows decoded");
            CHECK(s2.rowsMatched == s2.points.size() + s2.rowsNonNumeric,
                  "matched accounts for exactly plotted + non-numeric");
            CHECK(!s2.hitRowCap, "40 rows is under the cap");
            bool ordered = true, inRange = true;
            for (int i = 0; i < s2.points.size(); ++i) {
                if (i && s2.points[i].epochMs < s2.points[i-1].epochMs) ordered = false;
                if (s2.points[i].value < s2.minValue || s2.points[i].value > s2.maxValue)
                    inRange = false;
                if (s2.points[i].row < 0 || s2.points[i].row >= 40) inRange = false;
            }
            CHECK(ordered, "points are in time order");
            CHECK(inRange, "values within min/max and rows within the model");
            break;
        }
        CHECK(plotted > 0, "at least one discovered field plots numerically");

        const FieldSeries none = extractFieldSeries(&m, "no_such_field_xyz", d);
        CHECK(none.points.isEmpty(), "unknown field yields no points");
        CHECK(none.rowsDecoded > 0,  "but rows were still decoded");
        CHECK(none.rowsMatched == 0, "and none matched");

        if (!names.isEmpty()) {
            const FieldSeries capped = extractFieldSeries(&m, names.first(), d, 5);
            CHECK(capped.hitRowCap, "cap reported when it bites");
            CHECK(capped.rowsScanned <= 10, "scanned far fewer than 40 rows");
            if (!capped.points.isEmpty()) {
                CHECK(capped.maxMs > 100000 + 20*1000,
                      "sampling reaches the END of the span, not just the start");
            }
        }

        // Plain text rows must decode to nothing and be counted honestly.
        LogModel plain(nullptr, 100);
        QVector<LogEntryPtr> pv;
        auto t = QSharedPointer<LogEntry>::create();
        t->header.source_id=33; t->header.kvchId=1; t->epochMs=1;
        t->text = "RAD IN Link 1 Error"; t->cacheDerived(); pv.append(t);
        plain.appendEntries(pv);
        const FieldSeries ts = extractFieldSeries(&plain, "anything", d);
        CHECK(ts.rowsScanned == 1,  "text row scanned");
        CHECK(ts.rowsDecoded == 0,  "text row decodes to nothing");
        CHECK(ts.points.isEmpty(),  "and contributes no points");
        CHECK(discoverFieldNames(&plain, d).isEmpty(),
              "no fields discovered from text-only tabs");
    }
}

// =============================================================================
//  Axis ticks.
//
//  The bug this pins: LOCO_MODE takes the values 1, 2, 4 and 6, and the axis
//  was labelled by dividing the range into quarters — 2.25, 3.5, 4.75. Those
//  are numbers the field cannot hold, on a plot whose whole job is to show
//  what the field did. An enum axis has to be labelled in the field's own
//  units.
// =============================================================================

TEST_SUITE(fieldplotticks)
{
    // ---- the case from the screenshot --------------------------------------
    {
        const QVector<double> t = niceTicks(1.0, 6.0, 5, /*integerOnly=*/true);
        CHECK(t.size() >= 2, "an integer range gets ticks");
        for (double v : t) {
            CHECK(qAbs(v - qRound(v)) < 1e-9,
                  "every tick on an integer axis is a whole number");
        }
        CHECK(t.first() == 1.0 && t.last() == 6.0,
              "and the range's own endpoints are labelled");
        CHECK(t.size() == 6, "1..6 is labelled every step, not every other one");
        CHECK(tickDecimals(1.0) == 0, "an integer step needs no decimals");
    }

    // ---- a wider integer range must not produce sixty labels ---------------
    {
        const QVector<double> t = niceTicks(0.0, 1000.0, 5, true);
        CHECK(t.size() <= 8, "a wide range steps up rather than labelling every value");
        CHECK(t.size() >= 3, "but is still labelled");
        for (double v : t) {
            CHECK(qAbs(v - qRound(v)) < 1e-9, "still whole numbers");
        }
    }

    // ---- continuous values keep their fractions ----------------------------
    {
        const QVector<double> t = niceTicks(0.0, 1.0, 5, /*integerOnly=*/false);
        CHECK(t.size() >= 3, "a fractional range is divided");
        bool anyFraction = false;
        for (double v : t) { if (qAbs(v - qRound(v)) > 1e-9) { anyFraction = true; } }
        CHECK(anyFraction, "and is allowed fractional ticks — forcing integers here "
                           "would collapse the axis to 0 and 1");
        CHECK(tickDecimals(0.25) > 0, "a fractional step asks for decimals");
        CHECK(tickDecimals(0.25) <= 6, "but a bounded number of them");
    }

    // ---- degenerate ranges -------------------------------------------------
    {
        const QVector<double> flat = niceTicks(3.0, 3.0, 5, true);
        CHECK(flat.size() == 1 && flat.first() == 3.0,
              "a flat series still gets its one value labelled");
        CHECK(niceTicks(5.0, 1.0, 5, true).isEmpty(),
              "an inverted range produces nothing rather than looping");
        CHECK(niceTicks(0.0, 1e9, 5, false).size() <= 64,
              "and an enormous range cannot run away");
    }

    // ---- no floating-point litter in the labels ----------------------------
    // A 0.1 step accumulated by addition gives 0.30000000000000004, which
    // renders as a label nobody can read past.
    {
        const QVector<double> t = niceTicks(0.0, 0.5, 6, false);
        for (double v : t) {
            const QString label = QString::number(v, 'f', tickDecimals(0.1));
            CHECK(label.size() <= 6, "labels stay short: " + label.toUtf8());
        }
    }
}
