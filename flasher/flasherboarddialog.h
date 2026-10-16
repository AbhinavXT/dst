#ifndef FLASHERBOARDDIALOG_H
#define FLASHERBOARDDIALOG_H
// =============================================================================
//  flasherboarddialog.{h,cpp} -- "Ask the board…" (session 297)
//
//  The 2.6 updater answers INFO_REQ (0x04), HOLD (0x07) and BOOT (0x08) with
//  an INFO reply (0x05): its version, controller ID, both flash banks, the
//  transfer in progress, the VCC's card results and the firmware versions in
//  its banks. This is udp_file_v5's `flash info`, `flash hold` and
//  `flash boot` in one window.
//
//  BoardQuery does the talking, on the GUI thread with a QUdpSocket and a
//  timer (no worker thread: it is a few datagrams a second). Each round sends
//  HOLD, INFO_REQ and a POLL, so:
//    * a 2.2+ updater answers with INFO and is held there (2 min idle);
//    * a 2.0 / 2.1 updater ignores HOLD but answers INFO_REQ;
//    * an updater older than 2.0 answers only the POLL: it is there, but has
//      nothing to tell.
//  A board running its application answers nothing, so the query keeps
//  asking for the profile's updater wait while the operator power-cycles it.
// =============================================================================
#include <QDialog>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QPair>
#include <QVector>

#include "flashercore.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QUdpSocket;
class PortEdit;
class StatusLine;

class BoardQuery : public QObject
{
    Q_OBJECT
public:
    explicit BoardQuery(QObject *parent = nullptr);

    // Asks until the board answers or `waitMs` passes.
    void ask(const QHostAddress &address, quint16 port, int waitMs);
    // BOOT until the board acknowledges (INFO) or `waitMs` passes.
    void release(const QHostAddress &address, quint16 port, int waitMs);
    void stop();
    bool isBusy() const;

signals:
    void answered(const Flasher::BoardInfo &info);
    void olderUpdater();            // only the POLL was answered: updater before 2.0
    void noAnswer();
    void released(bool acknowledged);
    void waiting(int secondsLeft);

private:
    enum class Job { None, Ask, Release };
    void sendRound();
    void readReplies();
    void drain();

    QUdpSocket   *m_socket = nullptr;
    QTimer       *m_timer = nullptr;
    Job           m_job = Job::None;
    QHostAddress  m_address;
    quint16       m_port = 0;
    int           m_waitMs = 0;
    QElapsedTimer m_clock;
    bool          m_sawStatus = false;
    int           m_roundsSinceStatus = 0;
};

class FlasherBoardDialog : public QDialog
{
    Q_OBJECT
public:
    // `queued`: the queue's loaded images (label, CRC), to name the active
    // bank. `waitSeconds`: how long to keep asking (the profile's updater wait).
    FlasherBoardDialog(const QString &address, quint16 port, int waitSeconds,
                       const QVector<QPair<QString, quint32>> &queued, QWidget *parent = nullptr);

    // For the tests.
    void ask();
    void release();
    QString resultText() const;
    QString statusText() const;
    bool    hasAnswer() const { return m_info.valid; }
    const Flasher::BoardInfo &info() const { return m_info; }
    QPushButton *releaseButton() const { return m_releaseButton; }

signals:
    void finishedTalking();          // an answer, no answer, or a release: the tests wait on it

private:
    void setBusy(bool busy, const QString &what);
    bool target(QHostAddress *address, quint16 *port);

    BoardQuery     *m_query = nullptr;
    QLineEdit      *m_address = nullptr;
    PortEdit       *m_port = nullptr;
    QPushButton    *m_askButton = nullptr;
    QPushButton    *m_stopButton = nullptr;
    QPushButton    *m_releaseButton = nullptr;
    QPushButton    *m_copyButton = nullptr;
    StatusLine     *m_status = nullptr;
    QPlainTextEdit *m_text = nullptr;
    int             m_waitSeconds = 60;
    QVector<QPair<QString, quint32>> m_queued;
    Flasher::BoardInfo m_info;
};

#endif  // FLASHERBOARDDIALOG_H
