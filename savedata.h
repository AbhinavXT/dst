#ifndef SAVEDATA_H
#define SAVEDATA_H

// =============================================================================
//  SaveData
//  -----------------------------------------------------------------------------
//  Worker QThread that writes a snapshot of LogEntry rows to disk as plain
//  text. The format mirrors what the user sees on screen, with an explicit
//  timestamp prefix so saved files are useful for offline grep-and-diff.
//
//  Format (one row per line):
//      HH:mm:ss.zzz [SOURCE/FRIENDLY] DIR SEV  message text
//
//  E.g.
//      14:23:01.503 [21_1/LK_1_VCC_1] IN       RAD frame 0x42 received
//      14:23:01.731 [21_1/LK_1_VCC_1]    ERR   Error: timeout on link 1
//
//  The friendly name is resolved on the GUI thread before this worker is
//  started — that way the saver doesn't need a thread-safe view of the
//  NameMap, and a CSV reload mid-save can't change what's being written.
//
//  A byte-exact .dlr sidecar is written alongside, with the same basename,
//  when the snapshot contains raw datagrams. Saving only the rendered text
//  would mean a manually saved log — the one an operator deliberately keeps
//  because something interesting happened — is the one artefact that cannot
//  be re-decoded or reopened in the session viewer, while the automatic
//  rolling logs can. The sidecar makes a saved tab as useful as a recorded
//  session.
//
//  The class self-deletes via QObject::deleteLater() once finished — the
//  caller hooks that up via connect(thread, &QThread::finished, ...).
// =============================================================================

#include <QObject>
#include <QString>
#include <QThread>
#include <QVector>

#include "logentry.h"

class SaveData : public QThread
{
    Q_OBJECT

public:
    // friendlyName is the resolved display name for the source — there's
    // exactly one because we save one tab at a time.
    // `sourceId`/`kvchId` go into the .dlr file header so the saved session
    // identifies its own source when reopened, exactly as a recorded one
    // does. They are passed in rather than derived from the first entry
    // because a snapshot can begin with a synthetic banner row that carries
    // no real header.
    SaveData(const QString          &filePath,
             const QString          &friendlyName,
             QVector<LogEntryPtr>    snapshot,
             quint8                  sourceId = 0,
             quint16                 kvchId   = 0,
             bool                    writeRaw = true);

    void run() override;

signals:
    // `rawPath` is the .dlr written alongside, or empty when none was
    // (raw capture disabled, or no entry in the snapshot carried bytes).
    // `rawRecords` is how many datagrams it holds.
    void saveFinished(const QString &filePath,
                      const QString &rawPath = QString(),
                      int rawRecords = 0);

    // Emitted instead of saveFinished when the file could not be written.
    // Previously run() just returned on open failure, so the user clicked
    // Save, nothing was written, and nothing in the UI said so.
    void saveFailed(const QString &filePath, const QString &reason);

private:
    // Writes the .dlr sidecar. Returns the record count, or -1 if the file
    // could not be created. A failure here is reported but does NOT fail
    // the save: the readable log is the thing the operator asked for.
    int writeRawSidecar(const QString &rawPath) const;

    QString              m_filePath;
    QString              m_friendlyName;
    QVector<LogEntryPtr> m_snapshot;
    quint8               m_sourceId = 0;
    quint16              m_kvchId   = 0;
    bool                 m_writeRaw = true;
};

#endif // SAVEDATA_H
