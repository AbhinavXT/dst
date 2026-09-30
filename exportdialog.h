#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

// =============================================================================
//  ExportDialog
//  -----------------------------------------------------------------------------
//  Custom save-file dialog for tab exports. Combines:
//    – file path picker (line edit + Browse button)
//    – format radio buttons (CSV / JSON)
//    – column include checkboxes (Time, Source, Friendly, Direction,
//      Severity, Message, Raw bytes hex)
//
//  Why a custom dialog rather than getSaveFileName + a separate config?
//  Because the column choice is tightly coupled to the act of exporting:
//  you decide what to include at the moment you decide where to save.
//  A separate "Configure columns" menu would mean two dialogs to reach
//  one CSV file. A Settings entry would make column choices feel like a
//  permanent preference, which they usually aren't.
//
//  Defaults: all columns on, matching current export behavior. So if you
//  click through without unticking anything you get the same export as
//  before this dialog existed. No silent change.
//
//  Persistence (session-only): MainWindow keeps the last-used selection
//  alive across exports within a session. Not persisted to INI — column
//  choices are usually situational ("this analysis I want bytes; that
//  one I want minimal"). If we need cross-session memory, add later.
// =============================================================================

#include <QDialog>

#include "exporter.h"      // for Exporter::Format, Exporter::ColumnFlag

class QCheckBox;
class QLineEdit;
class QRadioButton;

class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(const QString &defaultPath,
                          Exporter::Format defaultFormat,
                          Exporter::ColumnFlags defaultColumns,
                          QWidget *parent = nullptr);

    QString               path()    const;
    Exporter::Format      format()  const;
    Exporter::ColumnFlags columns() const;

private slots:
    void onBrowse();
    void onFormatChanged();   // toggles column-availability hints

private:
    void updateExtensionForFormat();

    QLineEdit    *m_path        = nullptr;
    QRadioButton *m_radCsv      = nullptr;
    QRadioButton *m_radJson     = nullptr;

    QCheckBox    *m_cbTime      = nullptr;
    QCheckBox    *m_cbSource    = nullptr;
    QCheckBox    *m_cbFriendly  = nullptr;
    QCheckBox    *m_cbDirection = nullptr;
    QCheckBox    *m_cbSeverity  = nullptr;
    QCheckBox    *m_cbMessage   = nullptr;
    QCheckBox    *m_cbRaw       = nullptr;
};

#endif // EXPORTDIALOG_H
