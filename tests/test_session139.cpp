#include "testutil.h"
#include "layoutaudit.h"

#include "emptystate.h"
#include "packetbuilder.h"
#include "subpacketwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>

// =============================================================================
//  Session 139 — UI revamp, tool windows 16: the Sub-packet window.
//
//  Editors at their own size (a one-digit value had sat 600 px from its
//  name), the "if" condition muted after the field name, repeat tables that
//  say they are empty and how many rows they need, left headers, and the
//  rebuild hides what it removes. SLRP sub-packets 0 (MovementAuthority)
//  and 5 (TagLinking: two repeats) from the schema.
// =============================================================================

TEST_SUITE(session139)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    PacketBuilder builder;
    CHECK(builder.ready(), "fixture: the schema");
    if (!builder.ready()) return;
    QVector<Schema::SubEntry> subs;
    Schema::SubEntry ma; ma.type = 0;
    Schema::SubEntry tl; tl.type = 5;
    subs << ma << tl;

    SubPacketWindow w;
    w.resize(1100, 720);
    w.setTarget(&builder.encoder(), QStringLiteral("slrp"), &subs, 0);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    // ---- editors at their own size ----------------------------------------------------------
    int widest = 0, conditional = 0;
    for (QLabel *l : w.findChildren<QLabel *>(QStringLiteral("subFieldLabel"))) {
        if (!l->isVisible()) continue;
        if (QWidget *ed = l->buddy()) widest = qMax(widest, ed->width());
        if (!l->toolTip().isEmpty()) {
            ++conditional;
            CHECK(l->textFormat() == Qt::RichText && l->toolTip().startsWith(QLatin1String("Only on the wire when")),
                  QByteArray("a condition is muted after the name, explained in the tooltip (") + l->text().left(40).toUtf8() + ")");
        }
    }
    CHECK(widest > 0 && widest <= 320, QByteArray("editors keep their own size in a 1100-px window (widest ")
                                           + QByteArray::number(widest) + " px)");
    CHECK(conditional >= 3, "MovementAuthority's conditional fields are marked");

    // ---- TagLinking: two repeat tables -------------------------------------------------------
    w.setTarget(&builder.encoder(), QStringLiteral("slrp"), &subs, 1);
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    QList<QTableWidget *> tables;
    for (QTableWidget *t : w.findChildren<QTableWidget *>(QStringLiteral("subRepeatTable")))
        if (t->isVisible()) tables << t;
    CHECK(tables.size() == 2, "TagLinking has its two repeat tables");
    for (QTableWidget *t : tables) {
        CHECK(t->rowCount() == 0 && EmptyState::isShowing(t), "an empty repeat table says so");
        CHECK(t->horizontalHeader()->defaultAlignment() & Qt::AlignLeft, "its headers align left");
    }
    bool needsOne = false;
    for (QLabel *l : w.findChildren<QLabel *>(QStringLiteral("subRepeatCaption")))
        if (l->isVisible() && l->text().startsWith(QLatin1String("tag rows"), Qt::CaseInsensitive)
            && l->text().contains(QLatin1String("1 to 62"), Qt::CaseInsensitive)
            && l->text().contains(QLatin1String("ROUTE_RFID_CNT"), Qt::CaseInsensitive)) needsOne = true;
    CHECK(needsOne, "the tag table's caption gives its range and its count field");

    // ---- nothing stray after re-targeting -------------------------------------------------------
    w.setTarget(&builder.encoder(), QStringLiteral("slrp"), &subs, 0);
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("re-targeting leaves nothing stray (") + QByteArray::number(loose.size()) + ")");
    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366, "fits a laptop");
}
