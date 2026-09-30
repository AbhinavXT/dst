#include "sessionreader.h"

#include <QFileInfo>

bool SessionReader::open(const QString &path)
{
    close();

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        m_status = ReadError;
        m_error  = m_file.errorString();
        return false;
    }
    m_size = m_file.size();

    QByteArray hdr = m_file.read(SessionFile::kHeaderLen);
    if (hdr.size() != SessionFile::kHeaderLen
        || !SessionFile::decodeHeader(hdr, &m_hdr)) {
        m_status = BadMagic;
        m_error  = QStringLiteral("not a DLConsole session file "
                                  "(bad magic or unsupported version)");
        m_file.close();
        return false;
    }

    // headerLen may exceed kHeaderLen if written by a future version that
    // grew the header. Skip the remainder rather than misreading it as the
    // first record — this is the whole reason headerLen is in the format.
    if (m_hdr.headerLen > SessionFile::kHeaderLen) {
        const qint64 skip = m_hdr.headerLen - SessionFile::kHeaderLen;
        if (m_file.read(skip).size() != skip) {
            m_status = TruncatedTail;
            m_error  = QStringLiteral("file ends inside its header");
            return false;
        }
    }

    m_status = Ok;
    m_error.clear();
    return true;
}

void SessionReader::close()
{
    if (m_file.isOpen()) m_file.close();
    m_hdr         = SessionFile::FileHeader();
    m_arrivalMs   = 0;
    m_wire.clear();
    m_status      = NotOpen;
    m_error.clear();
    m_recordsRead = 0;
    m_size        = 0;
}

bool SessionReader::next()
{
    if (!m_file.isOpen()) {
        m_status = NotOpen;
        return false;
    }

    // --- length prefix ---
    char lenBuf[4];
    const qint64 gotLen = m_file.read(lenBuf, 4);
    if (gotLen == 0) {
        m_status = Ok;                 // clean EOF on a record boundary
        return false;
    }
    if (gotLen < 4) {
        m_status = TruncatedTail;
        m_error  = QStringLiteral("file ends inside a record length prefix");
        return false;
    }

    const quint32 bodyLen = SessionFile::getU32(lenBuf);

    // A body must at least hold arrivalMs + wireLen + crc, and must not
    // claim more than a datagram could ever be. Both checks exist to keep a
    // corrupt length from driving a huge allocation below.
    const quint32 minBody = 8 + 2 + 4;
    const quint32 maxBody = 8 + 2 + SessionFile::kMaxWireBytes + 4;
    if (bodyLen < minBody || bodyLen > maxBody) {
        m_status = Corrupt;
        m_error  = QStringLiteral("record at offset %1 declares an "
                                  "implausible length (%2 bytes)")
                       .arg(m_file.pos() - 4).arg(bodyLen);
        return false;
    }

    // --- body ---
    const QByteArray body = m_file.read(bodyLen);
    if (quint32(body.size()) != bodyLen) {
        // Short read at the end: the writer was interrupted mid-record.
        // Everything already returned is still good.
        m_status = TruncatedTail;
        m_error  = QStringLiteral("file ends inside a record "
                                  "(wanted %1 bytes, got %2)")
                       .arg(bodyLen).arg(body.size());
        return false;
    }

    const char *p = body.constData();
    const qint64  arrivalMs = SessionFile::getI64(p);
    const quint16 wireLen   = SessionFile::getU16(p + 8);

    // wireLen has to agree with bodyLen, or the record is internally
    // inconsistent regardless of what the CRC says.
    if (quint32(wireLen) + 8 + 2 + 4 != bodyLen) {
        m_status = Corrupt;
        m_error  = QStringLiteral("record at offset %1 has an inconsistent "
                                  "wire length").arg(m_file.pos() - bodyLen - 4);
        return false;
    }

    const quint32 stored   = SessionFile::getU32(p + 10 + wireLen);
    const quint32 computed = SessionFile::crc32Final(
        SessionFile::crc32(p, 10 + wireLen));
    if (stored != computed) {
        m_status = Corrupt;
        m_error  = QStringLiteral("CRC mismatch at offset %1 "
                                  "(stored %2, computed %3)")
                       .arg(m_file.pos() - bodyLen - 4)
                       .arg(stored, 8, 16, QChar('0'))
                       .arg(computed, 8, 16, QChar('0'));
        return false;
    }

    m_arrivalMs = arrivalMs;
    m_wire      = body.mid(10, wireLen);
    ++m_recordsRead;
    m_status = Ok;
    return true;
}

QString SessionReader::tabKey() const
{
    // Same decimal form LogEntry::tabKey() builds, so a loaded session lands
    // in the same key space as live traffic.
    return QString("%1_%2")
        .arg(static_cast<int>(m_hdr.sourceId))
        .arg(static_cast<int>(m_hdr.kvchId));
}
