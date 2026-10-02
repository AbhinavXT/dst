#include "rawbytespanel.h"
#include <QFontMetrics>
#include "uistyle.h"
#include "namemap.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFontDatabase>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QTextEdit>
#include <QVBoxLayout>

RawBytesPanel::RawBytesPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(8);

    // -------- Header section ------------------------------------------
    auto *headerGroup = new QGroupBox(tr("Header"));
    auto *headerLayout = new QVBoxLayout(headerGroup);
    m_headerLabel = new QLabel;
    m_headerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_headerLabel->setFont(UiStyle::monoFont());
    // As wide from the start as with a frame in it (session 146): it grew
    // from "(no row selected)" to the header on the first row clicked, and
    // the window it sat in grew with it, under the operator's hand.
    m_headerLabel->setMinimumWidth(
        QFontMetrics(m_headerLabel->font()).horizontalAdvance(QStringLiteral("received  : 88:88:88.888")));
    headerLayout->addWidget(m_headerLabel);
    root->addWidget(headerGroup);

    // -------- Hex dump section ----------------------------------------
    // The hex dump is the side panel's job. The decoded TEXT payload is
    // shown in the main window's Message column instead — duplicating it
    // here was redundant noise and pushed the useful information off
    // screen. (User feedback after Patch B: full text in main window,
    // bytes in side panel, no overlap.)
    auto *hexGroup = new QGroupBox(tr("Raw bytes"));
    auto *hexLayout = new QVBoxLayout(hexGroup);
    m_hexView = new QTextEdit;
    m_hexView->setReadOnly(true);
    m_hexView->setFont(UiStyle::monoFont());
    m_hexView->setLineWrapMode(QTextEdit::NoWrap);
    // Right-click here to send the bytes somewhere useful. The panel is
    // where an operator is already looking when they decide a frame is
    // worth decoding by hand or replaying, so this is where the shortcut
    // to those two windows belongs.
    m_hexView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_hexView, &QWidget::customContextMenuRequested,
            this,      &RawBytesPanel::onHexContextMenu);
    hexLayout->addWidget(m_hexView);
    root->addWidget(hexGroup, 1);

    clear();
}

void RawBytesPanel::clear()
{
    m_entry.clear();
    m_headerLabel->setText(tr("(no row selected)"));
    m_hexView   ->setExtraSelections({});
    m_hexView   ->setPlainText(QString());
}

void RawBytesPanel::showEntry(const LogEntryPtr &entry)
{
    if (!entry) {
        clear();
        return;
    }

    m_entry = entry;

    // -------- Decoded header ------------------------------------------
    const QString friendly =
        m_names ? m_names->lookup(entry->header.source_id, entry->header.kvchId)
                : entry->tabKey();
    const QString recvTime =
        QDateTime::fromMSecsSinceEpoch(entry->epochMs).toString("HH:mm:ss.zzz");

    // Lay it out as fixed-pitch lines for alignment.
    const QString headerText = QString(
        "source_id : %1 (0x%2)\n"
        "dest_id   : %3\n"
        "msg_id    : %4\n"
        "msg_len   : %5\n"
        "kvchId    : %6\n"
        "friendly  : %7\n"
        "received  : %8")
        .arg(entry->header.source_id)
        .arg(entry->header.source_id, 2, 16, QChar('0'))
        .arg(entry->header.destination_id)
        .arg(entry->header.message_id)
        .arg(entry->header.message_len)
        .arg(entry->header.kvchId)
        .arg(friendly)
        .arg(recvTime);

    m_headerLabel->setText(headerText);

    // -------- Hex dump -------------------------------------------------
    m_hexView->setPlainText(formatHexDump(entry->rawBytes));
}

void RawBytesPanel::onHexContextMenu(const QPoint &pos)
{
    QMenu menu(this);

    QAction *actCopy = menu.addAction(tr("&Copy hex"));
    menu.addSeparator();
    QAction *actWb   = menu.addAction(tr("Open in Decode &Workbench"));
    QAction *actPm   = menu.addAction(tr("Open in Packet &Maker"));

    // Nothing selected means nothing to act on; grey the entries rather than
    // hiding them, so the menu does not change shape between rows.
    const bool have = !m_entry.isNull();
    actCopy->setEnabled(have);
    actWb  ->setEnabled(have);
    actPm  ->setEnabled(have);

    QAction *chosen = menu.exec(m_hexView->viewport()->mapToGlobal(pos));
    if (!chosen || !have) { return; }

    if (chosen == actCopy) {
        // Bytes only, space-separated: the hex dump on screen carries offsets
        // and an ASCII gutter, and pasting those into any other tool means
        // deleting them again by hand.
        QApplication::clipboard()->setText(
            QString::fromLatin1(m_entry->rawBytes.toHex(' ')).toUpper());
    } else if (chosen == actWb) {
        emit openInDecodeWorkbenchRequested(m_entry);
    } else if (chosen == actPm) {
        emit openInPacketMakerRequested(m_entry);
    }
}

void RawBytesPanel::highlightBytes(int byteStart, int byteEnd)
{
    QList<QTextEdit::ExtraSelection> sels;
    m_hexView->setExtraSelections(sels);        // clear first

    if (byteStart < 0 || byteEnd < byteStart) return;

    // Map byte indices to character positions in the dump. The layout is
    // fixed-width by construction, so this is arithmetic rather than a
    // search — see formatHexDump() for the column widths these constants
    // come from. Keep the two in step if that format ever changes.
    constexpr int kPerLine   = 16;
    constexpr int kOffsetCol = 10;   // "%08x" + two spaces
    constexpr int kHexCell   = 3;    // "xx "
    constexpr int kGroupGap  = 1;    // extra space before column 8
    constexpr int kAsciiGap  = 2;    // " |" before the ASCII gutter
    const int lineLen = kOffsetCol + kPerLine * kHexCell + kGroupGap
                        + kAsciiGap + kPerLine + 2;   // "|\n"

    QColor hl = palette().highlight().color();
    hl.setAlpha(90);

    QTextCursor cur(m_hexView->document());
    for (int b = byteStart; b <= byteEnd; ++b) {
        const int line = b / kPerLine;
        const int col  = b % kPerLine;
        const int base = line * lineLen + kOffsetCol
                         + col * kHexCell + (col >= kPerLine / 2 ? kGroupGap : 0);

        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(m_hexView->document());
        sel.cursor.setPosition(base);
        sel.cursor.setPosition(base + 2, QTextCursor::KeepAnchor);
        sel.format.setBackground(hl);
        sels.append(sel);

        // Matching mark in the ASCII gutter, so the highlight is legible
        // whichever half you happen to be reading.
        const int asciiPos = line * lineLen + kOffsetCol
                             + kPerLine * kHexCell + kGroupGap
                             + kAsciiGap + col;
        QTextEdit::ExtraSelection asel;
        asel.cursor = QTextCursor(m_hexView->document());
        asel.cursor.setPosition(asciiPos);
        asel.cursor.setPosition(asciiPos + 1, QTextCursor::KeepAnchor);
        asel.format.setBackground(hl);
        sels.append(asel);
    }

    m_hexView->setExtraSelections(sels);

    // Scroll the first highlighted byte into view.
    QTextCursor show(m_hexView->document());
    show.setPosition((byteStart / kPerLine) * lineLen);
    m_hexView->setTextCursor(show);
    m_hexView->ensureCursorVisible();
}

QString RawBytesPanel::formatHexDump(const QByteArray &bytes)
{
    // Classic xxd-style: 16 bytes per line, offset on the left, hex in the
    // middle, printable ASCII (or '.') on the right.
    //
    //   00000000  21 65 03 13 00 01 00 52  41 44 20 49 4e 20 66 72  |!e.....RAD IN fr|
    //   00000010  61 6d 65 20 30 78 34 32                           |ame 0x42        |

    QString out;
    out.reserve(bytes.size() * 4);

    constexpr int kPerLine = 16;
    for (int off = 0; off < bytes.size(); off += kPerLine) {
        // Both operands cast to int: QByteArray::size() is int on Qt 5 and
        // qsizetype on Qt 6, and std::min cannot deduce a common type from
        // the mismatched pair. The cast is safe — the value is bounded by
        // kPerLine.
        const int n = std::min(kPerLine, int(bytes.size() - off));

        // Offset.
        out += QString("%1  ").arg(off, 8, 16, QChar('0'));

        // Hex columns. 16 columns wide always; pad with spaces past 'n'.
        for (int i = 0; i < kPerLine; ++i) {
            if (i == kPerLine / 2) out += ' ';   // group of 8 separator
            if (i < n) {
                out += QString("%1 ")
                           .arg(static_cast<quint8>(bytes[off + i]),
                                2, 16, QChar('0'));
            } else {
                out += "   ";
            }
        }

        // ASCII gutter.
        out += " |";
        for (int i = 0; i < n; ++i) {
            const char c = bytes[off + i];
            out += (c >= 32 && c <= 126) ? QChar(c) : QChar('.');
        }
        for (int i = n; i < kPerLine; ++i) out += ' ';
        out += "|\n";
    }

    return out;
}
