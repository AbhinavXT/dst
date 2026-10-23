#include "testutil.h"

#include "capturedecoder.h"
#include "fieldinspector.h"
#include "logentry.h"
#include "rejectrules.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>

// =============================================================================
//  Session 338 — sub-packet fields reach the reject rules. Decoder::walkStruct
//  took its Ctx by value, so CUR / NEXT_SIG_ASPECT, AUTHORITY_TYPE,
//  TRAIN_SECTION_TYPE, TSR_STATUS ... never reached rawValues and the rules
//  on them never fired in the Field Inspector, the run report, the hover
//  card or "Why is this row coloured?". Real frames: replay/.
// =============================================================================

namespace {
QStringList slrpLines(int max)
{
    QStringList out;
    QDir dir(QStringLiteral(DL_SRC_DIR "/replay"));
    for (const QString &name : dir.entryList({ QStringLiteral("*.cap") }, QDir::Files, QDir::Name)) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) continue;
        while (!f.atEnd() && out.size() < max) {
            const QString l = QString::fromLatin1(f.readLine()).trimmed();
            if (l.startsWith(QLatin1String("@slrp_"))) out << l;
        }
    }
    return out;
}

qint64 leadingNumber(const QString &value)
{
    return value.trimmed().section(QLatin1Char(' '), 0, 0).toLongLong();
}

// The line with `bytes` in place of its own, CRC made good (JAMCRC over all
// but the last four bytes, stored big-endian: the SLRP recipe).
QString withBytes(const QString &line, QByteArray bytes)
{
    const quint32 crc = CaptureDecoder::jamcrc(bytes, 0, bytes.size() - 4);
    for (int i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = char((crc >> (24 - 8 * i)) & 0xFF);
    return line.section(QLatin1Char(' '), 0, 2) + QLatin1Char(' ') + QString::fromLatin1(bytes.toHex(' ').toUpper());
}

void writeBits(QByteArray &b, int bitOffset, int bits, quint32 v)   // msb-first
{
    for (int i = 0; i < bits; ++i) {
        const int bit = bitOffset + i;
        const bool on = (v >> (bits - 1 - i)) & 1u;
        // data(), not operator[]: Qt 5's QByteRef does not bind to char&.
        char &byte = b.data()[bit / 8];
        const char mask = char(0x80 >> (bit % 8));
        byte = on ? char(byte | mask) : char(byte & ~mask);
    }
}
}  // namespace

TEST_SUITE(session338)
{
    const QStringList lines = slrpLines(400);
    CHECK(lines.size() >= 100, "fixture: real SLRP lines from replay/");
    if (lines.isEmpty()) return;

    // ---- the values are there, and agree with the rows ----------------------------
    const QString first = lines.first();
    const CaptureLine cap = CaptureDecoder::parseLine(first);
    QHash<QString, qint64> raw;
    const QVector<FieldRow> rows = CaptureDecoder::describe(cap, nullptr, 0, &raw);
    QStringList checked, wrong;
    for (const char *name : { "CUR_SIG_ASPECT", "NEXT_SIG_ASPECT", "AUTHORITY_TYPE", "TRAIN_SECTION_TYPE" }) {
        const QString f = QString::fromLatin1(name);
        for (const FieldRow &r : rows)
            if (r.field.trimmed() == f) {
                checked << f;
                if (!raw.contains(f) || raw.value(f) != leadingNumber(r.value)) wrong << f;
                break;
            }
    }
    CHECK(checked.size() == 4 && wrong.isEmpty(),
          QByteArray("the Movement Authority's fields are in the raw values, as decoded (wrong: ")
              + wrong.join(QLatin1Char(',')).toUtf8() + ")");
    CHECK(raw.contains(QStringLiteral("FRAME_NUM")) && raw.contains(QStringLiteral("LAST_REF_RFID")),
          "the header's still are");

    // ---- a spare current aspect is now judged ----------------------------------------
    int at = -1, bits = 0;
    for (const FieldRow &r : rows)
        if (r.field.trimmed() == QLatin1String("CUR_SIG_ASPECT")) { at = r.bitOffset; bits = r.bitLength; }
    CHECK(at > 0 && bits == 6, "fixture: CUR_SIG_ASPECT's bits in the real frame");
    if (at > 0) {
        QByteArray b = cap.bytes;
        writeBits(b, at, bits, 16);
        const QString spare = withBytes(first, b);
        const CaptureLine sc = CaptureDecoder::parseLine(spare);
        CHECK(sc.valid && sc.crcChecked && sc.crcOk, "the edited frame parses, CRC good");
        QHash<QString, qint64> sraw;
        CaptureDecoder::describe(sc, nullptr, 0, &sraw);
        bool fired = false;
        for (const auto &f : kavachRejectRules().evaluate(sraw, sc.typeToken))
            if (f.rule.clause == QLatin1String("31.23.17")) fired = true;
        CHECK(sraw.value(QStringLiteral("CUR_SIG_ASPECT")) == 16 && fired,
              "CUR_SIG_ASPECT 16 (spare): rule 31.23.17 fires");

        FieldInspector fi;
        auto e = QSharedPointer<LogEntry>::create();
        e->text = spare;
        e->cacheDerived();
        fi.setDecoder(&kavachSchema());
        fi.showEntry(e);
        CHECK(fi.rejectReasons().join(QLatin1Char('\n')).contains(QStringLiteral("CUR_SIG_ASPECT")),
              "and the Field Inspector says a receiver would not process it");
    }

    // What the fix changes on the real captures: frames a sub-packet rule now
    // fires on, by clause. Printed, not pinned: it is what the data says.
    QMap<QString, int> byClause;
    for (const QString &l : slrpLines(5000)) {
        const CaptureLine c = CaptureDecoder::parseLine(l);
        QHash<QString, qint64> r;
        CaptureDecoder::describe(c, nullptr, 0, &r);
        bool ref = false, auth = false;
        for (const auto &f : kavachRejectRules().evaluate(r, c.typeToken)) {
            ++byClause[f.rule.clause + QLatin1Char(' ') + f.rule.field];
            ref = ref || f.rule.field == QLatin1String("REF_PROF_ID");
            auth = auth || f.rule.field == QLatin1String("AUTHORITY_TYPE");
        }
        if (auth && !ref) ++byClause[QStringLiteral("(AUTHORITY_TYPE without REF_PROF_ID)")];
    }
    for (auto it = byClause.cbegin(); it != byClause.cend(); ++it)
        printf("      real SLRP frames: %4d  %s\n", it.value(), qPrintable(it.key()));
}
