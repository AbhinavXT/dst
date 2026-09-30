#ifndef SESSIONREADER_H
#define SESSIONREADER_H

// =============================================================================
//  SessionReader
//  -----------------------------------------------------------------------------
//  Streaming reader for a .dlr session file. Pulls one record at a time so a
//  multi-gigabyte archive can be walked without loading it into memory.
//
//  Truncation is a normal outcome, not an error. The writer appends and
//  flushes on a cycle, so a file captured through a power loss, a SIGKILL,
//  or simply copied while still being written will end mid-record. In that
//  case next() returns false and status() is TruncatedTail: every complete
//  record before the tear is still valid and has already been returned. A
//  reader that treated this as corruption would throw away the whole
//  archive over its last few bytes — which is exactly the archive you want
//  when the app died unexpectedly.
//
//  Usage:
//      SessionReader r;
//      if (!r.open(path)) { ... r.errorString() ... }
//      while (r.next()) {
//          use(r.arrivalMs(), r.wire());
//      }
//      if (r.status() == SessionReader::Corrupt) { warn(); }
// =============================================================================

#include <QByteArray>
#include <QFile>
#include <QString>

#include "sessionfile.h"

class SessionReader
{
public:
    enum Status {
        Ok,              // reached a clean end of file
        NotOpen,
        BadMagic,        // not a .dlr, or a version we don't understand
        TruncatedTail,   // ended mid-record; everything before it was good
        Corrupt,         // a record's CRC failed; stopped there
        ReadError
    };

    SessionReader() = default;
    ~SessionReader() { close(); }

    bool open(const QString &path);
    void close();

    // Advance to the next record. Returns false at end of file or on the
    // first record that cannot be trusted; check status() to tell which.
    bool next();

    // Valid after a next() that returned true.
    qint64             arrivalMs() const { return m_arrivalMs; }
    const QByteArray  &wire()      const { return m_wire; }

    // From the file header.
    const SessionFile::FileHeader &fileHeader() const { return m_hdr; }
    QString tabKey() const;

    Status  status()      const { return m_status; }
    QString errorString() const { return m_error; }

    // Records successfully returned so far.
    qint64 recordsRead() const { return m_recordsRead; }

    // Total file size, for progress reporting on a long load.
    qint64 fileSize() const { return m_size; }
    qint64 pos()      const { return m_file.isOpen() ? m_file.pos() : 0; }

private:
    QFile                   m_file;
    SessionFile::FileHeader m_hdr;
    qint64                  m_arrivalMs   = 0;
    QByteArray              m_wire;
    Status                  m_status      = NotOpen;
    QString                 m_error;
    qint64                  m_recordsRead = 0;
    qint64                  m_size        = 0;
};

#endif // SESSIONREADER_H
