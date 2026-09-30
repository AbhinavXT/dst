#include "roundtrip.h"

#include "messagedispatcher.h"
#include "sessionreader.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace RoundTrip {

const int kMaxDiffBytes = 32;

// Distinct parseBody notes kept per packet type. The same note repeats on
// every frame of a type; four different ones is already the whole story.
static const int kMaxNotes = 6;

QString outcomeName(Outcome o)
{
    switch (o) {
    case Outcome::Identical:    return QStringLiteral("reproduced");
    case Outcome::Differs:      return QStringLiteral("not reproduced");
    case Outcome::NotEncodable: return QStringLiteral("not encodable");
    case Outcome::ParseFailed:  return QStringLiteral("not parsed");
    case Outcome::EncodeFailed: return QStringLiteral("not built");
    case Outcome::NoPacket:     return QStringLiteral("no packet in schema");
    case Outcome::TooShort:     return QStringLiteral("shorter than its envelope");
    }
    return QStringLiteral("unknown");
}

bool isFrameVerdict(Outcome o)
{
    return o == Outcome::Identical || o == Outcome::Differs
        || o == Outcome::ParseFailed || o == Outcome::EncodeFailed;
}

int bodyStart(const Schema::PacketInfo &pi)
{
    return pi.bodyOffsetBits > 0 ? pi.bodyOffsetBits / 8 : 0;
}

Result check(const Schema::Encoder &enc, const QString &captype,
             const QByteArray &frame, bool keepBody)
{
    Result r;
    r.captype = captype;

    const Schema::PacketInfo pi = enc.packet(captype);
    if (!pi.ok) {
        r.outcome = Outcome::NoPacket;
        r.detail  = QStringLiteral("the schema defines no packet for '%1'").arg(captype);
        return r;
    }
    if (!pi.unsupported.isEmpty()) {
        r.outcome = Outcome::NotEncodable;
        r.detail  = pi.unsupported.join(QStringLiteral(", "));
        return r;
    }

    // The in-capture envelope. parseBody() starts at bit 0 of whatever it is
    // handed, so a captured arp/lsrp frame passed whole has its message
    // header read as PKT_TYPE — and the result is not an error, it is
    // confident nonsense that mostly round-trips.
    r.bodyStart = bodyStart(pi);
    if (frame.size() <= r.bodyStart) {
        r.outcome = Outcome::TooShort;
        r.detail  = QStringLiteral("%1 B, but the body starts at byte %2")
                        .arg(frame.size()).arg(r.bodyStart);
        return r;
    }

    const QByteArray body = frame.mid(r.bodyStart);
    const Schema::ParsedPacket pp = enc.parseBody(captype, body);
    r.notes = pp.notes;
    if (!pp.ok) {
        r.outcome = Outcome::ParseFailed;
        r.detail  = pp.error;
        return r;
    }
    r.bodyBytes = pp.bodyBytes;
    r.tailBytes = pp.tailBytes;
    if (pp.bodyBytes <= 0) {
        // A definition that consumes nothing "succeeds" at reading every
        // buffer. Nothing downstream can tell that apart from a packet with
        // no fields, so say it here.
        r.outcome = Outcome::ParseFailed;
        r.detail  = QStringLiteral("the packet definition consumed no bytes");
        return r;
    }

    QString encErr;
    const QByteArray rebuilt = enc.encodeBody(captype, pp.header, pp.subs, &encErr);
    if (rebuilt.isEmpty()) {
        r.outcome = Outcome::EncodeFailed;
        r.detail  = encErr.isEmpty()
                        ? QStringLiteral("the encoder produced no bytes and gave no reason")
                        : encErr;
        return r;
    }
    r.rebuiltBytes = rebuilt.size();
    if (keepBody) { r.rebuiltBody = rebuilt; }

    const QByteArray orig = body.left(pp.bodyBytes);
    if (rebuilt == orig) {
        r.outcome = Outcome::Identical;
        return r;
    }

    r.outcome = Outcome::Differs;
    const int common = qMin(rebuilt.size(), orig.size());
    for (int i = 0; i < common; ++i) {
        if (rebuilt.at(i) == orig.at(i)) { continue; }
        if (r.firstDiff < 0) { r.firstDiff = i; }
        if (r.diffBytes.size() < kMaxDiffBytes) { r.diffBytes.push_back(i); }
    }
    // A length difference is a difference at every byte past the shorter of
    // the two, and the first of those is where it starts.
    if (rebuilt.size() != orig.size()) {
        if (r.firstDiff < 0) { r.firstDiff = common; }
        for (int i = common; i < qMax(rebuilt.size(), orig.size())
                             && r.diffBytes.size() < kMaxDiffBytes; ++i) {
            r.diffBytes.push_back(i);
        }
        r.detail = QStringLiteral("rebuilt %1 B against a %2 B body; first difference at byte %3")
                       .arg(rebuilt.size()).arg(orig.size()).arg(r.firstDiff);
    } else {
        r.detail = QStringLiteral("%1 of %2 body bytes differ; first at byte %3")
                       .arg(r.diffBytes.size()).arg(orig.size()).arg(r.firstDiff);
    }
    return r;
}

QString fieldAtByte(const QVector<FieldRow> &rows, int absByte)
{
    QStringList hits;
    for (const FieldRow &row : rows) {
        if (!row.hasSpan()) { continue; }
        if (absByte < row.byteStart() || absByte > row.byteEnd()) { continue; }
        if (!hits.contains(row.field)) { hits << row.field; }
    }
    return hits.join(QStringLiteral(", "));
}

QString captureLineFor(const CaptureLine &c, const QByteArray &bytes)
{
    const QString hex = QString::fromLatin1(bytes.toHex(' ')).toUpper();
    return QStringLiteral("@%1_%2_%3 %4 %5 %6")
        .arg(c.typeToken)
        .arg(c.locoId).arg(c.ctrlId)
        .arg(c.rtc.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")))
        .arg(c.seq)
        .arg(hex);
}

// ---------------------------------------------------------------------------
//  Report
// ---------------------------------------------------------------------------

void Report::add(const CaptureLine &c, const Result &r, int maxSamples)
{
    Tally &t = byType[c.typeToken];
    t.captype = c.typeToken;
    ++t.frames;

    if (c.crcChecked) { ++t.crcChecked; if (c.crcOk) { ++t.crcOk; } }

    for (const QString &n : r.notes) {
        if (t.notes.size() < kMaxNotes && !t.notes.contains(n)) { t.notes << n; }
    }

    switch (r.outcome) {
    case Outcome::Identical:    ++t.identical;    break;
    case Outcome::Differs:      ++t.differs;      break;
    case Outcome::ParseFailed:  ++t.parseFailed;  break;
    case Outcome::EncodeFailed: ++t.encodeFailed; break;
    case Outcome::NotEncodable: ++t.notEncodable; break;
    case Outcome::TooShort:     ++t.tooShort;     break;
    case Outcome::NoPacket:                       break;
    }

    if (r.outcome == Outcome::Identical) { return; }

    if (t.firstProblem.isEmpty()) {
        // "not encodable" is about the packet definition, and the detail is
        // already the list of grammar the writer lacks; prefixing it with the
        // outcome name gives "not encodable — not encodable — repeat".
        t.firstProblem = (r.outcome == Outcome::NotEncodable)
                             ? r.detail
                             : QStringLiteral("%1 — %2").arg(outcomeName(r.outcome), r.detail);
    }
    if (r.outcome == Outcome::Differs && r.firstDiff >= 0) {
        ++t.diffHistogram[r.firstDiff];
    }

    // Samples are for the outcomes an operator would want the bytes of.
    // "not encodable" is a property of the schema and the same for every
    // frame of the type, so one line of text says all of it.
    const bool wantSample = (r.outcome == Outcome::Differs
                             || r.outcome == Outcome::ParseFailed
                             || r.outcome == Outcome::EncodeFailed);
    if (!wantSample || t.samples.size() >= maxSamples) { return; }

    Sample s;
    s.captype   = c.typeToken;
    s.rtc       = c.rtc;
    s.seq       = c.seq;
    s.locoId    = c.locoId;
    s.ctrlId    = c.ctrlId;
    s.original  = c.bytes;
    s.diffBytes = r.diffBytes;
    s.bodyStart = r.bodyStart;
    s.detail    = r.detail;
    if (!r.rebuiltBody.isEmpty()) {
        // The captured frame with the rebuilt body spliced back in: same
        // envelope, same tail, so the two frames can go to Frame Diff as a
        // pair and the difference is shown as fields rather than as bytes.
        s.rebuiltFrame = c.bytes.left(r.bodyStart)
                       + r.rebuiltBody
                       + c.bytes.mid(r.bodyStart + r.bodyBytes);
    }
    t.samples.push_back(s);
}

QStringList Report::captypes() const
{
    QStringList out = byType.keys();
    out.sort();
    return out;
}

int Report::totalIdentical() const
{
    int n = 0;
    for (const Tally &t : byType) { n += t.identical; }
    return n;
}

int Report::totalDiffers() const
{
    int n = 0;
    for (const Tally &t : byType) { n += t.differs; }
    return n;
}

int Report::totalAnswerable() const
{
    int n = 0;
    for (const Tally &t : byType) { n += t.answerable(); }
    return n;
}

QString Report::toText() const
{
    QStringList out;
    out << QStringLiteral("DLConsole round-trip validation");
    out << QStringLiteral("===============================");
    out << QString();
    out << QStringLiteral("Question: for each captured frame, does");
    out << QStringLiteral("    parseBody(bytes) -> encodeBody(...) == bytes");
    out << QStringLiteral("hold? Where it does not, a frame this program builds for that");
    out << QStringLiteral("packet type is not the shape the equipment builds.");
    out << QString();
    out << QStringLiteral("Schema      : %1")
               .arg(schemaPath.isEmpty() ? QStringLiteral("built into the binary")
                                         : schemaPath);
    out << QStringLiteral("Run at      : %1")
               .arg(startedAt.toString(Qt::ISODate));
    for (const QString &s : sources) {
        out << QStringLiteral("Source      : %1").arg(s);
    }
    out << QStringLiteral("Lines read  : %1").arg(linesSeen);
    out << QStringLiteral("Frames      : %1").arg(framesSeen);
    if (filesScanned || filesUnreadable) {
        out << QStringLiteral("Files       : %1 read, %2 unreadable")
                   .arg(filesScanned).arg(filesUnreadable);
    }
    if (cancelled) { out << QStringLiteral("*** CANCELLED — this is a partial scan ***"); }
    out << QString();

    out << QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
               .arg(QStringLiteral("type"),      -10)
               .arg(QStringLiteral("frames"),      8)
               .arg(QStringLiteral("reproduced"), 11)
               .arg(QStringLiteral("differ"),      7)
               .arg(QStringLiteral("no parse"),    9)
               .arg(QStringLiteral("no build"),    9)
               .arg(QStringLiteral("CRC ok"),      9);
    out << QString(66, QLatin1Char('-'));

    const QStringList types = captypes();
    for (const QString &k : types) {
        const Tally &t = byType.value(k);
        const QString crc = t.crcChecked
            ? QStringLiteral("%1/%2").arg(t.crcOk).arg(t.crcChecked)
            : QStringLiteral("—");
        out << QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
                   .arg(k, -10)
                   .arg(t.frames, 8)
                   .arg(t.notEncodable ? QStringLiteral("—") : QString::number(t.identical), 11)
                   .arg(t.differs, 7)
                   .arg(t.parseFailed, 9)
                   .arg(t.encodeFailed, 9)
                   .arg(crc, 9);
    }
    out << QString();

    // Per-type detail, but only where there is something to say.
    for (const QString &k : types) {
        const Tally &t = byType.value(k);
        if (t.notEncodable == 0 && t.differs == 0 && t.parseFailed == 0
            && t.encodeFailed == 0 && t.tooShort == 0 && t.notes.isEmpty()) {
            continue;
        }
        out << QStringLiteral("%1").arg(k);
        if (t.notEncodable) {
            out << QStringLiteral("  not encodable: %1").arg(t.firstProblem);
            out << QStringLiteral("  (%1 frame(s) seen; nothing can be built for this type)")
                       .arg(t.notEncodable);
        }
        if (!t.firstProblem.isEmpty() && !t.notEncodable) {
            out << QStringLiteral("  first problem: %1").arg(t.firstProblem);
        }
        if (t.tooShort) {
            out << QStringLiteral("  %1 frame(s) shorter than the packet's envelope")
                       .arg(t.tooShort);
        }
        for (const QString &n : t.notes) {
            out << QStringLiteral("  note: %1").arg(n);
        }
        if (!t.diffHistogram.isEmpty()) {
            QList<int> keys = t.diffHistogram.keys();
            std::sort(keys.begin(), keys.end());
            QStringList h;
            for (int b : keys) {
                h << QStringLiteral("byte %1 (%2)").arg(b).arg(t.diffHistogram.value(b));
            }
            out << QStringLiteral("  first difference at: %1").arg(h.join(QStringLiteral(", ")));
        }
        for (const Sample &s : t.samples) {
            out << QStringLiteral("  sample %1 seq %2: %3")
                       .arg(s.rtc.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                       .arg(s.seq)
                       .arg(s.detail);
        }
        out << QString();
    }

    out << QStringLiteral("Frames where the question applies: %1").arg(totalAnswerable());
    out << QStringLiteral("Reproduced byte for byte         : %1").arg(totalIdentical());
    out << QString();
    out << QStringLiteral("Reproduction is not correctness. A field that is mislabelled but");
    out << QStringLiteral("the right width round-trips perfectly; this says only that the");
    out << QStringLiteral("bytes can be rebuilt, not that they mean what the schema says.");
    return out.join(QStringLiteral("\n"));
}

// ---------------------------------------------------------------------------
//  Scanning
// ---------------------------------------------------------------------------

namespace {

void addLine(const Schema::Encoder &enc, const QString &line,
             Report *report, int maxSamples)
{
    ++report->linesSeen;
    const CaptureLine c = CaptureDecoder::parseLine(line);
    if (!c.valid) { return; }
    ++report->framesSeen;

    const Result r = check(enc, c.typeToken, c.bytes, /*keepBody=*/true);
    if (r.outcome == Outcome::NoPacket) { return; }   // not ours to judge
    report->add(c, r, maxSamples);
}

}  // namespace

void scanLines(const Schema::Encoder &enc, const QStringList &lines,
               Report *report, int maxSamples,
               const std::function<bool()> &cancelled,
               const std::function<void(qint64, qint64)> &progress)
{
    if (!report) { return; }
    const qint64 total = lines.size();
    qint64 done = 0;
    for (const QString &l : lines) {
        if (cancelled && cancelled()) { report->cancelled = true; return; }
        addLine(enc, l, report, maxSamples);
        if (progress && (++done % 2000) == 0) { progress(done, total); }
    }
    if (progress) { progress(done, total); }
}

// ---------------------------------------------------------------------------
//  Runner
// ---------------------------------------------------------------------------

Runner::Runner(Job job, QObject *parent)
    : QThread(parent), m_job(std::move(job))
{
    qRegisterMetaType<RoundTrip::Report>("RoundTrip::Report");
}

void Runner::run()
{
    Report rep;
    rep.startedAt  = QDateTime::currentDateTime();
    rep.schemaPath = m_job.schemaPath;

    // The worker's own encoder. Loading it here rather than borrowing the
    // one PacketBuilder holds keeps a schema reload on the GUI thread from
    // changing the definition mid-scan — and a scan whose first half used a
    // different schema than its second half is evidence of nothing.
    Schema::Encoder enc;
    QString err;
    const QString path = m_job.schemaPath.isEmpty()
                             ? QStringLiteral(":/schema/kavach.xml")
                             : m_job.schemaPath;
    if (!enc.load(path, &err)) {
        rep.warnings << QStringLiteral("schema %1 could not be loaded: %2").arg(path, err);
        emit finished(rep);
        return;
    }

    auto cancelled = [this] { return m_cancel.load(std::memory_order_relaxed); };

    const qint64 totalUnits = qint64(m_job.entries.size())
                            + qint64(m_job.capFiles.size() + m_job.sessionFiles.size());
    qint64 unitsDone = 0;

    // ---- live log snapshot -------------------------------------------------
    if (!m_job.entries.isEmpty()) {
        rep.sources << QStringLiteral("live log (%1 entries)").arg(m_job.entries.size());
        for (const LogEntryPtr &e : m_job.entries) {
            if (cancelled()) { rep.cancelled = true; emit finished(rep); return; }
            if (e) { addLine(enc, e->text, &rep, m_job.maxSamples); }
            if ((++unitsDone % 2000) == 0) { emit progress(unitsDone, totalUnits); }
        }
    }

    // ---- capture text files ------------------------------------------------
    for (const QString &p : m_job.capFiles) {
        if (cancelled()) { rep.cancelled = true; break; }
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            ++rep.filesUnreadable;
            rep.warnings << QStringLiteral("%1: %2")
                                .arg(QFileInfo(p).fileName(), f.errorString());
            ++unitsDone;
            continue;
        }
        ++rep.filesScanned;
        rep.sources << QFileInfo(p).fileName();
        QTextStream ts(&f);
        while (!ts.atEnd()) {
            if (cancelled()) { rep.cancelled = true; break; }
            addLine(enc, ts.readLine(), &rep, m_job.maxSamples);
            if ((rep.linesSeen % 5000) == 0) { emit progress(unitsDone, totalUnits); }
        }
        emit progress(++unitsDone, totalUnits);
    }

    // ---- recorded sessions -------------------------------------------------
    for (const QString &p : m_job.sessionFiles) {
        if (cancelled()) { rep.cancelled = true; break; }
        SessionReader reader;
        if (!reader.open(p)) {
            ++rep.filesUnreadable;
            rep.warnings << QStringLiteral("%1: %2")
                                .arg(QFileInfo(p).fileName(), reader.errorString());
            ++unitsDone;
            continue;
        }
        ++rep.filesScanned;
        rep.sources << QFileInfo(p).fileName();
        while (reader.next()) {
            if (cancelled()) { rep.cancelled = true; break; }
            // Colour classification is irrelevant here — only the text is —
            // so the entry is built without rules rather than with a copy of
            // them the scan would never read.
            const LogEntryPtr e =
                MessageDispatcher::buildEntry(reader.wire(), reader.arrivalMs(), nullptr);
            if (e) { addLine(enc, e->text, &rep, m_job.maxSamples); }
            if ((rep.linesSeen % 5000) == 0) { emit progress(unitsDone, totalUnits); }
        }
        switch (reader.status()) {
        case SessionReader::TruncatedTail:
            rep.warnings << QStringLiteral("%1: ends mid-record (interrupted while writing)")
                                .arg(QFileInfo(p).fileName());
            break;
        case SessionReader::Corrupt:
            rep.warnings << QStringLiteral("%1: %2")
                                .arg(QFileInfo(p).fileName(), reader.errorString());
            break;
        default:
            break;
        }
        emit progress(++unitsDone, totalUnits);
    }

    emit finished(rep);
}

}  // namespace RoundTrip
