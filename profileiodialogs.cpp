#include "profileiodialogs.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace ProfileIo {

QStringList pickNames(QWidget *parent, const QString &title, const QString &what,
                      const QStringList &all, const QStringList &preselected)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QObject::tr("Tick the %1 to put in the file:").arg(what), &dialog));
    auto *list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("profileIoPickList"));
    for (const QString &name : all) {
        auto *item = new QListWidgetItem(name, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(preselected.contains(name) ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list, 1);
    auto *quick = new QHBoxLayout;
    auto *allButton = new QPushButton(QObject::tr("All"), &dialog);
    auto *noneButton = new QPushButton(QObject::tr("None"), &dialog);
    quick->addWidget(allButton);
    quick->addWidget(noneButton);
    quick->addStretch(1);
    layout->addLayout(quick);
    auto setAll = [list](Qt::CheckState state) {
        for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(state);
    };
    QObject::connect(allButton, &QPushButton::clicked, &dialog, [setAll]() { setAll(Qt::Checked); });
    QObject::connect(noneButton, &QPushButton::clicked, &dialog, [setAll]() { setAll(Qt::Unchecked); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Export…"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(380, 360);
    if (dialog.exec() != QDialog::Accepted) return {};
    QStringList out;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked) out << list->item(i)->text();
    return out;
}

ImportClash askClash(QWidget *parent, const QString &what, const QString &name,
                     bool *sameForRest, bool *cancelled)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QObject::tr("Import"));
    box.setText(QObject::tr("There is already a %1 called \"%2\".").arg(what, name));
    box.setInformativeText(QObject::tr("Replace it with the imported one, keep both (the imported one is "
                                       "renamed), or skip the imported one?"));
    QPushButton *replace = box.addButton(QObject::tr("Replace"), QMessageBox::DestructiveRole);
    QPushButton *keep = box.addButton(QObject::tr("Keep both"), QMessageBox::AcceptRole);
    QPushButton *skip = box.addButton(QObject::tr("Skip"), QMessageBox::RejectRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(keep);
    auto *rest = new QCheckBox(QObject::tr("Do the same for the rest"), &box);
    box.setCheckBox(rest);
    box.exec();
    if (sameForRest) *sameForRest = rest->isChecked();
    if (cancelled) *cancelled = false;
    if (box.clickedButton() == replace) return ImportClash::Replace;
    if (box.clickedButton() == keep) return ImportClash::KeepBoth;
    if (box.clickedButton() == skip) return ImportClash::Skip;
    if (cancelled) *cancelled = true;
    return ImportClash::Skip;
}

}  // namespace ProfileIo
