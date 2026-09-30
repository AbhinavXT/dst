#ifndef DECODEWORKBENCH_H
#define DECODEWORKBENCH_H

#include <QMainWindow>
#include <QString>

class QPlainTextEdit;
class QComboBox;
class QTableWidget;
class QLabel;
class StatusLine;

// =============================================================================
//  DecodeWorkbench
//  -----------------------------------------------------------------------------
//  Standalone "paste a frame, see it decoded" tool, decoupled from capture.
//
//  A pasted hex body (or a whole "@<type>_<loco>_<ctrl> <ts> <seq> <hex>"
//  capture line) is turned into a synthetic CaptureLine and pushed through the
//  exact same path real captures take — CaptureDecoder::parseLine() ->
//  verifyCrc() -> describe() — so the result is byte-for-byte identical to what
//  the live/replay inspectors would show for the same bytes. No decode logic
//  lives in this class; it only presents.
//
//  "Auto-detect (CRC)" tries every CRC-bearing type and reports which verify.
//  Because several types share a CRC recipe (aap/slrp/aep, arp/lsrp,
//  nmsflt/nmshlth) a passing CRC confirms a *family*, not always a single type;
//  the status line says so honestly and the user disambiguates by picking.
// =============================================================================
class DecodeWorkbench : public QMainWindow
{
    Q_OBJECT
public:
    explicit DecodeWorkbench(QWidget *parent = nullptr);

    // Load a buffer from elsewhere in the application (a log row, the raw
    // bytes panel, the Check Buffer page) instead of making the operator
    // copy hex out of one window and paste it into this one.
    //
    // `text` may be a whole "@type_loco_ctrl <ts> <seq> <hex…>" capture line
    // or bare hex in any of the separator styles cleanHex() accepts — the
    // same two things a paste can be, so there is one code path and not two.
    // `typeToken` (e.g. "slrp") pre-selects the type combo; a capture line
    // overrides it with its own tag, which is the more reliable statement.
    void loadBuffer(const QString &text, const QString &typeToken = QString());

private slots:
    // Repopulate the session-key picker from the store, keeping the operator's
    // choice selected if it still exists.
    void refreshKeySets();

protected:
    // Remember size/position for the next opening.
    void closeEvent(QCloseEvent *event) override;

private:
    void decodeNow();        // re-decode from current input + selected type

    // Strip separators / 0x prefixes; validate hex. Returns normalised
    // upper-case hex (no spaces). *ok=false if a non-hex char or odd nibble
    // count is present; *nbytes set to the byte count when ok.
    QString cleanHex(const QString &raw, bool *ok, int *nbytes, QString *err) const;

    // Try each CRC-bearing type against `hex`; return the first token whose CRC
    // verifies (or empty). `report` is filled with the full list of matches.
    QString autoDetect(const QString &hex, QString *report) const;

    QPlainTextEdit *m_input    = nullptr;
    QComboBox      *m_typeBox  = nullptr;
    QComboBox      *m_keyBox   = nullptr;   // which captured key set to check the MAC against
    QTableWidget   *m_table    = nullptr;
    StatusLine     *m_status   = nullptr;
    bool            m_updating = false;   // re-entry guard for programmatic edits
};

#endif // DECODEWORKBENCH_H
