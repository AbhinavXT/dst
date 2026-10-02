#include "subpacketwindow.h"
#include "emptystate.h"
#include "uicolors.h"
#include "uistyle.h"
#include "windowgeometry.h"

#include "packetmakerdialog.h"      // for the shared field-editor factory

#include <QCloseEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

SubPacketWindow::SubPacketWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Sub-packet fields"));
    WindowGeometry::makeResizableWindow(this);
    // A tool window, not a modal: the whole point is to see it alongside the
    // Packet Maker while the header form is edited.
    setWindowFlag(Qt::Window, true);
    setModal(false);
    resize(720, 620);

    auto *root = new QVBoxLayout(this);

    m_title = new QLabel(tr("No sub-packet selected."), this);
    QFont tf = m_title->font();
    tf.setBold(true);
    m_title->setFont(tf);
    root->addWidget(m_title);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_host = new QWidget;
    m_form = new QFormLayout(m_host);
    // Editors at their own size (session 139): stretched across a wide
    // window, a one-digit value sat 600 px from its name.
    m_form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    m_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    scroll->setWidget(m_host);
    root->addWidget(scroll, 1);

    auto *btns = new QHBoxLayout;
    auto *applyBtn = new QPushButton(tr("Apply"), this);
    applyBtn->setToolTip(tr("Changes are also applied automatically when this "
                            "window closes or the selection moves; this is for "
                            "when you want the preview to catch up now."));
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btns->addStretch(1);
    btns->addWidget(applyBtn);
    btns->addWidget(closeBtn);
    root->addLayout(btns);

    connect(applyBtn, &QPushButton::clicked, this, [this]() { commit(); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

void SubPacketWindow::closeEvent(QCloseEvent *e)
{
    // Commit on the way out. Closing a window is not a cancel here — there is
    // no cancel, because the editor writes through to the packet and always
    // has; a close that silently discarded edits would be a new way to lose
    // work rather than a safety net.
    commit();
    QDialog::closeEvent(e);
}

void SubPacketWindow::clearTarget()
{
    m_subs  = nullptr;
    m_index = -1;
    m_enc   = nullptr;
    m_captype.clear();
    rebuild();
}

void SubPacketWindow::setTarget(const Schema::Encoder *enc, const QString &captype,
                                QVector<Schema::SubEntry> *subs, int index)
{
    // Anything pending on the previous target goes back before we retarget.
    commit();

    m_enc     = enc;
    m_captype = captype;
    m_subs    = subs;
    m_index   = (subs && index >= 0 && index < subs->size()) ? index : -1;
    rebuild();
}

void SubPacketWindow::rebuild()
{
    m_editors.clear();
    m_repeatTables.clear();

    // Guard on count(): QFormLayout::takeAt warns on an empty layout as well
    // as returning null, so the drain-until-null idiom logs a spurious
    // "Invalid index 0" every time the form is already clear.
    while (m_form->count() > 0) {
        QLayoutItem *it = m_form->takeAt(0);
        if (!it) { break; }
        // Hidden first: until deleteLater runs, a removed row stays visible
        // at the form's corner (session 134 found the same in the Flasher).
        if (it->widget()) { it->widget()->hide(); it->widget()->deleteLater(); }
        delete it;
    }

    if (!m_enc || !m_subs || m_index < 0 || m_index >= m_subs->size()) {
        m_title->setText(tr("No sub-packet selected."));
        return;
    }

    const Schema::SubEntry &se = (*m_subs)[m_index];
    const Schema::PacketInfo pi = m_enc->packet(m_captype);
    QString sname;
    for (const Schema::CaseInfo &c : pi.cases) {
        if (c.type == se.type) { sname = c.structName; break; }
    }
    m_title->setText(tr("Sub-packet %1 of %2  —  type %3, %4")
                         .arg(m_index + 1).arg(m_subs->size()).arg(se.type).arg(sname));
    setWindowTitle(tr("%1 — sub-packet fields").arg(sname));

    const Schema::StructLayout sl = m_enc->structLayout(sname);

    for (const Schema::FieldInfo &f : sl.scalarFields) {
        QWidget *ed = PacketMakerDialog::makeFieldEditor(f, *m_enc, m_host);
        PacketMakerDialog::writeFieldEditor(ed, se.values.value(f.name, 0));
        ed->setMinimumWidth(qMax(ed->minimumWidth(), 140));
        // The condition muted after the name: it qualifies the field, it is
        // not part of its name.
        auto *label = new QLabel(m_host);
        label->setObjectName(QStringLiteral("subFieldLabel"));
        if (f.when.isEmpty()) {
            label->setText(f.name);
        } else {
            label->setTextFormat(Qt::RichText);
            label->setText(QStringLiteral("%1 <span style=\"color:%2\">if %3</span>")
                               .arg(f.name.toHtmlEscaped(), UiColor::muted().name(), f.when.toHtmlEscaped()));
            label->setToolTip(tr("Only on the wire when %1").arg(f.when));
        }
        label->setBuddy(ed);
        m_form->addRow(label, ed);
        m_editors.insert(f.name, ed);
    }

    for (const Schema::RepeatInfo &ri : sl.repeats) {
        QStringList cols;
        for (const Schema::FieldInfo &f : ri.fields) { cols << f.name; }

        auto *box = new QWidget(m_host);
        auto *bl  = new QVBoxLayout(box);
        bl->setContentsMargins(0, 0, 0, 0);
        auto *caption = new QLabel(tr("%1 rows — %2 to %3%4")
                                       .arg(ri.name).arg(ri.cmin).arg(ri.cmax)
                                       .arg(ri.countField.isEmpty()
                                                ? tr(" (a fixed count)")
                                                : tr(" · the count goes in %1 by itself").arg(ri.countField)),
                                   box);
        caption->setObjectName(QStringLiteral("subRepeatCaption"));
        UiStyle::makeSectionLabel(caption);
        bl->addWidget(caption);

        auto *tbl = new QTableWidget(0, cols.size(), box);
        tbl->setHorizontalHeaderLabels(cols);
        tbl->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        tbl->setEditTriggers(QAbstractItemView::AllEditTriggers);
        tbl->verticalHeader()->setVisible(false);
        // Roomy on purpose: the repeat table is the reason this window exists.
        tbl->setObjectName(QStringLiteral("subRepeatTable"));
        tbl->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        tbl->setMinimumHeight(160);
        // Empty, it says so and what it needs, rather than a blank grid.
        const int need = ri.cmin;
        EmptyState::attach(tbl, need > 0 ? tr("No rows yet — this needs at least %1. \"+ row\" adds one.").arg(need)
                                         : tr("No rows. \"+ row\" adds one."));
        tbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        const auto &rows = se.repeats.value(ri.name);
        for (const QHash<QString, qint64> &row : rows) {
            const int r = tbl->rowCount();
            tbl->insertRow(r);
            for (int c = 0; c < cols.size(); ++c) {
                tbl->setItem(r, c, new QTableWidgetItem(QString::number(row.value(cols[c], 0))));
            }
        }
        bl->addWidget(tbl, 1);

        auto *rb   = new QHBoxLayout;
        auto *addR = new QPushButton(tr("+ row"), box);
        auto *delR = new QPushButton(tr("– row"), box);
        rb->addWidget(addR);
        rb->addWidget(delR);
        rb->addStretch(1);
        bl->addLayout(rb);

        connect(addR, &QPushButton::clicked, this, [tbl, cols]() {
            const int r = tbl->rowCount();
            tbl->insertRow(r);
            for (int c = 0; c < cols.size(); ++c) {
                tbl->setItem(r, c, new QTableWidgetItem(QStringLiteral("0")));
            }
        });
        connect(delR, &QPushButton::clicked, this, [tbl]() {
            const int r = tbl->currentRow();
            if (r >= 0)                 { tbl->removeRow(r); }
            else if (tbl->rowCount())   { tbl->removeRow(tbl->rowCount() - 1); }
        });

        m_form->addRow(box);
        m_repeatTables.insert(ri.name, tbl);
    }
}

void SubPacketWindow::commit()
{
    if (!m_subs || m_index < 0 || m_index >= m_subs->size()) { return; }

    Schema::SubEntry &se = (*m_subs)[m_index];

    for (auto it = m_editors.constBegin(); it != m_editors.constEnd(); ++it) {
        se.values.insert(it.key(), PacketMakerDialog::readFieldEditor(it.value()));
    }

    for (auto it = m_repeatTables.constBegin(); it != m_repeatTables.constEnd(); ++it) {
        QTableWidget *tbl = it.value();
        QVector<QHash<QString, qint64>> rows;
        for (int r = 0; r < tbl->rowCount(); ++r) {
            QHash<QString, qint64> row;
            for (int c = 0; c < tbl->columnCount(); ++c) {
                QTableWidgetItem *hdr = tbl->horizontalHeaderItem(c);
                if (!hdr) { continue; }
                const QTableWidgetItem *cell = tbl->item(r, c);
                const QString t = cell ? cell->text().trimmed() : QString();
                bool ok = false;
                const qint64 v = t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                                     ? t.mid(2).toLongLong(&ok, 16)
                                     : t.toLongLong(&ok, 10);
                row.insert(hdr->text(), ok ? v : 0);
            }
            rows.push_back(row);
        }
        se.repeats.insert(it.key(), rows);
    }

    emit edited();
}
