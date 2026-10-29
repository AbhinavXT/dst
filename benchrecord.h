#ifndef BENCHRECORD_H
#define BENCHRECORD_H

// =============================================================================
//  Test recording (session 404) — every frame the Loco Simulator and the
//  Brake Test Bench send or receive, in one file, on the PC clock the VCC
//  logs' .dlr files use.
//  -----------------------------------------------------------------------------
//  WHAT IS RECORDED
//    speed    each speed sensor message the Loco Simulator sends (UDP)
//    tag      each RFID tag frame its readers send (serial), or why one
//             could not go
//    health   (session 409) each reader's health frame
//    biu      every BIU frame, both ways (the VCC's commands, heartbeats and
//             NMT in; the BIU's status, heartbeat and NMT out)
//    analog   every analog card frame the bench sends
//    event    the recording's own marks: started, stopped, and what the
//             caller notes (session 407's scenario steps)
//  Each with the time (ms since the epoch), sent or received, the peer, the
//  bytes, and what the sender's own decoder says of them. Unfiltered: the
//  bench's Frames-tab filters (heartbeats, paused) do not apply here.
//
//  THE FILE (.benchrec): UTF-8 text, one JSON object a line.
//    line 1  {"format":"dlconsole-benchrec","version":1,"createdMs":…,"note":…}
//    then    {"t":ms,"k":"biu","d":"rx"|"tx","p":peer,"b":"hex","s":says,"n":note}
//  Append-only and flushed each line: a crash costs the last line at most,
//  and read() stops at the first line that does not parse.
//
//  Owned by MainWindow and handed to both simulator windows (CLAUDE.md:
//  session-state objects are not singletons).
// =============================================================================

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QString>
#include <QVector>

namespace BenchRecord {

struct Item {
    qint64     ms = 0;
    QString    kind;                 // "speed", "tag", "biu", "analog", "event"
    bool       tx = true;            // sent by a simulator; false: received
    QString    peer;                 // "192.168.25.16:50001", "port A", ...
    QByteArray bytes;
    QString    says;                 // the sender's decoder
    QString    note;                 // what is wrong or worth knowing
};

struct File {
    qint64         createdMs = 0;
    QString        note;
    QVector<Item>  items;
    bool           torn = false;     // stopped at a line that did not parse
};

QStringList kinds();                 // in the order the viewer offers them
QByteArray  headerLine(qint64 createdMs, const QString &note);
QByteArray  itemLine(const Item &item);
// False (and *err) when the file cannot be opened or is not a recording.
bool        read(const QString &path, File *out, QString *err = nullptr);
QString     defaultPath(qint64 nowMs);   // <disk log root>/bench/bench_yyyyMMdd_HHmmss.benchrec

class Recorder : public QObject
{
    Q_OBJECT
public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    // False (and *err) when the file cannot be made; the folder is made.
    bool    start(const QString &path, qint64 nowMs, const QString &note = QString(), QString *err = nullptr);
    void    stop(qint64 nowMs);
    bool    recording() const { return m_file.isOpen(); }
    QString path() const { return m_path; }
    int     count() const { return m_count; }      // items written, events included
    void    append(const Item &item);              // nothing while not recording
    void    mark(qint64 ms, const QString &text);  // an "event" item

signals:
    void started(const QString &path);
    void stopped(const QString &path, int count);

private:
    QFile   m_file;
    QString m_path;
    int     m_count = 0;
};

}  // namespace BenchRecord

#endif  // BENCHRECORD_H
