#include "pinpanel.h"

#include "settings.h"
#include "statusline.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QStringListModel>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

enum Col { ColField = 0, ColValue, ColWas, ColAge, ColCount };

// "just now", "12 s", "4 m 20 s". Ages are read at a glance and compared
// against each other, so the units are spelled rather than left as a raw
// second count that has to be divided in the operator's head.
QString ageText(qint64 ms)
{
    if (ms < 0)     { return QStringLiteral("—"); }
    if (ms < 1500)  { return QStringLiteral("just now"); }
    const qint64 s = ms / 1000;
    if (s < 60)     { return QStringLiteral("%1 s").arg(s); }
    const qint64 m = s / 60;
    if (m < 60)     { return QStringLiteral("%1 m %2 s").arg(m).arg(s % 60); }
    return QStringLiteral("%1 h %2 m").arg(m / 60).arg(m % 60);
}

}  // namespace

PinPanel::PinPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(UiStyle::margin(), UiStyle::margin(),
                             UiStyle::margin(), UiStyle::margin());
    root->setSpacing(UiStyle::gap());

    auto *pickRow = new QHBoxLayout;
    pickRow->setSpacing(UiStyle::gap());

    // Typing and browsing are two different questions, and one editable
    // combo answered neither well. An operator who knows the name wants to
    // type four letters and be done; an operator who does not wants to look
    // through what the equipment can send. So: a box that completes as you
    // type, and beside it a menu nested by packet.
    m_fieldEdit = new QLineEdit(this);
    m_fieldEdit->setObjectName(QStringLiteral("pinFieldBox"));
    m_fieldEdit->setPlaceholderText(tr("field name"));
    m_fieldEdit->setClearButtonEnabled(true);
    m_fieldEdit->setToolTip(
        tr("A decoder field name. Type it — completion matches anywhere in\n"
           "the name, not just the start — or browse them by packet with\n"
           "the button beside this one."));
    pickRow->addWidget(m_fieldEdit, 1);

    // Completion over every name the schema knows, flat and unnested: it is
    // answering "which name starts like this", where the packet a name
    // belongs to is not part of the question.
    m_completions = new QStringListModel(this);
    auto *complete = new QCompleter(m_completions, this);
    complete->setCaseSensitivity(Qt::CaseInsensitive);
    complete->setFilterMode(Qt::MatchContains);
    complete->setMaxVisibleItems(16);
    m_fieldEdit->setCompleter(complete);

    m_fieldMenu = new QMenu(this);
    m_browseBtn = new QToolButton(this);
    m_browseBtn->setObjectName(QStringLiteral("pinFieldBrowseBtn"));
    m_browseBtn->setText(tr("Browse"));
    m_browseBtn->setPopupMode(QToolButton::InstantPopup);
    m_browseBtn->setMenu(m_fieldMenu);
    m_browseBtn->setToolTip(
        tr("Every field the schema can decode, under the packet that\n"
           "carries it.\n\n"
           "Picking one here also sets the packet beside it, because the\n"
           "pick already said which packet was meant — thirty-five names\n"
           "occur in more than one, and FRAME_NUM is in five."));
    pickRow->addWidget(m_browseBtn);

    m_sourceBox = new QComboBox(this);
    m_sourceBox->setObjectName(QStringLiteral("pinSourceBox"));
    m_sourceBox->setToolTip(
        tr("Narrow the pin to one source, so another loco's value for the\n"
           "same field cannot overwrite it."));
    pickRow->addWidget(m_sourceBox);

    m_packetBox = new QComboBox(this);
    m_packetBox->setObjectName(QStringLiteral("pinPacketBox"));
    m_packetBox->setToolTip(
        tr("Narrow the pin to one packet.\n\n"
           "Thirty-five field names in the schema appear in more than one\n"
           "packet — FRAME_NUM is in five — so an unnarrowed pin on one of\n"
           "those shows whichever packet arrived last."));
    pickRow->addWidget(m_packetBox);

    m_addBtn = new QPushButton(tr("Pin"), this);
    m_addBtn->setObjectName(QStringLiteral("pinAddBtn"));
    pickRow->addWidget(m_addBtn);
    root->addLayout(pickRow);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setObjectName(QStringLiteral("pinTable"));
    m_table->setHorizontalHeaderLabels({ tr("Field"), tr("Now"),
                                         tr("Was"), tr("Changed") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(ColField, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColValue, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColWas,   QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColAge,   QHeaderView::ResizeToContents);
    root->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(UiStyle::gap());
    m_delBtn = new QPushButton(tr("Unpin"), this);
    m_delBtn->setObjectName(QStringLiteral("pinRemoveBtn"));
    btnRow->addWidget(m_delBtn);

    m_freezeBtn = new QPushButton(tr("Freeze"), this);
    m_freezeBtn->setObjectName(QStringLiteral("pinFreezeBtn"));
    m_freezeBtn->setCheckable(true);
    m_freezeBtn->setToolTip(
        tr("Hold every value where it is.\n\n"
           "The board answers \"what is it now\", which is the wrong tense the\n"
           "moment something happens: by the time you look up from the DMI,\n"
           "now has moved on. Nothing is lost by freezing except the updates."));
    btnRow->addWidget(m_freezeBtn);

    m_autoFreeze = new QCheckBox(tr("on watch"), this);
    m_autoFreeze->setObjectName(QStringLiteral("pinAutoFreezeBox"));
    m_autoFreeze->setToolTip(
        tr("Freeze automatically when a watch fires, so the state of every\n"
           "pinned field at that instant is still there to read."));
    m_autoFreeze->setChecked(Settings::pinFreezeOnWatch());
    btnRow->addWidget(m_autoFreeze);

    btnRow->addStretch(1);
    root->addLayout(btnRow);

    m_status = new StatusLine(this);
    m_status->state(tr("Pin a field to watch it while traffic runs."));
    root->addWidget(m_status);

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        QTableWidgetItem *it = m_table->itemAt(pos);
        if (!it || it->row() >= m_board.count()) { return; }
        const PinBoard::Pin &p = m_board.pins().at(it->row());

        QMenu menu(this);
        // Named for what it does: "the frame where it became this", not
        // "go to source", which is what the double-click used to do and is a
        // different and much less useful thing.
        QAction *go = p.originEpochMs() > 0
            ? menu.addAction(p.everChanged()
                                 ? tr("Go to the frame where it became %1").arg(p.value)
                                 : tr("Go to the frame it was first seen in"))
            : nullptr;
        QAction *plot = menu.addAction(tr("Plot %1 over time").arg(p.field));
        QAction *drop = menu.addAction(tr("Unpin %1").arg(p.field));
        QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
        if (go && chosen == go) {
            emit revealRequested(p.fromSource, p.originEpochMs());
        } else if (chosen == plot) {
            // The pin's own source if it has one, otherwise wherever the
            // current value came from — plotting a field means plotting it
            // somewhere, and "any source" is not a somewhere.
            emit plotRequested(p.field, p.sourceKey.isEmpty() ? p.fromSource
                                                              : p.sourceKey);
        } else if (chosen == drop) {
            m_table->setCurrentCell(it->row(), 0);
            removeSelected();
        }
    });

    connect(m_freezeBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on) {
            m_board.freeze(QDateTime::currentMSecsSinceEpoch(), tr("by hand"));
        } else {
            m_board.thaw();
        }
        refresh();
    });
    connect(m_autoFreeze, &QCheckBox::toggled, this, [](bool on) {
        Settings::setPinFreezeOnWatch(on);
    });

    connect(m_addBtn, &QPushButton::clicked, this, &PinPanel::addPin);
    connect(m_delBtn, &QPushButton::clicked, this, &PinPanel::removeSelected);
    connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *it) {
        if (!it) { return; }
        const int r = it->row();
        if (r < 0 || r >= m_board.count()) { return; }
        const PinBoard::Pin &p = m_board.pins().at(r);
        if (p.fromSource.isEmpty() || p.originEpochMs() <= 0) {
            m_status->warn(tr("Nothing has carried %1 yet, so there is no "
                              "frame to go to.").arg(p.field));
            return;
        }
        emit revealRequested(p.fromSource, p.originEpochMs());
    });

    // The values change when traffic arrives; the AGES change on their own.
    // Without this the panel looks frozen the moment a source goes quiet,
    // which is exactly when the age is the thing worth reading.
    m_ageTick = new QTimer(this);
    m_ageTick->setInterval(1000);
    connect(m_ageTick, &QTimer::timeout, this, &PinPanel::refresh);
    m_ageTick->start();
}

// "SLRP" when the token adds nothing, "LOCO_SOS (lsos)" when it does.
//
// Both halves earn their place. The packet name is what the schema and the
// specification call it; the token is what actually appears in a capture
// line and what a pin is matched against. They agree for most packets and
// differ for exactly the ones that have caused trouble — LOCO_SOS arrives
// as `lsos`, and ARP arrives as two different tokens depending on which end
// of the link sent it.
static QString packetLabel(const PinPacketFields &pk)
{
    if (pk.captype.isEmpty()) { return pk.packet; }
    if (pk.packet.compare(pk.captype, Qt::CaseInsensitive) == 0) {
        return pk.packet;
    }
    return QStringLiteral("%1 (%2)").arg(pk.packet, pk.captype);
}

void PinPanel::pickField(const QString &field, const QString &captype)
{
    m_fieldEdit->setText(field);
    m_fieldEdit->setFocus();
    if (!captype.isEmpty()) {
        const int i = m_packetBox->findData(captype);
        if (i >= 0) { m_packetBox->setCurrentIndex(i); }
    }
}

void PinPanel::addFieldActions(QMenu *menu, const QStringList &fields,
                               const QString &captype)
{
    if (fields.isEmpty()) {
        // Said out loud rather than left as an empty menu. NMSHLTH decodes
        // entirely through a flag table and names no fields at all, and a
        // submenu that opened onto nothing would read as a bug.
        QAction *none = menu->addAction(tr("(no named fields)"));
        none->setEnabled(false);
        return;
    }

    if (fields.size() <= kBucketLimit) {
        for (const QString &f : fields) {
            QAction *a = menu->addAction(f);
            a->setData(captype);
            connect(a, &QAction::triggered, this,
                    [this, f, captype] { pickField(f, captype); });
        }
        return;
    }

    // Alphabetical buckets, merged until each holds about kBucketTarget.
    // Split strictly by initial letter instead and LINFO would open onto a
    // row of submenus holding one name each, which is a worse list than the
    // one this replaced.
    int from = 0;
    while (from < fields.size()) {
        int to = qMin(from + kBucketTarget, fields.size());
        // Never split a letter across two buckets: an operator looking for
        // a name beginning with D should have one place to look.
        const QChar letter = fields.at(to - 1).at(0).toUpper();
        while (to < fields.size()
               && fields.at(to).at(0).toUpper() == letter) {
            ++to;
        }
        // Whatever is left over is folded into this bucket rather than left
        // as a bucket of two.
        if (fields.size() - to < kBucketTarget / 2) { to = fields.size(); }

        const QChar first = fields.at(from).at(0).toUpper();
        const QChar last  = fields.at(to - 1).at(0).toUpper();
        QMenu *bucket = menu->addMenu(
            first == last ? QString(first)
                          : QStringLiteral("%1 – %2").arg(first).arg(last));
        for (int i = from; i < to; ++i) {
            const QString f = fields.at(i);
            QAction *a = bucket->addAction(f);
            a->setData(captype);
            connect(a, &QAction::triggered, this,
                    [this, f, captype] { pickField(f, captype); });
        }
        from = to;
    }
}

QMenu *PinPanel::buildPacketMenu(const PinPacketFields &pk, QWidget *parent)
{
    auto *menu = new QMenu(packetLabel(pk), parent);
    addFieldActions(menu, pk.fields, pk.captype);
    return menu;
}

void PinPanel::setAvailableFields(const QStringList &seenHere,
                                  const QVector<PinPacketFields> &byPacket)
{
    m_fieldMenu->clear();

    // What this tab has actually carried, first and flat. It is a few dozen
    // names out of five hundred and nearly always the one wanted, so it is
    // not worth making the operator guess which packet it came from before
    // they can reach it.
    if (!seenHere.isEmpty()) {
        QMenu *here = m_fieldMenu->addMenu(
            tr("Seen in this tab (%1)").arg(seenHere.size()));
        addFieldActions(here, seenHere, QString());
        m_fieldMenu->addSeparator();
    }

    QSet<QString> everyName;
    for (const QString &f : seenHere) { everyName.insert(f); }
    for (const PinPacketFields &pk : byPacket) {
        m_fieldMenu->addMenu(buildPacketMenu(pk, m_fieldMenu));
        for (const QString &f : pk.fields) { everyName.insert(f); }
    }

    QStringList flat = everyName.values();
    flat.sort(Qt::CaseInsensitive);
    m_completions->setStringList(flat);

    m_fieldEdit->setToolTip(
        tr("A decoder field name. Type it — completion matches anywhere in "
           "the name — or browse the %1 the schema knows, by packet, with "
           "the button beside this one.\n\n"
           "%2 of them have arrived in this tab so far. The rest matter too: "
           "the field worth watching is often one that has not arrived yet.")
            .arg(flat.size()).arg(seenHere.size()));
}

void PinPanel::setAvailablePackets(const QVector<PinPacketFields> &packets)
{
    const QString keep = m_packetBox->currentData().toString();
    m_packetBox->clear();
    m_packetBox->addItem(tr("any packet"), QString());

    // Keyed on the CAPTYPE TOKEN, not the packet name, because the token is
    // what PinBoard::observe compares a narrowed pin against. Filled with
    // names this combo offered narrowings that could never match: a pin on
    // LOCO_SOS waited forever for a packet that arrives calling itself
    // `lsos`, and a pin on ARP quietly ignored every ARP received from
    // another loco, which arrives as `arprecv`.
    QVector<PinPacketFields> sorted = packets;
    std::sort(sorted.begin(), sorted.end(),
              [](const PinPacketFields &a, const PinPacketFields &b) {
                  return a.captype.compare(b.captype, Qt::CaseInsensitive) < 0;
              });
    for (const PinPacketFields &pk : sorted) {
        if (pk.captype.isEmpty()) { continue; }
        m_packetBox->addItem(packetLabel(pk), pk.captype);
    }
    const int i = m_packetBox->findData(keep);
    if (i >= 0) { m_packetBox->setCurrentIndex(i); }
}

void PinPanel::setAvailableSources(const QStringList &tabKeys)
{
    const QString keep = m_sourceBox->currentData().toString();
    m_sourceBox->clear();
    m_sourceBox->addItem(tr("any source"), QString());
    for (const QString &k : tabKeys) { m_sourceBox->addItem(k, k); }
    const int i = m_sourceBox->findData(keep);
    if (i >= 0) { m_sourceBox->setCurrentIndex(i); }
}

void PinPanel::addPin()
{
    const QString field = m_fieldEdit->text().trimmed();
    if (field.isEmpty()) {
        m_status->warn(tr("Name a field first."));
        return;
    }
    if (!m_board.add(field, m_sourceBox->currentData().toString(),
                     m_packetBox->currentData().toString())) {
        m_status->warn(tr("%1 is already pinned.").arg(field));
        return;
    }
    m_status->ok(tr("Pinned %1.").arg(field));
    persist();
    refresh();
}

void PinPanel::removeSelected()
{
    const int r = m_table->currentRow();
    if (r < 0) {
        m_status->warn(tr("Select a pin to remove."));
        return;
    }
    const QString name = m_board.pins().at(r).field;
    m_board.remove(r);
    m_status->say(tr("Unpinned %1.").arg(name));
    persist();
    refresh();
}

void PinPanel::observe(const LogEntryPtr &entry, const QString &sourceKey)
{
    if (m_board.observe(entry, sourceKey, QDateTime::currentMSecsSinceEpoch())) {
        m_dirty = true;
        refresh();
    }
}

void PinPanel::freezeNow(const QString &why)
{
    if (!m_autoFreeze || !m_autoFreeze->isChecked()) { return; }
    m_board.freeze(QDateTime::currentMSecsSinceEpoch(), why);
    // Keep the button honest: it is the one place that says whether the board
    // is live, and a frozen board under a button reading "Freeze" would be a
    // lie told by the control meant to report it.
    if (m_freezeBtn) {
        QSignalBlocker block(m_freezeBtn);
        m_freezeBtn->setChecked(true);
    }
    refresh();
}

void PinPanel::refresh()
{
    const auto &pins = m_board.pins();
    if (m_table->rowCount() != pins.size()) {
        m_table->setRowCount(pins.size());
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < pins.size(); ++i) {
        const PinBoard::Pin &p = pins.at(i);

        // Field, then whatever it is narrowed to. Two pins on the same field
        // narrowed differently are the normal case once FRAME_NUM is on the
        // board twice, and a label that showed only the field would make them
        // look like a duplicate.
        QStringList narrowing;
        if (!p.captype.isEmpty())   { narrowing << p.captype; }
        if (!p.sourceKey.isEmpty()) { narrowing << p.sourceKey; }
        const QString label = narrowing.isEmpty()
            ? p.field
            // Both separators QStringLiteral. QLatin1String on this one read
            // the middle dot's two UTF-8 bytes as two Latin-1 characters, so
            // the label came out "slrp Â· 81_1" on screen while the identical
            // dot beside it rendered correctly.
            : QStringLiteral("%1 · %2")
                  .arg(p.field, narrowing.join(QStringLiteral(" · ")));

        auto set = [this, i](int col, const QString &text, const QString &tip = QString()) {
            QTableWidgetItem *it = m_table->item(i, col);
            if (!it) { it = new QTableWidgetItem; m_table->setItem(i, col, it); }
            if (it->text() != text) { it->setText(text); }
            if (!tip.isEmpty()) { it->setToolTip(tip); }
            return it;
        };

        set(ColField, label);

        if (p.seen == 0) {
            // Pinned but never carried. Distinct from a value of zero, and
            // usually means the field is spelled differently or lives in a
            // packet this source does not send.
            QTableWidgetItem *v = set(ColValue, tr("not seen yet"),
                tr("Nothing carrying %1 has arrived since it was pinned.\n"
                   "Check the spelling, or that the source sends the packet "
                   "this field belongs to.").arg(p.field));
            v->setForeground(UiColor::muted());
            set(ColWas, QString());
            set(ColAge, QStringLiteral("—"));
            continue;
        }

        set(ColValue, p.value,
            tr("From %1, %2 ago.\n\n"
               "Double-click to go to the frame where it became this.")
                .arg(p.fromSource.isEmpty() ? tr("an unnamed source") : p.fromSource,
                     ageText(now - p.atMs)));
        set(ColWas, p.everChanged() ? p.prevValue : QString());
        set(ColAge, p.everChanged() ? ageText(now - p.changedMs)
                                    : tr("unchanged"));
    }

    if (m_board.isFrozen()) {
        // Sticky and stated: a frozen board looks exactly like a live board
        // whose traffic has stopped, and mistaking one for the other during a
        // run is how a stale reading gets written down as a current one.
        m_status->warn(tr("Frozen %1 (%2) — values held, updates paused.")
                           .arg(QDateTime::fromMSecsSinceEpoch(m_board.frozenAtMs())
                                    .toString(QStringLiteral("HH:mm:ss")),
                                m_board.frozenWhy().isEmpty() ? tr("by hand")
                                                              : m_board.frozenWhy()));
    } else if (pins.isEmpty()) {
        m_status->state(tr("Pin a field to watch it while traffic runs."));
    } else if (m_dirty) {
        m_dirty = false;
    }
}

void PinPanel::restore()
{
    m_board.fromStrings(Settings::pinnedFields());
    refresh();
}

void PinPanel::persist() const
{
    Settings::setPinnedFields(m_board.toStrings());
}
