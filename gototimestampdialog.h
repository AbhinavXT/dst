#ifndef GOTOTIMESTAMPDIALOG_H
#define GOTOTIMESTAMPDIALOG_H

// =============================================================================
//  GotoTimestampDialog  (Ctrl+G)
//  -----------------------------------------------------------------------------
//  A small modal dialog that collects ONE thing: which instant in time the
//  operator wants to jump to in the current tab, and how to resolve it when
//  no row sits exactly on that instant.
//
//  Deliberately dumb on purpose. It does NOT know about LogModel, the proxy,
//  the view, or how the jump is performed. It just gathers (targetMs, mode)
//  and hands them back. MainWindow owns all the model/proxy knowledge (it
//  already has entryFromProxyIndex, snapshotProxy, etc.) and does the actual
//  search + scroll. That keeps this class trivially testable and keeps the
//  search logic next to the other proxy-walking code it resembles.
//
//  Why a separate class at all (and not yet another method on MainWindow):
//  MainWindow is already ~1000 lines and trending toward god-class. The two
//  remaining dialog-style features (this and the per-tab statistics summary)
//  are self-contained enough that they each earn their own file. The pattern
//  mirrors ExportDialog / SettingsDialog, which are also standalone QDialogs.
//
//  Time handling:
//    – Timestamps in the app are wall-clock, milliseconds since the Unix
//      epoch, displayed in LOCAL time (see LogModel::formatTime, which uses
//      QDateTime::fromMSecsSinceEpoch with the default local spec). This
//      dialog uses the same local interpretation so what the operator types
//      matches what the Time column shows.
//    – The Time column only shows HH:mm:ss.zzz (date hidden, because the
//      capture is "almost always today"). A live capture CAN cross midnight,
//      though, so the editor here exposes the full date as well to keep the
//      target instant unambiguous. It is seeded from a reference row so the
//      operator usually only edits the time portion.
//
//  The editor's range is clamped to [minMs, maxMs] — the span actually
//  present in the (filtered) tab. That removes the "I asked for a time after
//  the last message, where did it land?" edge entirely: every value the
//  operator can dial in resolves to a real, visible row.
// =============================================================================

#include <QDialog>

class QComboBox;
class QDateTimeEdit;

class QAbstractItemModel;
class QSortFilterProxyModel;
class LogModel;

// ---------------------------------------------------------------------------
//  Resolving a target instant to a row.
//
//  Pulled out of MainWindow so the recorded-session window can jump the same
//  way rather than growing a second copy of ninety lines of tie-breaking —
//  and because the tie-breaking is the part worth testing, and it was not
//  reachable from a test while it lived inside a slot.
// ---------------------------------------------------------------------------
namespace GotoTimestamp {

// The epoch of a proxy row, or -1 if anything about it is off.
// `view` is the model the VIEW holds: a filter proxy over the LogModel, or
// the LogModel itself where a window binds one directly. Same tolerance as
// nextMarkedRow(), and for the same reason — the compare panes have no
// proxy and should not need a second implementation of "go to a time".
qint64 epochAtProxyRow(const QAbstractItemModel *view,
                       const LogModel *src, int row);

// The span actually present in the (filtered) view. ok is false when no row
// carries a readable time.
struct Span { qint64 minMs = 0; qint64 maxMs = 0; bool ok = false; };
Span spanOf(const QAbstractItemModel *view, const LogModel *src);

// The proxy row the target resolves to under `mode`, or -1. Falls back to
// nearest when the directional modes find nothing, so the shortcut always
// does something visible rather than silently no-op.
int resolveRow(const QAbstractItemModel *view, const LogModel *src,
               qint64 targetMs, int mode);

}  // namespace GotoTimestamp

class GotoTimestampDialog : public QDialog
{
    Q_OBJECT

public:
    // How to resolve a target instant that falls between two rows (or that
    // no row matches exactly). The names match the combo-box wording the
    // operator sees.
    enum class Mode {
        AtOrAfter  = 0,   // first row with time >= target  (default)
        AtOrBefore = 1,   // last  row with time <= target
        Nearest    = 2    // row minimising |time - target|
    };

    // seedMs : the instant the editor starts on (typically the top-visible
    //          row's time, so the operator nudges from where they already are).
    // minMs / maxMs : the span present in the current (filtered) tab. Used to
    //          clamp the editor and to show a "log spans …" hint. Pass
    //          minMs == maxMs for a single-row tab; that is handled fine.
    explicit GotoTimestampDialog(qint64   seedMs,
                                 qint64   minMs,
                                 qint64   maxMs,
                                 QWidget *parent = nullptr);

    // The chosen instant, milliseconds since the Unix epoch (local-time
    // interpretation, matching the rest of the app). Only meaningful after
    // exec() returns QDialog::Accepted.
    qint64 targetMs() const;

    // The chosen resolution mode.
    Mode mode() const;

private:
    QDateTimeEdit *m_edit    = nullptr;
    QComboBox     *m_modeBox = nullptr;
};

#endif // GOTOTIMESTAMPDIALOG_H
