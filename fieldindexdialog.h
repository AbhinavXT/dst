#ifndef FIELDINDEXDIALOG_H
#define FIELDINDEXDIALOG_H
// =============================================================================
//  fieldindexdialog.{h,cpp} — which packets carry a given field.
//
//  The schema knows this and nothing showed it. Thirty-five field names occur
//  in more than one packet and FRAME_NUM is in five, so "narrow this pin to a
//  packet" is a question an operator cannot answer from the pin panel alone —
//  the nested chooser says a packet HAS a field, never how many others do.
//
//  The answer that matters is usually the count. A field in one packet needs
//  no narrowing at all; a field in five is one where an unnarrowed pin will
//  quietly show whichever packet spoke last.
//
//  Read-only, non-modal, and built entirely from Schema::Decoder — it holds
//  no traffic and nothing here depends on a session being loaded.
// =============================================================================
#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace Schema { class Decoder; }

class FieldIndexDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FieldIndexDialog(const Schema::Decoder *decoder,
                              QWidget *parent = nullptr);

    // Pre-fill the filter. Used when the dialog is opened from a field the
    // operator right-clicked: arriving at a list of five hundred names when
    // one was asked about is an answer they then have to search for.
    void setFilter(const QString &text);

    // Rebuild from the decoder. Called on schema reload, so an edited
    // kavach.xml is reflected without reopening.
    void reload();

private:
    void applyFilter();

    const Schema::Decoder *m_decoder = nullptr;
    QLineEdit    *m_filter    = nullptr;
    QCheckBox    *m_sharedOnly = nullptr;
    QTableWidget *m_table     = nullptr;
    QLabel       *m_summary   = nullptr;
};

#endif  // FIELDINDEXDIALOG_H
