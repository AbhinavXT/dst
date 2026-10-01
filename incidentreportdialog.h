#ifndef INCIDENTREPORTDIALOG_H
#define INCIDENTREPORTDIALOG_H

// =============================================================================
//  IncidentReportDialog (session 97)
//  -----------------------------------------------------------------------------
//  A small modal dialog that collects the one thing incidentreport.h needs
//  to start: the moment to centre the report on, and how far before/after it
//  to pull in. Mirrors GotoTimestampDialog's shape (gototimestampdialog.h) —
//  dumb on purpose, knows nothing about LogModel or the report itself.
// =============================================================================

#include <QDialog>

class QDateTimeEdit;
class QSpinBox;

class IncidentReportDialog : public QDialog
{
    Q_OBJECT

public:
    // seedMs : the instant the editor starts on (typically the tab's latest row).
    // minMs / maxMs : the span actually present in the tab, to clamp the editor
    //                 and show a hint — the window itself may still reach past
    //                 either end; the report just finds fewer rows there.
    explicit IncidentReportDialog(qint64 seedMs, qint64 minMs, qint64 maxMs, QWidget *parent = nullptr);

    qint64 atMs() const;
    qint64 beforeMs() const;   // window before atMs, ms
    qint64 afterMs() const;    // window after atMs, ms

private:
    QDateTimeEdit *m_edit   = nullptr;
    QSpinBox      *m_before = nullptr;
    QSpinBox      *m_after  = nullptr;
};

#endif // INCIDENTREPORTDIALOG_H
