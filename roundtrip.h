#ifndef ROUNDTRIP_H
#define ROUNDTRIP_H
// =====================================================================
//  roundtrip.{h,cpp} -- can this program rebuild what the equipment
//  actually sent?
//
//  Every tool that transmits (Packet Maker, Field Sweep, and the station
//  responder) builds its frame with Schema::Encoder::encodeBody(). The
//  schema claims that encodeBody() is the inverse of parseBody(), so for
//  any captured frame
//
//      parseBody(bytes) -> encodeBody(...) == bytes
//
//  must hold. When it does, a frame this program builds for that packet
//  type is the same shape as one the equipment builds. When it does not,
//  anything built for that type is a guess -- and the failure is silent,
//  because a mis-encoded frame still has a valid CRC over its own wrong
//  bytes and still decodes back to the values that produced it.
//
//  So this walks a corpus -- the live log, capture files, recorded .dlr
//  sessions -- and answers the question per packet type, with counts:
//  how many frames were reproduced byte for byte, how many differ and at
//  which byte, and which types the encoder cannot emit at all.
//
//  WHAT IT DOES NOT DO
//    It does not say the schema is right. A field that is mislabelled but
//    the correct width round-trips perfectly. This checks reproduction,
//    not meaning -- which is exactly why the results are reported as
//    counts of frames reproduced rather than as a pass mark.
//
//  THREADING
//    The scan runs on a worker thread and loads its OWN Schema::Encoder
//    from the path the caller resolved on the GUI thread. It never
//    touches kavachSchema() or SessionKeyStore: both are GUI-thread
//    state that a schema reload mid-scan could pull out from under it.
//    Field attribution -- naming the field that owns a differing byte --
//    is therefore left to the caller, which has the shared decoder.
// =====================================================================
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <atomic>
#include <functional>

#include "capturedecoder.h"          // CaptureLine, FieldRow
#include "logentry.h"                // LogEntryPtr
#include "schema/schemaencoder.h"

namespace RoundTrip {

enum class Outcome {
    Identical,      // rebuilt body is byte-for-byte what was captured
    Differs,        // rebuilt, but not the same bytes
    NotEncodable,   // the packet uses schema grammar the encoder can't emit
    ParseFailed,    // parseBody() refused the buffer
    EncodeFailed,   // encodeBody() refused the parsed values
    NoPacket,       // no packet in the schema for this captype
    TooShort        // buffer ends before the body begins
};

QString outcomeName(Outcome o);

// True for outcomes that say something about THIS frame's fidelity.
// NoPacket / NotEncodable are properties of the schema, not of the frame,
// and counting them as failures would put every @dop1 line in the corpus
// into a column that reads like a fault.
bool isFrameVerdict(Outcome o);

struct Result {
    Outcome outcome = Outcome::NoPacket;
    QString captype;
    QString detail;             // the refusal text, or a one-line description

    int bodyStart    = 0;       // where the body begins inside the captured frame
    int bodyBytes    = 0;       // bytes parseBody consumed
    int tailBytes    = 0;       // reserved tail it measured but did not decode
    int rebuiltBytes = 0;

    int          firstDiff = -1;   // body-relative index of the first difference
    QVector<int> diffBytes;        // body-relative, capped (see kMaxDiffBytes)
    QStringList  notes;            // parseBody's own observations (holes, lengths)

    QByteArray rebuiltBody;        // kept only when the caller asked for samples

    bool identical() const { return outcome == Outcome::Identical; }
};

// Bytes of the captured frame that come before the packet body: the
// in-capture envelope (message header + station id) that body_offset
// describes. parseBody() starts at bit 0 of whatever it is given, so
// handing it a captured arp/lsrp frame whole reads the envelope as
// PKT_TYPE and produces confident nonsense.
int bodyStart(const Schema::PacketInfo &pi);

// The check itself, on bytes. `frame` is the captured frame exactly as it
// arrived. Pure: no globals, no widgets, safe on any thread.
// `keepBody` makes the result carry the rebuilt bytes, for a caller that
// wants to show them.
Result check(const Schema::Encoder &enc, const QString &captype,
             const QByteArray &frame, bool keepBody = false);

// At most this many differing byte offsets are recorded per frame. A frame
// whose every byte differs has already made its point.
extern const int kMaxDiffBytes;

// The field owning an absolute byte of the frame, from rows the CALLER
// decoded (their spans are frame-absolute). Empty when nothing claims it —
// which is itself a finding: a byte no field covers is a hole in the schema.
QString fieldAtByte(const QVector<FieldRow> &rows, int absByte);

// One failing frame, kept so the operator can see the actual bytes rather
// than only a count. `rebuiltFrame` is the captured frame with the rebuilt
// body spliced in, so the two can be handed to Frame Diff as a pair.
struct Sample {
    QString    captype;
    QDateTime  rtc;
    quint32    seq = 0;
    int        locoId = -1, ctrlId = -1;
    QByteArray original;
    QByteArray rebuiltFrame;
    QVector<int> diffBytes;       // body-relative
    int        bodyStart = 0;
    QString    detail;
};

// A capture line for `bytes`, tagged as `c` was, so a rebuilt frame can go
// through the same parse path as a captured one.
QString captureLineFor(const CaptureLine &c, const QByteArray &bytes);

struct Tally {
    QString captype;
    int frames       = 0;
    int identical    = 0;
    int differs      = 0;
    int parseFailed  = 0;
    int encodeFailed = 0;
    int notEncodable = 0;
    int tooShort     = 0;

    int crcChecked = 0, crcOk = 0;

    QString      firstProblem;      // detail of the first non-identical frame
    QStringList  notes;             // distinct parseBody notes, bounded
    QHash<int, int> diffHistogram;  // body byte -> how many frames first differ there
    QVector<Sample> samples;

    // Frames this program could rebuild exactly, over frames where the
    // question applies at all.
    int answerable() const { return identical + differs + parseFailed + encodeFailed; }
};

struct Report {
    QString   schemaPath;           // "" means the copy built into the binary
    QDateTime startedAt;
    QStringList sources;            // what was scanned, for the record

    QHash<QString, Tally> byType;
    qint64 linesSeen  = 0;          // lines / records offered
    qint64 framesSeen = 0;          // of those, well-formed capture frames
    int  filesScanned    = 0;
    int  filesUnreadable = 0;
    bool cancelled = false;
    QStringList warnings;

    void add(const CaptureLine &c, const Result &r, int maxSamples);

    QStringList captypes() const;   // sorted, only types actually seen
    int totalIdentical() const;
    int totalDiffers() const;
    int totalAnswerable() const;

    // The evidence document. Deliberately plain text: it gets pasted into a
    // report and read by someone who was not sitting here.
    QString toText() const;
};

// Scan already-parsed capture text. `cancelled` is polled per line, and
// `progress` is called with (done, total) occasionally. Both may be empty.
void scanLines(const Schema::Encoder &enc, const QStringList &lines,
               Report *report, int maxSamples,
               const std::function<bool()> &cancelled = {},
               const std::function<void(qint64, qint64)> &progress = {});

// The worker. One job, one report.
class Runner : public QThread
{
    Q_OBJECT

public:
    struct Job {
        QVector<LogEntryPtr> entries;      // a live-log snapshot (pointer copies)
        QStringList capFiles;              // capture text (.cap/.log/.txt)
        QStringList sessionFiles;          // recorded .dlr sessions
        QString     schemaPath;            // resolved by the caller; "" = built-in
        int         maxSamples = 8;        // failing frames kept per captype
    };

    explicit Runner(Job job, QObject *parent = nullptr);

    void cancel() { m_cancel.store(true, std::memory_order_relaxed); }

signals:
    void progress(qint64 done, qint64 total);
    void finished(RoundTrip::Report report);

protected:
    void run() override;

private:
    Job m_job;
    std::atomic<bool> m_cancel{ false };
};

}  // namespace RoundTrip

Q_DECLARE_METATYPE(RoundTrip::Report)

#endif  // ROUNDTRIP_H
