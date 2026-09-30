#ifndef FAULTPANELWINDOW_H
#define FAULTPANELWINDOW_H

// =============================================================================
//  FaultPanelWindow
//  -----------------------------------------------------------------------------
//  A standalone live "active faults" board built from the NMS fault stream
//  (@nmsfault). Each fault packet is a snapshot of the faults a reporting
//  subsystem currently asserts; this window merges those snapshots into a live
//  per-source view of what is faulted right now, fully named via the module /
//  fault-input dictionaries.
//
//  Active-fault semantics: a fault is held active while its reporting subsystem
//  keeps listing it. When a fresh snapshot from that subsystem no longer
//  contains it, the fault is marked cleared (kept briefly in history). FIRST
//  seen / LAST seen times are preserved across snapshots.
// =============================================================================

#include <QMainWindow>
#include <QHash>
#include <QString>

#include "capturedecoder.h"
#include "logentry.h"

class MessageDispatcher;
class QComboBox;
class QLabel;
class QCheckBox;
class QTableWidget;
class QStackedWidget;
class QTimer;
class QEvent;

class FaultPanelWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit FaultPanelWindow(MessageDispatcher *dispatcher, QWidget *parent = nullptr);
    ~FaultPanelWindow() override = default;

protected:
    // Remember size/position for the next opening.
    void closeEvent(QCloseEvent *event) override;

    // Re-skin when the application palette/style changes (theme toggle) so the
    // panel always matches Light/Dark instead of being a hardcoded dark island.
    void changeEvent(QEvent *e) override;

private slots:
    void onEntryAppended(QString tabKey, LogEntryPtr entry);
    void onSelectionChanged(int index);
    void onShowClearedToggled(bool on);
    void onRefreshTick();
    void onExportReport();

public:
    // Public for the report builder's tests: the struct is plain data and
    // the builder is a pure function of it, so exposing them is cheaper
    // than a friend declaration or a test-only shim.
    struct Fault {
        int    subsystem = 0;
        int    moduleId  = 0;
        int    codeType  = 0;
        int    faultId   = 0;
        QString subsystemName, moduleName, faultName;
        qint64 firstMs   = 0;
        qint64 lastMs    = 0;
        bool   active    = true;
    };

    static QString buildReportHtml(const QVector<Fault> &faults,
                                   const QString &title,
                                   qint64 nowMs);
    QVector<Fault> currentFaults() const;

private:
    struct KeyState {
        QHash<QString, Fault> faults;     // "sub:module:fault" -> Fault
        qint64 lastUpdateMs = 0;
    };

    void    buildUi();
    void    applyTheme();             // (re)apply palette-derived colors to all surfaces
    void    ingestFault(const CaptureLine &c, qint64 nowMs);
    void    rebuildTable();

    // Render the current fault picture as a self-contained HTML report.
    //
    // HTML rather than the CSV the exporters produce: the destination is an
    // incident record or a handover note, where the reader is a person, not
    // a script. It prints, saves and pastes into a ticket with its table
    // structure intact, and needs no external stylesheet to be readable
    // months later.
    //
    // Static and taking its data as arguments so it can be tested without a
    // window, a dispatcher or a live feed.

    static  QString durationText(qint64 ms);
    static  QString ageText(qint64 ms);

    MessageDispatcher        *m_dispatcher = nullptr;
    QHash<QString, KeyState>  m_keys;
    QString                   m_selectedKey;   // empty = all sources
    bool                      m_showCleared = false;
    bool                      m_dirty = false;

    QComboBox    *m_selector  = nullptr;
    QLabel       *m_summary   = nullptr;
    QLabel       *m_clock     = nullptr;
    QCheckBox    *m_clearedBox = nullptr;
    QTableWidget *m_table     = nullptr;
    QStackedWidget *m_stack   = nullptr;
    QLabel       *m_empty     = nullptr;
    QTimer       *m_refresh   = nullptr;
};

#endif // FAULTPANELWINDOW_H
