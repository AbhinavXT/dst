#include "fieldindex.h"

#include "capturedecoder.h"
#include "fieldplot.h"          // parseFieldNumber

namespace {

// Field labels arrive from the schema with a leading indent and sometimes
// trailing annotation. Normalise so `field:sig_ov` matches "  SIG_OV".
inline QString norm(const QString &s) { return s.trimmed().toLower(); }

}  // namespace

namespace FieldIndex {

bool ensureDecoded(const LogEntry &e)
{
    if (e.fieldsDecoded) return !e.decodedFields.isEmpty();
    e.fieldsDecoded = true;          // set first: a failed decode must not retry

    // Same path the field inspector uses. Decoding must start from the TEXT,
    // not the payload bytes: the schema selects a packet by captype, which
    // comes from the @type_loco_ctrl token at the head of a capture line.
    const CaptureLine cap = CaptureDecoder::parseLine(e.text);
    if (!cap.valid || cap.bytes.isEmpty()) return false;

    const QVector<FieldRow> rows = CaptureDecoder::describe(cap);
    e.decodedFields.reserve(rows.size());
    for (const FieldRow &r : rows) {
        const QString name = r.field.trimmed();
        if (name.isEmpty()) continue;
        e.decodedFields.append({ name, r.value });
    }
    return !e.decodedFields.isEmpty();
}

QString value(const LogEntry &e, const QString &fieldName)
{
    if (!ensureDecoded(e)) return QString();
    const QString want = norm(fieldName);
    for (const auto &p : e.decodedFields) {
        if (norm(p.first) == want) return p.second;
    }
    // Prefix match, so `field:Loco_Health` finds the multiplexed
    // "Loco_Health (faults 6-11)" without the caller knowing which group
    // the frame carried.
    for (const auto &p : e.decodedFields) {
        if (norm(p.first).startsWith(want)) return p.second;
    }
    // Substring, so an expanded health bit is reachable by its fault name
    // alone: the row is labelled "B22 SPEED_SENSOR2_FAULT" to cross-
    // reference the SIF tables, but nobody writing a query wants to
    // remember the bit number.
    for (const auto &p : e.decodedFields) {
        if (norm(p.first).contains(want)) return p.second;
    }
    return QString();
}

double number(const LogEntry &e, const QString &fieldName, bool *ok)
{
    if (ok) *ok = false;
    const QString v = value(e, fieldName);
    if (v.isNull()) return 0.0;
    return parseFieldNumber(v, ok);
}

QString captype(const LogEntry &e)
{
    // Cheap: parseLine only reads the token at the head of the line, and
    // does not decode the frame.
    const CaptureLine cap = CaptureDecoder::parseLine(e.text);
    if (!cap.valid) return QString();
    // typeToken is "lsrp_1234_1"; the captype is the part before the first
    // underscore.
    const QString t = cap.typeToken;
    const int us = t.indexOf(QLatin1Char('_'));
    return (us > 0 ? t.left(us) : t).toLower();
}

QStringList names(const LogEntry &e)
{
    QStringList out;
    if (!ensureDecoded(e)) return out;
    for (const auto &p : e.decodedFields) out << p.first;
    return out;
}

}  // namespace FieldIndex
