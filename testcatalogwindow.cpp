#include "testcatalogwindow.h"

#include "packetmatrixdialog.h"
#include "settings.h"
#include "statusline.h"
#include "uicolors.h"
#include "programdir.h"
#include "uistyle.h"
#include "undolog.h"
#include "windowgeometry.h"

#include <QAction>
#include <QButtonGroup>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QTimer>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextLayout>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

using namespace TestCatalog;

namespace {

enum Role { KindRole = Qt::UserRole, IdRole };
enum Kind { SectionItem = 1, ClauseItem, DeletedItem };

const char *const kFieldNames[] = { "title", "purpose", "methodology", "setup", "input", "expected",
                                    "result", "observed", "notes" };

QString &fieldRef(Case &k, TestCatalogWindow::Field f)
{
    switch (f) {
    case TestCatalogWindow::Title: return k.title;
    case TestCatalogWindow::Purpose: return k.purpose;
    case TestCatalogWindow::Methodology: return k.methodology;
    case TestCatalogWindow::Setup: return k.setup;
    case TestCatalogWindow::Input: return k.input;
    case TestCatalogWindow::Expected: return k.expected;
    case TestCatalogWindow::Result: return k.result;
    case TestCatalogWindow::Observed: return k.observed;
    default: return k.notes;
    }
}

QString pageList(const QVector<int> &pages)
{
    QStringList s;
    for (int p : pages) s << QString::number(p);
    return s.join(QStringLiteral(", "));
}

// An id typed or searched for: digits and dots only ("4.9", "31.6.").
bool idLike(const QString &t)
{
    for (const QChar c : t)
        if (!c.isDigit() && c != QLatin1Char('.')) return false;
    return !t.isEmpty();
}

// Session 263: a run's progress as one bar: pass, fail and N/A in their
// colours from the left, what is not tested yet as the empty track.
class RunProgress : public QWidget
{
public:
    explicit RunProgress(QWidget *parent) : QWidget(parent)
    {
        setMinimumWidth(120);
        setFixedHeight(10);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setCounts(const TestRun::Counts &c) { m_c = c; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath track;
        track.addRoundedRect(r, r.height() / 2, r.height() / 2);
        p.fillPath(track, UiColor::grid());
        if (m_c.total() <= 0) return;
        p.setClipPath(track);
        double x = r.left();
        const struct { int n; QColor c; } parts[] = { { m_c.pass, UiColor::ok() }, { m_c.fail, UiColor::error() },
                                                     { m_c.na, UiColor::muted() } };
        for (const auto &part : parts) {
            const double w = r.width() * part.n / m_c.total();
            p.fillRect(QRectF(x, r.top(), w, r.height()), part.c);
            x += w;
        }
    }

private:
    TestRun::Counts m_c;
};

const char *const kVerdictLabels[] = { "\u2713 Pass", "\u2717 Fail", "N/A", "Not tested" };
const TestRun::Verdict kVerdictOrder[] = { TestRun::Verdict::Pass, TestRun::Verdict::Fail,
                                           TestRun::Verdict::NotApplicable, TestRun::Verdict::NotTested };
int verdictButton(TestRun::Verdict v)
{
    for (int i = 0; i < 4; ++i) if (kVerdictOrder[i] == v) return i;
    return 3;
}
QString verdictCell(TestRun::Verdict v)
{
    switch (v) {
    case TestRun::Verdict::Pass: return QStringLiteral("\u2713 Pass");
    case TestRun::Verdict::Fail: return QStringLiteral("\u2717 Fail");
    case TestRun::Verdict::NotApplicable: return QStringLiteral("N/A");
    default: return QString();
    }
}

} // namespace

TestCatalogWindow::TestCatalogWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    WindowGeometry::makeResizableWindow(this);
    resize(1200, 720);

    // ---- bar ---------------------------------------------------------------------------------
    m_newBtn = new QToolButton(this);
    m_newBtn->setText(tr("New"));
    m_newBtn->setPopupMode(QToolButton::InstantPopup);
    m_newBtn->setToolTip(tr("A new clause in the selected section, a sub-clause under the selected clause, or a "
                            "new section"));
    auto *newMenu = new QMenu(m_newBtn);
    QAction *actNewClause = newMenu->addAction(tr("Clause"));
    actNewClause->setShortcut(QKeySequence::New);
    QAction *actNewSub = newMenu->addAction(tr("Sub-clause under the selected clause"));
    QAction *actNewSection = newMenu->addAction(tr("Section…"));
    m_newBtn->setMenu(newMenu);
    addAction(actNewClause);
    connect(newMenu, &QMenu::aboutToShow, this, [this, actNewClause, actNewSub]() {
        const int sec = m_current.startsWith(QLatin1Char('S')) ? m_current.mid(1).toInt() : sectionOf(m_current);
        actNewClause->setText(sec ? tr("Clause in section %1 (%2)").arg(sec).arg(nextId(m_cat, QString::number(sec)))
                                  : tr("Clause (select a section first)"));
        actNewClause->setEnabled(sec > 0);
        const bool sub = TestCatalog::depth(m_current) == 1 && indexOf(m_cat, m_current) >= 0;
        actNewSub->setText(sub ? tr("Sub-clause under %1 (%2)").arg(m_current, nextId(m_cat, m_current))
                               : tr("Sub-clause (select a clause first)"));
        actNewSub->setEnabled(sub);
    });
    connect(actNewClause, &QAction::triggered, this, [this]() { addClause(); });
    connect(actNewSub, &QAction::triggered, this, [this]() { addSubClause(); });
    connect(actNewSection, &QAction::triggered, this, [this]() {
        int top = 0;
        for (const Section &s : m_cat.sections) top = std::max(top, s.no);
        bool ok = false;
        const int no = QInputDialog::getInt(this, tr("New section"), tr("Section number:"), top + 1, 1, 999, 1, &ok);
        if (!ok) return;
        const QString title = QInputDialog::getText(this, tr("New section"), tr("Title:"), QLineEdit::Normal,
                                                    QString(), &ok);
        if (ok) addSection(no, title);
    });

    m_dupBtn = new QPushButton(tr("Duplicate"), this);
    m_dupBtn->setToolTip(tr("A copy of the selected clause under the next free number, to write a variant of it "
                            "(Ctrl+D)"));
    auto *actDup = new QAction(this);
    actDup->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    addAction(actDup);
    connect(actDup, &QAction::triggered, this, [this]() { duplicateCurrent(); });
    connect(m_dupBtn, &QPushButton::clicked, this, [this]() { duplicateCurrent(); });

    m_delBtn = new QPushButton(tr("Delete"), this);
    m_delBtn->setToolTip(tr("The selected clauses, with their sub-clauses (Delete). Ctrl+Z brings them back"));
    connect(m_delBtn, &QPushButton::clicked, this, [this]() { deleteSelected(); });

    m_undo = new UndoLog(this);
    QAction *undoAct = m_undo->createAction(this);
    addAction(undoAct);
    auto *undoBtn = new QToolButton(this);
    undoBtn->setText(tr("Undo"));
    connect(undoBtn, &QToolButton::clicked, undoAct, &QAction::trigger);
    auto syncUndo = [undoBtn, undoAct]() {
        undoBtn->setEnabled(undoAct->isEnabled());
        undoBtn->setToolTip(undoAct->text().remove(QLatin1Char('&')));
    };
    connect(undoAct, &QAction::changed, undoBtn, syncUndo);
    syncUndo();
    connect(m_undo, &UndoLog::undone, this, [this](const QString &label, bool ok) {
        if (ok) m_status->say(tr("Undone: %1").arg(label));
        else m_status->warn(tr("Could not undo: %1").arg(label));
    });

    auto *openBtn = new QToolButton(this);
    openBtn->setText(tr("Open"));
    openBtn->setPopupMode(QToolButton::InstantPopup);
    auto *openMenu = new QMenu(openBtn);
    QAction *actOpenFile = openMenu->addAction(tr("A catalog file…"));
    actOpenFile->setShortcut(QKeySequence::Open);
    addAction(actOpenFile);
    QAction *actOpenSif = openMenu->addAction(tr("The SIF as built in (SIF 0533 v4.22)"));
    actOpenSif->setToolTip(tr("Every clause as the document prints it. Saving it asks for a file: the built-in copy "
                              "is never changed"));
    openMenu->setToolTipsVisible(true);
    openBtn->setMenu(openMenu);
    connect(actOpenFile, &QAction::triggered, this, [this]() {
        if (!confirmDiscard()) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Open a test case catalog"),
                                                          QFileInfo(m_path).absolutePath(),
                                                          tr("Test case catalogs (*.json);;All files (*)"));
        if (!path.isEmpty()) loadFile(path);
    });
    connect(actOpenSif, &QAction::triggered, this, [this]() {
        if (confirmDiscard()) loadBuiltin();
    });

    auto *saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setToolTip(tr("Into the open file (Ctrl+S). The SIF as built in asks for a file"));
    auto *saveAsBtn = new QPushButton(tr("Save as…"), this);
    auto saveAs = [this]() {
        const QString start = m_path.startsWith(QLatin1Char(':'))
                                  ? programDir() + QStringLiteral("/test_catalog.json")
                                  : m_path;
        const QString path = QFileDialog::getSaveFileName(this, tr("Save the test case catalog"), start,
                                                          tr("Test case catalogs (*.json)"));
        if (!path.isEmpty()) saveFile(path);
    };
    auto save = [this, saveAs]() {
        if (m_path.startsWith(QLatin1Char(':'))) saveAs();
        else saveFile(m_path);
    };
    auto *actSave = new QAction(this);
    actSave->setShortcut(QKeySequence::Save);
    addAction(actSave);
    connect(actSave, &QAction::triggered, this, save);
    connect(saveBtn, &QPushButton::clicked, this, save);
    connect(saveAsBtn, &QPushButton::clicked, this, saveAs);

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_newBtn);
    bar->addWidget(m_dupBtn);
    bar->addWidget(m_delBtn);
    bar->addWidget(undoBtn);
    bar->addStretch(1);
    bar->addWidget(openBtn);
    bar->addWidget(saveBtn);
    bar->addWidget(saveAsBtn);

    // ---- the list ----------------------------------------------------------------------------
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search words, or 4.93"));
    m_search->setClearButtonEnabled(true);
    m_search->setToolTip(tr("Every word must appear somewhere in the clause. A number like 4.9 or 31.6 finds that "
                            "clause and its sub-clauses (Ctrl+F)"));
    m_search->setAccessibleName(tr("Search the catalog"));
    auto *actFind = new QAction(this);
    actFind->setShortcut(QKeySequence::Find);
    addAction(actFind);
    connect(actFind, &QAction::triggered, this, [this]() { m_search->setFocus(); m_search->selectAll(); });
    connect(m_search, &QLineEdit::textChanged, this, [this]() { rebuildTree(); });

    m_showBox = new QComboBox(this);
    m_showBox->addItem(tr("Every clause"), ShowAll);
    m_showBox->addItem(tr("Changed from the SIF"), ShowChanged);
    m_showBox->addItem(tr("Added here"), ShowAdded);
    m_showBox->addItem(tr("Deleted from the SIF"), ShowDeleted);
    m_showBox->addItem(tr("With a figure in the SIF"), ShowFigures);
    m_showBox->insertSeparator(m_showBox->count());
    m_showBox->addItem(tr("Failed in this run"), ShowFailed);
    m_showBox->addItem(tr("Passed in this run"), ShowPassed);
    m_showBox->addItem(tr("Not tested yet in this run"), ShowUntested);
    m_showBox->setToolTip(tr("Which clauses the list shows. Changed / added / deleted are against the SIF as "
                             "built in"));
    m_showBox->setAccessibleName(tr("Show"));
    connect(m_showBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_show = Show(m_showBox->itemData(i).toInt());
        rebuildTree();
    });

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({ tr("Clause"), tr("Test scenario"), QString(), tr("Result") });
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->setColumnWidth(0, fontMetrics().horizontalAdvance(QStringLiteral("88.888.888")) + 44);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setUniformRowHeights(true);
    m_tree->setAccessibleName(tr("Clauses by section"));
    m_tree->installEventFilter(this);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        if (!it) return;
        const int kind = it->data(0, KindRole).toInt();
        m_current = kind == SectionItem ? QStringLiteral("S") + it->data(0, IdRole).toString()
                                        : it->data(0, IdRole).toString();
        m_editKey.clear();
        showCurrent();
    });

    m_count = new QLabel(this);
    m_count->setForegroundRole(QPalette::PlaceholderText);

    auto *left = new QWidget(this);
    auto *leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(m_search, 1);
    filterRow->addWidget(m_showBox);
    leftLay->addLayout(filterRow);
    leftLay->addWidget(m_tree, 1);
    leftLay->addWidget(m_count);

    // ---- the clause --------------------------------------------------------------------------
    m_pages = new QStackedWidget(this);
    m_hint = new QLabel(tr("Select a clause to read and edit it."), this);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setWordWrap(true);
    m_pages->addWidget(m_hint);

    auto *clausePage = new QWidget(this);
    auto *cl = new QVBoxLayout(clausePage);
    cl->setContentsMargins(0, 0, 0, 0);
    m_idEdit = new QLineEdit(this);
    m_idEdit->setMaximumWidth(fontMetrics().horizontalAdvance(QStringLiteral("888.8888.8888")) + 16);
    m_idEdit->setToolTip(tr("The clause number. Change it to move the clause (its sub-clauses move with it); "
                            "Enter to apply"));
    m_idEdit->setAccessibleName(tr("Clause number"));
    QFont big = m_idEdit->font();
    big.setBold(true);
    m_idEdit->setFont(big);
    connect(m_idEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_filling || indexOf(m_cat, m_current) < 0) return;
        const QString to = m_idEdit->text().trimmed();
        if (to != m_current && !renameCurrent(to)) m_idEdit->setText(m_current);
    });
    m_heading = new QLabel(this);
    m_heading->setWordWrap(true);
    m_heading->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *idRow = new QHBoxLayout;
    idRow->addWidget(m_idEdit);
    idRow->addWidget(m_heading, 1);
    m_origin = new QLabel(this);
    m_origin->setWordWrap(true);
    m_figure = new QLabel(this);
    m_figure->setWordWrap(true);
    m_revert = new QPushButton(tr("Revert to the SIF"), this);
    m_revert->setToolTip(tr("Put back the text the SIF prints for this clause (Ctrl+Z undoes it)"));
    connect(m_revert, &QPushButton::clicked, this, [this]() { revertCurrent(); });
    m_restore = new QPushButton(tr("Restore"), this);
    m_restore->setToolTip(tr("Put this clause back in the catalog, as the SIF prints it"));
    connect(m_restore, &QPushButton::clicked, this, [this]() { restoreCurrent(); });
    m_matrix = new QPushButton(tr("Make presets\u2026"), this);
    m_matrix->setToolTip(tr("One Packet Maker preset per operation of this case, with the value the SIF names, and "
                            "a sequence to send them in order"));
    connect(m_matrix, &QPushButton::clicked, this, [this]() { openMatrix(); });
    m_allFields = new QCheckBox(tr("Every field"), this);
    m_allFields->setToolTip(tr("Show the fields this clause leaves empty too (purpose, methodology, set-up, "
                               "expected result, observed column) to fill them in"));
    connect(m_allFields, &QCheckBox::toggled, this, [this]() { showCurrent(); });
    auto *originRow = new QHBoxLayout;
    originRow->addWidget(m_origin, 1);
    originRow->addWidget(m_matrix);
    originRow->addWidget(m_allFields);
    originRow->addWidget(m_revert);
    originRow->addWidget(m_restore);

    auto *fields = new QWidget(this);
    auto *fl = new QVBoxLayout(fields);
    fl->setContentsMargins(0, 0, 4, 0);
    const QString fieldTitles[FieldCount] = { tr("Test scenario"), tr("Purpose"), tr("Methodology"), tr("Set-up"),
                                              tr("Input specification"), tr("Expected output / values"),
                                              tr("Expected result"), tr("Observed column (as printed)"),
                                              tr("Notes (yours, not the SIF's)") };
    for (int f = 0; f < FieldCount; ++f) {
        m_label[f] = new QLabel(fieldTitles[f], fields);
        QFont bold = m_label[f]->font();
        bold.setBold(true);
        m_label[f]->setFont(bold);
        m_edit[f] = new QPlainTextEdit(fields);
        m_edit[f]->setObjectName(QLatin1String(kFieldNames[f]));
        m_edit[f]->setAccessibleName(fieldTitles[f]);
        m_edit[f]->setTabChangesFocus(true);
        m_edit[f]->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_edit[f]->viewport()->installEventFilter(this);
        m_label[f]->setBuddy(m_edit[f]);
        fl->addWidget(m_label[f]);
        fl->addWidget(m_edit[f]);
        connect(m_edit[f], &QPlainTextEdit::textChanged, this, [this, f]() {
            fit(m_edit[f]);
            if (!m_filling) setField(Field(f), m_edit[f]->toPlainText());
        });
    }
    m_edit[Notes]->setPlaceholderText(tr("What you found, what to set up, which preset or tag route runs it…"));
    fl->addStretch(1);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(fields);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    cl->addLayout(idRow);
    cl->addLayout(originRow);
    cl->addWidget(m_figure);
    cl->addWidget(buildResultCard());
    cl->addWidget(m_scroll, 1);
    m_pages->addWidget(clausePage);

    // ---- a section ---------------------------------------------------------------------------
    auto *secPage = new QWidget(this);
    auto *sl = new QVBoxLayout(secPage);
    sl->setContentsMargins(0, 0, 0, 0);
    m_secHeading = new QLabel(this);
    m_secHeading->setFont(big);
    m_secHeading->setWordWrap(true);
    m_secCode = new QLineEdit(this);
    m_secTitle = new QLineEdit(this);
    m_secDesc = new QPlainTextEdit(this);
    m_secNotes = new QPlainTextEdit(this);
    m_secNotes->setToolTip(tr("What the SIF prints under the section's heading: section 22's M1-M6 / S1-S3 legend, "
                              "section 33's speed generator rules"));
    auto *sf = new QFormLayout;
    sf->addRow(tr("Test case ID:"), m_secCode);
    sf->addRow(tr("Title:"), m_secTitle);
    sf->addRow(tr("Description:"), m_secDesc);
    sf->addRow(tr("Notes under the heading:"), m_secNotes);
    sl->addWidget(m_secHeading);
    sl->addLayout(sf, 1);
    m_pages->addWidget(secPage);
    auto sectionEdited = [this]() {
        if (m_filling || !m_current.startsWith(QLatin1Char('S'))) return;
        const int i = sectionIndex(m_cat, m_current.mid(1).toInt());
        if (i < 0) return;
        if (m_editKey != m_current) {
            pushUndo(tr("edit section %1").arg(m_current.mid(1)));
            m_editKey = m_current;
        }
        Section &s = m_cat.sections[i];
        s.code = m_secCode->text();
        s.title = m_secTitle->text();
        s.description = m_secDesc->toPlainText();
        s.notes = m_secNotes->toPlainText();
        setModified(true);
        if (QTreeWidgetItem *it = m_items.value(m_current)) it->setText(0, QStringLiteral("%1  %2").arg(s.no).arg(s.title));
    };
    connect(m_secCode, &QLineEdit::textEdited, this, sectionEdited);
    connect(m_secTitle, &QLineEdit::textEdited, this, sectionEdited);
    connect(m_secDesc, &QPlainTextEdit::textChanged, this, sectionEdited);
    connect(m_secNotes, &QPlainTextEdit::textChanged, this, sectionEdited);

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(left);
    split->addWidget(m_pages);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setSizes({ 520, 680 });
    split->setChildrenCollapsible(false);

    m_status = new StatusLine(this);
    auto *root = new QVBoxLayout(this);
    root->addLayout(bar);
    buildRunBar(root);
    root->addWidget(split, 1);
    root->addWidget(m_status);

    UiColor::onThemeChange(this, [this]() {
        for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) updateItem(it.key());
        updateResultItems();
        restyleVerdicts();
        updateRunBar();
        showCurrent();
    });
    restyleVerdicts();
    updateRunBar();
    updateTitle();
}

// ---- files ------------------------------------------------------------------------------------

bool TestCatalogWindow::reopenLast()
{
    const QString last = Settings::testCatalogLastFile();
    if (!(!last.isEmpty() && QFileInfo::exists(last) && loadFile(last))) loadBuiltin();
    const QString run = Settings::testRunLastFile();
    if (!run.isEmpty() && QFileInfo::exists(run)) openRun(run);
    return true;
}

bool TestCatalogWindow::loadFile(const QString &path)
{
    Catalog c;
    QString err;
    if (!load(path, &c, &err)) {
        m_status->fail(tr("Could not open %1: %2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    m_cat = c;
    m_path = path;
    m_undo->clear();
    setModified(false);
    if (!path.startsWith(QLatin1Char(':'))) Settings::setTestCatalogLastFile(path);
    m_current.clear();
    rebuildTree();
    m_status->clear();
    emit fileChanged(path);
    return true;
}

void TestCatalogWindow::loadBuiltin()
{
    if (loadFile(builtinPath())) Settings::setTestCatalogLastFile(QString());
}

bool TestCatalogWindow::saveFile(const QString &path)
{
    if (path.startsWith(QLatin1Char(':'))) return false;
    QString err;
    if (!save(path, m_cat, &err)) {
        m_status->fail(tr("Could not save %1: %2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    m_path = path;
    Settings::setTestCatalogLastFile(path);
    setModified(false);
    m_status->ok(tr("Saved %1 clauses to %2").arg(m_cat.cases.size()).arg(QDir::toNativeSeparators(path)));
    emit fileChanged(path);
    return true;
}

bool TestCatalogWindow::confirmDiscard()
{
    if (!m_modified) return true;
    return QMessageBox::question(this, tr("Test Case Catalog"),
                                 tr("The catalog has changes that are not saved. Discard them?"),
                                 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel)
           == QMessageBox::Discard;
}

void TestCatalogWindow::closeEvent(QCloseEvent *e)
{
    saveRunNow();                    // results are never left unsaved
    if (confirmDiscard()) e->accept();
    else e->ignore();
}

void TestCatalogWindow::setModified(bool m)
{
    m_modified = m;
    updateTitle();
}

void TestCatalogWindow::updateTitle()
{
    const QString name = m_path.isEmpty() || m_path.startsWith(QLatin1Char(':'))
                             ? tr("SIF 0533 v4.22, as built in")
                             : QFileInfo(m_path).fileName();
    setWindowTitle(tr("Test Case Catalog — %1%2").arg(name, m_modified ? QStringLiteral(" *") : QString()));
}

// ---- the list ---------------------------------------------------------------------------------

void TestCatalogWindow::setSearch(const QString &text)
{
    m_search->setText(text);
}

void TestCatalogWindow::setShow(Show s)
{
    m_showBox->setCurrentIndex(m_showBox->findData(s));
}

bool TestCatalogWindow::matches(const Case &k) const
{
    const QStringList words = m_search->text().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) return true;
    const QString text = searchText(k);
    for (QString w : words) {
        if (idLike(w)) {
            if (w.endsWith(QLatin1Char('.'))) w.chop(1);
            if (k.id != w && !k.id.startsWith(w + QLatin1Char('.'))) return false;
        } else if (!text.contains(w, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

QString TestCatalogWindow::itemText(const Case &k) const
{
    for (const QString *t : { &k.title, &k.purpose, &k.input, &k.expected, &k.notes }) {
        const QString line = t->section(QLatin1Char('\n'), 0, 0).trimmed();
        if (!line.isEmpty()) return line;
    }
    return tr("(empty)");
}

void TestCatalogWindow::updateItem(const QString &id)
{
    QTreeWidgetItem *it = m_items.value(id);
    if (!it || it->data(0, KindRole).toInt() == SectionItem) return;
    const int i = indexOf(m_cat, id);
    if (it->data(0, KindRole).toInt() == DeletedItem || i < 0) {
        it->setText(2, tr("deleted"));
        it->setForeground(2, UiColor::muted());
        return;
    }
    const Case &k = m_cat.cases.at(i);
    it->setText(0, k.id);
    it->setText(1, itemText(k));
    it->setToolTip(1, k.title.isEmpty() ? QString() : k.title.left(600));
    const Origin o = originOf(k);
    it->setText(2, o == Origin::Changed ? tr("changed") : o == Origin::Added ? tr("added") : QString());
    it->setForeground(2, o == Origin::Changed ? UiColor::warning() : UiColor::accent());
}

void TestCatalogWindow::rebuildTree()
{
    const QString keep = m_current;
    m_tree->setUpdatesEnabled(false);
    m_tree->blockSignals(true);
    m_tree->clear();
    m_items.clear();
    m_shown = 0;
    const bool filtering = !m_search->text().trimmed().isEmpty() || m_show != ShowAll;

    QVector<Case> list;
    if (m_show == ShowDeleted) {
        list = deletedFromSif(m_cat);
    } else {
        for (const Case &k : m_cat.cases) {
            const Origin o = originOf(k);
            if ((m_show == ShowChanged && o != Origin::Changed) || (m_show == ShowAdded && o != Origin::Added)
                || (m_show == ShowFigures && k.figurePages.isEmpty()))
                continue;
            const TestRun::Verdict v = TestRun::resultOf(m_run, k.id).verdict;
            if ((m_show == ShowFailed && v != TestRun::Verdict::Fail) || (m_show == ShowPassed && v != TestRun::Verdict::Pass)
                || (m_show == ShowUntested && v != TestRun::Verdict::NotTested))
                continue;
            list << k;
        }
    }
    const Catalog &secSource = m_show == ShowDeleted ? builtin() : m_cat;
    QHash<int, QTreeWidgetItem *> secItems;
    QHash<int, int> secCounts;
    auto sectionItem = [&](int no) {
        if (QTreeWidgetItem *s = secItems.value(no)) return s;
        auto *s = new QTreeWidgetItem(m_tree);
        const Section *sec = section(secSource, no);
        s->setText(0, sec ? QStringLiteral("%1  %2").arg(no).arg(sec->title) : tr("%1  (no section heading)").arg(no));
        s->setFirstColumnSpanned(true);
        QFont f = s->font(0);
        f.setBold(true);
        s->setFont(0, f);
        s->setData(0, KindRole, SectionItem);
        s->setData(0, IdRole, QString::number(no));
        if (sec) s->setToolTip(0, sec->code + QStringLiteral(": ") + sec->description.left(400));
        secItems.insert(no, s);
        m_items.insert(QStringLiteral("S%1").arg(no), s);
        return s;
    };
    // sections with no clause shown still appear when nothing filters them
    if (!filtering)
        for (const Section &s : m_cat.sections) sectionItem(s.no);
    for (const Case &k : list) {
        if (!matches(k)) continue;
        QTreeWidgetItem *parent = nullptr;
        const QString p = parentOf(k.id);
        if (!p.isEmpty()) parent = m_items.value(p);
        if (!parent) parent = sectionItem(sectionOf(k.id));
        auto *it = new QTreeWidgetItem(parent);
        it->setData(0, KindRole, m_show == ShowDeleted ? DeletedItem : ClauseItem);
        it->setData(0, IdRole, k.id);
        it->setText(0, k.id);
        it->setText(1, itemText(k));
        m_items.insert(k.id, it);
        updateItem(k.id);
        ++m_shown;
        secCounts[sectionOf(k.id)]++;
    }
    for (auto it = secItems.cbegin(); it != secItems.cend(); ++it) {
        const Section *sec = section(secSource, it.key());
        it.value()->setText(0, QStringLiteral("%1  %2  (%3)").arg(it.key())
                                   .arg(sec ? sec->title : tr("no section heading"))
                                   .arg(secCounts.value(it.key())));
    }
    if (filtering) m_tree->expandAll();
    updateResultItems();
    m_tree->blockSignals(false);
    m_tree->setUpdatesEnabled(true);
    updateCount();
    if (!keep.isEmpty() && m_items.contains(keep)) {
        select(keep.startsWith(QLatin1Char('S')) ? keep.mid(1) : keep);
    } else {
        m_current.clear();
        showCurrent();
    }
}

void TestCatalogWindow::updateCount()
{
    const QString all = tr("%1 clauses in %2 sections").arg(m_cat.cases.size()).arg(m_cat.sections.size());
    const bool filtering = !m_search->text().trimmed().isEmpty() || m_show != ShowAll;
    m_count->setText(filtering ? tr("%1 shown · %2").arg(m_shown).arg(all) : all);
}

bool TestCatalogWindow::select(const QString &id)
{
    const bool isSection = !id.contains(QLatin1Char('.'));
    QTreeWidgetItem *it = m_items.value(isSection ? QStringLiteral("S") + id : id);
    if (!it) return false;
    for (QTreeWidgetItem *p = it->parent(); p; p = p->parent()) p->setExpanded(true);
    m_tree->setCurrentItem(it);
    m_tree->scrollToItem(it);
    m_current = isSection ? QStringLiteral("S") + id : id;
    showCurrent();
    return true;
}

QList<QTreeWidgetItem *> TestCatalogWindow::selectedItems() const
{
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty() && m_tree->currentItem()) sel << m_tree->currentItem();
    return sel;
}

bool TestCatalogWindow::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_tree && e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_Delete || (ke->key() == Qt::Key_Backspace && ke->modifiers() & Qt::ControlModifier)) {
            deleteSelected();
            return true;
        }
    }
    if (e->type() == QEvent::Resize) {
        for (QPlainTextEdit *ed : m_edit)
            if (ed && ed->viewport() == o) fit(ed);
    }
    return QWidget::eventFilter(o, e);
}

// ---- the editor -------------------------------------------------------------------------------

void TestCatalogWindow::fit(QPlainTextEdit *e)
{
    // The field grows with its text (2 to 16 lines); past that it scrolls.
    int lines = 0;
    for (QTextBlock b = e->document()->begin(); b.isValid(); b = b.next())
        lines += std::max(1, b.layout() ? b.layout()->lineCount() : 1);
    lines = std::max(2, std::min(16, lines));
    const int h = lines * e->fontMetrics().lineSpacing() + 2 * e->frameWidth()
                  + int(e->document()->documentMargin() * 2) + 4;
    if (e->height() != h) e->setFixedHeight(h);
}

void TestCatalogWindow::showCurrent()
{
    m_dupBtn->setEnabled(indexOf(m_cat, m_current) >= 0);
    m_delBtn->setEnabled(!m_current.isEmpty() && m_show != ShowDeleted);
    if (m_current.isEmpty()) {
        m_pages->setCurrentIndex(0);
        return;
    }
    if (m_current.startsWith(QLatin1Char('S'))) {
        const Catalog &src = m_show == ShowDeleted ? builtin() : m_cat;
        if (const Section *s = section(src, m_current.mid(1).toInt())) showSection(*s);
        return;
    }
    const int i = indexOf(m_cat, m_current);
    if (i >= 0) {
        showClause(m_cat.cases.at(i), false);
        return;
    }
    const int b = indexOf(builtin(), m_current);
    if (b >= 0) showClause(builtin().cases.at(b), true);
    else m_pages->setCurrentIndex(0);
}

void TestCatalogWindow::showSection(const Section &s)
{
    m_filling = true;
    m_secHeading->setText(tr("Section %1%2").arg(s.no).arg(s.page ? tr(" · SIF page %1").arg(s.page) : QString()));
    m_secCode->setText(s.code);
    m_secTitle->setText(s.title);
    m_secDesc->setPlainText(s.description);
    m_secNotes->setPlainText(s.notes);
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_secNotes->setFont(s.notes.contains(QStringLiteral("\n| ")) ? fixed : m_secDesc->font());
    m_filling = false;
    m_pages->setCurrentIndex(2);
}

void TestCatalogWindow::showClause(const Case &k, bool deleted)
{
    m_filling = true;
    m_idEdit->setText(k.id);
    m_idEdit->setReadOnly(deleted);
    const Section *sec = section(deleted ? builtin() : m_cat, sectionOf(k.id));
    QString heading = sec ? tr("Section %1 · %2").arg(sec->no).arg(sec->title) : tr("Section %1").arg(sectionOf(k.id));
    const QString parent = parentOf(k.id);
    if (!parent.isEmpty()) {
        const int p = indexOf(deleted ? builtin() : m_cat, parent);
        if (p >= 0) {
            const Case &pc = (deleted ? builtin() : m_cat).cases.at(p);
            heading += QStringLiteral("\n") + tr("Under %1: %2").arg(parent, itemText(pc));
        }
    }
    m_heading->setText(heading);

    const Origin o = deleted ? Origin::Sif : originOf(k);
    QString origin;
    if (deleted) origin = tr("Deleted from this catalog. The SIF prints it on page %1.").arg(k.page);
    else if (o == Origin::Sif) origin = tr("As the SIF prints it, page %1.").arg(k.page);
    else if (o == Origin::Changed) origin = tr("Changed from the SIF (page %1).").arg(k.page);
    else origin = k.page ? tr("Added here (from page %1 of the SIF).").arg(k.page) : tr("Added here: not in the SIF.");
    if (k.pages.size() > 1) origin += QStringLiteral(" ") + tr("Runs over pages %1.").arg(pageList(k.pages));
    m_origin->setText(origin);
    QPalette op = m_origin->palette();
    op.setColor(QPalette::WindowText, o == Origin::Changed ? UiColor::warning()
                                      : o == Origin::Added ? UiColor::accent()
                                                           : palette().color(QPalette::WindowText));
    m_origin->setPalette(op);
    m_revert->setVisible(!deleted && o == Origin::Changed);
    m_restore->setVisible(deleted);
    m_matrix->setVisible(!deleted && !matrixCase().isEmpty());
    m_allFields->setVisible(!deleted);

    m_figure->setVisible(!k.figurePages.isEmpty());
    m_figure->setText(tr("The SIF draws a figure on page %1 (a layout, diagram or table) that this text "
                         "cannot carry: read it there.").arg(pageList(k.figurePages)));
    QPalette fp = m_figure->palette();
    fp.setColor(QPalette::WindowText, UiColor::muted());
    m_figure->setPalette(fp);

    // A radio packet clause's two sides, as its columns are headed
    m_label[Input]->setText(k.inputLabel.isEmpty() ? tr("Input specification") : k.inputLabel);
    m_label[Expected]->setText(k.expectedLabel.isEmpty() ? tr("Expected output / values") : k.expectedLabel);
    const bool radioCase = !k.purpose.isEmpty() || !k.methodology.isEmpty();
    m_label[Title]->setText(radioCase || TestCatalog::depth(k.id) == 2 ? tr("Title") : tr("Test scenario"));

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QFont normal = font();
    Case copy = k;
    for (int f = 0; f < FieldCount; ++f) {
        const QString &text = fieldRef(copy, Field(f));
        // an operation (31.6.1) has no scenario of its own: its case's title heads it
        const bool core = (f == Title && k.inputLabel.isEmpty()) || f == Input || f == Expected || f == Notes;
        const bool visible = !text.isEmpty() || (core && !radioCase) || (f == Notes && !deleted)
                             || (m_allFields->isChecked() && !deleted);
        m_label[f]->setVisible(visible);
        m_edit[f]->setVisible(visible);
        m_edit[f]->setReadOnly(deleted);
        // Tables kept from the SIF ("| M1 | No change |") line up in a fixed font
        m_edit[f]->setFont(text.contains(QStringLiteral("| ")) && text.contains(QStringLiteral(" |")) ? fixed : normal);
        if (m_edit[f]->toPlainText() != text) m_edit[f]->setPlainText(text);
        fit(m_edit[f]);
    }
    m_filling = false;
    m_pages->setCurrentIndex(1);
    m_resultCard->setEnabled(!deleted);
    showResult();
}

void TestCatalogWindow::pushUndo(const QString &label)
{
    const Catalog snap = m_cat;
    const QString sel = m_current;
    const bool wasModified = m_modified;
    m_undo->push(label, [this, snap, sel, wasModified]() {
        m_cat = snap;
        m_editKey.clear();
        m_current = sel;
        setModified(wasModified);
        rebuildTree();
        return true;
    });
}

void TestCatalogWindow::setField(Field f, const QString &text)
{
    const int i = indexOf(m_cat, m_current);
    if (i < 0) return;
    if (fieldRef(m_cat.cases[i], f) == text) return;
    const QString key = m_current + QLatin1Char('/') + QLatin1String(kFieldNames[f]);
    if (m_editKey != key) {
        pushUndo(tr("edit %1 (%2)").arg(m_current, m_label[f]->text().toLower()));
        m_editKey = key;
    }
    fieldRef(m_cat.cases[i], f) = text;
    setModified(true);
    updateItem(m_current);
    // the origin line follows (as printed / changed)
    const Origin o = originOf(m_cat.cases.at(i));
    m_revert->setVisible(o == Origin::Changed);
    if (m_edit[f]->toPlainText() != text) {
        m_filling = true;
        m_edit[f]->setPlainText(text);
        m_filling = false;
    }
}

QString TestCatalogWindow::field(Field f) const
{
    const int i = indexOf(m_cat, m_current);
    if (i < 0) return QString();
    Case k = m_cat.cases.at(i);
    return fieldRef(k, f);
}

bool TestCatalogWindow::renameCurrent(const QString &to)
{
    const QString from = m_current;
    Catalog next = m_cat;
    QString why;
    if (!rename(&next, from, to, &why)) {
        m_status->fail(tr("%1 not renamed: %2").arg(from, why));
        return false;
    }
    if (sectionIndex(next, sectionOf(to)) < 0)
        m_status->warn(tr("Section %1 has no heading yet: add it with New ▸ Section").arg(sectionOf(to)));
    pushUndo(tr("rename %1 to %2").arg(from, to));
    m_cat = next;
    m_current = to;
    m_editKey.clear();
    setModified(true);
    rebuildTree();
    if (!m_status->text().startsWith(tr("Section"))) m_status->ok(tr("%1 is now %2").arg(from, to));
    return true;
}

QString TestCatalogWindow::addClause()
{
    const int sec = m_current.startsWith(QLatin1Char('S')) ? m_current.mid(1).toInt() : sectionOf(m_current);
    if (sec <= 0) {
        m_status->warn(tr("Select a section or a clause first: the new clause goes in its section"));
        return QString();
    }
    Case k;
    k.id = nextId(m_cat, QString::number(sec));
    pushUndo(tr("add %1").arg(k.id));
    m_cat.cases << k;
    sort(&m_cat);
    setModified(true);
    m_search->blockSignals(true);
    m_search->clear();          // so the new clause is in the list
    m_search->blockSignals(false);
    if (m_show != ShowAll && m_show != ShowAdded) setShow(ShowAll);
    m_current = k.id;
    rebuildTree();
    m_edit[Title]->setFocus();
    m_status->ok(tr("Added %1: write its scenario, input and expected output").arg(k.id));
    return k.id;
}

QString TestCatalogWindow::addSubClause()
{
    if (TestCatalog::depth(m_current) != 1 || indexOf(m_cat, m_current) < 0) {
        m_status->warn(tr("Select a clause (like 31.6) to add a sub-clause under it"));
        return QString();
    }
    Case k;
    k.id = nextId(m_cat, m_current);
    // an operation under a radio case is headed like its siblings
    const QStringList kids = childrenOf(m_cat, m_current);
    if (!kids.isEmpty()) {
        const Case &sib = m_cat.cases.at(indexOf(m_cat, kids.last()));
        k.inputLabel = sib.inputLabel;
        k.expectedLabel = sib.expectedLabel;
    }
    pushUndo(tr("add %1").arg(k.id));
    m_cat.cases << k;
    sort(&m_cat);
    setModified(true);
    m_search->blockSignals(true);
    m_search->clear();
    m_search->blockSignals(false);
    if (m_show != ShowAll && m_show != ShowAdded) setShow(ShowAll);
    m_current = k.id;
    rebuildTree();
    m_edit[Input]->setFocus();
    m_status->ok(tr("Added %1 under %2").arg(k.id, parentOf(k.id)));
    return k.id;
}

bool TestCatalogWindow::addSection(int no, const QString &title)
{
    if (no <= 0 || sectionIndex(m_cat, no) >= 0) {
        m_status->fail(tr("Section %1 is already there").arg(no));
        return false;
    }
    pushUndo(tr("add section %1").arg(no));
    Section s;
    s.no = no;
    s.title = title.trimmed();
    s.code = QStringLiteral("KAVACH_FRS_%1").arg(no, 2, 10, QLatin1Char('0'));
    m_cat.sections << s;
    sort(&m_cat);
    setModified(true);
    m_current = QStringLiteral("S%1").arg(no);
    if (m_show != ShowAll) setShow(ShowAll);
    rebuildTree();
    m_status->ok(tr("Added section %1: New ▸ Clause adds its first clause, %1.1").arg(no));
    return true;
}

QString TestCatalogWindow::duplicateCurrent()
{
    const int i = indexOf(m_cat, m_current);
    if (i < 0) return QString();
    Case k = m_cat.cases.at(i);
    const QString parent = parentOf(k.id);
    const QString from = k.id;
    k.id = nextId(m_cat, parent.isEmpty() ? QString::number(sectionOf(from)) : parent);
    k.pages.clear();
    k.figurePages.clear();
    k.page = 0;                 // a copy is not the SIF's: it shows as added
    k.notes = tr("Copy of %1.").arg(from) + (k.notes.isEmpty() ? QString() : QStringLiteral("\n") + k.notes);
    pushUndo(tr("duplicate %1").arg(from));
    m_cat.cases << k;
    sort(&m_cat);
    setModified(true);
    m_current = k.id;
    if (m_show != ShowAll && m_show != ShowAdded) setShow(ShowAll);
    rebuildTree();
    m_status->ok(tr("%1 is a copy of %2").arg(k.id, from));
    return k.id;
}

int TestCatalogWindow::deleteSelected()
{
    if (m_show == ShowDeleted) return 0;
    QSet<QString> drop;
    QSet<int> sections;
    for (QTreeWidgetItem *it : selectedItems()) {
        const QString id = it->data(0, IdRole).toString();
        if (it->data(0, KindRole).toInt() == SectionItem) sections.insert(id.toInt());
        else drop.insert(id);
    }
    if (!sections.isEmpty()) {
        int n = 0;
        for (const Case &k : m_cat.cases) n += sections.contains(sectionOf(k.id)) ? 1 : 0;
        QStringList names;
        for (int s : sections) names << QString::number(s);
        if (QMessageBox::question(this, tr("Delete sections"),
                                  tr("Delete section %1 and its %2 clauses? Ctrl+Z brings them back.")
                                      .arg(names.join(QStringLiteral(", "))).arg(n),
                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return 0;
    }
    for (const Case &k : m_cat.cases) {
        if (sections.contains(sectionOf(k.id)) || drop.contains(parentOf(k.id))) drop.insert(k.id);
    }
    if (drop.isEmpty() && sections.isEmpty()) return 0;
    QStringList ids = drop.values();
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) { return compareIds(a, b) < 0; });
    const QString label = sections.isEmpty() ? (ids.size() == 1 ? tr("delete %1").arg(ids.first())
                                                                : tr("delete %1 clauses").arg(ids.size()))
                                             : tr("delete %1 section(s)").arg(sections.size());
    pushUndo(label);
    // the clause after the last one deleted is selected next
    QString next;
    for (const Case &k : m_cat.cases)
        if (!drop.contains(k.id) && compareIds(k.id, ids.last()) > 0 && !sections.contains(sectionOf(k.id))) {
            next = k.id;
            break;
        }
    QVector<Case> keep;
    for (const Case &k : m_cat.cases)
        if (!drop.contains(k.id)) keep << k;
    m_cat.cases = keep;
    QVector<Section> keepSec;
    for (const Section &s : m_cat.sections)
        if (!sections.contains(s.no)) keepSec << s;
    m_cat.sections = keepSec;
    setModified(true);
    m_current = next;
    rebuildTree();
    m_status->ok(tr("Deleted %1. Ctrl+Z brings %2 back").arg(ids.size() == 1 ? ids.first() : tr("%1 clauses").arg(ids.size()),
                                                         ids.size() == 1 ? tr("it") : tr("them")));
    return ids.size();
}

bool TestCatalogWindow::revertCurrent()
{
    const int i = indexOf(m_cat, m_current);
    const int b = indexOf(builtin(), m_current);
    if (i < 0 || b < 0 || m_cat.cases.at(i) == builtin().cases.at(b)) return false;
    pushUndo(tr("revert %1 to the SIF").arg(m_current));
    m_cat.cases[i] = builtin().cases.at(b);
    m_editKey.clear();
    setModified(true);
    updateItem(m_current);
    showCurrent();
    m_status->ok(tr("%1 is as the SIF prints it again").arg(m_current));
    return true;
}

bool TestCatalogWindow::restoreCurrent()
{
    const int b = indexOf(builtin(), m_current);
    if (b < 0 || indexOf(m_cat, m_current) >= 0) return false;
    pushUndo(tr("restore %1").arg(m_current));
    m_cat.cases << builtin().cases.at(b);
    if (sectionIndex(m_cat, sectionOf(m_current)) < 0)
        if (const Section *s = section(builtin(), sectionOf(m_current))) m_cat.sections << *s;
    sort(&m_cat);
    setModified(true);
    const QString id = m_current;
    rebuildTree();      // it leaves the deleted list
    m_status->ok(tr("Restored %1").arg(id));
    return true;
}

bool TestCatalogWindow::undo()
{
    return m_undo->undo();
}

// ---- the Packet Matrix (session 232) -----------------------------------------------------------

QString TestCatalogWindow::matrixCase() const
{
    if (indexOf(m_cat, m_current) < 0) return QString();
    if (!childrenOf(m_cat, m_current).isEmpty()) return m_current;
    const QString parent = parentOf(m_current);
    return !parent.isEmpty() && indexOf(m_cat, parent) >= 0 ? parent : QString();
}

PacketMatrixDialog *TestCatalogWindow::openMatrix()
{
    const QString id = matrixCase();
    if (id.isEmpty()) return nullptr;
    QVector<Case> ops;
    for (const QString &k : childrenOf(m_cat, id)) ops << m_cat.cases.at(indexOf(m_cat, k));
    auto *d = new PacketMatrixDialog(m_cat.cases.at(indexOf(m_cat, id)), ops, this);
    connect(d, &PacketMatrixDialog::presetsMade, this, &TestCatalogWindow::notePresets);
    d->show();
    d->raise();
    return d;
}

void TestCatalogWindow::notePresets(const QHash<QString, QString> &files)
{
    bool pushed = false;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const int i = indexOf(m_cat, it.key());
        if (i < 0) continue;
        const QString line = tr("Preset: %1").arg(QDir::toNativeSeparators(it.value()));
        if (m_cat.cases.at(i).notes.split(QLatin1Char('\n')).contains(line)) continue;
        if (!pushed) {
            // the snapshot first: a reference taken into m_cat before it would
            // write into the snapshot's shared copy too
            pushUndo(tr("note %1 presets").arg(files.size()));
            pushed = true;
        }
        QString &notes = m_cat.cases[i].notes;
        notes = notes.isEmpty() ? line : notes + QLatin1Char('\n') + line;
        updateItem(it.key());
    }
    if (!pushed) return;
    m_editKey.clear();
    setModified(true);
    showCurrent();
    m_status->ok(tr("Noted %1 presets in their clauses' notes: Save keeps them").arg(files.size()));
}

// ---- test runs (session 263) -------------------------------------------------------------------

void TestCatalogWindow::buildRunBar(QVBoxLayout *root)
{
    auto *bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("testRunBar"));
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 2, 0, 2);
    auto *label = new QLabel(tr("Test run:"), bar);
    QFont bold = label->font();
    bold.setBold(true);
    label->setFont(bold);
    m_runName = new QLabel(bar);
    m_runName->setObjectName(QStringLiteral("testRunName"));
    m_runName->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_runProgress = new RunProgress(bar);
    m_runProgress->setObjectName(QStringLiteral("testRunProgress"));
    m_runSummary = new QLabel(bar);
    m_runSummary->setObjectName(QStringLiteral("testRunSummary"));
    // The two labels give way rather than widen the window (session231's
    // laptop check failed on Qt 5's font at 1110 px); their text is in the
    // tooltip when cut.
    for (QLabel *l : { m_runName, m_runSummary }) {
        l->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        l->setMinimumWidth(90);           // below its text's width only when the window is narrow
    }
    auto *newRun = new QPushButton(tr("New run…"), bar);
    newRun->setToolTip(tr("Start recording results: the loco, the build, the site, who is testing; then where to keep "
                          "the run's file (results are saved to it as you set them)"));
    auto *openRun = new QPushButton(tr("Open run…"), bar);
    m_reportBtn = new QPushButton(tr("Report…"), bar);
    m_reportBtn->setToolTip(tr("Every clause with its result, the failed ones first: HTML (prints, opens in Word) "
                               "or CSV (Excel)"));
    m_closeRunBtn = new QPushButton(tr("Close run"), bar);
    lay->addWidget(label);
    lay->addWidget(m_runName);
    lay->addSpacing(8);
    lay->addWidget(m_runProgress, 1);
    lay->addWidget(m_runSummary);
    lay->addSpacing(8);
    lay->addWidget(newRun);
    lay->addWidget(openRun);
    lay->addWidget(m_reportBtn);
    lay->addWidget(m_closeRunBtn);
    root->addWidget(bar);

    m_runSave = new QTimer(this);
    m_runSave->setSingleShot(true);
    m_runSave->setInterval(800);
    connect(m_runSave, &QTimer::timeout, this, &TestCatalogWindow::saveRunNow);

    connect(newRun, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("New test run"));
        auto *title = new QLineEdit(&dlg);
        auto *loco = new QLineEdit(&dlg);
        auto *fw = new QLineEdit(&dlg);
        auto *site = new QLineEdit(&dlg);
        auto *testers = new QLineEdit(Settings::testRunTester(), &dlg);
        title->setPlaceholderText(tr("e.g. FRS 4 full supervision, loco 21"));
        loco->setPlaceholderText(tr("loco number / unit id"));
        fw->setPlaceholderText(tr("e.g. LKAVACH v1.2.10"));
        site->setPlaceholderText(tr("bench, station, section"));
        auto *form = new QFormLayout;
        form->addRow(tr("Title:"), title);
        form->addRow(tr("Loco:"), loco);
        form->addRow(tr("Firmware build:"), fw);
        form->addRow(tr("Site:"), site);
        form->addRow(tr("Testers:"), testers);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Choose where to save…"));
        auto *v = new QVBoxLayout(&dlg);
        v->addLayout(form);
        v->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        dlg.resize(460, dlg.sizeHint().height());
        if (dlg.exec() != QDialog::Accepted) return;
        TestRun::Run r;
        r.title = title->text().trimmed();
        r.loco = loco->text().trimmed();
        r.firmware = fw->text().trimmed();
        r.site = site->text().trimmed();
        r.testers = testers->text().trimmed();
        QString base = r.title.isEmpty() ? QStringLiteral("test_run") : r.title;
        base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
        const QString start = QFileInfo(m_runPath.isEmpty() ? QDir::homePath() + QStringLiteral("/x") : m_runPath)
                                  .absolutePath() + QLatin1Char('/') + base + QStringLiteral(".testrun.json");
        const QString path = QFileDialog::getSaveFileName(this, tr("Keep the test run in"), start,
                                                          tr("Test runs (*.testrun.json)"));
        if (!path.isEmpty()) this->newRun(r, path);
    });
    connect(openRun, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open a test run"),
                                                          m_runPath.isEmpty() ? QDir::homePath() : QFileInfo(m_runPath).absolutePath(),
                                                          tr("Test runs (*.testrun.json);;All files (*)"));
        if (!path.isEmpty()) this->openRun(path);
    });
    connect(m_reportBtn, &QPushButton::clicked, this, [this]() {
        QString base = m_run.title.isEmpty() ? QStringLiteral("test_run") : m_run.title;
        base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Test run report"), QFileInfo(m_runPath).absolutePath() + QLatin1Char('/') + base + QStringLiteral("_report.html"),
            tr("HTML report (*.html);;CSV for Excel (*.csv)"));
        if (!path.isEmpty()) exportReport(path);
    });
    connect(m_closeRunBtn, &QPushButton::clicked, this, [this]() { closeRun(); });
}

QFrame *TestCatalogWindow::buildResultCard()
{
    m_resultCard = new QFrame(this);
    m_resultCard->setObjectName(QStringLiteral("testResultCard"));
    m_resultCard->setFrameShape(QFrame::StyledPanel);
    auto *outer = new QVBoxLayout(m_resultCard);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(6);

    m_noRun = new QLabel(tr("To record a result, start a test run (New run… above) or open one."), m_resultCard);
    m_noRun->setObjectName(QStringLiteral("testResultNoRun"));
    m_noRun->setStyleSheet(UiColor::mutedStyle());
    outer->addWidget(m_noRun);

    m_resultBody = new QWidget(m_resultCard);
    auto *body = new QVBoxLayout(m_resultBody);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(6);
    auto *top = new QHBoxLayout;
    auto *title = new QLabel(tr("Result"), m_resultBody);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    QStringList labels;
    for (const char *l : kVerdictLabels) labels << tr(l);
    QWidget *seg = UiStyle::segmented(labels, &m_verdicts, m_resultBody);
    seg->setObjectName(QStringLiteral("testVerdicts"));
    const char *const tips[] = { "The clause behaved as expected (Ctrl+1)", "It did not (Ctrl+2): say what in Remarks",
                                 "Does not apply to this loco or build (Ctrl+3)", "No result (Ctrl+4)" };
    const char *const keys[] = { "Ctrl+1", "Ctrl+2", "Ctrl+3", "Ctrl+4" };
    for (int i = 0; i < 4; ++i) {
        QAbstractButton *b = m_verdicts->button(i);
        b->setObjectName(QStringLiteral("testVerdict%1").arg(i));
        b->setToolTip(tr(tips[i]));
        auto *a = new QAction(this);
        a->setShortcut(QKeySequence(QLatin1String(keys[i])));
        addAction(a);
        connect(a, &QAction::triggered, this, [this, i]() {
            if (m_resultCard->isEnabled() && hasRun() && m_pages->currentIndex() == 1) setVerdict(kVerdictOrder[i]);
        });
    }
    connect(m_verdicts, &QButtonGroup::idClicked, this, [this](int id) { setVerdict(kVerdictOrder[id]); });
    m_when = new QLabel(m_resultBody);
    m_when->setObjectName(QStringLiteral("testResultWhen"));
    m_when->setStyleSheet(UiColor::mutedStyle());
    m_nextUntested = new QPushButton(tr("Next not tested ▸"), m_resultBody);
    m_nextUntested->setObjectName(QStringLiteral("testNextUntested"));
    m_nextUntested->setToolTip(tr("The next clause in the list with no result (Ctrl+.)"));
    auto *nextAct = new QAction(this);
    nextAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+.")));
    addAction(nextAct);
    connect(nextAct, &QAction::triggered, this, [this]() { if (hasRun()) selectNextUntested(); });
    connect(m_nextUntested, &QPushButton::clicked, this, [this]() { selectNextUntested(); });
    top->addWidget(title);
    top->addWidget(seg);
    top->addWidget(m_when, 1);
    top->addWidget(m_nextUntested);
    body->addLayout(top);

    m_tester = new QLineEdit(Settings::testRunTester(), m_resultBody);
    m_tester->setObjectName(QStringLiteral("testTester"));
    m_tester->setPlaceholderText(tr("who sets the result"));
    m_evidence = new QLineEdit(m_resultBody);
    m_evidence->setObjectName(QStringLiteral("testEvidence"));
    m_evidence->setPlaceholderText(tr("the log and time, e.g. loco_1_1_12102026_1354.cap 13:55:36; a clip; a photo"));
    m_remarks = new QPlainTextEdit(m_resultBody);
    m_remarks->setObjectName(QStringLiteral("testRemarks"));
    m_remarks->setPlaceholderText(tr("What was seen: on a Fail, what happened instead"));
    m_remarks->setTabChangesFocus(true);
    m_remarks->setFixedHeight(m_remarks->fontMetrics().lineSpacing() * 3 + 14);
    auto *form = new QFormLayout;
    form->addRow(tr("Tester:"), m_tester);
    form->addRow(tr("Remarks:"), m_remarks);
    form->addRow(tr("Evidence:"), m_evidence);
    body->addLayout(form);
    outer->addWidget(m_resultBody);

    connect(m_tester, &QLineEdit::textEdited, this, [this](const QString &t) { setTester(t); });
    connect(m_evidence, &QLineEdit::textEdited, this, [this](const QString &t) { setEvidence(t); });
    connect(m_remarks, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_filling) setRemarks(m_remarks->toPlainText());
    });
    return m_resultCard;
}

void TestCatalogWindow::restyleVerdicts()
{
    if (!m_verdicts) return;
    // The checked segment in its result's colours (chip fills: text lifted
    // to the contrast floor in every theme). Pass and Fail only: N/A and
    // Not tested keep the plain segment.
    const UiStyle::Tone tones[] = { UiStyle::Tone::Ok, UiStyle::Tone::Fail };
    for (int i = 0; i < 2; ++i) {
        QAbstractButton *b = m_verdicts->button(i);
        b->setStyleSheet(QStringLiteral("QPushButton:checked { background:%1; color:%2; border:1px solid %2; font-weight:600; }")
                             .arg(UiStyle::chipFill(tones[i]).name(), UiStyle::chipText(tones[i]).name()));
    }
}

TestRun::Counts TestCatalogWindow::runCounts() const
{
    QStringList ids;
    for (const Case &k : m_cat.cases) ids << k.id;
    return TestRun::count(m_run, ids);
}

void TestCatalogWindow::updateRunBar()
{
    if (!m_runName) return;
    const bool open = hasRun();
    m_reportBtn->setEnabled(open);
    m_closeRunBtn->setEnabled(open);
    m_runProgress->setVisible(open);
    if (!open) {
        m_runName->setText(tr("none open"));
        m_runName->setStyleSheet(UiColor::mutedStyle());
        m_runSummary->setText(tr("results are recorded in a test run: New run… or Open run…"));
        m_runSummary->setToolTip(m_runSummary->text());
        m_runSummary->setStyleSheet(UiColor::mutedStyle());
        return;
    }
    QStringList what;
    for (const QString &s : { m_run.title, m_run.loco.isEmpty() ? QString() : tr("loco %1").arg(m_run.loco), m_run.firmware })
        if (!s.isEmpty()) what << s;
    m_runName->setText(what.isEmpty() ? QFileInfo(m_runPath).fileName() : what.join(QStringLiteral(" · ")));
    m_runName->setStyleSheet(QString());
    m_runName->setToolTip(QDir::toNativeSeparators(m_runPath));
    const TestRun::Counts c = runCounts();
    static_cast<RunProgress *>(m_runProgress)->setCounts(c);
    QString text = tr("<span style=\"%1\">%2 Pass</span> · <span style=\"%3\">%4 Fail</span> · %5 N/A · %6 not tested")
                       .arg(UiColor::style(UiColor::ok())).arg(c.pass).arg(UiColor::style(UiColor::error())).arg(c.fail)
                       .arg(c.na).arg(c.notTested);
    QStringList ids;
    for (const Case &k : m_cat.cases) ids << k.id;
    const int lost = TestRun::orphans(m_run, ids).size();
    if (lost) text += tr(" · <span style=\"%1\">%2 for clauses not in this catalog</span>").arg(UiColor::style(UiColor::warning())).arg(lost);
    m_runSummary->setText(text);
    m_runSummary->setStyleSheet(QString());
    m_runSummary->setToolTip(tr("%1 of %2 clauses have a result").arg(c.done()).arg(c.total()));
}

void TestCatalogWindow::updateResultItems()
{
    QHash<int, QPair<int, int>> perSection;           // done, total
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        QTreeWidgetItem *item = it.value();
        if (item->data(0, KindRole).toInt() == SectionItem) continue;
        if (item->data(0, KindRole).toInt() == DeletedItem) { item->setText(3, QString()); continue; }
        const TestRun::Verdict v = TestRun::resultOf(m_run, it.key()).verdict;
        item->setText(3, verdictCell(v));
        item->setForeground(3, v == TestRun::Verdict::Pass ? UiColor::ok()
                               : v == TestRun::Verdict::Fail ? UiColor::error() : UiColor::muted());
        QPair<int, int> &s = perSection[sectionOf(it.key())];
        s.second++;
        if (v != TestRun::Verdict::NotTested) s.first++;
    }
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        QTreeWidgetItem *item = it.value();
        if (item->data(0, KindRole).toInt() != SectionItem) continue;
        // A section row spans the columns (its title is long): its progress
        // is in its tooltip, after the section's own description.
        const QPair<int, int> s = perSection.value(item->data(0, IdRole).toString().toInt());
        QString tip = item->toolTip(0).section(QStringLiteral("\n\n"), 0, 0);
        if (hasRun() && s.second) tip += (tip.isEmpty() ? QString() : QStringLiteral("\n\n"))
                                         + tr("This run: %1 of %2 shown clauses have a result").arg(s.first).arg(s.second);
        item->setToolTip(0, tip);
    }
}

void TestCatalogWindow::showResult()
{
    if (!m_resultCard) return;
    const bool open = hasRun();
    m_noRun->setVisible(!open);
    m_resultBody->setVisible(open);
    if (!open) return;
    const TestRun::Result r = currentResult();
    m_filling = true;
    if (QAbstractButton *b = m_verdicts->button(verdictButton(r.verdict))) b->setChecked(true);
    if (m_remarks->toPlainText() != r.remarks) m_remarks->setPlainText(r.remarks);
    if (m_evidence->text() != r.evidence) m_evidence->setText(r.evidence);
    m_filling = false;
    m_when->setText(r.verdict == TestRun::Verdict::NotTested
                        ? tr("not tested yet")
                        : tr("set by %1, %2").arg(r.tester.isEmpty() ? tr("(no name)") : r.tester,
                                                  r.when.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
}

TestRun::Result TestCatalogWindow::currentResult() const
{
    if (m_current.isEmpty() || m_current.startsWith(QLatin1Char('S'))) return {};
    return TestRun::resultOf(m_run, m_current);
}

void TestCatalogWindow::setVerdict(TestRun::Verdict v)
{
    if (!hasRun() || m_current.isEmpty() || m_current.startsWith(QLatin1Char('S')) || indexOf(m_cat, m_current) < 0) return;
    TestRun::Result r = TestRun::resultOf(m_run, m_current);
    r.verdict = v;
    r.tester = m_tester->text().trimmed();
    r.when = QDateTime::currentDateTime();
    TestRun::setResult(&m_run, m_current, r);
    if (!r.tester.isEmpty()) Settings::setTestRunTester(r.tester);
    runChanged();
    updateResultItems();
    showResult();
    if (v != TestRun::Verdict::NotTested && r.tester.isEmpty())
        m_status->warn(tr("%1: %2, with no tester's name. Fill in Tester: the result is theirs").arg(m_current, TestRun::verdictName(v)));
    else
        m_status->ok(tr("%1: %2").arg(m_current, TestRun::verdictName(v)));
    // A filter by result that no longer holds this clause keeps it on screen
    // until the list is rebuilt: the operator sees what they just set.
}

void TestCatalogWindow::setTester(const QString &name)
{
    // Who is at the bench now: the name each result set from here on is
    // recorded under. It never rewrites who set a result already.
    if (m_tester->text() != name) m_tester->setText(name);
    Settings::setTestRunTester(name.trimmed());
}

void TestCatalogWindow::setRemarks(const QString &text)
{
    if (m_remarks->toPlainText() != text) {
        m_filling = true;
        m_remarks->setPlainText(text);
        m_filling = false;
    }
    if (!hasRun() || m_current.isEmpty() || m_current.startsWith(QLatin1Char('S')) || indexOf(m_cat, m_current) < 0) return;
    TestRun::Result r = TestRun::resultOf(m_run, m_current);
    if (r.remarks == text) return;
    r.remarks = text;
    TestRun::setResult(&m_run, m_current, r);
    runChanged(true);
}

void TestCatalogWindow::setEvidence(const QString &text)
{
    if (m_evidence->text() != text) m_evidence->setText(text);
    if (!hasRun() || m_current.isEmpty() || m_current.startsWith(QLatin1Char('S')) || indexOf(m_cat, m_current) < 0) return;
    TestRun::Result r = TestRun::resultOf(m_run, m_current);
    if (r.evidence == text) return;
    r.evidence = text;
    TestRun::setResult(&m_run, m_current, r);
    runChanged(true);
}

void TestCatalogWindow::runChanged(bool soon)
{
    m_runDirty = true;
    updateRunBar();
    if (soon) m_runSave->start();
    else saveRunNow();
}

void TestCatalogWindow::saveRunNow()
{
    if (m_runSave) m_runSave->stop();
    if (!m_runDirty || !hasRun()) return;
    QString err;
    if (!TestRun::save(m_runPath, m_run, &err)) {
        m_status->fail(tr("The test run was NOT saved to %1: %2").arg(QFileInfo(m_runPath).fileName(), err));
        return;
    }
    m_runDirty = false;
}

bool TestCatalogWindow::newRun(const TestRun::Run &details, const QString &path)
{
    saveRunNow();
    TestRun::Run r = details;
    r.results.clear();
    r.created = QDateTime::currentDateTime();
    r.catalog = m_path.isEmpty() || m_path.startsWith(QLatin1Char(':')) ? QStringLiteral("SIF 0533 v4.22, as built in")
                                                                        : QFileInfo(m_path).fileName();
    QString err;
    if (!TestRun::save(path, r, &err)) {
        m_status->fail(tr("No run started: %1").arg(err));
        return false;
    }
    m_run = r;
    m_runPath = path;
    m_runDirty = false;
    Settings::setTestRunLastFile(path);
    if (!r.testers.isEmpty() && m_tester->text().isEmpty()) m_tester->setText(r.testers.section(QLatin1Char(','), 0, 0).trimmed());
    updateRunBar();
    updateResultItems();
    showResult();
    m_status->ok(tr("Test run started: %1. Results are saved to it as you set them").arg(QFileInfo(path).fileName()));
    return true;
}

bool TestCatalogWindow::openRun(const QString &path)
{
    TestRun::Run r;
    QString err;
    if (!TestRun::load(path, &r, &err)) {
        m_status->fail(tr("Not opened: %1: %2").arg(QFileInfo(path).fileName(), err));
        return false;
    }
    saveRunNow();
    m_run = r;
    m_runPath = path;
    m_runDirty = false;
    Settings::setTestRunLastFile(path);
    updateRunBar();
    if (m_show == ShowFailed || m_show == ShowPassed || m_show == ShowUntested) rebuildTree();
    else updateResultItems();
    showResult();
    const TestRun::Counts c = runCounts();
    m_status->ok(tr("Opened %1: %2 of %3 clauses have a result").arg(QFileInfo(path).fileName()).arg(c.done()).arg(c.total()));
    return true;
}

void TestCatalogWindow::closeRun()
{
    saveRunNow();
    m_run = TestRun::Run();
    m_runPath.clear();
    Settings::setTestRunLastFile(QString());
    updateRunBar();
    if (m_show == ShowFailed || m_show == ShowPassed || m_show == ShowUntested) rebuildTree();
    else updateResultItems();
    showResult();
}

bool TestCatalogWindow::selectNextUntested()
{
    // The list's order: walk the tree as shown.
    QStringList order;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
        if (it->data(0, KindRole).toInt() == ClauseItem) order << it->data(0, IdRole).toString();
        for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) walk(m_tree->topLevelItem(i));
    const int from = order.indexOf(m_current);
    for (int step = 1; step <= order.size(); ++step) {
        const QString id = order.at((qMax(from, -1) + step + order.size()) % order.size());
        if (TestRun::resultOf(m_run, id).verdict == TestRun::Verdict::NotTested) {
            select(id);
            return true;
        }
    }
    m_status->ok(tr("Every clause shown has a result"));
    return false;
}

bool TestCatalogWindow::exportReport(const QString &path)
{
    saveRunNow();
    if (!hasRun()) {
        m_status->warn(tr("No test run open"));
        return false;
    }
    const bool csv = path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive);
    const QByteArray data = (csv ? TestRun::toCsv(m_run, m_cat) : TestRun::toHtml(m_run, m_cat)).toUtf8();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        m_status->fail(tr("Report not written: %1").arg(f.errorString()));
        return false;
    }
    const TestRun::Counts c = runCounts();
    m_status->ok(tr("Wrote %1: %2 Pass, %3 Fail, %4 N/A, %5 not tested")
                     .arg(QFileInfo(path).fileName()).arg(c.pass).arg(c.fail).arg(c.na).arg(c.notTested));
    return true;
}
