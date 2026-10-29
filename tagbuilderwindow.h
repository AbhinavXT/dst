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
//    (session 199) Fix CRCs: every tag of the route whose CRC-30 fails gets
//                 the one its contents give (only the CRC bits change); and
//                 the same over a whole Configuration1.xml / route.xml file.
//    (session 200) several rows can be selected: Set field… sets one field
//                 on all of them (CRCs recomputed, refused whole if any tag
//                 would not take it, one Undo step); Delete deletes them all.
//    (session 201) Library…: tags_sim's KAV_CONFIG routes, built in and in a
//                 folder (taglibrary.h); the route last opened is reopened
//                 when the builder opens from the menu.
//    (session 221) opened from Station Layout to edit or add one of its tags:
//                 an Apply button sends the editor's tag back.
//    (session 401) on the route drawing: double-click a tag to change it in a
//                 window of its own (tageditdialog.h); right-click for Edit
//                 tag…, Delete tag and New tag here… (the nearest main tag's
//                 type and TINs, an unused id, the clicked metre). Each is one
//                 Undo step.
//    (session 402) routeChanged() on every change, saved or not: the Loco
//                 Simulator's RFID tab can run the open route as it is.
//    (session 203) a Simulator tab: what the RFID simulator (LocoTcasSimulator)
//                 would send for this route, at a speed, from a start tag, tick
//                 by tick, on reader 1 and reader 2 (simpreview.h).
//
//  Nothing here is sent anywhere: it makes files.
// =============================================================================

#include "planrun.h"
#include "rfidtag.h"
#include "simpreview.h"

#include <QWidget>

class MessageDispatcher;
class QComboBox;
class QMenu;
class TagEditDialog;
class QTabWidget;
class RouteStrip;
class UndoLog;
class QFormLayout;
class QLabel;
class QLineEdit;
class QCheckBox;
class QDateTime;
class QDoubleSpinBox;
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
    // Session 259: just the route's two <route_data> blocks, to paste into a
    // Configuration1.xml by hand (RfidExport::configurationRoute).
    bool exportConfigurationRoute(const QString &path);
    // Session 197: the route into an existing Configuration1.xml (`configPath`),
    // written to `outPath` (may be the same file); tags_sim's text files into
    // `folder`.
    bool exportIntoConfiguration(const QString &configPath, const QString &outPath);
    bool exportTextFiles(const QString &folder);
    // Session 281: the selected tag as its route.xml <rfid_data> line.
    bool exportSelectedTag(const QString &path);
    // Session 282: Export ▸ To shared folder. The export, under its usual
    // file name, straight into the shared folder (Settings, set once with
    // Export ▸ Set shared folder…), to hand it to another PC. Refused, on
    // the status line, when the folder is not set or cannot be reached; an
    // existing file is overwritten only after asking (`ask` false: the tests).
    enum class SharedExport { RouteXml, ConfigurationRoute, SelectedTag, TextFiles, Everything };
    bool exportToShared(SharedExport kind, bool ask = true);
    // The file name(s) `kind` gets there (Everything: its folder's name).
    QStringList sharedFileNames(SharedExport kind) const;
    // Session 286: Export ▸ Everything, in a route folder. A folder named
    // after the route (bundleFolderName) made in `parentFolder`, holding the
    // .tagroute.xml, route.xml, the Configuration1.xml route, the three text
    // files and a README.txt saying what the route is and what was written.
    // A file that cannot be made (route.xml with no direction) is left out
    // and the README and status line say why. An existing folder's files
    // are replaced only after asking (`ask` false: the tests).
    bool exportEverything(const QString &parentFolder, bool ask = true);
    QString bundleFolderName() const;
    // The README.txt exportEverything writes, for `written` and `skipped`
    // ("route.xml: why"). `when` is the time it names.
    QString bundleReadme(const QStringList &written, const QStringList &skipped, const QDateTime &when) const;
    // Session 199. The route's failing CRCs; the number of tags fixed (-1 none to fix).
    int fixRouteCrcs();
    // A whole file's failing CRCs, written to `outPath`; *changes one line per tag.
    bool fixFileCrcs(const QString &inPath, const QString &outPath, QStringList *changes = nullptr);
    bool isModified() const { return m_modified; }
    // Session 201: open the route last opened (Settings), if it is still
    // there; false when there is none. MainWindow calls it on open.
    bool reopenLast();

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
    // Session 235: Scenario — one change on the selected tag's main /
    // duplicate pair (tagscenario.h), one Undo step; the selected rows'
    // order reversed; and the whole SIF section 14 set for the pair written
    // to `folder` as <clause>.tagroute.xml (+ <clause>_route.xml for tags_sim
    // when the route has a direction). The files written; *why when none.
    bool applyScenario(int kind, const QString &tag);
    bool jumbleSelected();
    QStringList makeSection14(const QString &folder, const QString &tag, QString *why = nullptr);
    // Run tab.
    void setRunSource(const QString &key);
    QString runSource() const { return m_runKey; }
    bool compareRun();                    // the route against the run's log
    bool makeRouteFromRun();              // replaces the route (asks first if changed)
    // Simulator tab (session 203): fills the tab and returns what it showed.
    SimPreview::Result previewSimulator(const SimPreview::Options &options);
    SimPreview::Options simulatorOptions() const;   // as the tab's controls are set
    // Session 205: the preview (as the controls are set) against the Run
    // tab's log: each send's read and lag. False with no log or no preview.
    bool previewAgainstRun(SimPreview::Against *out = nullptr);
    QTableWidget *simTable() const { return m_simTable; }
    QLabel *simSummary() const { return m_simSummary; }
    void selectRow(int row);
    // Session 401: from the route drawing. The window opens (not modal to the
    // app) and OK puts its tag in the route, one Undo step. Null when there
    // is no such row. newTagInDialog: a new main tag at `loc` metres, placed
    // in the route in the order the loco meets it.
    TagEditDialog *editRowInDialog(int row);
    TagEditDialog *newTagInDialog(qint64 loc);
    QMenu *stripMenu(int row, double loc);   // the right-click menu; the caller owns it
    // Session 200.
    QList<int> selectedRows() const;      // ascending
    void selectRows(const QList<int> &rows);
    // The fields every one of `rows` carries, in wire order.
    QVector<RfidTag::Field> commonFields(const QList<int> &rows) const;
    bool setFieldOn(const QList<int> &rows, const QString &field, qint64 value);

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
    // Session 221: a button under the tag editor that sends the editor's tag
    // back (applyRequested); hidden until a caller offers it (Station Layout).
    void offerApply(const QString &text, const QString &tip);
    QPushButton *applyButton() const { return m_apply; }
    QLineEdit *pageXEdit() const { return m_pageX; }
    QLineEdit *pageYEdit() const { return m_pageY; }
    StatusLine *status() const { return m_status; }

    // Session 409: closed (deleted on close, so gone soon). A minimised builder
    // is hidden in the main window's dock but still open.
    bool isClosed() const { return m_closed; }

signals:
    void jumpRequested(const QString &key, qint64 ms);
    // Session 402: the route changed (opened, edited, undone, renamed), for
    // the Loco Simulator's RFID tab, which runs the route as it is now.
    void routeChanged();
    void applyRequested(const QByteArray &tag);

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
    void fillSimStart();
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
    QPushButton *m_apply = nullptr;

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
    QDoubleSpinBox *m_simSpeed = nullptr;
    QComboBox *m_simStart = nullptr;
    QCheckBox *m_simReader1 = nullptr;
    QCheckBox *m_simReader2 = nullptr;
    QLineEdit *m_simMissing = nullptr;
    QTableWidget *m_simTable = nullptr;
    QLabel *m_simSummary = nullptr;
    StatusLine *m_status = nullptr;
    bool m_closed = false;
};

#endif  // TAGBUILDERWINDOW_H
