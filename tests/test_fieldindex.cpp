#include "testutil.h"

#include "fieldindexdialog.h"
#include "schema/schemadecoder.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>

// =============================================================================
//  The field index — which packets carry what.
//
//  The schema has always known this and nothing showed it. Thirty-five names
//  occur in more than one packet and FRAME_NUM is in five, so "narrow this
//  pin to a packet" was a question the operator could not answer from the pin
//  panel, which says a packet HAS a field and never how many others do.
//
//  What is worth testing is the counting and the filtering. The rest is a
//  table.
// =============================================================================

TEST_SUITE(fieldindex)
{
    Schema::Decoder dec;
    QString err;
    CHECK(dec.load(QStringLiteral(":/schema/kavach.xml"), &err),
          "the schema loads");

    FieldIndexDialog dlg(&dec);

    auto *table  = dlg.findChild<QTableWidget *>(QStringLiteral("fieldIndexTable"));
    auto *filter = dlg.findChild<QLineEdit *>(QStringLiteral("fieldIndexFilter"));
    auto *shared = dlg.findChild<QCheckBox *>(QStringLiteral("fieldIndexSharedOnly"));
    auto *summary = dlg.findChild<QLabel *>(QStringLiteral("fieldIndexSummary"));
    CHECK(table && filter && shared && summary, "the dialog is assembled");

    CHECK(table->rowCount() == dec.allFieldNames().size(),
          "one row per field the schema knows — the same set the pin chooser "
          "offers, so the two cannot disagree about what exists");

    auto rowOf = [table](const QString &field) {
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->item(r, 0) && table->item(r, 0)->text() == field) { return r; }
        }
        return -1;
    };

    // ---- the count is the answer -------------------------------------------
    {
        const int r = rowOf(QStringLiteral("FRAME_NUM"));
        CHECK(r >= 0, "FRAME_NUM is listed");
        CHECK(r >= 0 && table->item(r, 1)->data(Qt::DisplayRole).toInt() >= 4,
              "and is carried by several packets — which is the fact that "
              "makes an unnarrowed pin on it show whichever spoke last");
        CHECK(r >= 0 && table->item(r, 2)->text().contains(QStringLiteral("LSRP")),
              "with the packets named, not merely counted");
    }

    // A packet whose token differs from its name is shown as both, because
    // the token is what a pin narrows on and what a capture line says.
    {
        bool sawToken = false;
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->item(r, 2)->text().contains(QStringLiteral("LOCO_SOS (lsos)"))) {
                sawToken = true;
            }
        }
        CHECK(sawToken, "LOCO_SOS is labelled with the token it arrives as");
    }

    auto visibleRows = [table] {
        int n = 0;
        for (int r = 0; r < table->rowCount(); ++r) {
            if (!table->isRowHidden(r)) { ++n; }
        }
        return n;
    };

    const int all = visibleRows();
    CHECK(all == table->rowCount(), "everything is shown before filtering");

    // ---- filtering ---------------------------------------------------------
    {
        filter->setText(QStringLiteral("FRAME_NUM"));
        CHECK(visibleRows() >= 1 && visibleRows() < all,
              "filtering by field name narrows the table");

        // Matching packets too: "what does LSRP carry" is the same question
        // read from the other end.
        filter->setText(QStringLiteral("lsrp"));
        const int byPacket = visibleRows();
        CHECK(byPacket > 1 && byPacket < all,
              "and filtering by packet answers it from the other end");
        CHECK(summary->text().contains(QString::number(byPacket)),
              "with the count said out loud, so a filter that matched almost "
              "everything is distinguishable from one that matched all of it");

        filter->setText(QStringLiteral("no_such_field_anywhere"));
        CHECK(visibleRows() == 0, "a miss shows nothing rather than everything");

        filter->clear();
        CHECK(visibleRows() == all, "clearing restores the whole table");
    }

    // ---- the subset where narrowing actually matters -----------------------
    {
        shared->setChecked(true);
        const int sharedCount = visibleRows();
        CHECK(sharedCount > 0 && sharedCount < all,
              "the shared-only view is a real subset");

        bool allShared = true;
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->isRowHidden(r)) { continue; }
            if (table->item(r, 1)->data(Qt::DisplayRole).toInt() < 2) {
                allShared = false;
            }
        }
        CHECK(allShared,
              "and holds only fields in more than one packet — a field in a "
              "single packet needs no narrowing at all");

        CHECK(shared->text().contains(QString::number(sharedCount)),
              "the checkbox says how many there are, which is the number "
              "worth knowing before pinning anything");

        // The two narrowings compose rather than one overriding the other.
        filter->setText(QStringLiteral("FRAME_NUM"));
        CHECK(visibleRows() == 1, "filter and shared-only apply together");
        shared->setChecked(false);
        filter->clear();
    }

    // ---- pre-filling from a right-clicked field ----------------------------
    {
        dlg.setFilter(QStringLiteral("FRAME_NUM"));
        CHECK(filter->text() == QStringLiteral("FRAME_NUM"),
              "opening from a field lands on that field — arriving at five "
              "hundred names when one was asked about is an answer the "
              "operator then has to search for");
        CHECK(visibleRows() >= 1 && visibleRows() < all,
              "and the table is already narrowed to it");
    }

    // ---- a schema that changed under it ------------------------------------
    {
        FieldIndexDialog empty(nullptr);
        auto *t = empty.findChild<QTableWidget *>(QStringLiteral("fieldIndexTable"));
        CHECK(t && t->rowCount() == 0,
              "with no decoder there are no rows, rather than a crash");
        empty.reload();
        CHECK(t && t->rowCount() == 0, "and reloading one is still safe");
    }
}
