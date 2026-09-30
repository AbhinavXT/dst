#ifndef FIELDINSPECTOR_H
#define FIELDINSPECTOR_H

// =============================================================================
//  FieldInspector
//  -----------------------------------------------------------------------------
//  Schema-decoded fields for the currently selected log row, with each
//  field linked to the bytes it came from.
//
//  WHY
//    The schema engine already decodes these frames, but only inside the
//    Loco Console and the Decode Workbench — both of which are separate
//    windows fed by their own streams. Selecting a row in an ordinary tab
//    gets you a hex dump and nothing else, so understanding a frame means
//    copying its bytes into the Workbench by hand.
//
//    The byte link is the part that matters. A decoded value that looks
//    wrong is ambiguous: bad data on the wire, or a schema offset that has
//    drifted? Clicking the field and seeing which bytes it claims answers
//    that immediately, and it is the difference between "the decoder says
//    42" and "the decoder says 42 because of these two bytes, which are
//    not where I expected".
//
//  DECODE FAILURES ARE SHOWN, NOT SWALLOWED
//    When no schema handles a frame, or a CRC check fails, that is
//    reported in the status line and the frame is recorded in the failure
//    log. Today those cases are silent: the row renders as text and the
//    fact that nothing could interpret it never surfaces, so schema gaps
//    accumulate unnoticed.
// =============================================================================

#include <QVector>
#include <QWidget>

class SessionKeyStore;
class LocoIdentity;

#include "capturedecoder.h"   // FieldRow
#include "logentry.h"

namespace Schema { class Decoder; }

class QLabel;
class StatusLine;
class QTableWidget;
class QPushButton;

class FieldInspector : public QWidget
{
    Q_OBJECT

public:
    // Reject reasons for the frame on show, one line each, already worded.
    // Readable so a test can check them without rendering, and so another
    // window can show them without decoding the frame a second time.
    QStringList rejectReasons() const { return m_findings; }

    // Shared with the window that owns it, so a loco identified while
    // reading a log tab is still identified in a compare pane. Borrowed,
    // never owned: one console watches one set of sources.
    void setLocoIdentity(LocoIdentity *identity) { m_identity = identity; }

    explicit FieldInspector(QWidget *parent = nullptr);
    // Session 96: the live session keys the SLRP "MAC (live)" row is checked
    // against (MainWindow's). None = no MAC row.
    void setSessionKeys(const SessionKeyStore *keys) { m_keys = keys; }

    // Borrowed; must outlive this widget. Null disables decoding and the
    // panel says so rather than silently showing nothing.
    void setDecoder(const Schema::Decoder *decoder);

    // The captype of the frame currently shown, or empty. Used when pinning
    // from a row: the operator is pointing at a field IN a packet, and the
    // same field name lives in other packets.
    QString currentCaptype() const;

public slots:
    void showEntry(const LogEntryPtr &entry);
    void clear();

signals:
    // Byte range within the entry's rawBytes that the selected field came
    // from. RawBytesPanel highlights it. byteEnd is inclusive; (-1,-1)
    // means "clear the highlight".
    void byteRangeSelected(int byteStart, int byteEnd);

    // A frame that nothing could decode, or whose CRC failed. MainWindow
    // collects these into the decode-failure dock.
    void decodeFailed(const LogEntryPtr &entry, const QString &reason);

    // The operator asked to keep this field in view, or to see it over time.
    // Raised rather than acted on: this panel knows which field was clicked
    // and nothing else — not which tab it belongs to, not where the pin board
    // lives. Opening windows is MainWindow's job everywhere else in this
    // program and there is no reason for it to stop being here.
    void pinFieldRequested(const QString &fieldName);
    // "Which packets carry this?" — the question the inspector raises but
    // cannot answer: it is looking at one frame of one packet, and the
    // schema is what knows about the other four.
    void locateFieldRequested(const QString &fieldName);

    void plotFieldRequested(const QString &fieldName);

private slots:
    void onRowChanged(int row);

private:
    void setStatus(const QString &text, bool bad);

    QStringList   m_findings;
    LocoIdentity *m_identity = nullptr;

    const Schema::Decoder *m_decoder = nullptr;
    LogEntryPtr            m_entry;
    QVector<FieldRow>      m_rows;

    StatusLine       *m_status = nullptr;
    QTableWidget *m_table  = nullptr;
    const SessionKeyStore *m_keys = nullptr;   // not owned
};

#endif // FIELDINSPECTOR_H
