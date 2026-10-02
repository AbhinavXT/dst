#include "testutil.h"
#include "logentry.h"

#include <QDateTime>
#include <QSharedPointer>

static LogEntryPtr mk(qint64 ms, quint8 src, quint16 kv,
                      Severity sev, LogDirection dir, const QString &text) {
    auto e = QSharedPointer<LogEntry>::create();
    e->epochMs = ms; e->header.source_id = src; e->header.kvchId = kv;
    e->severity = sev; e->direction = dir; e->text = text;
    e->cacheDerived();
    return e;
}

// Clipboard formatting. Destination is a report, spreadsheet or ticket, so
// the row structure has to survive — which means the delimiter must not
// appear inside a field, and the severity must be a word a filter can match
// rather than the on-screen glyph.
TEST_SUITE(clipboard)
{
    const qint64 ms = QDateTime(QDate(2026,8,8), QTime(14,5,30,250),
                                Qt::UTC).toMSecsSinceEpoch();

    // ---- empty and header -------------------------------------------------
    CHECK(formatEntriesForClipboard({}, false, true).isEmpty(),
          "no entries yields empty text");
    {
        const QString hdrOnly = formatEntriesForClipboard({}, true, true);
        CHECK(hdrOnly.count('\t') == 4, "header has five tab-separated fields");
        CHECK(!hdrOnly.contains('\n'), "header alone has no trailing newline");
    }

    // ---- a single row -----------------------------------------------------
    {
        QVector<LogEntryPtr> v{ mk(ms, 33, 1, Severity::Error,
                                   LogDirection::In, "RAD IN Link 1 Error") };
        const QString out = formatEntriesForClipboard(v, false, true);
        const QStringList f = out.split('\t');
        CHECK(f.size() == 5,            "five fields");
        CHECK(f.at(0).contains("14:05:30.250"), "UTC timestamp when utc=true");
        CHECK(f.at(1) == "33_1",        "source key");
        CHECK(f.at(2) == "IN",          "direction as a word");
        CHECK(f.at(3) == "ERROR",
              "severity is the WORD, not the on-screen glyph");
        CHECK(!f.at(3).contains("✕"),  "no glyph leaks into the clipboard");
        CHECK(f.at(4) == "RAD IN Link 1 Error", "message last");
    }

    // ---- zone follows the display ----------------------------------------
    {
        QVector<LogEntryPtr> v{ mk(ms, 1, 1, Severity::Info,
                                   LogDirection::None, "x") };
        const QString u = formatEntriesForClipboard(v, false, true);
        const QString l = formatEntriesForClipboard(v, false, false);
        CHECK(u.contains("14:05:30.250"), "utc=true renders UTC");
        const QString localStamp = QDateTime::fromMSecsSinceEpoch(ms)
                                       .toString(Qt::ISODateWithMs);
        CHECK(l.startsWith(localStamp), "utc=false renders local");
    }

    // ---- direction/severity words ----------------------------------------
    {
        QVector<LogEntryPtr> v{
            mk(ms, 1, 1, Severity::Info,  LogDirection::None, "a"),
            mk(ms, 1, 1, Severity::Warn,  LogDirection::Out,  "b"),
        };
        const QString out = formatEntriesForClipboard(v, false, true);
        const QStringList lines = out.split('\n');
        CHECK(lines.size() == 2, "two rows, two lines");
        CHECK(lines.at(0).split('\t').at(2).isEmpty(),
              "LogDirection::None is an empty field, not the word 'none'");
        CHECK(lines.at(0).split('\t').at(3) == "INFO", "info word");
        CHECK(lines.at(1).split('\t').at(2) == "OUT",  "out word");
        CHECK(lines.at(1).split('\t').at(3) == "WARN", "warn word");
    }

    // ---- delimiter safety -------------------------------------------------
    // A tab or newline inside a message would break the row structure of
    // whatever this is pasted into — silently, and only for the odd message
    // that contains one.
    {
        QVector<LogEntryPtr> v{
            mk(ms, 1, 1, Severity::Info, LogDirection::None,
               "has\ta tab and\na newline\r and a CR")
        };
        const QString out = formatEntriesForClipboard(v, false, true);
        CHECK(out.count('\n') == 0, "no stray newline from the message");
        CHECK(out.count('\t') == 4,
              "exactly four delimiters — the message's tab was flattened");
        CHECK(out.contains("has a tab and a newline  and a CR"),
              "content preserved with whitespace substituted");
    }

    // ---- ordering and header ---------------------------------------------
    {
        QVector<LogEntryPtr> v{
            mk(ms,      1, 1, Severity::Info, LogDirection::None, "first"),
            mk(ms+1000, 1, 1, Severity::Info, LogDirection::None, "second"),
        };
        const QString out = formatEntriesForClipboard(v, true, true);
        const QStringList lines = out.split('\n');
        CHECK(lines.size() == 3, "header plus two rows");
        CHECK(lines.at(0).startsWith("Time"), "header first");
        CHECK(lines.at(1).endsWith("first"),  "order preserved");
        CHECK(lines.at(2).endsWith("second"), "order preserved");
    }

    // ---- null entries are skipped, not crashed on ------------------------
    {
        QVector<LogEntryPtr> v{ LogEntryPtr(),
                                mk(ms, 1, 1, Severity::Info, LogDirection::None, "ok"),
                                LogEntryPtr() };
        const QString out = formatEntriesForClipboard(v, false, true);
        CHECK(out.split('\n').size() == 1, "nulls skipped, one row emitted");
        CHECK(out.endsWith("ok"), "the real entry survived");
    }

    // ---- Ctrl+C is the message and nothing else --------------------------
    //
    // The columns beside the message exist to help FIND the row. Once it is
    // found, what gets pasted into the Decode Workbench, a mail or back into
    // this program is the message, and four columns in front of it have to
    // be stripped off by hand every time.
    {
        QVector<LogEntryPtr> v{
            mk(ms, 21, 1, Severity::Error, LogDirection::In,  "@lsrp_21_1 AA BB"),
            mk(ms, 33, 1, Severity::Info,  LogDirection::Out, "second"),
        };
        const QString out = formatMessagesForClipboard(v);
        const QStringList lines = out.split('\n');
        CHECK(lines.size() == 2, "one line per message");
        CHECK(lines.at(0) == QStringLiteral("@lsrp_21_1 AA BB"),
              "and the line IS the message — a capture line comes back out "
              "in the form every frame tool here takes as input");
        CHECK(lines.at(1) == QStringLiteral("second"), "order preserved");
        CHECK(!out.contains('\t'),
              "no time, no source, no direction, no severity");
        CHECK(!out.contains(QStringLiteral("ERROR")),
              "severity in particular: it is the column most likely to be "
              "mistaken for part of the message once pasted");
    }

    // ---- a multi-line message is still one row ---------------------------
    {
        QVector<LogEntryPtr> v{
            mk(ms, 21, 1, Severity::Info, LogDirection::None, "one\ttwo\nthree"),
        };
        const QString out = formatMessagesForClipboard(v);
        CHECK(out.split('\n').size() == 1,
              "a message spanning two lines would paste as two rows and be "
              "counted as two");
        CHECK(out == QStringLiteral("one two three"), "flattened to spaces");
    }

    // ---- nulls and nothing -----------------------------------------------
    {
        CHECK(formatMessagesForClipboard({}).isEmpty(), "nothing copies as nothing");
        QVector<LogEntryPtr> v{ LogEntryPtr(),
                                mk(ms, 1, 1, Severity::Info, LogDirection::None, "ok"),
                                LogEntryPtr() };
        CHECK(formatMessagesForClipboard(v) == QStringLiteral("ok"),
              "nulls skipped, the real entry survived");
    }
}
