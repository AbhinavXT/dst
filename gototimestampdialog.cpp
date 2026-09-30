#include "gototimestampdialog.h"
#include <limits>
#include <QSortFilterProxyModel>
#include "logmodel.h"
#include "uicolors.h"

#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

// The editor format. Full date + time + milliseconds so the target instant is
// never ambiguous, even across a midnight boundary. The time portion matches
// the Time column's HH:mm:ss.zzz so the operator can transcribe a value they
// read off a row with no mental translation.
static const char *kEditFormat = "yyyy-MM-dd HH:mm:ss.zzz";

GotoTimestampDialog::GotoTimestampDialog(qint64   seedMs,
                                         qint64   minMs,
                                         qint64   maxMs,
                                         QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Go to timestamp"));
    setModal(true);

    // Defensive: if a caller passes the span the wrong way round, swap rather
    // than handing QDateTimeEdit an inverted range (which it rejects).
    if (minMs > maxMs) {
        const qint64 t = minMs;
        minMs = maxMs;
        maxMs = t;
    }
    // Clamp the seed into the span so the editor opens on a value inside its
    // own range. (A caller could hand us a seed from a row that has since
    // been trimmed; clamping is cheaper than validating upstream.)
    if (seedMs < minMs) seedMs = minMs;
    if (seedMs > maxMs) seedMs = maxMs;

    auto *root = new QVBoxLayout(this);

    // ---- Context hint: what time span this tab actually covers -----------
    // Shows date only when the span straddles more than one calendar day,
    // otherwise time-only to stay compact and match the column.
    {
        const QDateTime lo = QDateTime::fromMSecsSinceEpoch(minMs);
        const QDateTime hi = QDateTime::fromMSecsSinceEpoch(maxMs);
        const bool sameDay = (lo.date() == hi.date());
        const QString fmt = sameDay ? QStringLiteral("HH:mm:ss.zzz")
                                    : QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz");
        auto *hint = new QLabel(tr("This tab spans  %1  →  %2")
                                    .arg(lo.toString(fmt), hi.toString(fmt)));
        hint->setStyleSheet(UiColor::mutedStyle());
        root->addWidget(hint);
    }

    // ---- Form: target instant + resolution mode -------------------------
    auto *form = new QFormLayout;

    m_edit = new QDateTimeEdit;
    m_edit->setDisplayFormat(QString::fromLatin1(kEditFormat));
    m_edit->setCalendarPopup(true);
    m_edit->setDateTimeRange(QDateTime::fromMSecsSinceEpoch(minMs),
                             QDateTime::fromMSecsSinceEpoch(maxMs));
    m_edit->setDateTime(QDateTime::fromMSecsSinceEpoch(seedMs));
    form->addRow(tr("Jump to:"), m_edit);

    m_modeBox = new QComboBox;
    // Order MUST match the Mode enum values (the slot reads currentIndex()
    // straight into the enum). Index 0 = default.
    m_modeBox->addItem(tr("First message at or after this time"),
                       int(Mode::AtOrAfter));
    m_modeBox->addItem(tr("Last message at or before this time"),
                       int(Mode::AtOrBefore));
    m_modeBox->addItem(tr("Nearest message in time"),
                       int(Mode::Nearest));
    m_modeBox->setCurrentIndex(0);
    m_modeBox->setToolTip(tr(
        "How to resolve the jump when no message sits exactly on the chosen "
        "instant. 'At or after' lands on the next message; 'at or before' on "
        "the previous one; 'nearest' on whichever is closer in time."));
    form->addRow(tr("Land on:"), m_modeBox);

    root->addLayout(form);

    // ---- Buttons --------------------------------------------------------
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    // Enter in the editor confirms the dialog — Ctrl+G then immediate Enter
    // is the fast path for "jump near where I already am".
    m_edit->setFocus();
}

qint64 GotoTimestampDialog::targetMs() const
{
    return m_edit->dateTime().toMSecsSinceEpoch();
}

GotoTimestampDialog::Mode GotoTimestampDialog::mode() const
{
    // itemData carries the enum value explicitly, so this stays correct even
    // if the visible order is ever reshuffled.
    return static_cast<Mode>(m_modeBox->currentData().toInt());
}

// ---------------------------------------------------------------------------
//  Resolving a target instant to a row
// ---------------------------------------------------------------------------

qint64 GotoTimestamp::epochAtProxyRow(const QAbstractItemModel *view,
                                      const LogModel *src, int row)
{
    if (!view || !src) { return -1; }
    int sourceRow = row;
    if (const auto *proxy = qobject_cast<const QSortFilterProxyModel *>(view)) {
        const QModelIndex p = proxy->index(row, 0);
        if (!p.isValid()) { return -1; }
        const QModelIndex s = proxy->mapToSource(p);
        if (!s.isValid()) { return -1; }
        sourceRow = s.row();
    } else if (row < 0 || row >= view->rowCount()) {
        return -1;
    }
    const LogEntryPtr e = src->entryAt(sourceRow);
    return e ? e->epochMs : -1;
}

GotoTimestamp::Span GotoTimestamp::spanOf(const QAbstractItemModel *view,
                                          const LogModel *src)
{
    Span out;
    if (!view || !src) { return out; }
    const int rows = view->rowCount();
    qint64 lo = std::numeric_limits<qint64>::max();
    qint64 hi = std::numeric_limits<qint64>::min();
    for (int r = 0; r < rows; ++r) {
        const qint64 ms = epochAtProxyRow(view, src, r);
        if (ms < 0) { continue; }
        lo = qMin(lo, ms);
        hi = qMax(hi, ms);
    }
    if (lo > hi) { return out; }
    out.minMs = lo;
    out.maxMs = hi;
    out.ok    = true;
    return out;
}

int GotoTimestamp::resolveRow(const QAbstractItemModel *view, const LogModel *src,
                              qint64 targetMs, int mode)
{
    if (!view || !src) { return -1; }
    const int rows = view->rowCount();

    int    bestRow = -1;
    qint64 bestKey = 0;

    for (int r = 0; r < rows; ++r) {
        const qint64 ms = epochAtProxyRow(view, src, r);
        if (ms < 0) { continue; }

        bool eligible = false;
        qint64 keyVal = 0;
        switch (static_cast<GotoTimestampDialog::Mode>(mode)) {
        case GotoTimestampDialog::Mode::AtOrAfter:
            if (ms >= targetMs) { eligible = true; keyVal = ms - targetMs; }
            break;
        case GotoTimestampDialog::Mode::AtOrBefore:
            if (ms <= targetMs) { eligible = true; keyVal = targetMs - ms; }
            break;
        case GotoTimestampDialog::Mode::Nearest:
            eligible = true;
            keyVal   = qAbs(ms - targetMs);
            break;
        }
        if (!eligible) { continue; }

        // Ties happen constantly — a burst shares one timestamp to the
        // millisecond. "First at or after T" reads literally, so the earliest
        // row of a tied group wins; "last at or before T" means the most
        // recent, which is the highest row of the group.
        bool better;
        if (bestRow < 0) {
            better = true;
        } else if (static_cast<GotoTimestampDialog::Mode>(mode)
                       == GotoTimestampDialog::Mode::AtOrBefore) {
            better = (keyVal <= bestKey);
        } else {
            better = (keyVal < bestKey);
        }
        if (better) { bestRow = r; bestKey = keyVal; }
    }

    // Nothing on the requested side: fall back to nearest rather than
    // no-opping, so the key always does something the operator can see.
    if (bestRow < 0) {
        for (int r = 0; r < rows; ++r) {
            const qint64 ms = epochAtProxyRow(view, src, r);
            if (ms < 0) { continue; }
            const qint64 keyVal = qAbs(ms - targetMs);
            if (bestRow < 0 || keyVal < bestKey) { bestRow = r; bestKey = keyVal; }
        }
    }
    return bestRow;
}
