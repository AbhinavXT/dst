#ifndef SIMSENTLOG_H
#define SIMSENTLOG_H
// =============================================================================
//  SimSentLog (session 415) — the RFID frames the Loco Simulator sent, as a
//  list in its RFID frames tab: every tag frame (the route's, by hand, late, twice,
//  bad CRC) and every reader health frame, newest at the bottom.
//  -----------------------------------------------------------------------------
//  Each row: the time, reader, port, the frame's kind, the tag (or health
//  OK / fail), where the loco was, the note (late by…, sent twice, bad CRC,
//  not sent: port closed) and the bytes. A tag row's tooltip is the tag's
//  fields as the decoder reads them. Double-click a tag row: it opens in the
//  Decode Workbench as the @rfid line a VCC log would have (the reader byte
//  and the 16 tag bytes). A health frame has no packet in the schema: its
//  bytes only.
//
//  Kept to the last kMaxRows; the list follows the newest unless scrolled up.
// =============================================================================

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QWidget>

#include <cmath>
#include <functional>

class QCheckBox;
class QLabel;
class QTableWidget;

class SimSentLog : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kMaxRows = 5000;
    enum Column { ColTime, ColReader, ColPort, ColKind, ColWhat, ColAt, ColNote, ColBytes, Columns };

    struct Frame {
        qint64     ms = 0;             // when (epoch ms)
        bool       health = false;     // a health frame, else a tag frame
        int        reader = 1;
        QString    port;               // "port A" / "port B"
        QByteArray bytes;              // the frame as written
        double     atM = std::nan(""); // where the loco was (tags from the route)
        QString    note;               // "by hand", "late by 120 ms", "not sent: port A closed"
        bool       sent = true;
        int        seq = 0;            // set by add(): 1, 2, ... (the line's seq)
    };

    explicit SimSentLog(QWidget *parent = nullptr);

    void add(const Frame &frame);
    void clear();
    int  count() const;               // rows held (health ones too, shown or not)
    QTableWidget *table() const { return m_table; }

    // The @rfid line the Workbench is given for a tag frame ("" for health).
    // The loco and cab are 0_0: the simulator's, no real loco's.
    static QString workbenchLine(const Frame &frame);
    // Where a double-clicked tag row goes: (the line, a window label).
    void setOpenInWorkbench(std::function<void(const QString &, const QString &)> open) { m_open = std::move(open); }
    void openRow(int row);

signals:
    void countChanged(int rows);

private:
    void applyHealthFilter();
    void refreshCount();

    QTableWidget *m_table = nullptr;
    QCheckBox    *m_showHealth = nullptr;
    QLabel       *m_count = nullptr;
    QVector<Frame> m_frames;           // one per table row
    int m_seq = 0;
    int m_tagRows = 0, m_healthRows = 0;
    std::function<void(const QString &, const QString &)> m_open;
};

#endif // SIMSENTLOG_H
