#ifndef LOCOCONFIGCOMPAREDIALOG_H
#define LOCOCONFIGCOMPAREDIALOG_H
// =============================================================================
//  lococonfigcomparedialog.{h,cpp} -- two saved configurations, side by side.
//
//  Manage ▸ Compare configurations…  Pick A and B (say loco 7 and loco 9):
//  the table lists the fields whose values differ, A beside B, with the
//  group each field belongs to. "Show all fields" lists the rest too.
//
//  Why: a configuration is usually made by duplicating another loco's. The
//  fields that SHOULD differ (loco_unit_id, wheel diameters, vcc_crc) and
//  the ones that should not are then easy to tell apart, before a send.
//
//  Read-only: nothing here edits a configuration. Values are formatted as
//  the editor shows them (hex, dotted IPv4, ...). The send targets and
//  what was last sent are not compared: they are this PC's, not the loco's.
// =============================================================================
#include <QDialog>
#include <QList>

#include "lococonfigcore.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QTableWidget;

class LocoConfigCompareDialog : public QDialog
{
    Q_OBJECT
public:
    LocoConfigCompareDialog(const LocoInfo::Layout *layout, const LocoInfo::Presentation *presentation,
                            const QList<LocoInfo::LocoConfig> &configs, const QString &nameA,
                            QWidget *parent = nullptr);

    // For the tests (and the Copy button).
    void    setPair(const QString &nameA, const QString &nameB);
    void    setShowAll(bool on);
    int     rowCount() const;
    QString fieldAt(int row) const;           // the key of a row
    QString valueAt(int row, bool b) const;   // A's or B's value, as shown
    QString summary() const;                  // the line under the table
    QString asText() const;                   // what Copy puts on the clipboard

private:
    void rebuild();
    const LocoInfo::LocoConfig *config(const QString &name) const;

    const LocoInfo::Layout       *m_layout = nullptr;
    const LocoInfo::Presentation *m_presentation = nullptr;
    QList<LocoInfo::LocoConfig>   m_configs;

    QComboBox    *m_a = nullptr;
    QComboBox    *m_b = nullptr;
    QCheckBox    *m_all = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel       *m_summary = nullptr;
};

#endif  // LOCOCONFIGCOMPAREDIALOG_H
