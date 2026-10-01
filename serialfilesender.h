#ifndef SERIALFILESENDER_H
#define SERIALFILESENDER_H

// =============================================================================
//  SerialFileSender (session 108) — a file out of a serial port, paced.
//  -----------------------------------------------------------------------------
//  "Send file" used to be one write of the whole file: no pacing, no
//  progress, no way to stop, and the terminal echoed it as one giant TX>
//  line (garbage for a binary file). A card with a small receive buffer, or
//  a boot loader that takes one line at a time, needs it slower.
//
//  Two ways:
//    Chunks — N bytes, then wait D ms, until done.
//    Lines  — one line (with its own line end) at a time; after each, wait
//             for the card's prompt (a text it prints, e.g. ">") before the
//             next, giving up after a timeout. Without a prompt, D ms
//             between lines.
//
//  Runs on the GUI thread off a timer; the link does the writing on its
//  reader thread (session 103). Stop ends it at once.
// =============================================================================

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class QSettings;
class QTimer;
class SerialLink;

struct SerialSendOptions {
    enum class Mode { Chunks, Lines };
    Mode    mode = Mode::Chunks;
    int     chunkBytes = 256;
    int     delayMs = 20;
    QString prompt;                  // Lines: wait for this after each line ("" = just delay)
    int     promptTimeoutMs = 5000;

    QString summary() const;         // "256-byte chunks every 20 ms", "lines, waiting for '>'"
    void save(QSettings &s) const;
    static SerialSendOptions load(QSettings &s);
};

// The pieces a file goes out in. Lines keep their own line end; a last line
// without one is still a piece.
QVector<QByteArray> serialSendPieces(const QByteArray &data, const SerialSendOptions &o);

class SerialFileSender : public QObject
{
    Q_OBJECT
public:
    explicit SerialFileSender(QObject *parent = nullptr);

    // False (and nothing sent) if a send is running or the link is closed.
    bool start(SerialLink *link, const QByteArray &data, const SerialSendOptions &options);
    void stop();
    bool isRunning() const { return m_running; }
    qint64 bytesSent() const { return m_sent; }
    qint64 bytesTotal() const { return m_total; }

signals:
    void progress(qint64 sent, qint64 total);
    // ok = everything went; otherwise `message` says where it stopped and why.
    void finished(bool ok, const QString &message);

private:
    void sendNext();
    void onBytes(const QByteArray &bytes);
    void finish(bool ok, const QString &message);

    QPointer<SerialLink> m_link;
    SerialSendOptions    m_opt;
    QVector<QByteArray>  m_pieces;
    int                  m_next = 0;
    qint64               m_sent = 0, m_total = 0;
    bool                 m_running = false;
    bool                 m_waitingPrompt = false;
    QByteArray           m_heard;               // received since the last line went
    QTimer              *m_step = nullptr;
    QTimer              *m_timeout = nullptr;
    QElapsedTimer        m_clock;
};

#endif // SERIALFILESENDER_H
