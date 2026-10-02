#include "savedata.h"

#include <QDateTime>
#include <QDebug>
#include "sessionfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>
#include <QTextStream>

namespace {

// Severity printed in the saved file. Empty for Info so casual readers see
// uncluttered output; explicit for Warn/Error so grepping works.
QString severityTag(Severity s)
{
    switch (s) {
    case Severity::Info:  return QStringLiteral("    ");
    case Severity::Warn:  return QStringLiteral("WARN");
    case Severity::Error: return QStringLiteral("ERR ");
    }
    return QStringLiteral("    ");
}

QString directionTag(LogDirection d)
{
    switch (d) {
    case LogDirection::In:  return QStringLiteral("IN ");
    case LogDirection::Out: return QStringLiteral("OUT");
    case LogDirection::None: return QStringLiteral("   ");
    }
    return QStringLiteral("   ");
}

}   // anonymous namespace

SaveData::SaveData(const QString          &filePath,
                   const QString          &friendlyName,
                   QVector<LogEntryPtr>    snapshot,
                   quint8                  sourceId,
                   quint16                 kvchId,
                   bool                    writeRaw)
    : m_filePath    (filePath)
    , m_friendlyName(friendlyName)
    , m_snapshot    (std::move(snapshot))
    , m_sourceId    (sourceId)
    , m_kvchId      (kvchId)
    , m_writeRaw    (writeRaw)
{
    // Deliberately no filesystem work here. This constructor runs on the
    // GUI thread, and it used to mkpath a hard-coded "SAVED_LOGS" that had
    // no necessary relationship to m_filePath. The directory we actually
    // need is created in run(), on the worker thread, from the real path.
}

int SaveData::writeRawSidecar(const QString &rawPath) const
{
    // Count first. A tab can be entirely synthetic — dropped-message
    // banners, replayed rows — and writing a .dlr containing only a file
    // header would leave a file that looks like a recording and holds
    // nothing. Better to write none and say so.
    int usable = 0;
    for (const LogEntryPtr &e : m_snapshot) {
        if (e && !e->rawBytes.isEmpty()
            && e->rawBytes.size() <= SessionFile::kMaxWireBytes) {
            ++usable;
        }
    }
    if (usable == 0) return 0;

    QFile raw(rawPath);
    if (!raw.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "SaveData: cannot write raw sidecar" << rawPath
                   << ":" << raw.errorString();
        return -1;
    }

    SessionFile::FileHeader h;
    h.createdMs = QDateTime::currentMSecsSinceEpoch();
    h.sourceId  = m_sourceId;
    h.kvchId    = m_kvchId;
    if (raw.write(SessionFile::encodeHeader(h)) < 0) {
        qWarning() << "SaveData: raw header write failed on" << rawPath;
        return -1;
    }

    int written = 0;
    for (const LogEntryPtr &e : m_snapshot) {
        if (!e || e->rawBytes.isEmpty()) continue;
        if (e->rawBytes.size() > SessionFile::kMaxWireBytes) continue;
        const QByteArray rec =
            SessionFile::encodeRecord(e->epochMs, e->rawBytes);
        if (raw.write(rec) != rec.size()) {
            qWarning() << "SaveData: raw write truncated on" << rawPath;
            raw.close();
            return -1;
        }
        ++written;
    }
    raw.close();
    return written;
}

void SaveData::run()
{
    if (m_snapshot.isEmpty()) {
        emit saveFinished(m_filePath, QString(), 0);
        return;
    }

    // Create the destination directory from the path we were actually
    // given, rather than assuming one.
    const QFileInfo fi(m_filePath);
    if (!fi.absoluteDir().exists()
        && !QDir().mkpath(fi.absolutePath())) {
        const QString reason =
            QStringLiteral("cannot create directory %1").arg(fi.absolutePath());
        qWarning() << "SaveData:" << reason;
        emit saveFailed(m_filePath, reason);
        return;
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "SaveData: cannot open" << m_filePath
                   << "for writing:" << file.errorString();
        emit saveFailed(m_filePath, file.errorString());
        return;
    }

    QTextStream out(&file);
    // Qt 5's QTextStream defaults to the LOCALE codec; Qt 6 defaults to
    // UTF-8. Exporter and ReplayWindow already pin this, LogWriter writes
    // explicit toUtf8() — without this line, saved logs were the one output
    // path whose encoding depended on the operator's system locale.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif

    for (const LogEntryPtr &e : m_snapshot) {
        if (!e) continue;
        const QString time =
            QDateTime::fromMSecsSinceEpoch(e->epochMs).toString("HH:mm:ss.zzz");
        const QString rawKey = e->tabKey();
        // The tag is "21_1/LK_1_VCC_1" if the friendly name differs from
        // the raw key, or just "21_1" if no mapping is in effect — matches
        // the on-screen tab labelling.
        const QString tag = (m_friendlyName != rawKey)
                                ? QString("%1/%2").arg(rawKey, m_friendlyName)
                                : rawKey;
        out << time << " [" << tag << "] "
            << directionTag(e->direction) << ' '
            << severityTag (e->severity)  << "  "
            << e->text << '\n';
    }
    out.flush();
    if (file.error() != QFile::NoError) {
        const QString reason = file.errorString();
        file.close();
        qWarning() << "SaveData: write failed on" << m_filePath << ":" << reason;
        emit saveFailed(m_filePath, reason);
        return;
    }
    file.close();

    // Sidecar last: the readable log is what was asked for, and a raw
    // failure must not make a successful text save look like a failure.
    QString rawPath;
    int rawRecords = 0;
    if (m_writeRaw) {
        // Same basename, .dlr extension — so the pair stays obviously
        // related in a directory listing and when attached to a report.
        const QFileInfo fi(m_filePath);
        rawPath = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
                  + QLatin1String(SessionFile::kExtension);
        rawRecords = writeRawSidecar(rawPath);
        if (rawRecords <= 0) {
            // Nothing written: either no entry carried bytes, or the file
            // could not be created. Report no path rather than one that
            // does not exist or holds nothing.
            if (rawRecords < 0) QFile::remove(rawPath);
            rawPath.clear();
            rawRecords = 0;
        }
    }

    emit saveFinished(m_filePath, rawPath, rawRecords);
}
