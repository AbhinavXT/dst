#ifndef RAWBYTESPANEL_H
#define RAWBYTESPANEL_H

// =============================================================================
//  RawBytesPanel
//  -----------------------------------------------------------------------------
//  Side panel that shows the raw bytes of the currently-selected log entry
//  along with a decoded view of the wire header.
//
//  The widget is designed to live inside a QDockWidget on the right edge of
//  MainWindow.
//
//  Layout (top to bottom):
//
//      ┌────────────────────────────────────────┐
//      │  Header                                │
//      │   source_id : 33 (0x21)                │
//      │   dest_id   : 101                      │
//      │   msg_id    : 1                        │
//      │   msg_len   : 17                       │
//      │   kvchId    : 1                        │
//      │   friendly  : LK_1_VCC_1               │
//      │   received  : 14:23:01.503             │
//      ├────────────────────────────────────────┤
//      │  Raw bytes                             │
//      │  00000000  21 65 03 13 00 01 00 52 41  │
//      │            44 20 49 4e 20 66 72 61 6d  │
//      │            65 20 30 78 34 32           │
//      └────────────────────────────────────────┘
//
//  The decoded text payload is shown in the main window's Message column,
//  not here — duplicating it pushed useful information off-screen.
//
//  Memory cost: zero. The full raw datagram bytes are already stored on
//  every LogEntry (we built that into Patch A), so this panel is purely
//  a renderer — no extra storage on inbound traffic.
// =============================================================================

#include <QWidget>

#include "logentry.h"

class QLabel;
class QTextEdit;
class NameMap;

class RawBytesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit RawBytesPanel(QWidget *parent = nullptr);

    void setNameMap(const NameMap *names) { m_names = names; }

    // The entry currently on display, or null. Whoever owns this panel
    // needs it to act on what the operator is looking at.
    LogEntryPtr currentEntry() const { return m_entry; }

signals:
    // The operator asked, from this panel's context menu, to take these
    // bytes somewhere they can be worked on. The panel deliberately does
    // NOT open those windows itself: it has no business knowing about the
    // workbench or the packet maker, and MainWindow already owns both.
    void openInDecodeWorkbenchRequested(LogEntryPtr entry);
    void openInPacketMakerRequested(LogEntryPtr entry);

public slots:
    // Show the given entry. If null, shows the empty-state placeholder.
    void showEntry(const LogEntryPtr &entry);
    void clear();

    // Highlight bytes [byteStart, byteEnd] (inclusive) in the dump.
    // (-1, -1) clears. Driven by FieldInspector so clicking a decoded
    // field shows exactly which bytes produced it.
    void highlightBytes(int byteStart, int byteEnd);

private slots:
    // Right-click inside the hex dump: copy the bytes, or hand them to the
    // Decode Workbench / Packet Maker.
    void onHexContextMenu(const QPoint &pos);

private:
    static QString formatHexDump(const QByteArray &bytes);

    QLabel    *m_headerLabel = nullptr;   // decoded header fields
    QTextEdit *m_hexView     = nullptr;   // monospace hex dump

    // The entry being displayed. Held so the context menu can act on it;
    // it is a shared pointer, so keeping one here does not stop the model
    // from evicting the row.
    LogEntryPtr m_entry;

    const NameMap *m_names = nullptr;      // borrowed; may be null
};

#endif // RAWBYTESPANEL_H
