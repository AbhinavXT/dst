#include "tagbuilderwindow.h"

#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QCloseEvent>
#include <QComboBox>
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
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

const int kTypes[] = { 9, 10, 11, 12 };

QString hex8(quint32 v) { return QStringLiteral("%1").arg(v, 8, 16, QLatin1Char('0')).toUpper(); }

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

TagBuilderWindow::TagBuilderWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
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
    auto *newBtn = new QPushButton(tr("New"), this);
    auto *openBtn = new QPushButton(tr("Open…"), this);
    openBtn->setToolTip(tr("A DLConsole route (.tagroute.xml), a tags_sim route.xml, or a Configuration1.xml "
                           "(pick one of its routes). tags_sim .xlsx routes: convert them with "
                           "scripts/tags_sim_import.py"));
    auto *saveBtn = new QPushButton(tr("Save…"), this);
    saveBtn->setToolTip(tr("The route as a DLConsole route file: tags, signals, name and direction"));
    auto *exportBtn = new QPushButton(tr("Export route.xml…"), this);
    exportBtn->setToolTip(tr("tags_sim's route.xml, as the RFID simulator's Configuration1.xml holds it: "
                             "the route and its REV route"));
    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Route:"), this));
    bar->addWidget(m_routeName, 1);
    bar->addWidget(m_dir);
    bar->addSpacing(12);
    bar->addWidget(newBtn);
    bar->addWidget(openBtn);
    bar->addWidget(saveBtn);
    bar->addWidget(exportBtn);

    // ---- route table -------------------------------------------------------------------------
    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({ tr("#"), tr("Tag"), tr("CRC-30"), tr("Type"), tr("Abs loc (m)"),
                                         tr("TIN nom / rev"), tr("Placement"), tr("page_x  page_y") });
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
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
    auto *rowBtns = new QHBoxLayout;
    for (QPushButton *b : { addBtn, insBtn, repBtn, delBtn, upBtn, downBtn }) rowBtns->addWidget(b);
    rowBtns->addStretch(1);

    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_table, 1);
    leftLayout->addLayout(rowBtns);

    // ---- tag editor --------------------------------------------------------------------------
    auto *editor = new QGroupBox(tr("Tag"), this);
    m_type = new QComboBox(editor);
    for (int t : kTypes) m_type->addItem(QStringLiteral("%1  %2").arg(t).arg(RfidTag::typeName(t)), t);
    m_type->setToolTip(tr("Tag type 10 (LC gate) has never been seen in a capture: its layout is the "
                          "specification's, not checked against a real tag"));
    const QRegularExpression hex(QStringLiteral("[0-9A-Fa-f]{0,16}"));
    m_pageX = new QLineEdit(editor);
    m_pageY = new QLineEdit(editor);
    for (QLineEdit *e : { m_pageX, m_pageY }) {
        e->setValidator(new QRegularExpressionValidator(hex, e));
        e->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        e->setToolTip(tr("16 hex digits, as tags_sim and the simulator write them. Paste a tag here to decode it"));
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
        if (m_updating) return;
        m_route.dir = m_dir->currentData().toInt();
        setModified(true);
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = currentRow();
        if (row >= 0 && row < m_route.tags.size()) setTag(m_route.tags.at(row).bytes);
    });
    connect(addBtn, &QPushButton::clicked, this, &TagBuilderWindow::addTag);
    connect(insBtn, &QPushButton::clicked, this, &TagBuilderWindow::insertTag);
    connect(repBtn, &QPushButton::clicked, this, &TagBuilderWindow::replaceTag);
    connect(delBtn, &QPushButton::clicked, this, &TagBuilderWindow::deleteTag);
    connect(upBtn, &QPushButton::clicked, this, [this]() { moveTag(-1); });
    connect(downBtn, &QPushButton::clicked, this, [this]() { moveTag(+1); });
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
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export tags_sim route.xml"),
                                                          QStringLiteral("route.xml"), tr("XML (*.xml)"));
        if (!path.isEmpty()) exportRouteXml(path);
    });

    setTag(blankTag(9));
    fillTable();
    m_status->state(tr("A new route. Build a tag on the right and Add it, or Open a route"));
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

void TagBuilderWindow::showTag()
{
    m_updating = true;
    const RfidTag::Summary s = RfidTag::summary(m_tag);
    const int ti = m_type->findData(s.type);
    m_type->setCurrentIndex(ti);
    rebuildForm();
    fillForm();
    if (m_pageX->text().compare(RfidTag::pageX(m_tag), Qt::CaseInsensitive) != 0) m_pageX->setText(RfidTag::pageX(m_tag));
    if (m_pageY->text().compare(RfidTag::pageY(m_tag), Qt::CaseInsensitive) != 0) m_pageY->setText(RfidTag::pageY(m_tag));
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
    m_pageX->setText(px);
    m_pageY->setText(py);
    pagesEdited();
}

void TagBuilderWindow::pagesEdited()
{
    QString err;
    const QByteArray t = RfidTag::fromPages(m_pageX->text(), m_pageY->text(), &err);
    if (t.isEmpty()) {
        m_status->warn(err);
        return;
    }
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
    m_table->setRowCount(m_route.tags.size());
    for (int i = 0; i < m_route.tags.size(); ++i) {
        const QByteArray &b = m_route.tags.at(i).bytes;
        const RfidTag::Summary s = RfidTag::summary(b);
        m_table->setItem(i, 0, cell(QString::number(i + 1)));
        QTableWidgetItem *name = cell(RfidTag::nameOf(b));
        if (m_route.tags.at(i).name != name->text() && !m_route.tags.at(i).name.isEmpty())
            name->setToolTip(tr("The file named it %1").arg(m_route.tags.at(i).name));
        m_table->setItem(i, 1, name);
        m_table->setItem(i, 3, cell(QStringLiteral("%1 %2").arg(s.type).arg(RfidTag::typeName(s.type))));
        m_table->setItem(i, 4, cell(QString::number(s.absLoc)));
        m_table->setItem(i, 5, cell(QStringLiteral("%1 / %2").arg(s.tinNom).arg(s.tinRev)));
        m_table->setItem(i, 6, cell(s.type == 9 || s.type == 10 ? QString::number(s.placement) : QString()));
        QTableWidgetItem *crc = cell(s.crcOk ? tr("pass") : tr("FAIL"));
        crc->setForeground(s.crcOk ? UiColor::ok() : UiColor::error());
        if (!s.crcOk) crc->setToolTip(tr("Stored %1, the contents give %2: a loco would not process this tag")
                                          .arg(hex8(s.crcStored), hex8(s.crcCalc)));
        m_table->setItem(i, 2, crc);
        m_table->setItem(i, 7, cell(RfidTag::pageX(b) + QStringLiteral("  ") + RfidTag::pageY(b)));
    }
    m_updating = true;
    if (m_routeName->text() != m_route.name) m_routeName->setText(m_route.name);
    m_dir->setCurrentIndex(qMax(0, m_dir->findData(m_route.dir)));
    m_updating = false;
}

int TagBuilderWindow::currentRow() const
{
    const QList<QTableWidgetItem *> sel = m_table->selectedItems();
    return sel.isEmpty() ? -1 : sel.first()->row();
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
    fillTable();
    setModified(false);
    if (!m_route.tags.isEmpty()) selectRow(0);
}

void TagBuilderWindow::addTag()
{
    const int at = currentRow() < 0 ? m_route.tags.size() : currentRow() + 1;
    m_route.tags.insert(at, { RfidTag::nameOf(m_tag), m_tag });
    fillTable();
    setModified(true);
    selectRow(at);
    m_status->ok(tr("Tag %1 added at row %2").arg(RfidTag::nameOf(m_tag)).arg(at + 1));
}

void TagBuilderWindow::insertTag()
{
    const int at = qMax(0, currentRow());
    m_route.tags.insert(at, { RfidTag::nameOf(m_tag), m_tag });
    fillTable();
    setModified(true);
    selectRow(at);
    m_status->ok(tr("Tag %1 inserted at row %2").arg(RfidTag::nameOf(m_tag)).arg(at + 1));
}

void TagBuilderWindow::replaceTag()
{
    const int row = currentRow();
    if (row < 0) {
        m_status->warn(tr("Select the row to replace"));
        return;
    }
    m_route.tags[row] = { RfidTag::nameOf(m_tag), m_tag };
    fillTable();
    setModified(true);
    selectRow(row);
    m_status->ok(tr("Row %1 is now tag %2").arg(row + 1).arg(RfidTag::nameOf(m_tag)));
}

void TagBuilderWindow::deleteTag()
{
    const int row = currentRow();
    if (row < 0) return;
    const QString name = RfidTag::nameOf(m_route.tags.at(row).bytes);
    m_route.tags.remove(row);
    fillTable();
    setModified(true);
    selectRow(qMin(row, m_route.tags.size() - 1));
    m_status->ok(tr("Tag %1 deleted from row %2").arg(name).arg(row + 1));
}

void TagBuilderWindow::moveTag(int delta)
{
    const int row = currentRow(), to = row + delta;
    if (row < 0 || to < 0 || to >= m_route.tags.size()) return;
    std::swap(m_route.tags[row], m_route.tags[to]);
    fillTable();
    setModified(true);
    selectRow(to);
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
