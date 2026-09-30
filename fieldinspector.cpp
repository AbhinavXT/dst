#include "fieldinspector.h"
#include "statusline.h"
#include "uicolors.h"

#include "capturedecoder.h"
#include "locoidentity.h"
#include "rejectrules.h"
#include "schema/schemadecoder.h"

#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QMenu>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

FieldInspector::FieldInspector(QWidget *parent)
    : QWidget(parent)
{
    m_status = new StatusLine;
    m_status->state(tr("Select a row to decode it."));

    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({ tr("Field"), tr("Value"), tr("Bytes") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);

    // Right-click a row to keep that field in view, or to plot it. The field
    // chooser in either window is the long way round to something the
    // operator is already pointing at.
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        QTableWidgetItem *it = m_table->itemAt(pos);
        if (!it) { return; }
        const QTableWidgetItem *nameItem = m_table->item(it->row(), 0);
        if (!nameItem) { return; }
        const QString field = nameItem->text().trimmed();
        if (field.isEmpty()) { return; }

        QMenu menu(this);
        // Named with the field in them: a bare "Pin" beside a table of forty
        // rows leaves the operator checking which row they were on.
        QAction *pin  = menu.addAction(tr("Pin %1").arg(field));
        QAction *plot = menu.addAction(tr("Plot %1 over time").arg(field));
        QAction *copy = menu.addAction(tr("Copy value"));
        menu.addSeparator();
        QAction *where = menu.addAction(tr("Which packets carry %1?").arg(field));
        QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
        if (chosen == pin)  { emit pinFieldRequested(field); }
        if (chosen == plot) { emit plotFieldRequested(field); }
        if (chosen == where) { emit locateFieldRequested(field); }
        if (chosen == copy) {
            const QTableWidgetItem *v = m_table->item(it->row(), 1);
            if (v) { QGuiApplication::clipboard()->setText(v->text().trimmed()); }
        }
    });
    m_table->verticalHeader()->setVisible(false);
    // 190 + 190 + 70 needs 450px; this panel is docked at roughly 280 and
    // so was permanently scrolled sideways with its headers cut to "alt"
    // and "Byte". Field and Value share the width, Bytes takes what it
    // needs, and nothing is truncated at the width the dock actually has.
    QHeaderView *h = m_table->horizontalHeader();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    h->setSectionResizeMode(1, QHeaderView::Stretch);
    h->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(m_status);
    layout->addWidget(m_table, 1);

    connect(m_table, &QTableWidget::currentCellChanged,
            this, [this](int row, int, int, int) { onRowChanged(row); });
}

void FieldInspector::setDecoder(const Schema::Decoder *decoder)
{
    m_decoder = decoder;
}

void FieldInspector::setStatus(const QString &text, bool bad)
{
    // Everything this panel says is a caption for what it is currently
    // showing — "Select a row to decode it", "18 fields, 2 flagged". None of
    // it is news, so none of it expires: a panel that explains itself for
    // six seconds and then goes blank is worse than one that never did.
    if (bad) { m_status->fail(text); } else { m_status->state(text); }
}

void FieldInspector::clear()
{
    m_entry.reset();
    m_rows.clear();
    m_table->setRowCount(0);
    setStatus(tr("Select a row to decode it."), false);
    emit byteRangeSelected(-1, -1);
}

QString FieldInspector::currentCaptype() const
{
    if (m_entry.isNull()) { return QString(); }
    // Parsed from the entry rather than remembered alongside it: one source of
    // truth for what this frame is, and it is the same one the decode used.
    const CaptureLine c = CaptureDecoder::parseLine(m_entry->text);
    return c.valid ? c.typeToken : QString();
}

void FieldInspector::showEntry(const LogEntryPtr &entry)
{
    m_entry = entry;
    m_rows.clear();
    m_table->setRowCount(0);
    emit byteRangeSelected(-1, -1);

    if (!entry) { clear(); return; }

    if (!m_decoder || !m_decoder->isLoaded()) {
        setStatus(tr("No schema loaded — kavach.xml was not found or failed "
                     "to parse, so fields cannot be decoded."), true);
        return;
    }

    // Decode through CaptureDecoder::describe(), NOT by handing raw bytes
    // to the schema directly.
    //
    // The schema selects a packet by `captype`, and captype comes from the
    // @<type>_<loco>_<ctrl> token at the head of a capture line's TEXT — not
    // from the payload bytes. kavach.xml defines no `pkt_type==` packets, so
    // packetFor() with an empty captype always returns null: decoding the
    // payload directly could never match anything, for any frame. describe()
    // is the path that parses the token, checks the CRC, and dispatches on
    // type, and it is what the Loco Console and Decode Workbench already use.
    const CaptureLine cap = CaptureDecoder::parseLine(entry->text);

    if (!cap.valid) {
        // Ordinary text diagnostics are the common case on most tabs and
        // are not a fault, so this is stated plainly rather than in red and
        // is NOT recorded as a decode failure — flooding the failure dock
        // with every human-readable log line would bury the real gaps.
        setStatus(tr("Not a capture frame — this row is a text message, so "
                     "there are no binary fields to decode."), false);
        return;
    }

    if (cap.bytes.isEmpty()) {
        setStatus(tr("Capture line '%1' carries no frame bytes.")
                      .arg(cap.typeToken), true);
        emit decodeFailed(entry, tr("capture line with no bytes"));
        return;
    }

    QHash<QString, qint64> raw;
    m_rows = CaptureDecoder::describe(cap, nullptr, 0, &raw);

    // Why the equipment at the other end would not act on this frame.
    //
    // Evaluated on the decoded NUMBERS, not on the rows above: a row's value
    // is a display string, and comparing a clause against "2 (Reverse)"
    // would break the day the label is reworded.
    // Learn who we are from this frame before judging it. ARP and LSRP
    // carry SOURCE_LOCO_ID; everything else teaches nothing and is ignored.
    const QString sourceKey = entry->tabKey();
    if (m_identity) {
        m_identity->observe(sourceKey, cap.typeToken, raw);

        // The learned identity travels beside the decoded fields, so the
        // rules stay a plain comparison of two values and the engine needs
        // no notion of identity at all.
        const QHash<QString, qint64> ctx = m_identity->contextFor(sourceKey);
        for (auto it = ctx.cbegin(); it != ctx.cend(); ++it) {
            raw.insert(it.key(), it.value());
        }
    }

    m_findings.clear();
    for (const RejectRules::Finding &f : kavachRejectRules().evaluate(raw, cap.typeToken)) {
        m_findings << f.text;
    }

    if (m_rows.isEmpty()) {
        setStatus(tr("Nothing could decode a '%1' frame of %2 byte(s). The "
                     "schema may not cover this type yet.")
                      .arg(cap.typeToken).arg(cap.bytes.size()), true);
        emit decodeFailed(entry, tr("no decoder for type '%1'").arg(cap.typeToken));
        return;
    }

    // A CRC result from the capture layer is worth surfacing even when
    // every field decoded cleanly: it says the bytes themselves are suspect.
    if (cap.crcChecked && !cap.crcOk) {
        emit decodeFailed(entry, tr("CRC failed on '%1'").arg(cap.typeToken));
    }

    m_table->setRowCount(m_rows.size());
    int badRows = 0;
    int spanned = 0;

    for (int i = 0; i < m_rows.size(); ++i) {
        const FieldRow &r = m_rows.at(i);

        auto *nameItem = new QTableWidgetItem(r.field);
        auto *valItem  = new QTableWidgetItem(r.value);

        QString bytes;
        if (r.hasSpan()) {
            ++spanned;
            bytes = (r.byteStart() == r.byteEnd())
                        ? QString::number(r.byteStart())
                        : QStringLiteral("%1–%2").arg(r.byteStart())
                                                 .arg(r.byteEnd());
        }
        auto *byteItem = new QTableWidgetItem(bytes);
        byteItem->setForeground(UiColor::muted());

        // Surface CRC and schema-error rows in colour. A failed CRC buried
        // in a hundred-row list is the same as no CRC check at all.
        const QString v = r.value.toUpper();
        const bool bad = r.field.contains(QLatin1String("<schema error>"))
                         || v.contains(QLatin1String("BAD"))
                         || v.contains(QLatin1String("FAIL"))
                         || v.contains(QLatin1String("MISMATCH"));
        if (bad) {
            ++badRows;
            const QColor red("#cc3300");
            nameItem->setForeground(red);
            valItem->setForeground(red);
        }

        m_table->setItem(i, 0, nameItem);
        m_table->setItem(i, 1, valItem);
        m_table->setItem(i, 2, byteItem);
    }

    // Reject reasons come first when there are any: a frame the receiver
    // would drop is a more urgent fact than how many of its fields carry
    // byte ranges.
    //
    // "would not process it" and never "invalid" — the console reports the
    // condition and the clause, and the verdict belongs to whoever signs the
    // test sheet. Silence means no rule in rejectrules.xml matched, which is
    // not the same claim as the packet being good, so nothing here says so.
    if (!m_findings.isEmpty()) {
        setStatus(tr("A receiver would not process this frame — %1")
                      .arg(m_findings.join(QStringLiteral("; "))), true);
    } else if (badRows > 0) {
        setStatus(tr("%1 field(s), %2 flagged — CRC or schema problem. "
                     "Click a flagged row to see which bytes it covers.")
                      .arg(m_rows.size()).arg(badRows), true);
        emit decodeFailed(entry, tr("%1 field(s) failed validation").arg(badRows));
    } else {
        setStatus(tr("%1 field(s), %2 with byte ranges. Click a field to "
                     "highlight its bytes.").arg(m_rows.size()).arg(spanned),
                  false);
    }
}

void FieldInspector::onRowChanged(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        emit byteRangeSelected(-1, -1);
        return;
    }
    const FieldRow &r = m_rows.at(row);
    if (!r.hasSpan()) {
        emit byteRangeSelected(-1, -1);
        return;
    }

    // Offsets are relative to the CAPTURE FRAME (CaptureLine::bytes), which
    // is what describe() decoded. The hex panel shows the datagram, and the
    // frame arrives as hex text inside that datagram's payload rather than
    // as bytes at a fixed offset — so there is no honest mapping between
    // the two, and inventing one would highlight unrelated bytes.
    //
    // Emitting frame-relative offsets is still useful for the Bytes column;
    // the hex highlight is suppressed rather than made up.
    emit byteRangeSelected(-1, -1);
}
