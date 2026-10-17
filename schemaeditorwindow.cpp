#include "schemaeditorwindow.h"

#include "programdir.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>

using SchemaEdit::AttrSpec;
using SchemaEdit::Kind;

namespace {

const char kAttrProperty[] = "schemaAttr";
constexpr int kNodeRole = Qt::UserRole;

QString sizeText(int bits)
{
    if (bits < 0) return QObject::tr("variable");
    if (bits % 8 == 0) return QObject::tr("%1 B").arg(bits / 8);
    return QObject::tr("%1 bits").arg(bits);
}

QString parentTag(const SchemaDoc::Document &doc, int node)
{
    const int p = doc.node(node).parent;
    return p >= 0 ? doc.node(p).tag : QString();
}

// The attributes worth a glance in the member list, beside name and bits.
QString details(const SchemaDoc::Node &n)
{
    QStringList out;
    for (const SchemaDoc::Attr &a : n.attrs) {
        if (a.name == QLatin1String("name") || a.name == QLatin1String("bits")) continue;
        if (n.tag == QLatin1String("when") && a.name == QLatin1String("test")) continue;
        if (n.tag == QLatin1String("repeat") && a.name == QLatin1String("count")) continue;
        if (a.value == QLatin1String("true")) out << a.name;
        else out << QStringLiteral("%1 %2").arg(a.name, a.value);
    }
    return out.join(QStringLiteral(" · "));
}

QStringList namesOf(const SchemaDoc::Document &doc, const QString &tag, const QString &parent = QString())
{
    QStringList out;
    for (int i : doc.all(tag)) {
        if (!parent.isEmpty() && parentTag(doc, i) != parent) continue;
        out << doc.node(i).value(QStringLiteral("name"));
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

// Does this element, or anything inside it, carry `needle` in a name, label or text?
bool matches(const SchemaDoc::Document &doc, int node, const QString &needle)
{
    const SchemaDoc::Node &n = doc.node(node);
    for (const char *a : { "name", "label", "text", "test" }) {
        if (n.value(QString::fromLatin1(a)).contains(needle, Qt::CaseInsensitive)) return true;
    }
    for (int c : n.children) {
        if (matches(doc, c, needle)) return true;
    }
    return false;
}

}  // namespace

SchemaEditorWindow::SchemaEditorWindow(const QString &path, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Schema Editor"));
    WindowGeometry::makeResizableWindow(this);
    buildUi();
    resize(1280, 780);
    WindowGeometry::restore(this, QStringLiteral("schemaEditor"));
    openPath(path);
}

SchemaEditorWindow::~SchemaEditorWindow() = default;

void SchemaEditorWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // File bar: which file, and the staged-edit commands.
    auto *bar = new QHBoxLayout;
    m_fileLabel = new QLabel(this);
    m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bar->addWidget(m_fileLabel, 1);
    m_undoButton = new QPushButton(tr("Undo"), this);
    m_undoButton->setShortcut(QKeySequence::Undo);
    m_redoButton = new QPushButton(tr("Redo"), this);
    m_redoButton->setShortcut(QKeySequence::Redo);
    m_discardButton = new QPushButton(tr("Discard all"), this);
    m_saveButton = new QPushButton(tr("Review && save…"), this);
    m_saveButton->setDefault(false);
    m_saveButton->setAutoDefault(false);
    for (QPushButton *b : { m_undoButton, m_redoButton, m_discardButton }) {
        b->setAutoDefault(false);
        bar->addWidget(b);
    }
    bar->addWidget(m_saveButton);
    root->addLayout(bar);
    connect(m_undoButton, &QPushButton::clicked, this, [this]() { undo(); });
    connect(m_redoButton, &QPushButton::clicked, this, [this]() { redo(); });
    connect(m_discardButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("Discard all changes"),
                                  tr("Discard the %n staged change(s)? The file is not touched.", nullptr,
                                     m_session.changeCount()))
            != QMessageBox::Yes) {
            return;
        }
        m_session.discard();
        m_status->say(tr("Every staged change discarded"));
        refreshNow();
    });
    connect(m_saveButton, &QPushButton::clicked, this, &SchemaEditorWindow::reviewAndSave);

    // The built-in schema: read-only, with the way out.
    auto *bannerRow = new QHBoxLayout;
    m_readOnlyBanner = new QLabel(tr("This is the schema built into DLConsole, which cannot be changed. "
                                     "Save an editable copy to edit it; DLConsole decodes with that copy from then on "
                                     "(Schema ▸ Revert to built-in schema goes back)."),
                                  this);
    m_readOnlyBanner->setWordWrap(true);
    m_readOnlyBanner->setStyleSheet(UiColor::bannerStyle());
    m_copyButton = new QPushButton(tr("Make an editable copy…"), this);
    m_copyButton->setAutoDefault(false);
    connect(m_copyButton, &QPushButton::clicked, this, &SchemaEditorWindow::makeEditableCopy);
    bannerRow->addWidget(m_readOnlyBanner, 1);
    bannerRow->addWidget(m_copyButton);
    root->addLayout(bannerRow);

    auto *split = new QSplitter(Qt::Horizontal, this);

    // Outline.
    auto *left = new QWidget(split);
    auto *leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_find = new QLineEdit(left);
    m_find->setPlaceholderText(tr("Find a packet, field, enum, flag…"));
    m_find->setClearButtonEnabled(true);
    leftLay->addWidget(m_find);
    m_outline = new QTreeWidget(left);
    m_outline->setHeaderLabels({ tr("Name"), tr("Size") });
    m_outline->setRootIsDecorated(true);
    m_outline->setUniformRowHeights(true);
    m_outline->header()->setStretchLastSection(false);
    m_outline->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_outline->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    leftLay->addWidget(m_outline, 1);
    connect(m_find, &QLineEdit::textChanged, this, &SchemaEditorWindow::applyFind);
    connect(m_find, &QLineEdit::returnPressed, this, [this]() {
        // The first container that matches, and in it the first member that does.
        for (int c = 0; c < m_outline->topLevelItemCount(); ++c) {
            QTreeWidgetItem *cat = m_outline->topLevelItem(c);
            for (int k = 0; k < cat->childCount(); ++k) {
                if (cat->child(k)->isHidden()) continue;
                const int node = cat->child(k)->data(0, kNodeRole).toInt();
                selectNode(node);
                const QString needle = m_find->text().trimmed();
                for (int i = node + 1; i < m_session.doc().count() && !needle.isEmpty(); ++i) {
                    if (m_session.doc().container(i) != node) break;
                    const SchemaDoc::Node &n = m_session.doc().node(i);
                    if (n.value(QStringLiteral("name")).contains(needle, Qt::CaseInsensitive)
                        || n.value(QStringLiteral("label")).contains(needle, Qt::CaseInsensitive)) {
                        selectNode(i);
                        break;
                    }
                }
                return;
            }
        }
    });
    connect(m_outline, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!item || m_refreshQueued) return;
        const int node = item->data(0, kNodeRole).toInt();
        // Queued: selectNode() refills panes, and this item's tree is mid-signal.
        if (node > 0 && node != m_container) {
            QTimer::singleShot(0, this, [this, node]() { selectNode(node); });
        }
    });

    // Members.
    auto *mid = new QWidget(split);
    auto *midLay = new QVBoxLayout(mid);
    midLay->setContentsMargins(0, 0, 0, 0);
    m_membersTitle = new QLabel(mid);
    m_membersInfo = new QLabel(mid);
    m_membersInfo->setWordWrap(true);
    m_membersInfo->setStyleSheet(UiColor::mutedStyle());
    m_members = new QTreeWidget(mid);
    m_members->setUniformRowHeights(true);
    m_members->setAlternatingRowColors(true);
    midLay->addWidget(m_membersTitle);
    midLay->addWidget(m_membersInfo);
    midLay->addWidget(m_members, 1);
    connect(m_members, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!item || m_refreshQueued) return;
        const int node = item->data(0, kNodeRole).toInt();
        if (node > 0 && node != m_selected) {
            m_selected = node;
            fillForm();
        }
    });

    // Properties.
    m_formArea = new QScrollArea(split);
    m_formArea->setWidgetResizable(true);
    m_formArea->setFrameShape(QFrame::NoFrame);

    split->addWidget(left);
    split->addWidget(mid);
    split->addWidget(m_formArea);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 5);
    split->setStretchFactor(2, 3);
    split->setSizes({ 260, 600, 420 });
    root->addWidget(split, 1);

    auto *foot = new QHBoxLayout;
    m_status = new StatusLine(this);
    m_changesLabel = new QLabel(this);
    m_changesLabel->setStyleSheet(UiColor::mutedStyle());
    foot->addWidget(m_status, 1);
    foot->addWidget(m_changesLabel);
    root->addLayout(foot);
}

void SchemaEditorWindow::openPath(const QString &path)
{
    QString error;
    if (!m_session.open(path, &error)) {
        m_status->fail(error);
        return;
    }
    m_container = -1;
    m_selected = -1;
    refreshNow();
    const QVector<int> packets = m_session.doc().all(QStringLiteral("packet"));
    if (!packets.isEmpty()) selectNode(packets.first());
    m_status->state(m_session.readOnly() ? tr("Browsing the built-in schema")
                                         : tr("Select an element to see and change its properties"));
}

void SchemaEditorWindow::scheduleRefresh()
{
    if (m_refreshQueued) return;
    m_refreshQueued = true;
    QTimer::singleShot(0, this, [this]() { refreshNow(); });
}

void SchemaEditorWindow::refreshNow()
{
    m_refreshQueued = false;
    if (QWidget *f = QApplication::focusWidget()) {
        if (m_formArea->isAncestorOf(f)) m_refocus = f->property(kAttrProperty).toString();
    }
    const SchemaDoc::Document &doc = m_session.doc();
    if (m_container >= doc.count()) m_container = -1;
    if (m_selected >= doc.count()) m_selected = m_container;
    fillOutline();
    fillMembers();
    fillForm();
    updateActions();
    if (!m_refocus.isEmpty()) {
        if (QWidget *w = m_editors.value(m_refocus)) w->setFocus();
        m_refocus.clear();
    }
}

void SchemaEditorWindow::fillOutline()
{
    const SchemaDoc::Document &doc = m_session.doc();
    QHash<QString, bool> expanded;
    for (int c = 0; c < m_outline->topLevelItemCount(); ++c) {
        expanded.insert(m_outline->topLevelItem(c)->text(0).section(QLatin1Char(' '), 0, 0),
                        m_outline->topLevelItem(c)->isExpanded());
    }
    const QSignalBlocker block(m_outline);
    m_outline->clear();
    struct Category { QString title; QString tag; QString parent; };
    const QVector<Category> cats = {
        { tr("Packets"), QStringLiteral("packet"), QStringLiteral("schema") },
        { tr("Structs"), QStringLiteral("struct"), QStringLiteral("schema") },
        { tr("Enums"), QStringLiteral("enum"), QStringLiteral("enums") },
        { tr("Flag tables"), QStringLiteral("flagtable"), QStringLiteral("flagtables") },
        { tr("Event tables"), QStringLiteral("eventtable"), QStringLiteral("eventtables") },
        { tr("CRCs"), QStringLiteral("crc"), QStringLiteral("crcs") },
    };
    QFont bold = m_outline->font();
    bold.setBold(true);
    QTreeWidgetItem *current = nullptr;
    for (const Category &cat : cats) {
        QVector<int> items;
        for (int i : doc.all(cat.tag)) {
            if (parentTag(doc, i) == cat.parent) items.push_back(i);
        }
        if (items.isEmpty()) continue;
        auto *top = new QTreeWidgetItem(m_outline, { QStringLiteral("%1 (%2)").arg(cat.title).arg(items.size()) });
        top->setFont(0, bold);
        top->setData(0, kNodeRole, -1);
        top->setFlags(Qt::ItemIsEnabled);
        for (int i : items) {
            const SchemaDoc::Node &n = doc.node(i);
            QString size;
            if (cat.tag == QLatin1String("packet") || cat.tag == QLatin1String("struct")) {
                size = sizeText(SchemaEdit::fixedBits(doc, i));
            } else if (cat.tag == QLatin1String("enum")) {
                size = tr("%n value(s)", nullptr, int(n.children.size()));
            } else if (cat.tag == QLatin1String("flagtable")) {
                size = tr("%n flag(s)", nullptr, int(n.children.size()));
            } else if (cat.tag == QLatin1String("eventtable")) {
                size = tr("%n event(s)", nullptr, int(n.children.size()));
            } else {
                size = n.value(QStringLiteral("type"));
            }
            auto *item = new QTreeWidgetItem(top, { n.value(QStringLiteral("name")), size });
            item->setData(0, kNodeRole, i);
            item->setToolTip(0, n.comment.left(400));
            item->setForeground(1, UiColor::muted());
            if (i == m_container) current = item;
        }
        const QString key = top->text(0).section(QLatin1Char(' '), 0, 0);
        top->setExpanded(expanded.value(key, cat.tag == QLatin1String("packet")));
    }
    if (current) {
        current->parent()->setExpanded(true);
        m_outline->setCurrentItem(current);
        m_outline->scrollToItem(current);
    }
    applyFind();
}

void SchemaEditorWindow::syncOutline()
{
    const QSignalBlocker block(m_outline);
    QTreeWidgetItemIterator it(m_outline);
    for (; *it; ++it) {
        if ((*it)->data(0, kNodeRole).toInt() == m_container) {
            if ((*it)->parent()) (*it)->parent()->setExpanded(true);
            m_outline->setCurrentItem(*it);
            m_outline->scrollToItem(*it);
            return;
        }
    }
}

void SchemaEditorWindow::applyFind()
{
    const QString needle = m_find->text().trimmed();
    const SchemaDoc::Document &doc = m_session.doc();
    for (int c = 0; c < m_outline->topLevelItemCount(); ++c) {
        QTreeWidgetItem *cat = m_outline->topLevelItem(c);
        int shown = 0;
        for (int k = 0; k < cat->childCount(); ++k) {
            QTreeWidgetItem *item = cat->child(k);
            const bool hit = needle.isEmpty() || matches(doc, item->data(0, kNodeRole).toInt(), needle);
            item->setHidden(!hit);
            if (hit) ++shown;
        }
        cat->setHidden(shown == 0);
        if (!needle.isEmpty() && shown > 0) cat->setExpanded(true);
    }
}

void SchemaEditorWindow::addMemberItems(QTreeWidgetItem *parent, int node, int offset)
{
    // `offset` is the bit at which `node`'s first child starts (-1 unknown);
    // inside a repeat it is the offset within one entry, shown as "+n".
    const SchemaDoc::Document &doc = m_session.doc();
    const SchemaDoc::Node &n = doc.node(node);
    const bool inEntry = n.tag == QLatin1String("repeat");
    const QVector<int> offsets = SchemaEdit::childOffsets(doc, node);
    const QString needle = m_find->text().trimmed();
    QFont bold = m_members->font();
    bold.setBold(true);
    for (int k = 0; k < n.children.size(); ++k) {
        const int c = n.children.at(k);
        const SchemaDoc::Node &m = doc.node(c);
        const int base = inEntry ? 0 : offset;
        const int at = base < 0 || offsets.at(k) < 0 ? -1 : base + offsets.at(k);
        const int bits = SchemaEdit::fixedBits(doc, c);
        QString name = m.value(QStringLiteral("name"));
        QString kind = SchemaEdit::title(m.tag);
        QString bitsText = bits < 0 ? QStringLiteral("—") : QString::number(bits);
        bool quiet = false;
        if (m.tag == QLatin1String("pad")) {
            name = tr("(pad)");
            quiet = true;
        } else if (m.tag == QLatin1String("when")) {
            name = tr("when %1").arg(m.value(QStringLiteral("test")));
        } else if (m.tag == QLatin1String("repeat")) {
            kind = tr("Repeat × %1").arg(m.value(QStringLiteral("count")));
            const int entry = [&]() {
                int t = 0;
                for (int e : m.children) {
                    const int b = SchemaEdit::fixedBits(doc, e);
                    if (b < 0) return -1;
                    t += b;
                }
                return t;
            }();
            if (bits < 0 && entry >= 0) bitsText = tr("%1 / entry").arg(entry);
        } else if (m.tag == QLatin1String("subpackets")) {
            name = tr("(sub-packets)");
        } else if (m.tag == QLatin1String("case")) {
            name = tr("type %1 → %2").arg(m.value(QStringLiteral("type")), m.value(QStringLiteral("struct")));
        } else if (m.tag == QLatin1String("note")) {
            name = m.value(QStringLiteral("text"));
            quiet = true;
        } else if (m.tag == QLatin1String("align")) {
            name = tr("(to the next byte)");
            quiet = true;
        } else if (m.tag == QLatin1String("field")) {
            const QString t = m.value(QStringLiteral("type"));
            if (!t.isEmpty()) kind = tr("Field (%1)").arg(t);
        }
        QString atText;
        if (n.tag == QLatin1String("subpackets")) atText.clear();   // each case starts where its sub-packet does
        else if (at >= 0) atText = inEntry ? QStringLiteral("+%1").arg(at) : QString::number(at);
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_members);
        item->setText(0, name);
        item->setText(1, kind);
        item->setText(2, bitsText);
        item->setText(3, atText);
        item->setText(4, m.tag == QLatin1String("case") ? QString() : details(m));
        item->setData(0, kNodeRole, c);
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        item->setToolTip(0, m.comment.left(400));
        if (quiet) {
            for (int col = 0; col < 5; ++col) item->setForeground(col, UiColor::muted());
        }
        if (!needle.isEmpty() && name.contains(needle, Qt::CaseInsensitive)) item->setFont(0, bold);
        if (!m.children.isEmpty()) {
            addMemberItems(item, c, at);
            item->setExpanded(true);
        }
    }
}

void SchemaEditorWindow::fillMembers()
{
    const SchemaDoc::Document &doc = m_session.doc();
    const QSignalBlocker block(m_members);
    m_members->clear();
    if (m_container < 0) {
        m_membersTitle->clear();
        m_membersInfo->clear();
        return;
    }
    const SchemaDoc::Node &c = doc.node(m_container);
    const QString tag = c.tag;
    m_membersTitle->setText(QStringLiteral("<b style='font-size:large'>%1</b> &nbsp; %2")
                                .arg(c.value(QStringLiteral("name")).toHtmlEscaped(),
                                     SchemaEdit::title(tag).toHtmlEscaped()));
    QStringList info;
    if (tag == QLatin1String("packet") || tag == QLatin1String("struct")) {
        const int bits = SchemaEdit::fixedBits(doc, m_container);
        if (bits >= 0) {
            info << tr("%1 bits on the wire (%2 bytes)").arg(bits).arg((bits + 7) / 8);
        } else {
            int prefix = 0;
            const QVector<int> offs = SchemaEdit::childOffsets(doc, m_container);
            for (int k = 0; k < offs.size(); ++k) {
                if (offs.at(k) < 0) break;
                prefix = offs.at(k);
            }
            info << tr("Fixed for the first %1 bits; the rest depends on the frame").arg(prefix);
        }
        if (c.has(QStringLiteral("wire"))) info << c.value(QStringLiteral("wire"));
        if (c.has(QStringLiteral("match"))) info << c.value(QStringLiteral("match"));
        if (c.has(QStringLiteral("crc"))) info << tr("CRC %1").arg(c.value(QStringLiteral("crc")));
        if (c.has(QStringLiteral("body_offset"))) info << tr("body from bit %1").arg(c.value(QStringLiteral("body_offset")));
        m_members->setHeaderLabels({ tr("Name"), tr("Kind"), tr("Bits"), tr("Offset"), tr("Details") });
        m_members->setColumnCount(5);
        addMemberItems(nullptr, m_container, 0);
    } else if (tag == QLatin1String("enum")) {
        m_members->setColumnCount(3);
        m_members->setHeaderLabels({ tr("Value"), tr("Label"), tr("Formula / unit") });
        for (int i : c.children) {
            const SchemaDoc::Node &m = doc.node(i);
            QString v = m.value(QStringLiteral("v"));
            if (m.has(QStringLiteral("from"))) v = QStringLiteral("%1 – %2").arg(m.value(QStringLiteral("from")), m.value(QStringLiteral("to")));
            if (m.value(QStringLiteral("default")) == QLatin1String("true")) v = tr("otherwise");
            QStringList f;
            if (m.has(QStringLiteral("formula"))) f << m.value(QStringLiteral("formula"));
            if (m.has(QStringLiteral("unit"))) f << m.value(QStringLiteral("unit"));
            auto *item = new QTreeWidgetItem(m_members, { v, m.value(QStringLiteral("label")), f.join(QLatin1Char(' ')) });
            item->setData(0, kNodeRole, i);
        }
        int users = 0;
        for (int f : doc.all(QStringLiteral("field"))) {
            if (doc.node(f).value(QStringLiteral("enum")) == c.value(QStringLiteral("name"))) ++users;
        }
        info << tr("Used by %n field(s)", nullptr, users);
    } else if (tag == QLatin1String("flagtable")) {
        m_members->setColumnCount(2);
        m_members->setHeaderLabels({ tr("Bit"), tr("Name") });
        int bit = 0;
        for (int i : c.children) {
            auto *item = new QTreeWidgetItem(m_members, { QString::number(bit++), doc.node(i).value(QStringLiteral("name")) });
            item->setData(0, kNodeRole, i);
        }
        info << tr("Bit 0 is the table's first flag");
    } else if (tag == QLatin1String("eventtable")) {
        m_members->setColumnCount(4);
        m_members->setHeaderLabels({ tr("Id"), tr("Name"), tr("Bytes"), tr("Signed") });
        for (int i : c.children) {
            const SchemaDoc::Node &m = doc.node(i);
            auto *item = new QTreeWidgetItem(m_members, { m.value(QStringLiteral("id")), m.value(QStringLiteral("name")),
                                                          m.value(QStringLiteral("bytes")),
                                                          m.value(QStringLiteral("signed")) == QLatin1String("true") ? tr("yes") : QString() });
            item->setData(0, kNodeRole, i);
        }
    } else {
        m_members->setColumnCount(1);
        m_members->setHeaderLabels({ tr("Name") });
        info << tr("A definition with no members: its properties are on the right");
    }
    m_membersInfo->setText(info.join(QStringLiteral("  ·  ")));
    for (int col = 0; col + 1 < m_members->columnCount(); ++col) m_members->resizeColumnToContents(col);
    // Keep the selection.
    QTreeWidgetItemIterator it(m_members);
    for (; *it; ++it) {
        if ((*it)->data(0, kNodeRole).toInt() == m_selected) {
            m_members->setCurrentItem(*it);
            m_members->scrollToItem(*it);
            break;
        }
    }
}

QWidget *SchemaEditorWindow::makeEditor(int node, const AttrSpec &spec, bool present, const QString &value)
{
    Q_UNUSED(present);
    const SchemaDoc::Document &doc = m_session.doc();
    const QString attr = spec.name;
    QWidget *w = nullptr;
    auto refChoices = [&]() -> QStringList {
        switch (spec.kind) {
        case Kind::EnumRef:       return namesOf(doc, QStringLiteral("enum"));
        case Kind::StructRef:     return namesOf(doc, QStringLiteral("struct"));
        case Kind::FlagTableRef:  return namesOf(doc, QStringLiteral("flagtable"));
        case Kind::EventTableRef: return namesOf(doc, QStringLiteral("eventtable"));
        case Kind::CrcRef:        return namesOf(doc, QStringLiteral("crc"), QStringLiteral("crcs"));
        default:                  return {};
        }
    };
    auto lineEditor = [&]() -> QWidget * {
        auto *edit = new QLineEdit(value);
        if (spec.kind == Kind::Condition) edit->setPlaceholderText(tr("e.g. cls==1, type in 3..7"));
        connect(edit, &QLineEdit::editingFinished, this, [this, node, attr, edit]() {
            // editingFinished comes again when the rename question takes the focus.
            if (m_asking) return;
            const QString v = edit->text();
            const SchemaDoc::Node &n = m_session.doc().node(node);
            if (v == n.value(attr)) return;
            if (attr == QLatin1String("name") && n.has(attr)) askRename(node, v);
            else editAttribute(node, attr, v);
        });
        return edit;
    };
    switch (spec.kind) {
    case Kind::Bool: {
        auto *box = new QCheckBox;
        box->setChecked(value == QLatin1String("true"));
        connect(box, &QCheckBox::toggled, this, [this, node, attr](bool on) {
            if (on) editAttribute(node, attr, QStringLiteral("true"));
            else dropAttribute(node, attr);
        });
        w = box;
        break;
    }
    case Kind::Choice:
    case Kind::EnumRef:
    case Kind::StructRef:
    case Kind::FlagTableRef:
    case Kind::EventTableRef:
    case Kind::CrcRef: {
        auto *box = new QComboBox;
        const bool ref = spec.kind != Kind::Choice;
        QStringList values = ref ? refChoices() : spec.choices;
        if (ref && !SchemaEdit::isRequired(doc, node, attr)) values.prepend(QString());
        if (!value.isEmpty() && !values.contains(value)) values.prepend(value);   // dangling: still shown
        for (const QString &v : values) {
            box->addItem(v.isEmpty() ? (ref ? tr("(none)") : tr("(default)")) : v, v);
        }
        box->setCurrentIndex(std::max(0, int(values.indexOf(value))));
        if (values.size() > 12) box->setMaxVisibleItems(20);
        connect(box, QOverload<int>::of(&QComboBox::activated), this, [this, node, attr, box](int) {
            const QString v = box->currentData().toString();
            if (v == m_session.doc().node(node).value(attr) && m_session.doc().node(node).has(attr) == !v.isEmpty()) return;
            if (v.isEmpty()) dropAttribute(node, attr);
            else editAttribute(node, attr, v);
        });
        w = box;
        break;
    }
    case Kind::Int: {
        bool ok = false;
        const qint64 v = value.toLongLong(&ok);
        // A value the spin box cannot hold stays plain text, shown as it is.
        if (!ok || v < spec.min || v > spec.max) {
            w = lineEditor();
            break;
        }
        {
            auto *spin = new QSpinBox;
            spin->setRange(spec.min, spec.max);
            spin->setValue(int(v));
            spin->setKeyboardTracking(false);
            spin->setAccelerated(true);
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, node, attr](int n) {
                editAttribute(node, attr, QString::number(n));
            });
            w = spin;
        }
        break;
    }
    default:
        w = lineEditor();
        break;
    }
    w->setProperty(kAttrProperty, attr);
    w->setToolTip(spec.hint);
    w->setEnabled(!m_session.readOnly());
    return w;
}

void SchemaEditorWindow::fillForm()
{
    // The old form's widgets must not report editingFinished while they go.
    if (QWidget *old = m_formArea->takeWidget()) {
        for (QWidget *k : old->findChildren<QWidget *>()) k->blockSignals(true);
        old->hide();
        old->deleteLater();
    }
    m_editors.clear();
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(8, 0, 4, 0);
    m_formArea->setWidget(page);
    const SchemaDoc::Document &doc = m_session.doc();
    if (m_selected < 0 || m_selected >= doc.count()) {
        lay->addWidget(new QLabel(tr("Select an element.")));
        lay->addStretch(1);
        return;
    }
    const SchemaDoc::Node &n = doc.node(m_selected);
    const QString pTag = parentTag(doc, m_selected);

    QString name = n.value(QStringLiteral("name"));
    if (n.tag == QLatin1String("map")) name = n.value(QStringLiteral("label"));
    if (n.tag == QLatin1String("when")) name = n.value(QStringLiteral("test"));
    auto *title = new QLabel(QStringLiteral("<b style='font-size:large'>%1</b> &nbsp; %2")
                                 .arg(SchemaEdit::title(n.tag).toHtmlEscaped(), name.toHtmlEscaped()));
    title->setWordWrap(true);
    lay->addWidget(title);
    auto *what = new QLabel(SchemaEdit::describe(n.tag, pTag));
    what->setWordWrap(true);
    lay->addWidget(what);
    const int owner = doc.container(m_selected);
    QString where = tr("%1, line %2").arg(QFileInfo(m_session.path()).fileName()).arg(n.line);
    if (owner >= 0 && owner != m_selected) {
        where += tr(" · in %1").arg(doc.node(owner).value(QStringLiteral("name")));
    }
    auto *loc = new QLabel(where);
    loc->setStyleSheet(UiColor::mutedStyle());
    lay->addWidget(loc);
    if (!n.comment.isEmpty()) {
        QString c = n.comment;
        if (c.size() > 700) c = c.left(700) + QStringLiteral(" …");
        auto *comment = new QLabel(c);
        comment->setWordWrap(true);
        comment->setTextFormat(Qt::PlainText);
        comment->setStyleSheet(UiColor::mutedStyle() + QStringLiteral("font-style:italic;"));
        comment->setToolTip(tr("The comment above this element in the file"));
        lay->addWidget(comment);
    }

    // Properties: the ones the element has, in the spec's order, then any
    // the spec does not know.
    const QVector<AttrSpec> specs = SchemaEdit::specsFor(n.tag, pTag);
    QVector<AttrSpec> shown;
    for (const AttrSpec &s : specs) {
        if (n.has(s.name)) shown.push_back(s);
    }
    for (const SchemaDoc::Attr &a : n.attrs) {
        if (!SchemaEdit::specFor(specs, a.name)) {
            AttrSpec s;
            s.name = a.name;
            s.label = a.name;
            s.hint = tr("Not an attribute the editor knows; edited as text");
            shown.push_back(s);
        }
    }
    auto *formBox = new QWidget;
    auto *form = new QFormLayout(formBox);
    form->setContentsMargins(0, 8, 0, 8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    for (const AttrSpec &s : shown) {
        QWidget *editor = makeEditor(m_selected, s, true, n.value(s.name));
        m_editors.insert(s.name, editor);
        auto *row = new QWidget;
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->addWidget(editor, 1);
        // A check box removes its attribute by being cleared: no ✕ beside it.
        if (!SchemaEdit::isRequired(doc, m_selected, s.name) && !m_session.readOnly() && s.kind != Kind::Bool) {
            auto *remove = new QToolButton;
            remove->setText(QStringLiteral("✕"));
            remove->setAutoRaise(true);
            remove->setToolTip(tr("Remove %1").arg(s.name));
            const QString attr = s.name;
            const int node = m_selected;
            connect(remove, &QToolButton::clicked, this, [this, node, attr]() { dropAttribute(node, attr); });
            rowLay->addWidget(remove);
        }
        auto *label = new QLabel(s.label);
        label->setToolTip(QStringLiteral("%1 — %2").arg(s.name, s.hint));
        form->addRow(label, row);
    }
    if (shown.isEmpty()) form->addRow(new QLabel(tr("No properties.")));
    lay->addWidget(formBox);

    // The ones it could have.
    QVector<AttrSpec> absent;
    for (const AttrSpec &s : specs) {
        if (!n.has(s.name)) absent.push_back(s);
    }
    if (!absent.isEmpty()) {
        auto *add = new QToolButton;
        add->setText(tr("＋ Add a property"));
        add->setPopupMode(QToolButton::InstantPopup);
        add->setToolButtonStyle(Qt::ToolButtonTextOnly);
        add->setEnabled(!m_session.readOnly());
        auto *menu = new QMenu(add);
        const int node = m_selected;
        for (const AttrSpec &s : absent) {
            QAction *act = menu->addAction(QStringLiteral("%1  —  %2").arg(s.label, s.hint));
            act->setData(s.name);
            connect(act, &QAction::triggered, this, [this, node, s]() {
                QString value;
                switch (s.kind) {
                case Kind::Bool:
                    value = QStringLiteral("true");
                    break;
                case Kind::Choice: {
                    QStringList options;
                    for (const QString &c : s.choices) if (!c.isEmpty()) options << c;
                    bool ok = false;
                    value = QInputDialog::getItem(this, tr("Add %1").arg(s.label), s.hint, options, 0, false, &ok);
                    if (!ok) return;
                    break;
                }
                case Kind::EnumRef:
                case Kind::StructRef:
                case Kind::FlagTableRef:
                case Kind::EventTableRef:
                case Kind::CrcRef: {
                    const SchemaDoc::Document &d = m_session.doc();
                    QStringList options = s.kind == Kind::EnumRef ? namesOf(d, QStringLiteral("enum"))
                                        : s.kind == Kind::StructRef ? namesOf(d, QStringLiteral("struct"))
                                        : s.kind == Kind::FlagTableRef ? namesOf(d, QStringLiteral("flagtable"))
                                        : s.kind == Kind::EventTableRef ? namesOf(d, QStringLiteral("eventtable"))
                                                                        : namesOf(d, QStringLiteral("crc"), QStringLiteral("crcs"));
                    bool ok = false;
                    value = QInputDialog::getItem(this, tr("Add %1").arg(s.label), s.hint, options, 0, false, &ok);
                    if (!ok) return;
                    break;
                }
                default: {
                    bool ok = false;
                    value = QInputDialog::getText(this, tr("Add %1").arg(s.label), s.hint, QLineEdit::Normal, QString(), &ok);
                    if (!ok) return;
                    break;
                }
                }
                if (editAttribute(node, s.name, value)) m_refocus = s.name;
            });
        }
        add->setMenu(menu);
        lay->addWidget(add, 0, Qt::AlignLeft);
    }

    // The start tag as the file now has it.
    auto *inFile = new QLabel(tr("In the file:"));
    inFile->setStyleSheet(UiColor::mutedStyle());
    lay->addSpacing(8);
    lay->addWidget(inFile);
    auto *tag = new QLabel(doc.startTag(m_selected));
    tag->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    tag->setWordWrap(true);
    tag->setTextFormat(Qt::PlainText);
    tag->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(tag);
    lay->addStretch(1);
}

void SchemaEditorWindow::updateActions()
{
    const bool ro = m_session.readOnly();
    m_readOnlyBanner->setVisible(ro);
    m_copyButton->setVisible(ro);
    m_fileLabel->setText(ro ? tr("<b>Built-in schema</b> (read-only)")
                            : tr("<b>%1</b> &nbsp; %2").arg(QFileInfo(m_session.path()).fileName().toHtmlEscaped(),
                                                             QDir::toNativeSeparators(m_session.path()).toHtmlEscaped()));
    m_undoButton->setEnabled(m_session.canUndo());
    m_undoButton->setToolTip(m_session.canUndo() ? tr("Undo: %1").arg(m_session.undoText()) : QString());
    m_redoButton->setEnabled(m_session.canRedo());
    m_redoButton->setToolTip(m_session.canRedo() ? tr("Redo: %1").arg(m_session.redoText()) : QString());
    m_discardButton->setEnabled(m_session.dirty());
    m_saveButton->setEnabled(m_session.dirty() && !ro);
    m_changesLabel->setText(m_session.dirty() ? tr("%n change(s) not saved", nullptr, m_session.changeCount())
                                              : tr("No unsaved changes"));
    setWindowTitle(m_session.dirty() ? tr("Schema Editor — %1 *").arg(QFileInfo(m_session.path()).fileName())
                                     : tr("Schema Editor — %1").arg(ro ? tr("built-in") : QFileInfo(m_session.path()).fileName()));
}

void SchemaEditorWindow::showResult(bool ok, const QString &why)
{
    if (ok) m_status->ok(tr("Staged: %1").arg(m_session.undoText()));
    else if (!why.isEmpty() && why != tr("No change")) m_status->fail(why);
    scheduleRefresh();
}

void SchemaEditorWindow::selectNode(int node)
{
    const SchemaDoc::Document &doc = m_session.doc();
    if (node <= 0 || node >= doc.count()) return;
    const int c = doc.container(node);
    const bool newContainer = c != m_container;
    m_container = c;
    m_selected = node;
    if (newContainer) {
        syncOutline();
        fillMembers();
    } else {
        const QSignalBlocker block(m_members);
        QTreeWidgetItemIterator it(m_members);
        for (; *it; ++it) {
            if ((*it)->data(0, kNodeRole).toInt() == node) {
                m_members->setCurrentItem(*it);
                m_members->scrollToItem(*it);
                break;
            }
        }
        if (node == c) m_members->setCurrentItem(nullptr);
    }
    fillForm();
}

bool SchemaEditorWindow::selectContainer(const QString &tag, const QString &name)
{
    for (int i : m_session.doc().all(tag)) {
        if (m_session.doc().node(i).value(QStringLiteral("name")) == name && m_session.doc().container(i) == i) {
            selectNode(i);
            return true;
        }
    }
    return false;
}

bool SchemaEditorWindow::editAttribute(int node, const QString &attr, const QString &value)
{
    QString why;
    const bool ok = m_session.setAttribute(node, attr, value, &why);
    showResult(ok, why);
    return ok;
}

bool SchemaEditorWindow::dropAttribute(int node, const QString &attr)
{
    QString why;
    const bool ok = m_session.removeAttribute(node, attr, &why);
    showResult(ok, why);
    return ok;
}

bool SchemaEditorWindow::rename(int node, const QString &newName, bool withReferences)
{
    QString why;
    const bool ok = m_session.rename(node, newName, withReferences, &why);
    showResult(ok, why);
    return ok;
}

bool SchemaEditorWindow::undo()
{
    if (!m_session.undo()) return false;
    m_status->say(tr("Undone"));
    scheduleRefresh();
    return true;
}

bool SchemaEditorWindow::redo()
{
    if (!m_session.redo()) return false;
    m_status->say(tr("Redone: %1").arg(m_session.undoText()));
    scheduleRefresh();
    return true;
}

void SchemaEditorWindow::askRename(int node, const QString &newName)
{
    const SchemaDoc::Document &doc = m_session.doc();
    const SchemaEdit::RenamePlan plan = SchemaEdit::planRename(doc, node, newName);
    m_asking = true;
    struct Reset { bool &flag; ~Reset() { flag = false; } } reset{ m_asking };
    const QString old = doc.node(node).value(QStringLiteral("name"));
    if (plan.changes.isEmpty() && plan.elsewhere.isEmpty() && plan.warning.isEmpty()) {
        rename(node, newName, false);
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("Rename %1").arg(old));
    box.setIcon(QMessageBox::Question);
    QString text = tr("Rename %1 to %2?").arg(old, newName);
    if (!plan.changes.isEmpty()) {
        text += QStringLiteral("\n\n") + tr("%n place(s) in the schema refer to it by name:", nullptr, int(plan.changes.size()));
        int shown = 0;
        for (const SchemaEdit::Reference &r : plan.changes) {
            if (++shown > 8) {
                text += QStringLiteral("\n  …");
                break;
            }
            text += QStringLiteral("\n  %1=\"%2\" → \"%3\"").arg(r.attr, r.before, r.after);
        }
    }
    if (!plan.elsewhere.isEmpty()) {
        text += QStringLiteral("\n\n") + tr("Not changed (another packet or struct, which may mean a different %1):").arg(old);
        for (int i = 0; i < plan.elsewhere.size() && i < 6; ++i) text += QStringLiteral("\n  ") + plan.elsewhere.at(i);
        if (plan.elsewhere.size() > 6) text += QStringLiteral("\n  …");
    }
    if (!plan.warning.isEmpty()) text += QStringLiteral("\n\n") + plan.warning;
    text += QStringLiteral("\n\n") + tr("DLConsole's own views, the Python validators and fielddocs.json may also use "
                                        "the old name; they are not changed.");
    box.setText(text);
    QPushButton *withRefs = plan.changes.isEmpty() ? nullptr
                                                   : box.addButton(tr("Rename and update %n reference(s)", nullptr,
                                                                      int(plan.changes.size())), QMessageBox::AcceptRole);
    QPushButton *only = box.addButton(plan.changes.isEmpty() ? tr("Rename") : tr("Rename only"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(withRefs ? withRefs : only);
    box.exec();
    if (box.clickedButton() == withRefs && withRefs) rename(node, newName, true);
    else if (box.clickedButton() == only) rename(node, newName, false);
    else scheduleRefresh();   // put the old name back in the box
}

QStringList SchemaEditorWindow::sizeChanges() const
{
    QStringList out;
    const SchemaDoc::Document &now = m_session.doc();
    SchemaDoc::Document before;
    before.parse(m_session.original());
    auto sizes = [](const SchemaDoc::Document &d) {
        QHash<QString, int> s;
        for (const char *tag : { "packet", "struct" }) {
            for (int i : d.all(QString::fromLatin1(tag))) {
                s.insert(QStringLiteral("%1 %2").arg(QString::fromLatin1(tag), d.node(i).value(QStringLiteral("name"))),
                         SchemaEdit::fixedBits(d, i));
            }
        }
        return s;
    };
    const QHash<QString, int> a = sizes(before), b = sizes(now);
    QStringList keys = b.keys();
    keys.sort();
    for (const QString &k : keys) {
        if (a.contains(k) && a.value(k) != b.value(k)) {
            out << tr("%1: %2 → %3").arg(k.section(QLatin1Char(' '), 1), sizeText(a.value(k)), sizeText(b.value(k)));
        }
    }
    return out;
}

QStringList SchemaEditorWindow::reviewLines() const
{
    QStringList out;
    out << tr("%n change(s) to %1:", nullptr, m_session.changeCount()).arg(QDir::toNativeSeparators(m_session.path()));
    for (const QString &c : m_session.changes()) out << QStringLiteral("  • ") + c;
    const QStringList sizes = sizeChanges();
    if (!sizes.isEmpty()) {
        out << QString() << tr("Sizes on the wire that change (the equipment and Packet Maker must agree):");
        for (const QString &s : sizes) out << QStringLiteral("  • ") + s;
    }
    out << QString() << tr("The file, line by line:");
    out << m_session.diff();
    return out;
}

void SchemaEditorWindow::reviewAndSave()
{
    if (!m_session.dirty() || m_session.readOnly()) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Review and save"));
    auto *lay = new QVBoxLayout(&dlg);
    auto *view = new QTextEdit(&dlg);
    view->setReadOnly(true);
    view->setLineWrapMode(QTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QString html = QStringLiteral("<pre style='margin:0'>");
    for (const QString &line : reviewLines()) {
        const QString esc = line.toHtmlEscaped();
        if (line.startsWith(QLatin1String("+ "))) html += QStringLiteral("<span style='color:%1'>%2</span>\n").arg(UiColor::ok().name(), esc);
        else if (line.startsWith(QLatin1String("- "))) html += QStringLiteral("<span style='color:%1'>%2</span>\n").arg(UiColor::error().name(), esc);
        else if (line.startsWith(QLatin1String("@@"))) html += QStringLiteral("<span style='color:%1'>%2</span>\n").arg(UiColor::muted().name(), esc);
        else html += esc + QLatin1Char('\n');
    }
    html += QStringLiteral("</pre>");
    view->setHtml(html);
    lay->addWidget(view, 1);
    auto *note = new QLabel(tr("A copy of the file as it is now is kept beside it (%1.bak-<date>-<time>). After saving, "
                               "DLConsole decodes with the new schema. Check it against real captures (Schema ▸ "
                               "Round-trip Validator, and the Python validators) before relying on it.")
                                .arg(QFileInfo(m_session.path()).fileName()));
    note->setWordWrap(true);
    note->setStyleSheet(UiColor::mutedStyle());
    lay->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.resize(900, 600);
    if (dlg.exec() != QDialog::Accepted) return;
    QString summary, error;
    if (!save(&summary, &error)) QMessageBox::warning(this, tr("Not saved"), error);
}

bool SchemaEditorWindow::save(QString *summary, QString *error)
{
    const int n = m_session.changeCount();
    const QStringList sizes = sizeChanges();
    QString backup, why;
    if (!m_session.save(&backup, &why)) {
        m_status->fail(why);
        if (error) *error = why;
        return false;
    }
    QString text = tr("%n change(s) saved to %1. Backup: %2.", nullptr, n)
                       .arg(QFileInfo(m_session.path()).fileName(), QFileInfo(backup).fileName());
    if (!sizes.isEmpty()) text += QLatin1Char(' ') + tr("Sizes changed: %1.").arg(sizes.join(QStringLiteral("; ")));
    if (summary) *summary = text;
    m_status->ok(text);
    refreshNow();
    emit schemaSaved(m_session.path(), text);
    return true;
}

void SchemaEditorWindow::makeEditableCopy()
{
    const QString dest = QFileDialog::getSaveFileName(this, tr("Save an editable kavach.xml"),
                                                      programDir() + QStringLiteral("/kavach.xml"),
                                                      tr("Schema (*.xml)"));
    if (dest.isEmpty()) return;
    QString error;
    if (!SchemaEdit::makeEditableCopy(dest, &error)) {
        m_status->fail(error);
        return;
    }
    const int keep = m_selected;
    openPath(dest);
    selectNode(keep);
    m_status->ok(tr("Editing %1; DLConsole decodes with it from now on").arg(QDir::toNativeSeparators(dest)));
    emit schemaFileChanged(dest);
}

void SchemaEditorWindow::reject()
{
    close();
}

void SchemaEditorWindow::closeEvent(QCloseEvent *event)
{
    if (m_session.dirty()) {
        const auto answer = QMessageBox::question(
            this, tr("Unsaved schema changes"),
            tr("%n change(s) to %1 are not saved.", nullptr, m_session.changeCount()).arg(QFileInfo(m_session.path()).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Save) {
            QString summary, error;
            if (!save(&summary, &error)) {
                QMessageBox::warning(this, tr("Not saved"), error);
                event->ignore();
                return;
            }
        }
    }
    WindowGeometry::save(this, QStringLiteral("schemaEditor"));
    QDialog::closeEvent(event);
}
