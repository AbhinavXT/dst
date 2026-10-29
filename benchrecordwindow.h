#ifndef BENCHRECORDWINDOW_H
#define BENCHRECORDWINDOW_H

// =============================================================================
//  Test recording viewer (session 404) — Tools ▸ Simulators ▸ Test
//  recording…: a .benchrec file (benchrecord.h) as a table.
//  -----------------------------------------------------------------------------
//  One row per frame: the time and the seconds since the recording started,
//  what kind (speed, tag, biu, analog, event), sent or received, the peer,
//  what it says, the note, the bytes. A check box per kind (with its count),
//  find (every word, anywhere in the row), Go to a second, Export CSV (the
//  rows shown, or all).
// =============================================================================

#include "benchrecord.h"

#include <QHash>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class StatusLine;

class BenchRecordWindow : public QWidget
{
    Q_OBJECT
public:
    explicit BenchRecordWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);       // false (and said) when it is not a recording
    const BenchRecord::File &file() const { return m_file; }
    QTableWidget *table() const { return m_table; }
    void setKindShown(const QString &kind, bool shown);
    void setFind(const QString &text);
    int  shownRows() const;
    int  goTo(double seconds);                // selects the first row at or after it; the row, -1 none
    QString toCsv(bool shownOnly) const;
    bool exportCsv(const QString &path, bool shownOnly);

private:
    void fill();
    void applyFilter();

    BenchRecord::File m_file;
    QString        m_path;
    QLabel        *m_fileLabel = nullptr;
    QHash<QString, QCheckBox *> m_kindBox;
    QLineEdit     *m_find = nullptr;
    QDoubleSpinBox *m_goTo = nullptr;
    QTableWidget  *m_table = nullptr;
    StatusLine    *m_status = nullptr;
};

#endif  // BENCHRECORDWINDOW_H
