#include "readerdir.h"

#include "capturedecoder.h"
#include "logmodel.h"

#include <algorithm>

namespace ReaderDir {
namespace {

QString typeOf(const QString &text)
{
    if (!text.startsWith(QLatin1Char('@'))) return QString();
    const int sp = text.indexOf(QLatin1Char(' '));
    const QStringList parts = text.mid(1, sp < 0 ? -1 : sp - 1).split(QLatin1Char('_'));
    return parts.size() < 3 ? QString() : parts.mid(0, parts.size() - 2).join(QLatin1Char('_'));
}

}  // namespace

QString letter(int dir)
{
    switch (dir) {
    case U: return QStringLiteral("U");
    case N: return QStringLiteral("N");
    case R: return QStringLiteral("R");
    default: return QString::number(dir);
    }
}

bool decode(const QByteArray &b, Read *out)
{
    if (b.size() != 5) return false;
    out->reader = quint8(b.at(0));
    out->tag = quint16(quint8(b.at(1)) | (quint8(b.at(2)) << 8));
    out->readerDir = quint8(b.at(3));
    out->movementDir = quint8(b.at(4));
    return true;
}

bool hasRdir(const LogModel *model)
{
    if (!model) return false;
    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (e && e->text.startsWith(QLatin1String("@rdir_"))) return true;
    }
    return false;
}

Log extract(const LogModel *model)
{
    Log log;
    if (!model) return log;
    int lastArpTag = -1;
    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        const QString type = typeOf(e->text);
        if (type != QLatin1String("rdir") && type != QLatin1String("arp") && type != QLatin1String("rfid")) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid) continue;
        if (cap.type == CapType::Rdir) {
            Read r;
            if (!decode(cap.bytes, &r)) { ++log.badFrames; continue; }
            r.row = i;
            r.ms = e->epochMs;
            log.reads << r;
        } else if (cap.type == CapType::ARP) {
            if (cap.crcChecked && !cap.crcOk) continue;          // a damaged ARP says nothing
            QHash<QString, qint64> raw;
            CaptureDecoder::describe(cap, nullptr, 0, &raw);
            if (!raw.contains(QStringLiteral("LAST_RFID_TAG"))) continue;
            ++log.arpFrames;
            const int tag = int(raw.value(QStringLiteral("LAST_RFID_TAG")));
            // Reported after the last read, before the next one: that read's.
            if (tag != lastArpTag && !log.reads.isEmpty() && log.reads.last().reported < 0) {
                log.reads.last().reported = tag;
                log.reads.last().reportedRow = i;
            }
            lastArpTag = tag;
        } else if (cap.type == CapType::Rfid) {
            const RfidInfo t = CaptureDecoder::decodeRfid(cap.bytes);
            if (t.valid && t.crcOk && t.unique > 0) log.tagLocM.insert(quint16(t.unique), double(t.absLoc));
        }
    }
    // Each reader's direction after each read, over the whole log.
    int d[3] = { U, U, U };
    for (Read &r : log.reads) {
        if (r.reader == 1 || r.reader == 2) d[r.reader] = r.readerDir;
        r.r1Dir = d[1];
        r.r2Dir = d[2];
    }
    return log;
}

QVector<Read> fromRead(const Log &log, int from, int to)
{
    QVector<Read> out;
    int d[3] = { U, U, U };
    const int last = (to < 0 || to >= log.reads.size()) ? log.reads.size() - 1 : to;
    for (int i = qMax(0, from); i <= last; ++i) {
        Read r = log.reads.at(i);
        if (r.reader == 1 || r.reader == 2) d[r.reader] = r.readerDir;
        r.r1Dir = d[1];
        r.r2Dir = d[2];
        out << r;
    }
    return out;
}

QVector<quint16> tagOrder(const Log &log, const QVector<Read> &reads, bool *byLocation)
{
    QVector<quint16> tags;
    for (const Read &r : reads)
        if (!tags.contains(r.tag)) tags << r.tag;
    bool allLocated = !tags.isEmpty();
    for (quint16 t : tags) allLocated = allLocated && log.tagLocM.contains(t);
    if (allLocated)
        std::sort(tags.begin(), tags.end(), [&log](quint16 a, quint16 b) { return log.tagLocM.value(a) < log.tagLocM.value(b); });
    else
        std::sort(tags.begin(), tags.end());
    if (byLocation) *byLocation = allLocated;
    return tags;
}

QString tagLabel(int tag, const QVector<quint16> &order)
{
    if (tag < 0) return QString();
    const int i = order.indexOf(quint16(tag));
    return i >= 0 ? QStringLiteral("R%1 (%2)").arg(i + 1).arg(tag) : QString::number(tag);
}

const QVector<TestCase> &frs18()
{
    // RDSO/SPN/196/2020 v4.0, SIF-0533 v4.22, pages 359-362: FRS 18.
    static const QVector<TestCase> cases = {
        { QStringLiteral("18.1"), QStringLiteral("Reader-1 and reader-2 read every tag, in sequence"),
          QStringLiteral("R1/R1, R2/R2, R3/R3"),
          { { U, U, U, 1 }, { N, N, N, 2 }, { N, N, N, 3 } } },
        { QStringLiteral("18.2"), QStringLiteral("Reader-1 reads every tag; reader-2 does not read R1"),
          QStringLiteral("R1/not read, R2/R2, R3/R3"),
          { { U, U, U, 1 }, { N, U, N, 2 }, { N, N, N, 3 } } },
        { QStringLiteral("18.3"), QStringLiteral("Reader-1 does not read R1; reader-2 reads R1 and R2"),
          QStringLiteral("not read/R1, R2/R2, R3/R3"),
          { { U, U, U, 1 }, { U, N, N, 2 }, { N, N, N, 3 } } },
        { QStringLiteral("18.4"), QStringLiteral("Reader-1 reads R2 first; reader-2 reads R1 and R2 after"),
          QStringLiteral("R2/-, -/R1, -/R2, R3/R3"),
          { { U, U, U, 2 }, { U, U, U, 1 }, { U, N, N, 2 }, { N, N, N, 3 } } },
        { QStringLiteral("18.5"), QStringLiteral("Reader-1 does not read R2; reader-2 does not read R1"),
          QStringLiteral("R1/-, -/R2, R3/-, -/R3"),
          { { U, U, U, 1 }, { U, U, U, 2 }, { N, U, N, 3 }, { N, N, N, 0 } } },
        { QStringLiteral("18.6"), QStringLiteral("Reader-2 reads R1 after reader-1 read R2"),
          QStringLiteral("R1/-, R2/R1, R3/R2, -/R3"),
          { { U, U, U, 1 }, { N, U, N, 2 }, { N, N, N, 3 }, { N, N, N, 0 } } },
        { QStringLiteral("18.7"), QStringLiteral("Reader-2 reads R1 after reader-1 read R3"),
          QStringLiteral("R1/-, R2/-, R3/R1, -/R2, -/R3, -/R4, R4/-"),
          { { U, U, U, 1 }, { N, U, N, 2 }, { N, U, N, 3 }, { N, N, N, 0 }, { N, N, N, 0 },
            { N, N, N, 4 }, { N, N, N, 0 } } },
    };
    return cases;
}

QVector<int> match(const TestCase &tc, const QVector<Read> &reads, const QVector<quint16> &order)
{
    QVector<int> out;
    int next = 0;
    for (const ExpectedRow &row : tc.rows) {
        const int wantTag = row.tag == 0 ? -1 : (row.tag - 1 < order.size() ? int(order.at(row.tag - 1)) : -2);
        int found = -1;
        for (int j = next; j < reads.size(); ++j) {
            const Read &r = reads.at(j);
            if (r.r1Dir == row.r1 && r.r2Dir == row.r2 && r.movementDir == row.ovk && r.reported == wantTag) {
                found = j;
                break;
            }
        }
        out << found;
        if (found >= 0) next = found + 1;
    }
    return out;
}

}  // namespace ReaderDir
