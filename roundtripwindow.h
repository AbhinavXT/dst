#ifndef ROUNDTRIPWINDOW_H
#define ROUNDTRIPWINDOW_H
// =====================================================================
//  roundtripwindow.{h,cpp} -- run the round-trip validator over a
//  corpus and show the answer per packet type.
//
//  All the judgement lives in roundtrip.{h,cpp}; this picks the corpus,
//  drives the worker, and renders. Two things it adds that the core
//  deliberately cannot:
//
//    * FIELD ATTRIBUTION. The core records "the bytes differ at body
//      byte 20"; naming the field that owns byte 20 needs the shared
//      decoder, which is GUI-thread state, so the naming happens here.
//    * A HANDOFF TO FRAME DIFF. A failing frame and its rebuild are the
//      same shape, so sending both to Frame Diff turns "byte 20 differs"
//      into "L_DOUBTOVER read back as 227". That is usually the whole
//      diagnosis.
//
//  This window sends nothing. It reads a corpus and reports.
// =====================================================================
#include <QDialog>
#include <QPointer>
#include <QVector>

#include "logentry.h"
#include "roundtrip.h"

class QCheckBox;
class QLabel;
class StatusLine;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTextEdit;

class RoundTripWindow : public QDialog
{
    Q_OBJECT

public:
    explicit RoundTripWindow(QWidget *parent = nullptr);
    ~RoundTripWindow() override;

    // A snapshot of the live log, taken by MainWindow (pointer copies, so
    // the cost is the vector). Passing it in rather than reaching for the
    // models keeps this window out of MainWindow's internals.
    void setLiveEntries(const QVector<LogEntryPtr> &entries);

signals:
    // A failing frame and its rebuild, as capture lines, for Frame Diff.
    void diffRequested(const QString &captured, const QString &rebuilt);

private slots:
    void onAddCaptureFiles();
    void onAddSessions();
    void onClearFiles();
    void onRun();
    void onCancel();
    void onProgress(qint64 done, qint64 total);
    void onFinished(RoundTrip::Report report);
    void onTypeSelected();
    void onDiffSample();
    void onCopyReport();
    void onSaveReport();

protected:
    // Escape while a scan is in flight cancels the scan and keeps the
    // window, rather than throwing away a run over a large corpus that
    // takes minutes. Same rule as the transmitting dialogs — see
    // sendguard.h — for the same reason: one keypress, one thing.
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void stopScan();          // cancel and join a scan in flight
    void refreshRunState();
    void fillTable();
    void showDetail(const QString &captype);

    QVector<LogEntryPtr> m_entries;
    QStringList m_capFiles;
    QStringList m_sessionFiles;

    RoundTrip::Report m_report;
    bool m_haveReport = false;

    QPointer<RoundTrip::Runner> m_runner;

    QCheckBox       *m_useLive   = nullptr;
    QListWidget     *m_fileList  = nullptr;
    QPushButton     *m_addCap    = nullptr;
    QPushButton     *m_addSess   = nullptr;
    QPushButton     *m_clearBtn  = nullptr;
    QPushButton     *m_runBtn    = nullptr;
    QPushButton     *m_cancelBtn = nullptr;
    QProgressBar    *m_progress  = nullptr;
    QTableWidget    *m_table     = nullptr;
    QTextEdit       *m_detail    = nullptr;
    StatusLine      *m_status    = nullptr;
    QPushButton     *m_diffBtn   = nullptr;
    QPushButton     *m_copyBtn   = nullptr;
    QPushButton     *m_saveBtn   = nullptr;
};

#endif  // ROUNDTRIPWINDOW_H
