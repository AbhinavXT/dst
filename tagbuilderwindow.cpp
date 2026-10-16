#include "tagbuilderwindow.h"
#include "fielddocs.h"
#include "tagscenario.h"

#include <algorithm>

#include "rfidcheck.h"
#include "rfidexport.h"
#include "settings.h"
#include "taglibrary.h"
#include "messagedispatcher.h"
#include "routestrip.h"
#include "statusline.h"
#include "undolog.h"
#include "uicolors.h"
#include "windowgeometry.h"
#include "recentfiles.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QDateTime>
#include <QDir>
#include <QDialogButtonBox>
#include <QDialog>
#include <QMenu>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSpinBox>
#include <QPointer>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
// The hex digits of a page box, spaces dropped (session 265).
QString hexDigits(const QString &s)
{
    QString d = s;
    d.remove(QLatin1Char(' '));
    return d;
}
}  // namespace

namespace {

const int kTypes[] = { 9, 10, 11, 12 };

QString hex8(quint32 v) { return QStringLiteral("%1").arg(v, 8, 16, QLatin1Char('0')).toUpper(); }

// Session 304: Annexure D's description of a tag field, after any tooltip
// the widget already had (that one is plain text; the description is HTML).
QString withTagDoc(const QString &field, int tagType, const QString &existing = QString())
{
    const QString doc = FieldDocs::instance().tooltip(QStringLiteral("rfid"), field, tagType);
    if (doc.isEmpty()) return existing;
    if (existing.isEmpty()) return doc;
    return existing.toHtmlEscaped() + QStringLiteral("<br><br>") + doc;
}

QTableWidgetItem *cell(const QString &text)
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return it;
}

// A starting tag of each type: a Normal tag is the common case, so the
// editor opens on one with nothing set but its type and version.
QByteArray blankTag(int type)
{
    return RfidTag::build({ { QStringLiteral("type"), type }, { QStringLiteral("version"), 1 } });
}

}  // namespace

TagBuilderWindow::TagBuilderWindow(MessageDispatcher *dispatcher, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_dispatcher(dispatcher)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("RFID Tag Builder"));
    WindowGeometry::makeResizableWindow(this);
    resize(1180, 680);

    // ---- route bar ---------------------------------------------------------------------------
    m_routeName = new QLineEdit(this);
    m_routeName->setPlaceholderText(tr("route name"));
    m_dir = new QComboBox(this);
    m_dir->addItem(tr("Direction not set"), RfidTag::DirUnset);
    m_dir->addItem(tr("Nominal (dir 1, DN)"), RfidTag::DirNominal);
    m_dir->addItem(tr("Reverse (dir 2, UP)"), RfidTag::DirReverse);
    m_dir->setToolTip(tr("The direction the loco runs this route in. The route.xml export needs it: it names "
                         "the route DN_ or UP_ and writes the REV route the other way"));
    auto *libBtn = new QPushButton(tr("Library…"), this);
    libBtn->setToolTip(tr("tags_sim's KAV_CONFIG routes, ready to open: built into DLConsole, and in the library "
                          "folder (%1)").arg(QDir::toNativeSeparators(Settings::tagLibraryPath())));
    auto *newBtn = new QPushButton(tr("New"), this);
    auto *openBtn = new QPushButton(tr("Open…"), this);
    openBtn->setToolTip(tr("A DLConsole route (.tagroute.xml), a tags_sim route.xml, or a Configuration1.xml "
                           "(pick one of its routes). tags_sim .xlsx routes: convert them with "
                           "scripts/tags_sim_import.py"));
    auto *saveBtn = new QPushButton(tr("Save…"), this);
    saveBtn->setToolTip(tr("The route as a DLConsole route file: tags, signals, name and direction"));
    auto *exportBtn = new QPushButton(tr("Export"), this);
    auto *exportMenu = new QMenu(exportBtn);
    QAction *actRouteXml = exportMenu->addAction(tr("route.xml…"));
    actRouteXml->setToolTip(tr("tags_sim's route.xml: the route and its REV route"));
    QAction *actConfig = exportMenu->addAction(tr("Into Configuration1.xml…"));
    actConfig->setToolTip(tr("Put the route and its REV route into an existing Configuration1.xml, replacing the "
                             "routes of the same name; the rest of the file is left as it was"));
    QAction *actRouteOnly = exportMenu->addAction(tr("Route for Configuration1.xml…"));
    actRouteOnly->setToolTip(tr("Just the route and its REV route, as the two <route_data> blocks a "
                                "Configuration1.xml holds: no other file needed, to paste inside its <route>"));
    QAction *actTag = exportMenu->addAction(tr("Selected tag as <rfid_data>…"));
    actTag->setToolTip(tr("Just the selected tag, as the one <rfid_data> line route.xml writes for it "
                          "(locations from its place in the route), to paste into a <route_data>"));
    QAction *actText = exportMenu->addAction(tr("Text files (rfid, sigID, tag_link_info)…"));
    // Session 286: all of the above at once, in a folder named after the route.
    QAction *actAll = exportMenu->addAction(tr("Everything, in a route folder…"));
    actAll->setToolTip(tr("A folder named after the route holding the .tagroute.xml, route.xml, the "
                          "Configuration1.xml route, the three text files and a README.txt: what the route is, "
                          "its CRCs and checks, and what was written"));
    // Session 282: the same exports straight into a shared folder, for
    // another PC; no save dialog.
    exportMenu->addSeparator();
    QMenu *sharedMenu = exportMenu->addMenu(tr("To shared folder"));
    sharedMenu->setToolTipsVisible(true);
    const QVector<QPair<QString, SharedExport>> sharedItems {
        { tr("route.xml"),                        SharedExport::RouteXml },
        { tr("Route for Configuration1.xml"),     SharedExport::ConfigurationRoute },
        { tr("Selected tag as <rfid_data>"),      SharedExport::SelectedTag },
        { tr("Text files (rfid, sigID, tag_link_info)"), SharedExport::TextFiles },
        { tr("Everything, in a route folder"),    SharedExport::Everything },
    };
    for (const auto &item : sharedItems) {
        const SharedExport kind = item.second;
        connect(sharedMenu->addAction(item.first), &QAction::triggered, this, [this, kind]() { exportToShared(kind); });
    }
    QAction *actShared = exportMenu->addAction(tr("Set shared folder…"));
    actShared->setToolTip(tr("A folder another PC can read, e.g. \\\\PC2\\share: To shared folder writes there"));
    connect(exportMenu, &QMenu::aboutToShow, this, [sharedMenu]() {
        const QString folder = Settings::tagSharedFolder();
        sharedMenu->setTitle(folder.isEmpty() ? tr("To shared folder (not set)")
                                              : tr("To shared folder: %1").arg(QDir::toNativeSeparators(folder)));
        sharedMenu->setEnabled(!folder.isEmpty());
    });
    connect(actShared, &QAction::triggered, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Shared folder for exports (another PC reads it)"),
                                                              Settings::tagSharedFolder());
        if (dir.isEmpty()) return;
        Settings::setTagSharedFolder(dir);
        m_status->ok(tr("Exports to the shared folder go to %1").arg(QDir::toNativeSeparators(dir)));
    });
    exportMenu->addSeparator();
    QAction *actFixFile = exportMenu->addAction(tr("Fix the CRCs in a Configuration1.xml or route.xml…"));
    actFixFile->setToolTip(tr("Every tag in the file whose CRC-30 fails gets the CRC its contents give (page_y only, "
                              "as tags_sim's corrector does); the rest of the file is left as it was"));
    exportMenu->setToolTipsVisible(true);
    exportBtn->setMenu(exportMenu);
    exportBtn->setToolTip(tr("route.xml, a route for (or into) a Configuration1.xml, or tags_sim's text files"));
    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Route:"), this));
    bar->addWidget(m_routeName, 1);
    bar->addWidget(m_dir);
    bar->addSpacing(12);
    bar->addWidget(libBtn);
    bar->addWidget(newBtn);
    bar->addWidget(openBtn);
    bar->addWidget(saveBtn);
    bar->addWidget(exportBtn);

    // ---- route table -------------------------------------------------------------------------
    m_table = new QTableWidget(this);
    m_table->setColumnCount(10);
    m_table->setHorizontalHeaderLabels({ tr("#"), tr("Tag"), tr("CRC-30"), tr("Type"), tr("Abs loc (m)"),
                                         tr("route.xml loc"), tr("Δ (m)"), tr("TIN nom / rev"), tr("Placement"),
                                         tr("page_x  page_y") });
    m_table->horizontalHeaderItem(5)->setToolTip(tr("Where route.xml puts the tag, when not at its own location: after "
                                                    "an adjustment tag, tags_sim writes locations back in the numbering "
                                                    "before it, so the simulator sees one continuous line"));
    m_table->horizontalHeaderItem(6)->setToolTip(tr("Metres from the tag before, along the direction (+ = further on). "
                                                    "Blank after an adjustment tag, whose numbering may change"));
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setToolTip(tr("The tags in the order the loco meets them. Select one to edit it on the right"));

    auto *addBtn = new QPushButton(tr("Add"), this);
    addBtn->setToolTip(tr("The editor's tag, after the selected row (at the end when none is selected)"));
    auto *insBtn = new QPushButton(tr("Insert above"), this);
    auto *repBtn = new QPushButton(tr("Replace"), this);
    repBtn->setToolTip(tr("The selected row becomes the editor's tag"));
    auto *delBtn = new QPushButton(tr("Delete"), this);
    auto *upBtn = new QPushButton(tr("Up"), this);
    auto *downBtn = new QPushButton(tr("Down"), this);
    auto *dupBtn = new QToolButton(this);
    dupBtn->setText(tr("Add duplicate"));
    dupBtn->setToolTip(tr("The selected main tag's duplicate tag, %1 m further along the direction, right after it")
                           .arg(RfidCheck::kDuplicateGap));
    auto *setBtn = new QPushButton(tr("Set field…"), this);
    setBtn->setToolTip(tr("Set one field on every selected tag (Shift- or Ctrl-click to select several), "
                          "CRCs recomputed"));
    auto *fixBtn = new QPushButton(tr("Fix CRCs"), this);
    fixBtn->setToolTip(tr("Every tag whose CRC-30 fails gets the CRC its contents give. Only the CRC bits change"));
    auto *shiftBtn = new QPushButton(tr("Shift…"), this);
    shiftBtn->setToolTip(tr("Move every tag's location by the same number of metres (CRCs recomputed). "
                            "Adjustment tags: location-1 only, as tags_sim shifts them"));
    m_undo = new UndoLog(this);
    QAction *undoAct = m_undo->createAction(this);
    addAction(undoAct);
    // Not setDefaultAction(): the action's text grows with what it would undo
    // ("Undo shift all tags -1000 m"), which widened the window past a laptop
    // on Linux. The button stays "Undo"; the step is in its tooltip.
    auto *undoBtn = new QToolButton(this);
    undoBtn->setText(tr("Undo"));
    connect(undoBtn, &QToolButton::clicked, undoAct, &QAction::trigger);
    auto syncUndo = [undoBtn, undoAct]() {
        undoBtn->setEnabled(undoAct->isEnabled());
        undoBtn->setToolTip(undoAct->text().remove(QLatin1Char('&')));
    };
    connect(undoAct, &QAction::changed, undoBtn, syncUndo);
    syncUndo();
    auto *rowBtns = new QHBoxLayout;
    for (QPushButton *b : { addBtn, insBtn, repBtn, delBtn, upBtn, downBtn }) rowBtns->addWidget(b);
    rowBtns->addStretch(1);
    auto *rowBtns2 = new QHBoxLayout;
    rowBtns2->addWidget(setBtn);
    rowBtns2->addWidget(dupBtn);   // its arrow: the session 235 scenarios
    rowBtns2->addWidget(shiftBtn);
    rowBtns2->addWidget(fixBtn);
    // Session 235: the SIF's tag scenarios on the selected main / duplicate pair.
    // On Add duplicate's arrow rather than a button of its own: they act on the
    // same pair, and another button widened the window past a laptop on Linux.
    auto *scenMenu = new QMenu(dupBtn);
    QAction *addDupAct = scenMenu->addAction(tr("Add duplicate"));
    connect(addDupAct, &QAction::triggered, this, &TagBuilderWindow::addDuplicate);
    scenMenu->addSection(tr("Scenario (SIF section 14) on the selected pair"));
    for (int k = TagScenario::DuplicateMissing; k <= TagScenario::GapOver4; ++k) {
        QAction *a = scenMenu->addAction(TagScenario::describe(TagScenario::Kind(k)).left(1).toUpper()
                                         + TagScenario::describe(TagScenario::Kind(k)).mid(1));
        connect(a, &QAction::triggered, this, [this, k]() {
            const int row = currentRow();
            if (row < 0) { m_status->warn(tr("Select a main tag or its duplicate first")); return; }
            applyScenario(k, m_route.tags.at(row).name);
        });
    }
    scenMenu->addSeparator();
    QAction *jumbleAct = scenMenu->addAction(tr("Jumble the selected tags (read in reverse order)"));
    connect(jumbleAct, &QAction::triggered, this, [this]() { jumbleSelected(); });
    scenMenu->addSeparator();
    QAction *setAct = scenMenu->addAction(tr("Make the section 14 set for this tag\u2026"));
    setAct->setToolTip(tr("14.1-14.9 (duplicate in the nominal direction) or 14.11-14.19 (reverse): one route file "
                          "per clause, in a folder you choose"));
    connect(setAct, &QAction::triggered, this, [this]() {
        const int row = currentRow();
        if (row < 0) { m_status->warn(tr("Select a main tag or its duplicate first")); return; }
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Folder for the section 14 routes"));
        if (!folder.isEmpty()) makeSection14(folder, m_route.tags.at(row).name);
    });
    scenMenu->setToolTipsVisible(true);
    dupBtn->setMenu(scenMenu);
    dupBtn->setPopupMode(QToolButton::MenuButtonPopup);
    dupBtn->setToolTip(dupBtn->toolTip() + tr(". Its arrow: the SIF section 14 scenarios on the selected tag and its "
                                              "duplicate (missing, interchanged, same location, under / over 4 m, "
                                              "jumbled), one at a time or the whole set as files named by clause"));
    rowBtns2->addStretch(1);
    rowBtns2->addWidget(undoBtn);

    auto *tagsPage = new QWidget(this);
    auto *tagsLayout = new QVBoxLayout(tagsPage);
    tagsLayout->setContentsMargins(0, 0, 0, 0);
    tagsLayout->addWidget(m_table, 1);
    tagsLayout->addLayout(rowBtns);
    tagsLayout->addLayout(rowBtns2);

    // ---- signals -----------------------------------------------------------------------------
    m_signals = new QTableWidget(this);
    m_signals->setColumnCount(3);
    m_signals->setHorizontalHeaderLabels({ tr("Foot tag"), tr("Signal"), tr("Signal id") });
    m_signals->verticalHeader()->hide();
    m_signals->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_signals->setSelectionMode(QAbstractItemView::SingleSelection);
    m_signals->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_signals->setToolTip(tr("tags_sim's signals sheet: the signal at each signal-foot tag. Double-click to edit"));
    auto *addSig = new QPushButton(tr("Add signal"), this);
    addSig->setToolTip(tr("A signal at the selected tag of the Tags tab"));
    auto *delSig = new QPushButton(tr("Delete signal"), this);
    auto *sigBtns = new QHBoxLayout;
    sigBtns->addWidget(addSig);
    sigBtns->addWidget(delSig);
    sigBtns->addStretch(1);
    auto *sigPage = new QWidget(this);
    auto *sigLayout = new QVBoxLayout(sigPage);
    sigLayout->setContentsMargins(0, 0, 0, 0);
    sigLayout->addWidget(m_signals, 1);
    sigLayout->addLayout(sigBtns);

    // ---- checks ------------------------------------------------------------------------------
    m_checks = new QTableWidget(this);
    m_checks->setColumnCount(3);
    m_checks->setHorizontalHeaderLabels({ tr("#"), tr("Tag"), tr("Finding") });
    m_checks->verticalHeader()->hide();
    m_checks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_checks->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_checks->setWordWrap(true);
    // Rows follow their wrapped text as the column width changes (a one-off
    // resizeRowsToContents() measured while the tab was hidden made them huge).
    m_checks->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_checks->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_checks->horizontalHeader()->setStretchLastSection(true);
    m_checks->setToolTip(tr("What the route's tags say about themselves and each other. Observed, not judged. "
                            "Double-click: select the tag"));

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(tagsPage, tr("Tags"));
    m_tabs->addTab(sigPage, tr("Signals"));
    m_tabs->addTab(m_checks, tr("Checks"));

    // ---- run ---------------------------------------------------------------------------------
    m_runPicker = new QComboBox(this);
    m_runPicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_runPicker->setMinimumContentsLength(8);       // the whole name is in the list; Linux fonts made 14 too wide
    m_runPicker->setToolTip(tr("The loco log (tab) to compare the route with: its @rfid frames"));
    auto *compareBtn = new QPushButton(tr("Compare"), this);
    compareBtn->setToolTip(tr("Each tag of the route against what the loco read: first read only"));
    auto *fromRunBtn = new QPushButton(tr("Route from run"), this);
    fromRunBtn->setToolTip(tr("A new route of the tags the loco read, in the order first read, exactly as read"));
    m_runSummary = new QLabel(this);
    m_runSummary->setWordWrap(true);
    m_runTable = new QTableWidget(this);
    m_runTable->setColumnCount(5);
    m_runTable->setHorizontalHeaderLabels({ tr("#"), tr("Tag"), tr("Result"), tr("First read"), tr("Detail") });
    m_runTable->verticalHeader()->hide();
    m_runTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_runTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_runTable->setWordWrap(true);
    m_runTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_runTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_runTable->horizontalHeader()->setStretchLastSection(true);
    m_runTable->setToolTip(tr("Double-click: show the read in the log"));
    auto *runBar = new QHBoxLayout;
    runBar->addWidget(new QLabel(tr("Loco log:"), this));
    runBar->addWidget(m_runPicker, 1);
    runBar->addWidget(compareBtn);
    runBar->addWidget(fromRunBtn);
    auto *runPage = new QWidget(this);
    auto *runLayout = new QVBoxLayout(runPage);
    runLayout->setContentsMargins(0, 0, 0, 0);
    runLayout->addLayout(runBar);
    runLayout->addWidget(m_runSummary);
    runLayout->addWidget(m_runTable, 1);
    m_tabs->addTab(runPage, tr("Run"));

    // ---- simulator preview -------------------------------------------------------------------
    m_simSpeed = new QDoubleSpinBox(this);
    m_simSpeed->setRange(1, 250);
    m_simSpeed->setDecimals(0);
    m_simSpeed->setValue(60);
    m_simSpeed->setSuffix(tr(" km/h"));
    m_simSpeed->setToolTip(tr("A constant speed, as set on the simulator's speed field"));
    m_simStart = new QComboBox(this);
    m_simStart->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_simStart->setMinimumContentsLength(8);
    m_simStart->setToolTip(tr("The simulator's start tag: it starts at that row's abs_loc"));
    m_simReader1 = new QCheckBox(tr("Reader 1"), this);
    m_simReader2 = new QCheckBox(tr("Reader 2"), this);
    m_simReader1->setChecked(true);
    m_simReader2->setChecked(true);
    m_simMissing = new QLineEdit(this);
    m_simMissing->setPlaceholderText(tr("missing tags, e.g. 904,906D"));
    m_simMissing->setToolTip(tr("The simulator's \"missing tag ids\": tags it does not send (the row before stays current)"));
    auto *simBtn = new QPushButton(tr("Preview"), this);
    simBtn->setToolTip(tr("What the RFID simulator would send for this route, tick by tick (100 ms), on each reader"));
    auto *simRunBtn = new QPushButton(tr("Against the run"), this);
    simRunBtn->setToolTip(tr("The preview against the loco log picked on the Run tab: when each send was read "
                             "on its reader, and how far its time is from the preview's"));
    m_simSummary = new QLabel(this);
    m_simSummary->setWordWrap(true);
    m_simTable = new QTableWidget(this);
    m_simTable->setColumnCount(8);
    m_simTable->setHorizontalHeaderLabels({ tr("Time (s)"), tr("At (m)"), tr("Reader"), tr("Tag"), tr("Tag's location"), tr("Row"),
                                            tr("Read at"), tr("Lag (s)") });
    m_simTable->verticalHeader()->hide();
    m_simTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_simTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_simTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_simTable->horizontalHeader()->setStretchLastSection(true);
    m_simTable->setToolTip(tr("Each tag the simulator would send: when, where the simulated loco was, on which reader. "
                              "Double-click: select the tag"));
    auto *simBar = new QHBoxLayout;
    simBar->addWidget(m_simSpeed);
    simBar->addWidget(new QLabel(tr("from"), this));
    simBar->addWidget(m_simStart, 1);
    simBar->addWidget(m_simReader1);
    simBar->addWidget(m_simReader2);
    auto *simPage = new QWidget(this);
    auto *simLayout = new QVBoxLayout(simPage);
    simLayout->setContentsMargins(0, 0, 0, 0);
    simLayout->addLayout(simBar);
    auto *simBar2 = new QHBoxLayout;
    simBar2->addWidget(m_simMissing, 1);
    simBar2->addWidget(simBtn);              // on this row, not the first: Linux fonts made that one too wide
    simBar2->addWidget(simRunBtn);
    simLayout->addLayout(simBar2);
    simLayout->addWidget(m_simSummary);
    simLayout->addWidget(m_simTable, 1);
    m_tabs->addTab(simPage, tr("Simulator"));
    refreshRunPicker();
    m_strip = new RouteStrip(this);

    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_strip);
    leftLayout->addWidget(m_tabs, 1);

    // ---- tag editor --------------------------------------------------------------------------
    auto *editor = new QGroupBox(tr("Tag"), this);
    m_type = new QComboBox(editor);
    for (int t : kTypes) m_type->addItem(QStringLiteral("%1  %2").arg(t).arg(RfidTag::typeName(t)), t);
    m_type->setToolTip(tr("Tag type 10 (LC gate) has never been seen in a capture: its layout is the "
                          "specification's, not checked against a real tag"));
    // Session 265: room to type past 16 while editing (insert, then delete
    // elsewhere) and spaces (a tag pasted as "08 2a 2a ..."). The tag is
    // read from a box only once it holds exactly 16 hex digits.
    const QRegularExpression hex(QStringLiteral("[0-9A-Fa-f ]{0,48}"));
    m_pageX = new QLineEdit(editor);
    m_pageY = new QLineEdit(editor);
    for (QLineEdit *e : { m_pageX, m_pageY }) {
        e->setValidator(new QRegularExpressionValidator(hex, e));
        e->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        e->setToolTip(tr("16 hex digits, as tags_sim and the simulator write them. Paste a tag here to decode it. "
                         "Edit freely: the tag changes once the box holds 16 digits again"));
    }
    m_name = new QLabel(editor);
    m_crc = new QLabel(editor);
    m_crc->setWordWrap(true);
    auto *fixCrc = new QPushButton(tr("Recompute CRC"), editor);
    fixCrc->setToolTip(tr("Rebuild the tag from its fields, with the CRC-30 its contents give"));

    m_formHost = new QWidget(editor);
    m_form = new QFormLayout(m_formHost);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *scroll = new QScrollArea(editor);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_formHost);

    auto *pages = new QFormLayout;
    pages->addRow(tr("Type:"), m_type);
    pages->addRow(tr("page_x:"), m_pageX);
    pages->addRow(tr("page_y:"), m_pageY);
    auto *nameRow = new QHBoxLayout;
    nameRow->addWidget(m_name, 1);
    nameRow->addWidget(fixCrc);
    auto *editorLayout = new QVBoxLayout(editor);
    editorLayout->addLayout(pages);
    editorLayout->addLayout(nameRow);
    editorLayout->addWidget(m_crc);
    editorLayout->addWidget(scroll, 1);
    m_apply = new QPushButton(editor);
    m_apply->hide();
    editorLayout->addWidget(m_apply);
    connect(m_apply, &QPushButton::clicked, this, [this]() { emit applyRequested(m_tag); });

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(left);
    split->addWidget(editor);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setSizes({ 600, 480 });
    split->setChildrenCollapsible(false);

    m_status = new StatusLine;
    auto *root = new QVBoxLayout(this);
    root->addLayout(bar);
    root->addWidget(split, 1);
    root->addWidget(m_status);

    // ---- wiring ------------------------------------------------------------------------------
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_updating) setType(m_type->currentData().toInt());
    });
    connect(m_pageX, &QLineEdit::textEdited, this, [this]() { pagesEdited(); });
    connect(m_pageY, &QLineEdit::textEdited, this, [this]() { pagesEdited(); });
    connect(fixCrc, &QPushButton::clicked, this, [this]() { buildFromForm(); });
    connect(m_routeName, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_route.name = t;
        setModified(true);
    });
    connect(m_dir, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_updating) setDirection(m_dir->currentData().toInt());
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = currentRow();
        m_strip->setSelectedRow(row);
        if (row >= 0 && row < m_route.tags.size()) setTag(m_route.tags.at(row).bytes);
    });
    connect(m_strip, &RouteStrip::rowClicked, this, [this](int row) {
        m_tabs->setCurrentIndex(0);
        selectRow(row);
    });
    connect(m_checks, &QTableWidget::cellDoubleClicked, this, [this](int r) {
        const int row = m_checks->item(r, 0) ? m_checks->item(r, 0)->data(Qt::UserRole).toInt() : -1;
        if (row < 0) return;
        m_tabs->setCurrentIndex(0);
        selectRow(row);
    });
    connect(m_signals, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_updating || it->row() >= m_route.signalList.size()) return;
        const RfidTag::Route before = m_route;
        RfidTag::Signal &sg = m_route.signalList[it->row()];
        const QString text = it->text().trimmed();
        if (it->column() == 0) sg.footTag = text;
        else if (it->column() == 1) sg.name = text;
        else sg.sigId = text;
        changed(tr("Edit signal"), before, currentRow());
    });
    connect(addSig, &QPushButton::clicked, this, &TagBuilderWindow::addSignal);
    connect(delSig, &QPushButton::clicked, this, &TagBuilderWindow::deleteSignal);
    connect(dupBtn, &QToolButton::clicked, this, &TagBuilderWindow::addDuplicate);
    connect(shiftBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const int m = QInputDialog::getInt(this, tr("Shift every location"),
                                           tr("Metres to add to every tag's location (negative to subtract):"),
                                           0, -8388607, 8388607, 1, &ok);
        if (ok && m != 0) shiftAll(m);
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == 3) refreshRunPicker();
    });
    connect(m_runPicker, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_runKey = m_runPicker->currentData().toString();
    });
    connect(compareBtn, &QPushButton::clicked, this, &TagBuilderWindow::compareRun);
    connect(simBtn, &QPushButton::clicked, this, [this]() { previewSimulator(simulatorOptions()); });
    connect(simRunBtn, &QPushButton::clicked, this, [this]() { previewAgainstRun(); });
    connect(m_simTable, &QTableWidget::cellDoubleClicked, this, [this](int r) {
        const int row = m_simTable->item(r, 5) ? m_simTable->item(r, 5)->data(Qt::UserRole).toInt() : -1;
        if (row < 0) return;
        m_tabs->setCurrentIndex(0);
        selectRow(row);
    });
    connect(fromRunBtn, &QPushButton::clicked, this, &TagBuilderWindow::makeRouteFromRun);
    connect(m_runTable, &QTableWidget::cellDoubleClicked, this, [this](int r) {
        const qint64 ms = m_runTable->item(r, 3) ? m_runTable->item(r, 3)->data(Qt::UserRole).toLongLong() : 0;
        if (ms > 0) emit jumpRequested(m_runKey, ms);
    });
    connect(m_undo, &UndoLog::undone, this, [this](const QString &label, bool restored) {
        if (restored) m_status->ok(tr("Undone: %1").arg(label));
        else m_status->warn(tr("Could not undo %1").arg(label));
    });
    connect(addBtn, &QPushButton::clicked, this, &TagBuilderWindow::addTag);
    connect(insBtn, &QPushButton::clicked, this, &TagBuilderWindow::insertTag);
    connect(repBtn, &QPushButton::clicked, this, &TagBuilderWindow::replaceTag);
    connect(delBtn, &QPushButton::clicked, this, &TagBuilderWindow::deleteTag);
    connect(upBtn, &QPushButton::clicked, this, [this]() { moveTag(-1); });
    connect(downBtn, &QPushButton::clicked, this, [this]() { moveTag(+1); });
    connect(libBtn, &QPushButton::clicked, this, [this]() {
        if (!confirmDiscard()) return;
        TagLibraryDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) loadFile(dlg.chosen().file, dlg.chosen().route);
    });
    connect(newBtn, &QPushButton::clicked, this, [this]() {
        if (!confirmDiscard()) return;
        setRoute(RfidTag::Route());
    });
    connect(openBtn, &QPushButton::clicked, this, [this]() {
        if (!confirmDiscard()) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Open a tag route"), QString(),
                                                          tr("Tag routes (*.xml);;All files (*)"));
        if (!path.isEmpty()) loadFile(path);
    });
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        const QString name = m_route.name.isEmpty() ? QStringLiteral("route") : m_route.name;
        const QString path = QFileDialog::getSaveFileName(this, tr("Save the tag route"), name + QStringLiteral(".tagroute.xml"),
                                                          tr("DLConsole tag routes (*.tagroute.xml)"));
        if (!path.isEmpty()) saveFile(path);
    });
    connect(actRouteXml, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export tags_sim route.xml"),
                                                          QStringLiteral("route.xml"), tr("XML (*.xml)"));
        if (!path.isEmpty()) exportRouteXml(path);
    });
    connect(actRouteOnly, &QAction::triggered, this, [this]() {
        const QString name = m_route.name.isEmpty() ? QStringLiteral("route") : m_route.name;
        const QString path = QFileDialog::getSaveFileName(this, tr("Export the route for Configuration1.xml"),
                                                          name + QStringLiteral("_configuration_route.xml"), tr("XML (*.xml)"));
        if (!path.isEmpty()) exportConfigurationRoute(path);
    });
    connect(actTag, &QAction::triggered, this, [this]() {
        const int row = currentRow();
        if (row < 0 || row >= m_route.tags.size()) {
            m_status->warn(tr("Select the tag to export"));
            return;
        }
        const QString name = RfidTag::nameOf(m_route.tags.at(row).bytes);
        const QString path = QFileDialog::getSaveFileName(this, tr("Export one tag as <rfid_data>"),
                                                          name + QStringLiteral("_rfid_data.xml"), tr("XML (*.xml)"));
        if (!path.isEmpty()) exportSelectedTag(path);
    });
    connect(actConfig, &QAction::triggered, this, [this]() {
        const QString in = QFileDialog::getOpenFileName(this, tr("The Configuration1.xml to put the route into"),
                                                        QString(), tr("XML (*.xml)"));
        if (in.isEmpty()) return;
        const QString out = QFileDialog::getSaveFileName(this, tr("Write the merged Configuration1.xml as"), in,
                                                         tr("XML (*.xml)"));
        if (!out.isEmpty()) exportIntoConfiguration(in, out);
    });
    connect(fixBtn, &QPushButton::clicked, this, &TagBuilderWindow::fixRouteCrcs);
    connect(setBtn, &QPushButton::clicked, this, [this]() {
        const QList<int> rows = selectedRows();
        const QVector<RfidTag::Field> fields = commonFields(rows);
        if (rows.isEmpty() || fields.isEmpty()) {
            m_status->warn(rows.isEmpty() ? tr("Select the tags to set a field on")
                                          : tr("The selected tags have no field in common"));
            return;
        }
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Set a field on %1 tag%2").arg(rows.size()).arg(rows.size() == 1 ? "" : "s"));
        auto *field = new QComboBox(&dlg);
        // Session 304: each field's Annexure D description, for the first selected tag's type.
        const int firstType = RfidTag::summary(m_route.tags.at(rows.first()).bytes).type;
        for (const RfidTag::Field &f : fields) {
            field->addItem(QStringLiteral("%1 (%2 bit%3)").arg(f.name).arg(f.bits).arg(f.bits == 1 ? "" : "s"), f.name);
            field->setItemData(field->count() - 1, withTagDoc(f.name, firstType), Qt::ToolTipRole);
        }
        auto *coded = new QComboBox(&dlg);
        auto *number = new QSpinBox(&dlg);
        auto *form = new QFormLayout;
        form->addRow(tr("Field:"), field);
        auto *value = new QHBoxLayout;
        value->addWidget(coded, 1);
        value->addWidget(number, 1);
        form->addRow(tr("Value:"), value);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        auto *lay = new QVBoxLayout(&dlg);
        lay->addLayout(form);
        lay->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        // The value editor follows the field: a named list for a short coded
        // field, a number otherwise; it starts at the first selected tag's value.
        auto showField = [&]() {
            const RfidTag::Field &f = fields.at(field->currentIndex());
            field->setToolTip(withTagDoc(f.name, firstType));
            const qint64 now = RfidTag::values(m_route.tags.at(rows.first()).bytes).value(f.name);
            const bool isCoded = !f.enumName.isEmpty() && f.bits <= 4;
            coded->clear();
            if (isCoded)
                for (qint64 v = 0; v < (qint64(1) << f.bits); ++v) coded->addItem(RfidTag::enumLabel(f.enumName, v), v);
            coded->setCurrentIndex(coded->findData(now));
            number->setRange(0, int(qMin<qint64>((qint64(1) << f.bits) - 1, 0x7FFFFFFF)));
            number->setValue(int(now));
            coded->setVisible(isCoded);
            number->setVisible(!isCoded);
        };
        connect(field, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, showField);
        showField();
        if (dlg.exec() != QDialog::Accepted) return;
        const RfidTag::Field &f = fields.at(field->currentIndex());
        const bool isCoded = !f.enumName.isEmpty() && f.bits <= 4;
        setFieldOn(rows, f.name, isCoded ? coded->currentData().toLongLong() : number->value());
    });
    connect(actFixFile, &QAction::triggered, this, [this]() {
        const QString in = QFileDialog::getOpenFileName(this, tr("The file whose CRCs to fix"), QString(), tr("XML (*.xml)"));
        if (in.isEmpty()) return;
        const QString out = QFileDialog::getSaveFileName(this, tr("Write the corrected file as"), in, tr("XML (*.xml)"));
        if (out.isEmpty()) return;
        QStringList changes;
        if (!fixFileCrcs(in, out, &changes) || changes.isEmpty()) return;
        QMessageBox box(QMessageBox::Information, tr("CRCs fixed"),
                        tr("%1 tags in %2 had a CRC-30 that fails. Each now has the CRC its contents give; "
                           "nothing else in the file changed.").arg(changes.size()).arg(QFileInfo(in).fileName()),
                        QMessageBox::Ok, this);
        box.setDetailedText(changes.join(QLatin1Char('\n')));
        box.exec();
    });
    connect(actAll, &QAction::triggered, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Where to make the %1 folder").arg(bundleFolderName()));
        if (!dir.isEmpty()) exportEverything(dir);
    });
    connect(actText, &QAction::triggered, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder for the text files"));
        if (!dir.isEmpty()) exportTextFiles(dir);
    });

    setTag(blankTag(9));
    refreshAll();
    m_status->state(tr("A new route. Build a tag on the right and Add it, or open one from the Library"));
}

// ---- tag editor --------------------------------------------------------------------------------

void TagBuilderWindow::rebuildForm()
{
    const int type = RfidTag::summary(m_tag).type;
    if (type == m_formType) return;
    m_formType = type;
    while (m_form->rowCount() > 0) m_form->removeRow(0);
    m_editors.clear();
    for (const RfidTag::Field &f : RfidTag::fieldsOf(type)) {
        QWidget *w = nullptr;
        // A short coded field is a choice of every value it can hold, named
        // where the schema names it; a long one (a location) is a number.
        if (!f.enumName.isEmpty() && f.bits <= 4) {
            auto *c = new QComboBox(m_formHost);
            // Some names are long ("0 (Reset Dir. unknown (derive from next tags))"):
            // the box may be narrower than they are; the list shows them whole.
            c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            c->setMinimumContentsLength(12);
            for (qint64 v = 0; v < (qint64(1) << f.bits); ++v) c->addItem(RfidTag::enumLabel(f.enumName, v), v);
            connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
                if (!m_updating) buildFromForm();
            });
            w = c;
        } else {
            auto *s = new QSpinBox(m_formHost);
            s->setRange(0, int(qMin<qint64>((qint64(1) << f.bits) - 1, 0x7FFFFFFF)));
            if (f.enumName == QLatin1String("rfidAbs")) {
                s->setSuffix(tr(" m"));
                s->setToolTip(tr("%1 = not applicable").arg(s->maximum()));
            }
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
                if (!m_updating) buildFromForm();
            });
            w = s;
        }
        w->setObjectName(f.name);
        m_form->addRow(QStringLiteral("%1 (%2 bit%3):").arg(f.name).arg(f.bits).arg(f.bits == 1 ? "" : "s"), w);
        // Session 304: what Annexure D says the field is, on its label and its editor.
        w->setToolTip(withTagDoc(f.name, type, w->toolTip()));
        if (QWidget *label = m_form->labelForField(w)) label->setToolTip(withTagDoc(f.name, type));
        m_editors.insert(f.name, w);
    }
}

void TagBuilderWindow::fillForm()
{
    const QHash<QString, qint64> v = RfidTag::values(m_tag);
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
        const qint64 x = v.value(it.key());
        if (auto *c = qobject_cast<QComboBox *>(it.value())) c->setCurrentIndex(c->findData(x));
        else if (auto *s = qobject_cast<QSpinBox *>(it.value())) s->setValue(int(x));
    }
}

void TagBuilderWindow::offerApply(const QString &text, const QString &tip)
{
    m_apply->setText(text);
    m_apply->setToolTip(tip);
    m_apply->show();
}

void TagBuilderWindow::showTag()
{
    m_updating = true;
    const RfidTag::Summary s = RfidTag::summary(m_tag);
    const int ti = m_type->findData(s.type);
    m_type->setCurrentIndex(ti);
    rebuildForm();
    fillForm();
    // Rewritten only when they say something else: not to drop the spaces
    // or the case the operator typed, and never under a cursor mid-edit.
    for (auto [e, want] : { std::make_pair(m_pageX, RfidTag::pageX(m_tag)), std::make_pair(m_pageY, RfidTag::pageY(m_tag)) })
        if (hexDigits(e->text()).compare(want, Qt::CaseInsensitive) != 0) e->setText(want);
    m_name->setText(ti < 0 ? tr("Tag type %1: not a type this editor builds (9 to 12)").arg(s.type)
                           : tr("Tag %1, %2 tag").arg(RfidTag::nameOf(m_tag), RfidTag::typeName(s.type)));
    if (s.crcOk) {
        m_crc->setText(tr("CRC-30 %1: matches the contents").arg(hex8(s.crcStored)));
        m_crc->setStyleSheet(UiColor::style(UiColor::ok()));
    } else {
        m_crc->setText(tr("CRC-30 stored %1, the contents give %2: a loco would not process this tag")
                           .arg(hex8(s.crcStored), hex8(s.crcCalc)));
        m_crc->setStyleSheet(UiColor::style(UiColor::error()));
    }
    m_updating = false;
}

void TagBuilderWindow::setTag(const QByteArray &tag)
{
    if (tag.size() != RfidTag::TagBytes) return;
    m_tag = tag;
    showTag();
}

void TagBuilderWindow::setPages(const QString &px, const QString &py)
{
    // From a file or another window: a value, not typing. Leading zeros
    // a file left out are put back.
    auto full = [](const QString &s) {
        const QString d = hexDigits(s);
        return d.size() < 16 && !d.isEmpty() ? QString(16 - d.size(), QLatin1Char('0')) + d : d;
    };
    m_pageX->setText(full(px));
    m_pageY->setText(full(py));
    pagesEdited();
}

void TagBuilderWindow::pagesEdited()
{
    // Session 265: each keystroke used to be read at once, 15 digits as a
    // smaller number, and the box rewritten with it zero-padded: Backspace
    // put a 0 at the front, and a full box took no digit anywhere. Now the
    // box keeps what is typed; the tag changes only at 16 digits.
    const QString x = hexDigits(m_pageX->text()), y = hexDigits(m_pageY->text());
    if (x.size() != 16 || y.size() != 16) {
        QStringList what;
        if (x.size() != 16) what << tr("page_x has %1 of 16 hex digits").arg(x.size());
        if (y.size() != 16) what << tr("page_y has %1 of 16 hex digits").arg(y.size());
        m_status->warn(what.join(QStringLiteral(", ")) + tr(": the tag shown is the last complete one"));
        return;
    }
    QString err;
    const QByteArray t = RfidTag::fromPages(x, y, &err);
    if (t.isEmpty()) {
        m_status->warn(err);
        return;
    }
    m_status->state(tr("page_x and page_y read: tag %1").arg(RfidTag::nameOf(t)));
    setTag(t);
}

void TagBuilderWindow::buildFromForm()
{
    QHash<QString, qint64> v;
    v.insert(QStringLiteral("type"), m_type->currentData().toInt());
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
        if (auto *c = qobject_cast<QComboBox *>(it.value())) v.insert(it.key(), c->currentData().toLongLong());
        else if (auto *s = qobject_cast<QSpinBox *>(it.value())) v.insert(it.key(), s->value());
    }
    QString err;
    const QByteArray t = RfidTag::build(v, &err);
    if (t.isEmpty()) {
        m_status->fail(tr("Not built: %1").arg(err));
        return;
    }
    setTag(t);
}

bool TagBuilderWindow::setField(const QString &name, qint64 value)
{
    QWidget *w = m_editors.value(name);
    if (!w) return false;
    if (auto *c = qobject_cast<QComboBox *>(w)) c->setCurrentIndex(c->findData(value));
    else if (auto *s = qobject_cast<QSpinBox *>(w)) s->setValue(int(value));
    return true;
}

void TagBuilderWindow::setType(int type)
{
    // The fields both types carry keep their values; the rest start at 0.
    QHash<QString, qint64> v = RfidTag::values(m_tag);
    v.insert(QStringLiteral("type"), type);
    QString err;
    const QByteArray t = RfidTag::build(v, &err);
    if (t.isEmpty()) {
        m_status->fail(tr("Not built: %1").arg(err));
        return;
    }
    setTag(t);
}

// ---- route -------------------------------------------------------------------------------------

void TagBuilderWindow::fillTable()
{
    const QSignalBlocker block(m_table);
    const QVector<RfidTag::RouteRow> rows = RfidTag::routeRows(m_route.tags, m_route.dir);
    m_table->setRowCount(m_route.tags.size());
    for (int i = 0; i < m_route.tags.size(); ++i) {
        const QByteArray &b = m_route.tags.at(i).bytes;
        const RfidTag::Summary s = RfidTag::summary(b);
        m_table->setItem(i, 0, cell(QString::number(i + 1)));
        QTableWidgetItem *name = cell(RfidTag::nameOf(b));
        if (m_route.tags.at(i).name != name->text() && !m_route.tags.at(i).name.isEmpty())
            name->setToolTip(tr("The file named it %1").arg(m_route.tags.at(i).name));
        m_table->setItem(i, 1, name);
        // Session 304: the columns that are tag fields carry Annexure D's
        // description for this row's tag type.
        QTableWidgetItem *typeCell = cell(QStringLiteral("%1 %2").arg(s.type).arg(RfidTag::typeName(s.type)));
        typeCell->setToolTip(withTagDoc(QStringLiteral("type"), s.type));
        m_table->setItem(i, 3, typeCell);
        QTableWidgetItem *absCell = cell(s.absLoc == RfidCheck::kNotApplicable ? tr("N/A") : QString::number(s.absLoc));
        absCell->setToolTip(withTagDoc(s.type == 12 ? QStringLiteral("abs_loc_1") : QStringLiteral("abs_loc"), s.type));
        m_table->setItem(i, 4, absCell);
        const QString written = i < rows.size() && rows.at(i).absLoc != rows.at(i).ownLoc ? QString::number(rows.at(i).absLoc) : QString();
        QTableWidgetItem *wr = cell(written);
        if (i < rows.size() && !rows.at(i).adjustNote.isEmpty()) wr->setToolTip(rows.at(i).adjustNote);
        m_table->setItem(i, 5, wr);
        m_table->setItem(i, 6, cell(RfidCheck::deltaText(m_route, i)));
        QTableWidgetItem *tinCell = cell(QStringLiteral("%1 / %2").arg(s.tinNom).arg(s.tinRev));
        tinCell->setToolTip(withTagDoc(QStringLiteral("tin"), s.type));
        m_table->setItem(i, 7, tinCell);
        QTableWidgetItem *placeCell = cell(s.type == 9 || s.type == 10 ? QString::number(s.placement) : QString());
        if (s.type == 9 || s.type == 10) placeCell->setToolTip(withTagDoc(QStringLiteral("placement"), s.type));
        m_table->setItem(i, 8, placeCell);
        QTableWidgetItem *crc = cell(s.crcOk ? tr("pass") : tr("FAIL"));
        crc->setForeground(s.crcOk ? UiColor::ok() : UiColor::error());
        crc->setToolTip(withTagDoc(QStringLiteral("CRC-30"), s.type,
                                   s.crcOk ? QString()
                                           : tr("Stored %1, the contents give %2: a loco would not process this tag")
                                                 .arg(hex8(s.crcStored), hex8(s.crcCalc))));
        m_table->setItem(i, 2, crc);
        m_table->setItem(i, 9, cell(RfidTag::pageX(b) + QStringLiteral("  ") + RfidTag::pageY(b)));
    }
    m_updating = true;
    if (m_routeName->text() != m_route.name) m_routeName->setText(m_route.name);
    m_dir->setCurrentIndex(qMax(0, m_dir->findData(m_route.dir)));
    m_updating = false;
}

int TagBuilderWindow::currentRow() const
{
    const QList<int> rows = selectedRows();
    return rows.isEmpty() ? -1 : rows.first();
}

QList<int> TagBuilderWindow::selectedRows() const
{
    QList<int> rows;
    for (const QModelIndex &i : m_table->selectionModel()->selectedRows()) rows << i.row();
    std::sort(rows.begin(), rows.end());
    return rows;
}

void TagBuilderWindow::selectRows(const QList<int> &rows)
{
    m_table->clearSelection();
    for (int r : rows) m_table->selectionModel()->select(m_table->model()->index(r, 0),
                                                         QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

QVector<RfidTag::Field> TagBuilderWindow::commonFields(const QList<int> &rows) const
{
    QVector<RfidTag::Field> out;
    if (rows.isEmpty() || rows.first() < 0 || rows.last() >= m_route.tags.size()) return out;
    out = RfidTag::fieldsOf(RfidTag::summary(m_route.tags.at(rows.first()).bytes).type);
    for (int r : rows) {
        QStringList names;
        for (const RfidTag::Field &f : RfidTag::fieldsOf(RfidTag::summary(m_route.tags.at(r).bytes).type)) names << f.name;
        for (int i = out.size() - 1; i >= 0; --i)
            if (!names.contains(out.at(i).name)) out.remove(i);
    }
    return out;
}

bool TagBuilderWindow::setFieldOn(const QList<int> &rows, const QString &field, qint64 value)
{
    bool known = false;
    for (const RfidTag::Field &f : commonFields(rows)) known = known || f.name == field;
    if (!known) {
        m_status->fail(tr("Not set: %1 is not a field of every selected tag").arg(field));
        return false;
    }
    RfidTag::Route next = m_route;
    for (int r : rows) {
        QHash<QString, qint64> v = RfidTag::values(next.tags.at(r).bytes);
        v.insert(field, value);
        QString err;
        const QByteArray b = RfidTag::build(v, &err);
        if (b.isEmpty()) {
            m_status->fail(tr("Nothing set: tag %1 (row %2): %3").arg(RfidTag::nameOf(next.tags.at(r).bytes)).arg(r + 1).arg(err));
            return false;
        }
        next.tags[r].bytes = b;
    }
    const RfidTag::Route before = m_route;
    m_route = next;
    changed(tr("Set %1 on %2 tags").arg(field).arg(rows.size()), before, rows.first());
    selectRows(rows);
    m_status->ok(tr("%1 = %2 on %3 tag%4, their CRCs recomputed").arg(field).arg(value).arg(rows.size())
                     .arg(rows.size() == 1 ? "" : "s"));
    return true;
}

void TagBuilderWindow::selectRow(int row)
{
    if (row >= 0 && row < m_table->rowCount()) m_table->selectRow(row);
    else m_table->clearSelection();
}

void TagBuilderWindow::setModified(bool on)
{
    m_modified = on;
    setWindowTitle(tr("RFID Tag Builder") + (on ? QStringLiteral(" *") : QString()));
}

void TagBuilderWindow::setRoute(const RfidTag::Route &r)
{
    m_route = r;
    m_undo->clear();
    refreshAll();
    setModified(false);
    if (!m_route.tags.isEmpty()) selectRow(0);
}

void TagBuilderWindow::refreshAll()
{
    fillSimStart();
    fillTable();
    fillSignals();
    fillChecks();
    m_strip->setRoute(m_route);
    m_strip->setSelectedRow(currentRow());
}

void TagBuilderWindow::changed(const QString &label, const RfidTag::Route &before, int row)
{
    QPointer<TagBuilderWindow> self(this);
    m_undo->push(label, [self, before]() {
        if (!self) return false;
        self->m_route = before;
        self->refreshAll();
        self->setModified(true);
        return true;
    });
    refreshAll();
    setModified(true);
    selectRow(row);
}

void TagBuilderWindow::fillSignals()
{
    m_updating = true;
    m_signals->setRowCount(m_route.signalList.size());
    for (int i = 0; i < m_route.signalList.size(); ++i) {
        const RfidTag::Signal &sg = m_route.signalList.at(i);
        m_signals->setItem(i, 0, new QTableWidgetItem(sg.footTag));
        m_signals->setItem(i, 1, new QTableWidgetItem(sg.name));
        m_signals->setItem(i, 2, new QTableWidgetItem(sg.sigId));
    }
    m_tabs->setTabText(1, tr("Signals (%1)").arg(m_route.signalList.size()));
    m_updating = false;
}

void TagBuilderWindow::fillChecks()
{
    const QVector<RfidCheck::Finding> found = RfidCheck::check(m_route);
    m_checks->setRowCount(found.size());
    int attention = 0;
    for (int i = 0; i < found.size(); ++i) {
        const RfidCheck::Finding &f = found.at(i);
        const bool look = f.level == RfidCheck::Level::Attention;
        attention += look ? 1 : 0;
        QTableWidgetItem *row = cell(f.row < 0 ? QString() : QString::number(f.row + 1));
        row->setData(Qt::UserRole, f.row);
        m_checks->setItem(i, 0, row);
        m_checks->setItem(i, 1, cell(f.tag));
        QTableWidgetItem *text = cell(f.text);
        if (look) text->setForeground(UiColor::warning());
        m_checks->setItem(i, 2, text);
    }
    m_tabs->setTabText(2, attention ? tr("Checks (%1 to look at)").arg(attention) : tr("Checks"));
}

void TagBuilderWindow::addTag()
{
    const RfidTag::Route before = m_route;
    const int at = currentRow() < 0 ? m_route.tags.size() : currentRow() + 1;
    m_route.tags.insert(at, { RfidTag::nameOf(m_tag), m_tag });
    changed(tr("Add tag %1").arg(RfidTag::nameOf(m_tag)), before, at);
    m_status->ok(tr("Tag %1 added at row %2").arg(RfidTag::nameOf(m_tag)).arg(at + 1));
}

void TagBuilderWindow::insertTag()
{
    const RfidTag::Route before = m_route;
    const int at = qMax(0, currentRow());
    m_route.tags.insert(at, { RfidTag::nameOf(m_tag), m_tag });
    changed(tr("Insert tag %1").arg(RfidTag::nameOf(m_tag)), before, at);
    m_status->ok(tr("Tag %1 inserted at row %2").arg(RfidTag::nameOf(m_tag)).arg(at + 1));
}

void TagBuilderWindow::replaceTag()
{
    const int row = currentRow();
    if (row < 0) {
        m_status->warn(tr("Select the row to replace"));
        return;
    }
    const RfidTag::Route before = m_route;
    m_route.tags[row] = { RfidTag::nameOf(m_tag), m_tag };
    changed(tr("Replace row %1").arg(row + 1), before, row);
    m_status->ok(tr("Row %1 is now tag %2").arg(row + 1).arg(RfidTag::nameOf(m_tag)));
}

void TagBuilderWindow::deleteTag()
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) return;
    const RfidTag::Route before = m_route;
    QStringList names;
    for (int i = rows.size() - 1; i >= 0; --i) {
        names.prepend(RfidTag::nameOf(m_route.tags.at(rows.at(i)).bytes));
        m_route.tags.remove(rows.at(i));
    }
    const QString list = names.join(QStringLiteral(", "));
    changed(tr("Delete %1").arg(list), before, qMin(rows.first(), m_route.tags.size() - 1));
    m_status->ok(names.size() == 1 ? tr("Tag %1 deleted from row %2").arg(list).arg(rows.first() + 1)
                                   : tr("%1 tags deleted: %2").arg(names.size()).arg(list));
}

void TagBuilderWindow::moveTag(int delta)
{
    const int row = currentRow(), to = row + delta;
    if (row < 0 || to < 0 || to >= m_route.tags.size()) return;
    const RfidTag::Route before = m_route;
    std::swap(m_route.tags[row], m_route.tags[to]);
    changed(tr("Move tag %1").arg(RfidTag::nameOf(m_route.tags.at(to).bytes)), before, to);
}

void TagBuilderWindow::addDuplicate()
{
    const int row = currentRow();
    if (row < 0) {
        m_status->warn(tr("Select the main tag to add a duplicate of"));
        return;
    }
    const RfidTag::Summary s = RfidTag::summary(m_route.tags.at(row).bytes);
    if (s.duplicate) {
        m_status->warn(tr("Row %1 is a duplicate tag already").arg(row + 1));
        return;
    }
    if (row + 1 < m_route.tags.size()) {
        const RfidTag::Summary n = RfidTag::summary(m_route.tags.at(row + 1).bytes);
        if (n.duplicate && n.unique == s.unique) {
            m_status->warn(tr("Tag %1 has its duplicate in the next row").arg(s.unique));
            return;
        }
    }
    QHash<QString, qint64> v = RfidTag::values(m_route.tags.at(row).bytes);
    v.insert(QStringLiteral("duplication"), 1);
    const QString loc = s.type == 12 ? QStringLiteral("abs_loc_1") : QStringLiteral("abs_loc");
    if (s.absLoc != RfidCheck::kNotApplicable)
        v.insert(loc, s.absLoc + (m_route.dir == RfidTag::DirReverse ? -1 : 1) * RfidCheck::kDuplicateGap);
    QString err;
    const QByteArray dup = RfidTag::build(v, &err);
    if (dup.isEmpty()) {
        m_status->fail(tr("No duplicate made: %1").arg(err));
        return;
    }
    const RfidTag::Route before = m_route;
    m_route.tags.insert(row + 1, { RfidTag::nameOf(dup), dup });
    changed(tr("Add duplicate %1").arg(RfidTag::nameOf(dup)), before, row + 1);
    m_status->ok(tr("Duplicate tag %1 added at %2 m").arg(RfidTag::nameOf(dup)).arg(RfidTag::summary(dup).absLoc));
}

bool TagBuilderWindow::shiftAll(int metres)
{
    RfidTag::Route next = m_route;
    int moved = 0;
    for (int i = 0; i < next.tags.size(); ++i) {
        const RfidTag::Summary s = RfidTag::summary(next.tags.at(i).bytes);
        if (s.absLoc == RfidCheck::kNotApplicable) continue;
        QHash<QString, qint64> v = RfidTag::values(next.tags.at(i).bytes);
        v.insert(s.type == 12 ? QStringLiteral("abs_loc_1") : QStringLiteral("abs_loc"), s.absLoc + metres);
        QString err;
        const QByteArray b = RfidTag::build(v, &err);
        if (b.isEmpty() || s.absLoc + metres < 0) {
            m_status->fail(tr("Nothing shifted: tag %1 at %2 m would be at %3 m, outside 0 to 8388606")
                               .arg(RfidTag::nameOf(next.tags.at(i).bytes)).arg(s.absLoc).arg(s.absLoc + metres));
            return false;
        }
        next.tags[i].bytes = b;
        ++moved;
    }
    const RfidTag::Route before = m_route;
    m_route = next;
    changed(tr("Shift by %1 m").arg(metres), before, currentRow());
    m_status->ok(tr("%1 tags moved by %2 m, their CRCs recomputed").arg(moved).arg(metres));
    return true;
}

void TagBuilderWindow::setDirection(int dir)
{
    if (dir == m_route.dir) return;
    const RfidTag::Route before = m_route;
    m_route.dir = dir;
    changed(tr("Direction"), before, currentRow());
}

void TagBuilderWindow::addSignal()
{
    const int row = currentRow();
    QString foot;
    if (row >= 0) {
        const RfidTag::Summary s = RfidTag::summary(m_route.tags.at(row).bytes);
        foot = QString::number(s.unique);
    }
    const RfidTag::Route before = m_route;
    m_route.signalList.append({ foot, QString(), QString() });
    changed(tr("Add signal"), before, row);
    m_tabs->setCurrentIndex(1);
    m_signals->setCurrentCell(m_route.signalList.size() - 1, 1);
}

void TagBuilderWindow::deleteSignal()
{
    const QList<QTableWidgetItem *> sel = m_signals->selectedItems();
    if (sel.isEmpty()) return;
    const int at = sel.first()->row();
    const RfidTag::Route before = m_route;
    m_route.signalList.remove(at);
    changed(tr("Delete signal"), before, currentRow());
}

bool TagBuilderWindow::applyScenario(int kind, const QString &tag)
{
    const TagScenario::Result r = TagScenario::apply(m_route, TagScenario::Kind(kind), tag);
    if (!r.ok) {
        m_status->fail(tr("Not changed: %1").arg(r.why));
        return false;
    }
    const RfidTag::Route before = m_route;
    m_route = r.route;
    int row = 0;
    const QString main = tag.endsWith(QLatin1Char('D')) ? tag.chopped(1) : tag;
    for (int i = 0; i < m_route.tags.size(); ++i)
        if (m_route.tags.at(i).name == main || m_route.tags.at(i).name == main + QLatin1Char('D')) { row = i; break; }
    changed(tr("Scenario: %1").arg(r.change), before, row);
    m_status->ok(tr("Scenario: %1 (Ctrl+Z puts it back)").arg(r.change));
    return true;
}

bool TagBuilderWindow::jumbleSelected()
{
    QStringList names;
    for (int row : selectedRows()) names << m_route.tags.at(row).name;
    const TagScenario::Result r = TagScenario::jumble(m_route, names);
    if (!r.ok) {
        m_status->warn(r.why);
        return false;
    }
    const RfidTag::Route before = m_route;
    m_route = r.route;
    changed(tr("Jumble %1 tags").arg(names.size()), before, selectedRows().value(0, 0));
    m_status->ok(tr("Jumbled: %1").arg(r.change));
    return true;
}

QStringList TagBuilderWindow::makeSection14(const QString &folder, const QString &tag, QString *why)
{
    QStringList written;
    QString err;
    const QVector<TagScenario::Planned> plan = TagScenario::section14(m_route, tag, &err);
    if (plan.isEmpty()) {
        m_status->fail(tr("No set made: %1").arg(err));
        if (why) *why = err;
        return written;
    }
    const QDir dir(folder);
    QStringList refused;
    for (const TagScenario::Planned &p : plan) {
        TagScenario::Result r = TagScenario::apply(m_route, p.kind, tag);
        if (!r.ok) { refused << QStringLiteral("%1 (%2)").arg(p.clause, r.why); continue; }
        r.route.name = QStringLiteral("%1 %2 %3").arg(m_route.name.isEmpty() ? QStringLiteral("route") : m_route.name,
                                                     p.clause, TagScenario::describe(p.kind));
        const QString file = dir.absoluteFilePath(p.clause + QStringLiteral(".tagroute.xml"));
        if (!RfidTag::writeFile(file, RfidTag::toTagRouteXml(r.route), &err)) { refused << QStringLiteral("%1 (%2)").arg(p.clause, err); continue; }
        written << file;
        // tags_sim's route.xml too, for the RFID simulator, when the route has a direction
        if (m_route.dir != RfidTag::DirUnset) {
            const QByteArray xml = RfidTag::toRouteXml(r.route, &err);
            const QString sim = dir.absoluteFilePath(p.clause + QStringLiteral("_route.xml"));
            if (!xml.isEmpty() && RfidTag::writeFile(sim, xml, &err)) written << sim;
        }
    }
    if (written.isEmpty()) {
        m_status->fail(tr("No set made: %1").arg(refused.join(QStringLiteral("; "))));
        if (why) *why = refused.join(QStringLiteral("; "));
    } else {
        m_status->ok(tr("Wrote %1 routes (%2 to %3) to %4%5")
                         .arg(plan.size() - refused.size()).arg(plan.first().clause, plan.last().clause)
                         .arg(QDir::toNativeSeparators(folder))
                         .arg(m_route.dir == RfidTag::DirUnset ? tr("; no route.xml: the route has no direction") : QString()));
    }
    return written;
}

bool TagBuilderWindow::undo()
{
    return m_undo->undo();
}

// ---- files -------------------------------------------------------------------------------------

bool TagBuilderWindow::loadFile(const QString &path, int routeIndex)
{
    QString err;
    QStringList notes;
    const QVector<RfidTag::Route> routes = RfidTag::readFile(path, &err, &notes);
    if (routes.isEmpty()) {
        m_status->fail(tr("%1 not opened: %2").arg(QFileInfo(path).fileName(), err));
        return false;
    }
    int pick = routeIndex;
    if (pick < 0 && routes.size() == 1) pick = 0;
    if (pick < 0) {
        QStringList items;
        for (const RfidTag::Route &r : routes)
            items << tr("%1  (dir %2, %3 tags)").arg(r.name).arg(r.dir).arg(r.tags.size());
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Open a route"),
                                                     tr("%1 holds %2 routes. Open:").arg(QFileInfo(path).fileName()).arg(routes.size()),
                                                     items, 0, false, &ok);
        if (!ok) return false;
        pick = items.indexOf(chosen);
    }
    if (pick < 0 || pick >= routes.size()) {
        m_status->fail(tr("%1 has no route %2").arg(QFileInfo(path).fileName()).arg(pick + 1));
        return false;
    }
    setRoute(routes.at(pick));
    Settings::setTagBuilderLast(path, pick);
    // Session 287: a route file on disk goes on File ▸ Open Recent (not the
    // built-in library's ":/" copies, nor a route.xml: see recentfiles.h).
    if (!path.startsWith(QLatin1String(":/")) && RecentFiles::kindOf(path) == RecentFiles::Kind::TagRoute)
        RecentFiles::add(path);
    int bad = 0;
    for (const RfidTag::Tag &t : m_route.tags) bad += RfidTag::summary(t.bytes).crcOk ? 0 : 1;
    QString msg = tr("Opened %1 from %2: %3 tags, %4 signals")
                      .arg(m_route.name, QFileInfo(path).fileName()).arg(m_route.tags.size()).arg(m_route.signalList.size());
    if (!notes.isEmpty()) msg += tr("; %1 rows not read (%2)").arg(notes.size()).arg(notes.first());
    if (bad == 1) msg += tr("; 1 tag fails its CRC-30 (a loco would not process it)");
    else if (bad) msg += tr("; %1 tags fail their CRC-30 (a loco would not process them)").arg(bad);
    if (bad || !notes.isEmpty()) m_status->warn(msg);
    else m_status->ok(msg);
    return true;
}

bool TagBuilderWindow::reopenLast()
{
    const QString last = Settings::tagBuilderLastFile();
    if (last.isEmpty() || !QFile::exists(last)) return false;
    return loadFile(last, Settings::tagBuilderLastRoute());
}

bool TagBuilderWindow::saveFile(const QString &path)
{
    QString err;
    if (!RfidTag::writeFile(path, RfidTag::toTagRouteXml(m_route), &err)) {
        m_status->fail(tr("Not saved: %1").arg(err));
        return false;
    }
    setModified(false);
    m_status->ok(tr("Saved %1 (%2 tags)").arg(QFileInfo(path).fileName()).arg(m_route.tags.size()));
    return true;
}

bool TagBuilderWindow::exportRouteXml(const QString &path)
{
    QString err;
    const QByteArray xml = RfidTag::toRouteXml(m_route, &err);
    if (xml.isEmpty() || !RfidTag::writeFile(path, xml, &err)) {
        m_status->fail(tr("Not exported: %1").arg(err));
        return false;
    }
    m_status->ok(tr("Exported %1: the route and its REV route, %2 tags each")
                     .arg(QFileInfo(path).fileName()).arg(m_route.tags.size()));
    return true;
}

bool TagBuilderWindow::exportSelectedTag(const QString &path)
{
    QString err;
    const int row = currentRow();
    const QByteArray xml = RfidExport::tagRow(m_route, row, &err);
    if (xml.isEmpty() || !RfidTag::writeFile(path, xml, &err)) {
        m_status->fail(tr("Not exported: %1").arg(err));
        return false;
    }
    m_status->ok(tr("Exported %1: tag %2 (row %3) as its route.xml <rfid_data> line")
                     .arg(QFileInfo(path).fileName(), RfidTag::nameOf(m_route.tags.at(row).bytes))
                     .arg(row + 1));
    return true;
}

QStringList TagBuilderWindow::sharedFileNames(SharedExport kind) const
{
    const QString name = m_route.name.isEmpty() ? QStringLiteral("route") : m_route.name;
    switch (kind) {
    case SharedExport::RouteXml:
        return { QStringLiteral("route.xml") };
    case SharedExport::ConfigurationRoute:
        return { name + QStringLiteral("_configuration_route.xml") };
    case SharedExport::SelectedTag: {
        const int row = currentRow();
        if (row < 0 || row >= m_route.tags.size()) return {};
        return { RfidTag::nameOf(m_route.tags.at(row).bytes) + QStringLiteral("_rfid_data.xml") };
    }
    case SharedExport::TextFiles: {
        const QString stem = RfidExport::textFiles(m_route).stem;
        return { stem + QStringLiteral("rfid.txt"), stem + QStringLiteral("sigID.txt"),
                 stem + QStringLiteral("tag_link_info.txt") };
    }
    case SharedExport::Everything:
        return { bundleFolderName() };
    }
    return {};
}

QString TagBuilderWindow::bundleFolderName() const
{
    // The route's name, less what Windows forbids in a file name.
    QString name = m_route.name.trimmed();
    static const QString kForbidden = QStringLiteral("<>:\"/\\|?*");
    for (QChar &c : name)
        if (kForbidden.contains(c) || c.unicode() < 32) c = QLatin1Char('_');
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) name.chop(1);
    return name.isEmpty() ? QStringLiteral("route") : name;
}

QString TagBuilderWindow::bundleReadme(const QStringList &written, const QStringList &skipped, const QDateTime &when) const
{
    QStringList out;
    const QString dir = m_route.dir == RfidTag::DirNominal   ? QStringLiteral("Nominal (dir 1, DN)")
                        : m_route.dir == RfidTag::DirReverse ? QStringLiteral("Reverse (dir 2, UP)")
                                                             : QStringLiteral("not set");
    out << QStringLiteral("Route %1, from the DLConsole RFID Tag Builder").arg(m_route.name.isEmpty() ? QStringLiteral("(no name)") : m_route.name)
        << QStringLiteral("Written %1").arg(when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
        << QString()
        << QStringLiteral("Direction: %1").arg(dir)
        << QStringLiteral("Tags:      %1").arg(m_route.tags.size())
        << QStringLiteral("Signals:   %1").arg(m_route.signalList.size());
    if (!m_route.tags.isEmpty()) {
        out << QStringLiteral("First tag: %1   Last tag: %2")
                   .arg(RfidTag::nameOf(m_route.tags.first().bytes), RfidTag::nameOf(m_route.tags.last().bytes));
    }
    QStringList badCrc;
    for (int i = 0; i < m_route.tags.size(); ++i)
        if (!RfidTag::summary(m_route.tags.at(i).bytes).crcOk)
            badCrc << QStringLiteral("  row %1, tag %2").arg(i + 1).arg(RfidTag::nameOf(m_route.tags.at(i).bytes));
    out << QString();
    if (badCrc.isEmpty()) {
        out << QStringLiteral("CRC-30: every tag's CRC matches its contents.");
    } else {
        out << QStringLiteral("CRC-30: %1 tag(s) fail their CRC; a loco would not process them:").arg(badCrc.size());
        out << badCrc;
    }
    QStringList attention;
    for (const RfidCheck::Finding &f : RfidCheck::check(m_route)) {
        if (f.level != RfidCheck::Level::Attention) continue;
        attention << (f.row < 0 ? QStringLiteral("  route: %1").arg(f.text)
                                : QStringLiteral("  row %1, tag %2: %3").arg(f.row + 1).arg(f.tag, f.text));
    }
    out << QString();
    if (attention.isEmpty()) {
        out << QStringLiteral("Checks: nothing marked for attention.");
    } else {
        out << QStringLiteral("Checks marked for attention (%1):").arg(attention.size());
        out << attention;
    }
    out << QString() << QStringLiteral("Files:");
    for (const QString &f : written) out << QStringLiteral("  ") + f;
    if (!skipped.isEmpty()) {
        out << QString() << QStringLiteral("Not written:");
        for (const QString &f : skipped) out << QStringLiteral("  ") + f;
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool TagBuilderWindow::exportEverything(const QString &parentFolder, bool ask)
{
    if (m_route.tags.isEmpty()) {
        m_status->warn(tr("The route has no tags"));
        return false;
    }
    const QString folderName = bundleFolderName();
    QDir parent(parentFolder);
    if (!parent.exists()) {
        m_status->fail(tr("Not exported: %1 cannot be reached").arg(QDir::toNativeSeparators(parentFolder)));
        return false;
    }
    const QString folder = parent.filePath(folderName);
    const QDir dir(folder);
    if (dir.exists() && !dir.entryList(QDir::Files).isEmpty() && ask
        && QMessageBox::question(this, tr("Export everything"),
                                 tr("%1 already has files. Replace the route's files in it?")
                                     .arg(QDir::toNativeSeparators(folder)))
               != QMessageBox::Yes) {
        m_status->warn(tr("Not exported: %1 kept as it was").arg(QDir::toNativeSeparators(folder)));
        return false;
    }
    if (!parent.mkpath(folderName)) {
        m_status->fail(tr("Not exported: cannot make %1").arg(QDir::toNativeSeparators(folder)));
        return false;
    }

    QStringList written, skipped;
    QString err;
    auto put = [&](const QString &name, const QByteArray &data, const QString &why) {
        if (data.isEmpty()) {
            skipped << QStringLiteral("%1: %2").arg(name, why);
            return true;
        }
        if (!RfidTag::writeFile(dir.filePath(name), data, &err)) return false;
        written << name;
        return true;
    };
    QString routeErr, configErr;
    const QByteArray routeXml = RfidTag::toRouteXml(m_route, &routeErr);
    const QByteArray configXml = RfidExport::configurationRoute(m_route, &configErr);
    bool ok = put(folderName + QStringLiteral(".tagroute.xml"), RfidTag::toTagRouteXml(m_route), QString())
              && put(QStringLiteral("route.xml"), routeXml, routeErr)
              && put(folderName + QStringLiteral("_configuration_route.xml"), configXml, configErr);
    const RfidExport::TextFiles t = RfidExport::textFiles(m_route);
    const QVector<QPair<QString, QStringList>> texts{ { QStringLiteral("rfid.txt"), t.rfid },
                                                      { QStringLiteral("sigID.txt"), t.sigId },
                                                      { QStringLiteral("tag_link_info.txt"), t.tagLinkInfo } };
    for (const auto &f : texts) {
        if (!ok) break;
        QByteArray data;
        for (const QString &line : f.second) data += line.toUtf8() + '\n';
        ok = put(t.stem + f.first, data, tr("nothing to write"));
    }
    if (ok) {
        const QByteArray readme = bundleReadme(written + QStringList{ QStringLiteral("README.txt") }, skipped,
                                               QDateTime::currentDateTime()).toUtf8();
        ok = put(QStringLiteral("README.txt"), readme, QString());
    }
    if (!ok) {
        m_status->fail(tr("Not exported: %1").arg(err));
        return false;
    }
    const QString where = QDir::toNativeSeparators(folder);
    if (skipped.isEmpty()) {
        m_status->ok(tr("Exported %1 files to %2").arg(written.size()).arg(where));
    } else {
        m_status->warn(tr("Exported %1 files to %2; not written: %3")
                           .arg(written.size()).arg(where, skipped.join(QStringLiteral("; "))));
    }
    return true;
}

bool TagBuilderWindow::exportToShared(SharedExport kind, bool ask)
{
    const QString folder = Settings::tagSharedFolder();
    if (folder.isEmpty()) {
        m_status->warn(tr("No shared folder set: Export \u25B8 Set shared folder\u2026"));
        return false;
    }
    const QDir dir(folder);
    if (!dir.exists()) {
        m_status->fail(tr("Not exported: the shared folder %1 cannot be reached").arg(QDir::toNativeSeparators(folder)));
        return false;
    }
    const QStringList names = sharedFileNames(kind);
    if (names.isEmpty()) {
        m_status->warn(tr("Select the tag to export"));
        return false;
    }
    QStringList existing;
    for (const QString &n : names)
        if (QFileInfo::exists(dir.filePath(n))) existing << n;
    if (!existing.isEmpty() && ask
        && QMessageBox::question(this, tr("Shared folder"),
                                 tr("%1 already in %2. Replace?").arg(existing.join(QStringLiteral(", ")),
                                                                     QDir::toNativeSeparators(folder)))
               != QMessageBox::Yes) {
        m_status->warn(tr("Not exported: %1 kept").arg(existing.join(QStringLiteral(", "))));
        return false;
    }
    switch (kind) {
    case SharedExport::RouteXml:           return exportRouteXml(dir.filePath(names.first()));
    case SharedExport::ConfigurationRoute: return exportConfigurationRoute(dir.filePath(names.first()));
    case SharedExport::SelectedTag:        return exportSelectedTag(dir.filePath(names.first()));
    case SharedExport::TextFiles:          return exportTextFiles(folder);
    case SharedExport::Everything:         return exportEverything(folder, false);   // asked above
    }
    return false;
}

bool TagBuilderWindow::exportConfigurationRoute(const QString &path)
{
    QString err;
    const QByteArray xml = RfidExport::configurationRoute(m_route, &err);
    if (xml.isEmpty() || !RfidTag::writeFile(path, xml, &err)) {
        m_status->fail(tr("Not exported: %1").arg(err));
        return false;
    }
    m_status->ok(tr("Exported %1: the route and its REV route (%2 tags each) as Configuration1.xml <route_data> "
                    "blocks, to paste inside its <route>")
                     .arg(QFileInfo(path).fileName()).arg(m_route.tags.size()));
    return true;
}

bool TagBuilderWindow::exportIntoConfiguration(const QString &configPath, const QString &outPath)
{
    QFile in(configPath);
    if (!in.open(QIODevice::ReadOnly)) {
        m_status->fail(tr("Not exported: %1: %2").arg(QFileInfo(configPath).fileName(), in.errorString()));
        return false;
    }
    const QByteArray original = in.readAll();
    in.close();                 // Windows will not replace a file that is open (exporting into itself)
    QString err;
    QStringList notes;
    const QByteArray merged = RfidExport::mergeIntoConfiguration(original, m_route, &err, &notes);
    if (merged.isEmpty() || !RfidTag::writeFile(outPath, merged, &err)) {
        m_status->fail(tr("Not exported: %1").arg(err));
        return false;
    }
    m_status->ok(tr("%1: %2; the rest of the file as it was").arg(QFileInfo(outPath).fileName(), notes.join(QStringLiteral(", "))));
    return true;
}

bool TagBuilderWindow::exportTextFiles(const QString &folder)
{
    if (m_route.tags.isEmpty()) {
        m_status->warn(tr("The route has no tags"));
        return false;
    }
    const RfidExport::TextFiles t = RfidExport::textFiles(m_route);
    const QDir dir(folder);
    QString err;
    const QVector<QPair<QString, QStringList>> files{ { QStringLiteral("rfid.txt"), t.rfid },
                                                      { QStringLiteral("sigID.txt"), t.sigId },
                                                      { QStringLiteral("tag_link_info.txt"), t.tagLinkInfo } };
    for (const auto &f : files) {
        QByteArray data;
        for (const QString &line : f.second) data += line.toUtf8() + '\n';
        if (!RfidTag::writeFile(dir.filePath(t.stem + f.first), data, &err)) {
            m_status->fail(tr("Not exported: %1").arg(err));
            return false;
        }
    }
    m_status->ok(tr("Wrote %1rfid.txt, %1sigID.txt and %1tag_link_info.txt").arg(t.stem));
    return true;
}

// ---- run ---------------------------------------------------------------------------------------

void TagBuilderWindow::refreshRunPicker()
{
    if (!m_dispatcher) return;
    QStringList keys = m_dispatcher->knownKeys();
    keys.sort();
    const QSignalBlocker block(m_runPicker);
    m_runPicker->clear();
    for (const QString &k : keys) {
        const QString friendly = m_dispatcher->friendlyNameFor(k);
        m_runPicker->addItem(friendly == k ? k : QStringLiteral("%1   (%2)").arg(friendly, k), k);
    }
    const int idx = m_runPicker->findData(m_runKey);
    if (idx >= 0) m_runPicker->setCurrentIndex(idx);
    else m_runKey = m_runPicker->currentData().toString();
}

void TagBuilderWindow::setRunSource(const QString &key)
{
    m_runKey = key;
    refreshRunPicker();
}

bool TagBuilderWindow::compareRun()
{
    const LogModel *model = m_dispatcher && !m_runKey.isEmpty() ? m_dispatcher->modelForKey(m_runKey) : nullptr;
    if (!model) {
        m_status->warn(tr("Pick a loco log to compare with"));
        return false;
    }
    const QVector<PlanRun::Read> reads = PlanRun::readsOf(model);
    m_runResult = PlanRun::compare(m_route, reads);
    const PlanRun::Result &r = m_runResult;
    m_runTable->setRowCount(r.planned.size() + r.notPlanned.size());
    auto hms = [](qint64 ms) { return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")) : QString(); };
    int i = 0;
    for (const PlanRun::Planned &p : r.planned) {
        m_runTable->setItem(i, 0, cell(QString::number(p.row + 1)));
        m_runTable->setItem(i, 1, cell(p.name));
        QTableWidgetItem *st = cell(PlanRun::stateText(p.state));
        if (p.state != PlanRun::State::Read) st->setForeground(UiColor::warning());
        m_runTable->setItem(i, 2, st);
        QTableWidgetItem *t = cell(hms(p.firstMs));
        t->setData(Qt::UserRole, p.firstMs);
        m_runTable->setItem(i, 3, t);
        m_runTable->setItem(i, 4, cell(p.detail));
        ++i;
    }
    for (int k = 0; k < r.notPlanned.size(); ++k, ++i) {
        m_runTable->setItem(i, 0, cell(QString()));
        m_runTable->setItem(i, 1, cell(r.notPlanned.at(k)));
        QTableWidgetItem *st = cell(tr("not planned"));
        st->setForeground(UiColor::warning());
        m_runTable->setItem(i, 2, st);
        QTableWidgetItem *t = cell(hms(r.notPlannedMs.at(k)));
        t->setData(Qt::UserRole, r.notPlannedMs.at(k));
        m_runTable->setItem(i, 3, t);
        m_runTable->setItem(i, 4, cell(tr("read in the log; not in this route")));
    }
    m_runSummary->setText(tr("%1: %2 tag reads. Of the route's %3 tags: %4 read as planned, %5 read but different, "
                             "%6 read out of order, %7 not read. %8 tags read that the route does not hold.")
                              .arg(m_runPicker->currentText()).arg(reads.size()).arg(r.planned.size())
                              .arg(r.count(PlanRun::State::Read)).arg(r.count(PlanRun::State::Different))
                              .arg(r.count(PlanRun::State::OutOfOrder)).arg(r.count(PlanRun::State::NotRead))
                              .arg(r.notPlanned.size()));
    m_tabs->setCurrentIndex(3);
    if (reads.isEmpty()) m_status->warn(tr("%1 has no @rfid frames").arg(m_runPicker->currentText()));
    else m_status->ok(tr("Compared with %1").arg(m_runPicker->currentText()));
    return true;
}

bool TagBuilderWindow::makeRouteFromRun()
{
    const LogModel *model = m_dispatcher && !m_runKey.isEmpty() ? m_dispatcher->modelForKey(m_runKey) : nullptr;
    const QVector<PlanRun::Read> reads = PlanRun::readsOf(model);
    if (reads.isEmpty()) {
        m_status->warn(tr("No @rfid frames to make a route from"));
        return false;
    }
    if (!confirmDiscard()) return false;
    RfidTag::Route r = PlanRun::routeFromRun(reads);
    r.name = tr("run %1").arg(m_runKey);
    setRoute(r);
    setModified(true);
    m_status->ok(tr("A route of the %1 tags %2 read, in the order first read").arg(r.tags.size()).arg(m_runPicker->currentText()));
    return true;
}

int TagBuilderWindow::fixRouteCrcs()
{
    const RfidTag::Route before = m_route;
    QStringList fixed;
    for (RfidTag::Tag &t : m_route.tags) {
        if (RfidTag::summary(t.bytes).crcOk) continue;
        t.bytes = RfidTag::fixCrc(t.bytes);
        fixed << RfidTag::nameOf(t.bytes);
    }
    if (fixed.isEmpty()) {
        m_status->ok(tr("Every tag's CRC-30 already matches its contents"));
        return -1;
    }
    changed(tr("Fix %1 CRCs").arg(fixed.size()), before, currentRow());
    if (currentRow() >= 0) setTag(m_route.tags.at(currentRow()).bytes);
    m_status->ok(tr("CRC-30 recomputed for %1 tag%2: %3").arg(fixed.size()).arg(fixed.size() == 1 ? "" : "s")
                     .arg(fixed.join(QStringLiteral(", "))));
    return fixed.size();
}

bool TagBuilderWindow::fixFileCrcs(const QString &inPath, const QString &outPath, QStringList *changes)
{
    QFile in(inPath);
    if (!in.open(QIODevice::ReadOnly)) {
        m_status->fail(tr("Not fixed: %1: %2").arg(QFileInfo(inPath).fileName(), in.errorString()));
        return false;
    }
    const QByteArray original = in.readAll();
    in.close();                 // Windows will not replace a file that is open (fixing in place)
    QString err;
    QStringList lines;
    const QByteArray out = RfidExport::fixCrcs(original, &lines, &err);
    if (out.isEmpty() || !RfidTag::writeFile(outPath, out, &err)) {
        m_status->fail(tr("Not fixed: %1").arg(err));
        return false;
    }
    if (changes) *changes = lines;
    if (lines.isEmpty()) m_status->ok(tr("%1: every CRC-30 already matches; written unchanged").arg(QFileInfo(outPath).fileName()));
    else m_status->ok(tr("%1: CRC-30 fixed for %2 tags; nothing else changed").arg(QFileInfo(outPath).fileName()).arg(lines.size()));
    return true;
}

// ---- simulator preview -------------------------------------------------------------------------

void TagBuilderWindow::fillSimStart()
{
    const int keep = m_simStart->currentIndex();
    const QSignalBlocker block(m_simStart);
    m_simStart->clear();
    for (int i = 0; i < m_route.tags.size(); ++i)
        m_simStart->addItem(QStringLiteral("%1  %2").arg(i + 1).arg(RfidTag::nameOf(m_route.tags.at(i).bytes)), i);
    m_simStart->setCurrentIndex(keep >= 0 && keep < m_simStart->count() ? keep : 0);
}

SimPreview::Options TagBuilderWindow::simulatorOptions() const
{
    SimPreview::Options o;
    o.speedKmh = m_simSpeed->value();
    o.startRow = qMax(0, m_simStart->currentData().toInt());
    o.reader1 = m_simReader1->isChecked();
    o.reader2 = m_simReader2->isChecked();
    for (const QString &m : m_simMissing->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) o.missing << m.trimmed();
    return o;
}

SimPreview::Result TagBuilderWindow::previewSimulator(const SimPreview::Options &o)
{
    // The controls show what was run, whoever asked for it.
    m_simSpeed->setValue(o.speedKmh);
    m_simStart->setCurrentIndex(qMax(0, m_simStart->findData(o.startRow)));
    m_simReader1->setChecked(o.reader1);
    m_simReader2->setChecked(o.reader2);
    m_simMissing->setText(o.missing.join(QLatin1Char(',')));
    const SimPreview::Result r = SimPreview::run(m_route, o);
    m_tabs->setCurrentIndex(4);
    if (!r.ok) {
        m_simTable->setRowCount(0);
        m_simSummary->setText(QString());
        m_status->warn(tr("No preview: %1").arg(r.error));
        return r;
    }
    m_simTable->clearContents();          // no reads left from an earlier Against the run
    m_simTable->setRowCount(r.events.size());
    for (int i = 0; i < r.events.size(); ++i) {
        const SimPreview::Event &e = r.events.at(i);
        m_simTable->setItem(i, 0, cell(QString::number(e.timeS, 'f', 1)));
        m_simTable->setItem(i, 1, cell(QString::number(e.distance, 'f', 1)));
        m_simTable->setItem(i, 2, cell(QString::number(e.reader)));
        m_simTable->setItem(i, 3, cell(e.tag));
        m_simTable->setItem(i, 4, cell(QString::number(e.tagLoc)));
        QTableWidgetItem *row = cell(QString::number(e.row + 1));
        row->setData(Qt::UserRole, e.row);
        m_simTable->setItem(i, 5, row);
    }
    int on1 = 0, on2 = 0;
    for (const SimPreview::Event &e : r.events) (e.reader == 1 ? on1 : on2)++;
    QString text = tr("At %1 km/h the simulator counts %2 pulses and moves %3 m every 100 ms. %4 tags on reader 1, "
                      "%5 on reader 2, over %6 s; it stops at %7 m (%8). Locations: %9.")
                       .arg(o.speedKmh).arg(r.pulsesPerTick).arg(r.metresPerTick, 0, 'f', 3).arg(on1).arg(on2)
                       .arg(r.ticks / 10.0, 0, 'f', 1).arg(r.endDistance, 0, 'f', 1).arg(r.endReason)
                       .arg(r.fromFileRows ? tr("the file's own abs_loc / next_rfid_abs_loc")
                                           : tr("as route.xml would write them"));
    if (!r.neverOnReader1.isEmpty())
        text += tr(" Never sent on reader 1: %1.").arg(r.neverOnReader1.join(QStringLiteral(", ")));
    if (!r.mainsMissedOnReader2.isEmpty())
        text += tr(" Main tags not sent on reader 2: %1%2.")
                    .arg(r.mainsMissedOnReader2.mid(0, 12).join(QStringLiteral(", ")))
                    .arg(r.mainsMissedOnReader2.size() > 12 ? tr(" and %1 more").arg(r.mainsMissedOnReader2.size() - 12) : QString());
    if (r.metresPerTick >= 4)
        text += tr(" At this speed a tick is %1 m, more than reader 2's 4 m window (20-24 m past a tag): "
                   "the simulator can step over it.").arg(r.metresPerTick, 0, 'f', 2);
    m_simSummary->setText(text);
    m_status->ok(tr("Simulator preview: %1 tags sent").arg(r.events.size()));
    return r;
}

bool TagBuilderWindow::previewAgainstRun(SimPreview::Against *out)
{
    const LogModel *model = m_dispatcher && !m_runKey.isEmpty() ? m_dispatcher->modelForKey(m_runKey) : nullptr;
    if (!model) {
        m_status->warn(tr("Pick a loco log on the Run tab to compare with"));
        return false;
    }
    const SimPreview::Result r = previewSimulator(simulatorOptions());
    if (!r.ok) return false;
    const QVector<PlanRun::Read> reads = PlanRun::readsOf(model);
    const SimPreview::Against a = SimPreview::against(r, reads);
    for (int i = 0; i < r.events.size(); ++i) {
        const int k = a.readOf.at(i);
        QTableWidgetItem *at = cell(k < 0 ? tr("not read") : QDateTime::fromMSecsSinceEpoch(reads.at(k).ms).toString(QStringLiteral("HH:mm:ss.zzz")));
        if (k < 0) at->setForeground(UiColor::warning());
        m_simTable->setItem(i, 6, at);
        m_simTable->setItem(i, 7, cell(k < 0 ? QString() : QString::number(a.lagS.at(i), 'f', 1)));
    }
    QStringList extra;
    for (int k : a.unpredicted) extra << QStringLiteral("%1 on %2").arg(RfidTag::nameOf(reads.at(k).bytes)).arg(reads.at(k).reader);
    QString text = tr(" Against %1: %2 of %3 sends read on their reader").arg(m_runPicker->currentText()).arg(a.matched).arg(r.events.size());
    text += a.matched ? tr(", times lined up on the first; the largest lag %1 s.").arg(a.maxLagS, 0, 'f', 1) : tr(".");
    if (!extra.isEmpty())
        text += tr(" Read but not predicted: %1%2.").arg(extra.mid(0, 12).join(QStringLiteral(", ")))
                    .arg(extra.size() > 12 ? tr(" and %1 more").arg(extra.size() - 12) : QString());
    const SimPreview::SpeedSeen sp = SimPreview::speedsOf(model);
    if (sp.frames == 0) {
        text += tr(" No @speed frames in it.");
    } else {
        text += tr(" Its @speed frames: %1, %2 moving").arg(sp.frames).arg(sp.moving);
        if (sp.moving)
            text += tr("; tachometer 1 at %1 km/h (median; %2 to %3) by the simulator's arithmetic, against the previewed %4 km/h")
                        .arg(sp.medianKmh, 0, 'f', 1).arg(sp.minKmh, 0, 'f', 1).arg(sp.maxKmh, 0, 'f', 1)
                        .arg(simulatorOptions().speedKmh);
        text += tr("; %1 with CRC 0 (the simulator sends 0).").arg(sp.crcZero);
    }
    m_simSummary->setText(m_simSummary->text() + text);
    if (a.matched == 0) m_status->warn(tr("No send of the preview was read in %1").arg(m_runPicker->currentText()));
    else m_status->ok(tr("Against the run: %1 of %2 sends read").arg(a.matched).arg(r.events.size()));
    if (out) *out = a;
    return a.matched > 0;
}

bool TagBuilderWindow::confirmDiscard()
{
    if (!m_modified) return true;
    return QMessageBox::question(this, tr("RFID Tag Builder"),
                                 tr("The route has changes that are not saved. Discard them?"),
                                 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel)
        == QMessageBox::Discard;
}

void TagBuilderWindow::closeEvent(QCloseEvent *e)
{
    if (confirmDiscard()) e->accept();
    else e->ignore();
}
