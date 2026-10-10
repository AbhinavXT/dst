#ifndef TAGBUILDERWINDOW_H
#define TAGBUILDERWINDOW_H

// =============================================================================
//  RFID Tag Builder (session 195) — Tools ▸ RFID Tag Builder… (Ctrl+Alt+B)
//  -----------------------------------------------------------------------------
//  tags_sim, in DLConsole (docs/RFID_TAG_BUILDER.md):
//
//    tag editor   one tag: its type, every field the schema gives that type
//                 (coded values by name), page_x / page_y. Edit a field and
//                 the page values follow, CRC-30 computed; paste page values
//                 and the fields follow, with the stored CRC checked.
//    route        the tags in the order the loco meets them, with the route's
//                 name and direction: add, insert, replace, delete, move.
//                 Open a DLConsole route, a tags_sim route.xml or a
//                 Configuration1.xml (one of its routes); save the route;
//                 export tags_sim's route.xml (the route and its REV twin).
//    (session 196) the route drawn by location above it (routestrip.h);
//                 a Signals tab (the signal at each foot tag); a Checks tab
//                 (rfidcheck.h), each finding a double-click from its row;
//                 Add duplicate (the selected main tag's duplicate, 4 m on
//                 along the direction); Shift (every location by N m, CRCs
//                 recomputed); Undo (Ctrl+Z) for every route edit.
//    (session 197) Export ▸ route.xml / into Configuration1.xml / text files
//                 (rfidexport.h); the "route.xml loc" column: where route.xml
//                 puts a tag after an adjustment tag, when not its own place.
//    (session 198) a Run tab: the route against a loco's log (planrun.h),
//                 tag by tag: read, read but different, not read, out of
//                 order, and tags read that the route does not hold; and
//                 "Make a route from this run". Double-click: show the read
//                 in the log.
//
//  Nothing here is sent anywhere: it makes files.
// =============================================================================

#include "planrun.h"
#include "rfidtag.h"

#include <QWidget>

class MessageDispatcher;
class QComboBox;
class QTabWidget;
class RouteStrip;
class UndoLog;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class StatusLine;

class TagBuilderWindow : public QWidget
{
    Q_OBJECT
public:
    // `dispatcher` (may be null): the loco logs the Run tab compares against.
    explicit TagBuilderWindow(MessageDispatcher *dispatcher = nullptr, QWidget *parent = nullptr);

    // The editor's tag (16 bytes) and setting it (fills the fields).
    QByteArray tag() const { return m_tag; }
    void setTag(const QByteArray &tag);
    void setPages(const QString &pageX, const QString &pageY);
    // Set one field of the editor's tag by schema name; false if the editor
    // has no such field. The tag is rebuilt, CRC included.
    bool setField(const QString &name, qint64 value);
    void setType(int type);

    const RfidTag::Route &route() const { return m_route; }
    void setRoute(const RfidTag::Route &r);
    bool loadFile(const QString &path, int routeIndex = -1);   // -1: ask when several
    bool saveFile(const QString &path);
    bool exportRouteXml(const QString &path);
    // Session 197: the route into an existing Configuration1.xml (`configPath`),
    // written to `outPath` (may be the same file); tags_sim's text files into
    // `folder`.
    bool exportIntoConfiguration(const QString &configPath, const QString &outPath);
    bool exportTextFiles(const QString &folder);
    bool isModified() const { return m_modified; }

    // Route edits, on the selected row (append when none is selected).
    void addTag();
    void insertTag();
    void replaceTag();
    void deleteTag();
    void moveTag(int delta);
    void addDuplicate();                  // of the selected main tag
    bool shiftAll(int metres);            // every tag's location; refused whole if one would not fit
    void setDirection(int dir);
    void addSignal();
    void deleteSignal();
    bool undo();
    // Run tab.
    void setRunSource(const QString &key);
    QString runSource() const { return m_runKey; }
    bool compareRun();                    // the route against the run's log
    bool makeRouteFromRun();              // replaces the route (asks first if changed)
    void selectRow(int row);

    QTableWidget *routeTable() const { return m_table; }
    QTableWidget *signalTable() const { return m_signals; }
    QTableWidget *checkTable() const { return m_checks; }
    QTabWidget *tabs() const { return m_tabs; }
    QTableWidget *runTable() const { return m_runTable; }
    QLabel *runSummary() const { return m_runSummary; }
    const PlanRun::Result &runResult() const { return m_runResult; }
    RouteStrip *strip() const { return m_strip; }
    UndoLog *undoLog() const { return m_undo; }
    QLabel *crcLabel() const { return m_crc; }
    QLineEdit *pageXEdit() const { return m_pageX; }
    QLineEdit *pageYEdit() const { return m_pageY; }
    StatusLine *status() const { return m_status; }

signals:
    void jumpRequested(const QString &key, qint64 ms);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void rebuildForm();
    void fillForm();
    void buildFromForm();
    void pagesEdited();
    void showTag();
    void fillTable();
    void fillSignals();
    void fillChecks();
    void refreshAll();
    // After an edit of m_route: one Undo step back to `before`, everything
    // redrawn, the route marked changed, `row` selected.
    void changed(const QString &label, const RfidTag::Route &before, int row);
    void setModified(bool on);
    void refreshRunPicker();
    int  currentRow() const;
    bool confirmDiscard();

    QByteArray m_tag;
    RfidTag::Route m_route;
    bool m_modified = false;
    bool m_updating = false;
    int  m_formType = -1;

    QComboBox *m_type = nullptr;
    QFormLayout *m_form = nullptr;
    QWidget *m_formHost = nullptr;
    QHash<QString, QWidget *> m_editors;   // field name -> QSpinBox / QComboBox
    QLineEdit *m_pageX = nullptr;
    QLineEdit *m_pageY = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_crc = nullptr;

    QLineEdit *m_routeName = nullptr;
    QComboBox *m_dir = nullptr;
    QTableWidget *m_table = nullptr;
    QTableWidget *m_signals = nullptr;
    QTableWidget *m_checks = nullptr;
    QTabWidget *m_tabs = nullptr;
    RouteStrip *m_strip = nullptr;
    UndoLog *m_undo = nullptr;
    MessageDispatcher *m_dispatcher = nullptr;
    QComboBox *m_runPicker = nullptr;
    QTableWidget *m_runTable = nullptr;
    QLabel *m_runSummary = nullptr;
    QString m_runKey;
    PlanRun::Result m_runResult;
    StatusLine *m_status = nullptr;
};

#endif  // TAGBUILDERWINDOW_H
