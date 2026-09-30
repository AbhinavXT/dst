#ifndef ARCHIVESEARCHWINDOW_H
#define ARCHIVESEARCHWINDOW_H

// =============================================================================
//  ArchiveSearchWindow
//  -----------------------------------------------------------------------------
//  UI for searching recorded .dlr sessions on disk.
//
//  Separate from SearchWindow rather than a mode of it, because the two
//  differ in every way that matters to the user: this one is bounded by
//  dates rather than tabs, takes seconds to minutes rather than being
//  instant, needs cancelling, and its results open an archive viewer rather
//  than jumping to a live row. Folding them together would mean a dialog
//  where half the controls are inert at any moment.
// =============================================================================

#include <QWidget>

#include "archivesearch.h"

class ColorRules;
class NameMap;
class QCheckBox;
class QDateEdit;
class QLabel;
class StatusLine;
class QueryLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

class ArchiveSearchWindow : public QWidget
{
    Q_OBJECT

public:
    ArchiveSearchWindow(const ColorRules *rules,
                        const NameMap    *names,
                        QWidget          *parent = nullptr);
    ~ArchiveSearchWindow() override;

signals:
    // Offered so the two search windows are reachable from one another
    // rather than being two menu items a user has to know about
    // independently.
    void liveSearchRequested(const QString &queryText);

    // Ask MainWindow to open this .dlr in a SessionWindow, positioned at
    // the matching record.
    void openSessionRequested(const QString &filePath, qint64 epochMs);

protected:
    // Remember size/position for the next opening.
    void closeEvent(QCloseEvent *event) override;

private slots:
    void startSearch();
    void cancelSearch();
    void onProgress(int done, int total);
    void onFinished(ArchiveScanResult result);
    void onRowActivated(int row, int column);

private:
    void setBusy(bool busy);

    const ColorRules *m_rules = nullptr;
    const NameMap    *m_names = nullptr;

    QueryLineEdit *m_query   = nullptr;
    QDateEdit    *m_from     = nullptr;
    QDateEdit    *m_to       = nullptr;
    QCheckBox    *m_allDates = nullptr;
    QCheckBox    *m_capOnly  = nullptr;
    QPushButton  *m_go       = nullptr;
    QPushButton  *m_cancel   = nullptr;
    QProgressBar *m_progress = nullptr;
    StatusLine       *m_status   = nullptr;
    QTableWidget *m_results  = nullptr;

    ArchiveSearcher     *m_worker = nullptr;
    QVector<ArchiveHit>  m_hits;

    static constexpr int kMaxHits = 5000;
};

#endif // ARCHIVESEARCHWINDOW_H
