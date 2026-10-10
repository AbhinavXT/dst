#include "taglibrary.h"

#include "rfidtag.h"
#include "settings.h"
#include "windowgeometry.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include <QHash>

namespace TagLibrary {

QString builtInRoot() { return QStringLiteral(":/tag_scenarios"); }

QVector<Entry> scan(const QString &root)
{
    QVector<Entry> out;
    QStringList files;
    QDirIterator it(root, { QStringLiteral("*.xml") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) files << it.next();
    // Folder first, then file: the tree reads like the KAV_CONFIG folder did.
    std::sort(files.begin(), files.end(), [&root](const QString &a, const QString &b) {
        const QString ra = QDir(root).relativeFilePath(a), rb = QDir(root).relativeFilePath(b);
        const QString fa = QFileInfo(ra).path(), fb = QFileInfo(rb).path();
        if (fa != fb) return QString::compare(fa, fb, Qt::CaseInsensitive) < 0;
        return QString::compare(ra, rb, Qt::CaseInsensitive) < 0;
    });
    for (const QString &f : files) {
        const QVector<RfidTag::Route> routes = RfidTag::readFile(f);
        const QString rel = QDir(root).relativeFilePath(f);
        const QString folder = QFileInfo(rel).path() == QLatin1String(".") ? QString() : QFileInfo(rel).path();
        for (int i = 0; i < routes.size(); ++i) {
            if (routes.at(i).tags.isEmpty()) continue;
            Entry e;
            e.file = f;
            e.folder = folder;
            e.fileName = QFileInfo(f).fileName();
            e.route = i;
            e.routesInFile = routes.size();
            e.name = routes.at(i).name;
            e.tags = routes.at(i).tags.size();
            e.signalCount = routes.at(i).signalList.size();
            e.dir = routes.at(i).dir;
            out.append(e);
        }
    }
    return out;
}

}  // namespace TagLibrary

namespace {

constexpr int kEntryRole = Qt::UserRole + 1;   // index into m_entries, or -1

QString dirText(int dir)
{
    return dir == RfidTag::DirNominal ? QStringLiteral("nominal") : dir == RfidTag::DirReverse ? QStringLiteral("reverse")
                                                                                             : QStringLiteral("not set");
}

}  // namespace

TagLibraryDialog::TagLibraryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Tag scenario library"));
    WindowGeometry::makeResizableWindow(this);
    resize(820, 600);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter: part of a folder, file or route name"));
    m_filter->setClearButtonEnabled(true);
    auto *folderBtn = new QPushButton(tr("Library folder…"), this);
    folderBtn->setToolTip(tr("The folder listed under the built-in library: %1").arg(QDir::toNativeSeparators(Settings::tagLibraryPath())));

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({ tr("Route"), tr("Tags"), tr("Signals"), tr("Direction") });
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c) m_tree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_tree->setToolTip(tr("tags_sim's KAV_CONFIG routes. Double-click a route to open it"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_open = buttons->addButton(tr("Open"), QDialogButtonBox::AcceptRole);
    m_open->setEnabled(false);

    auto *top = new QHBoxLayout;
    top->addWidget(m_filter, 1);
    top->addWidget(folderBtn);
    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addWidget(m_tree, 1);
    root->addWidget(buttons);

    connect(m_filter, &QLineEdit::textChanged, this, &TagLibraryDialog::setFilter);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        m_open->setEnabled(it && it->data(0, kEntryRole).toInt() >= 0);
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        if (choose(it)) accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (choose(m_tree->currentItem())) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(folderBtn, &QPushButton::clicked, this, [this, folderBtn]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("The tag scenario library folder"), Settings::tagLibraryPath());
        if (dir.isEmpty()) return;
        Settings::setTagLibraryPath(dir);
        folderBtn->setToolTip(tr("The folder listed under the built-in library: %1").arg(QDir::toNativeSeparators(dir)));
        fill();
    });
    fill();
}

void TagLibraryDialog::fill()
{
    m_tree->clear();
    m_entries.clear();
    const QString folder = Settings::tagLibraryPath();
    const QVector<QPair<QString, QString>> roots{ { TagLibrary::builtInRoot(), tr("Built in") },
                                                  { folder, tr("Folder %1").arg(QDir::toNativeSeparators(folder)) } };
    for (const auto &r : roots) {
        const bool exists = QFileInfo(r.first).isDir();
        const QVector<TagLibrary::Entry> found = exists ? TagLibrary::scan(r.first) : QVector<TagLibrary::Entry>();
        auto *top = new QTreeWidgetItem(m_tree, { exists ? tr("%1  (%2 routes)").arg(r.second).arg(found.size())
                                                         : tr("%1  (not there)").arg(r.second) });
        top->setData(0, kEntryRole, -1);
        QHash<QString, QTreeWidgetItem *> folders, files;
        for (const TagLibrary::Entry &e : found) {
            QTreeWidgetItem *parent = top;
            if (!e.folder.isEmpty()) {
                QString path;
                for (const QString &part : e.folder.split(QLatin1Char('/'))) {
                    path += QLatin1Char('/') + part;
                    if (!folders.contains(path)) {
                        auto *f = new QTreeWidgetItem(parent, { part });
                        f->setData(0, kEntryRole, -1);
                        folders.insert(path, f);
                    }
                    parent = folders.value(path);
                }
            }
            // A file of several routes (a Configuration1.xml, a route.xml
            // with its REV) is a node; a one-route file is the route itself.
            if (e.routesInFile > 1) {
                if (!files.contains(e.file)) {
                    auto *f = new QTreeWidgetItem(parent, { tr("%1  (%2 routes)").arg(e.fileName).arg(e.routesInFile) });
                    f->setData(0, kEntryRole, -1);
                    files.insert(e.file, f);
                }
                parent = files.value(e.file);
            }
            const QString label = e.routesInFile > 1 ? e.name : e.fileName;
            auto *it = new QTreeWidgetItem(parent, { label, QString::number(e.tags),
                                                     e.signalCount ? QString::number(e.signalCount) : QString(), dirText(e.dir) });
            it->setToolTip(0, QStringLiteral("%1\n%2").arg(e.name, QDir::toNativeSeparators(e.file)));
            it->setData(0, kEntryRole, m_entries.size());
            m_entries.append(e);
        }
        top->setExpanded(true);
    }
    setFilter(m_filter->text());
}

void TagLibraryDialog::setFilter(const QString &text)
{
    if (m_filter->text() != text) m_filter->setText(text);
    const QString t = text.trimmed();
    // A route shows when it, or any folder or file above it, matches.
    std::function<bool(QTreeWidgetItem *, bool)> walk = [&](QTreeWidgetItem *it, bool above) -> bool {
        const bool here = above || t.isEmpty() || it->text(0).contains(t, Qt::CaseInsensitive);
        bool any = false;
        for (int i = 0; i < it->childCount(); ++i) any = walk(it->child(i), here) || any;
        const bool isRoute = it->data(0, kEntryRole).toInt() >= 0;
        const bool show = isRoute ? here : any;     // a folder or file shows while it holds a route that does
        it->setHidden(!show && it->parent() != nullptr);
        if (!t.isEmpty() && any) it->setExpanded(true);
        return show;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) walk(m_tree->topLevelItem(i), false);
}

int TagLibraryDialog::visibleRoutes() const
{
    int n = 0;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
        if (it->isHidden()) return;
        if (it->data(0, kEntryRole).toInt() >= 0) ++n;
        for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) walk(m_tree->topLevelItem(i));
    return n;
}

bool TagLibraryDialog::choose(QTreeWidgetItem *item)
{
    const int i = item ? item->data(0, kEntryRole).toInt() : -1;
    if (i < 0 || i >= m_entries.size()) return false;
    m_chosen = m_entries.at(i);
    return true;
}
