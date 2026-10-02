#include "testutil.h"
#include "layoutaudit.h"

#include "packetmakerdialog.h"
#include "statusline.h"
#include "uistyle.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QGroupBox>
#include <QHeaderView>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>

// =============================================================================
//  Session 128 — UI revamp, tool windows 4: the Packet Maker, finished
//  (session 125 was the first pass and had no suite; its points are here).
//
//  Output and Vary per send share one pane as tabs; the Vary table's columns
//  fit their contents; folded sections cost one short line; the header form
//  gets the wider share; the window fits a 1366 x 768 laptop.
// =============================================================================

namespace {

QGroupBox *boxTitled(QWidget *w, const char *start)
{
    for (QGroupBox *g : w->findChildren<QGroupBox *>())
        if (g->title().startsWith(QLatin1String(start))) return g;
    return nullptr;
}

QPushButton *buttonText(QWidget *w, const QString &text)
{
    for (QPushButton *b : w->findChildren<QPushButton *>())
        if (b->text() == text) return b;
    return nullptr;
}

void settle()
{
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
}

}  // namespace

TEST_SUITE(session128)
{
    PacketMakerDialog dlg(nullptr);
    dlg.resize(1100, 720);
    dlg.show();
    settle();

    // ---- fits a laptop, nothing loose -------------------------------------
    const QSize min = dlg.minimumSizeHint();
    CHECK(min.height() <= 700, QByteArray("the window fits a 768-px screen with its title and task bars (minimum ")
                                   + QByteArray::number(min.height()) + " px; was 789)");
    CHECK(min.width() <= 1366, QByteArray("and a 1366-px width (minimum ")
                                   + QByteArray::number(min.width()) + " px)");
    CHECK(LayoutAudit::orphans(&dlg).isEmpty(), "no visible widget outside every layout");

    // ---- Output | Vary per send --------------------------------------------
    QTableWidget *vary = nullptr;
    for (QTableWidget *t : dlg.findChildren<QTableWidget *>())
        if (t->columnCount() == 5 && t->horizontalHeaderItem(1)
            && t->horizontalHeaderItem(1)->text() == QLatin1String("Mode")) vary = t;
    CHECK(vary != nullptr, "the Vary per send table is there");
    if (!vary) return;
    QTabWidget *tabs = nullptr;
    for (QTabWidget *t : dlg.findChildren<QTabWidget *>())
        if (t->isAncestorOf(vary)) tabs = t;
    CHECK(tabs && tabs->count() == 2 && tabs->tabText(0) == QLatin1String("Output"),
          "Output and Vary per send share one pane as tabs");
    CHECK(boxTitled(&dlg, "Vary per send") == nullptr, "Vary per send is no longer a box of its own");
    if (!tabs) return;

    // slrp carries FRAME_NUM, so the default "from live" rule is seeded.
    CHECK(vary->rowCount() == 1 && tabs->tabText(1) == QLatin1String("Vary per send (1 rule)"),
          QByteArray("the tab counts the seeded rule (\"") + tabs->tabText(1).toUtf8() + "\")");
    QPushButton *add = buttonText(&dlg, QStringLiteral("Add rule"));
    QPushButton *del = buttonText(&dlg, QStringLiteral("Remove rule"));
    CHECK(add && del, "the rule buttons are on the tab");
    if (add) add->click();
    CHECK(tabs->tabText(1) == QLatin1String("Vary per send (2 rules)"), "adding one counts two");
    if (del) { vary->setCurrentCell(0, 2); del->click(); vary->setCurrentCell(0, 2); del->click(); }
    CHECK(vary->rowCount() == 0 && tabs->tabText(1) == QLatin1String("Vary per send"),
          "no rules: no count");
    if (add) add->click();

    // ---- the table's columns fit what is in them ----------------------------
    tabs->setCurrentIndex(1);
    settle();
    auto *mode = qobject_cast<QComboBox *>(vary->cellWidget(0, 1));
    auto *field = qobject_cast<QComboBox *>(vary->cellWidget(0, 0));
    CHECK(mode && vary->columnWidth(1) >= mode->sizeHint().width(),
          QByteArray("the mode column shows its combo whole (")
              + QByteArray::number(vary->columnWidth(1)) + " px for " + QByteArray::number(mode ? mode->sizeHint().width() : -1)
              + "; it was cut to \"from liv\")");
    CHECK(field && vary->columnWidth(0) >= vary->columnWidth(4),
          "the field name, not Max, takes the spare width");
    CHECK(vary->horizontalHeaderItem(4)->text() == QLatin1String("Max")
              && vary->horizontalHeaderItem(4)->toolTip().contains(QLatin1String("width")),
          "\"Max\", with \"0 = full width\" as its tooltip");
    tabs->setCurrentIndex(0);

    // ---- folded sections are one short line --------------------------------
    for (const char *t : { "Also send to", "Extra header fields", "Fill from buffer" }) {
        QGroupBox *g = boxTitled(&dlg, t);
        CHECK(g && g->isCheckable() && !g->isChecked(), QByteArray(t) + ": folds, and starts folded");
        if (!g) continue;
        const int folded = g->height();
        CHECK(folded <= g->fontMetrics().height() + 16,
              QByteArray(t) + ": folded, one short line (" + QByteArray::number(folded) + " px; was 36)");
        g->setChecked(true);
        settle();
        CHECK(g->height() > folded + 20, QByteArray(t) + ": unfolded, it opens up");
        g->setChecked(false);
        settle();
        CHECK(g->height() == folded, QByteArray(t) + ": and folds back to the same line");
    }

    // ---- the header form is the wider side ---------------------------------
    QGroupBox *hdr = boxTitled(&dlg, "Header fields");
    QGroupBox *sub = boxTitled(&dlg, "Sub-packets");
    CHECK(hdr && sub && sub->isVisible() && hdr->width() > sub->width(),
          QByteArray("the header form gets the wider share (")
              + QByteArray::number(hdr ? hdr->width() : -1) + " vs " + QByteArray::number(sub ? sub->width() : -1) + " px)");
    QSplitter *vsplit = nullptr;
    for (QSplitter *s : dlg.findChildren<QSplitter *>())
        if (s->orientation() == Qt::Vertical && s->isAncestorOf(tabs)) vsplit = s;
    CHECK(vsplit && vsplit->sizes().value(0) > 2 * vsplit->sizes().value(1),
          "the field editors get most of the height, not the output");

    // ---- session 125's points ----------------------------------------------
    QPushButton *build = buttonText(&dlg, QStringLiteral("Build && Verify"));
    CHECK(build && build->property("dlRole").toString() == QLatin1String("primary"),
          "Build & Verify is the primary action");
    StatusLine *status = dlg.findChild<StatusLine *>();
    CHECK(status && status->text().contains(QLatin1String("Build & Verify"))
              && !status->text().contains(QLatin1String("&&")),
          QByteArray("status messages say \"Build & Verify\", not \"&&\" (\"")
              + (status ? status->text().left(60).toUtf8() : QByteArray()) + "\")");
    if (build) build->click();
    settle();
    CHECK(status && !status->text().contains(QLatin1String("&&")), "nor after a build");

    // ---- closing it leaves nothing dangling ---------------------------------
    // A QTableWidget emits modelReset while being destroyed, after the
    // dialog's destructor; the tab-title slot then wrote to a dying tab
    // widget, and the next restyle (another suite's UiStyle::apply())
    // crashed. Close one, restyle, and still be here.
    {
        auto *d = new PacketMakerDialog(nullptr);
        d->show();
        settle();
        delete d;
        UiStyle::apply();
        settle();
        CHECK(true, "a closed Packet Maker leaves nothing for a restyle to trip on");
    }
}
