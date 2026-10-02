#include "testutil.h"
#include "layoutaudit.h"

#include "fieldsweepdialog.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

// =============================================================================
//  Session 145 — UI revamp, tool windows 22: Field Sweep.
//
//  Setup in two columns (Packet | Sweep + Send) over the results: stacked,
//  the three boxes took 787 px, more than a 768-px screen, and left the
//  results table one row and the base values one field of lsrp's 29. Range
//  and List show only in the modes that read them; the results are in
//  UiStyle's mono with every column but Seen fitted; counts in English.
//  Seeded from a real frame: the first @lsrp of
//  replay/loco_1_1_27062026_140226.cap.
// =============================================================================

TEST_SUITE(session145)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    QString lsrp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        if (f.open(QIODevice::ReadOnly))
            while (!f.atEnd() && lsrp.isEmpty()) {
                const QString l = QString::fromUtf8(f.readLine()).trimmed();
                if (l.startsWith(QLatin1String("@lsrp"))) lsrp = l;
            }
    }
    CHECK(!lsrp.isEmpty(), "fixture: a real lsrp line");

    FieldSweepDialog d;
    d.seedFromBuffer(lsrp);
    d.resize(1100, 700);
    d.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    auto *packet = d.findChildren<QComboBox *>().value(0);
    CHECK(packet && packet->currentText() == QLatin1String("lsrp"), "fixture: seeded as lsrp");

    // ---- fits ---------------------------------------------------------------
    CHECK(d.minimumSizeHint().height() <= 700, "fits a 768-px screen (was 787 px tall at its minimum)");
    CHECK(d.minimumSizeHint().width() <= 1366, "and a 1366-px one");

    // ---- the two tables get the room ----------------------------------------
    QTableWidget *base = nullptr, *results = nullptr;
    for (QTableWidget *t : d.findChildren<QTableWidget *>())
        (t->columnCount() == 2 ? base : results) = t;
    CHECK(base && results, "fixture: both tables");
    if (!base || !results) return;
    const int rowH = base->verticalHeader()->defaultSectionSize();
    CHECK(base->rowCount() == 29, "fixture: lsrp has 29 header fields");
    CHECK(base->viewport()->height() / rowH >= 5,
          QByteArray("at least five base values in view, was one (") + QByteArray::number(base->viewport()->height())
              + " px / " + QByteArray::number(rowH) + ", dialog " + QByteArray::number(d.height()) + ")");
    CHECK(base->columnWidth(0) >= QFontMetrics(base->font()).horizontalAdvance(QStringLiteral("SOURCE_LOCO_ID")) + 8,
          "field names whole (\"SOURCE_LOCO_...\" was cut)");
    CHECK(base->columnWidth(1) >= QFontMetrics(base->font()).horizontalAdvance(QStringLiteral("16777215")) + 8,
          "and room for a 24-bit value beside them");
    CHECK(results->viewport()->height() / rowH >= 5, "the results show at least five rows (was one)");
    CHECK(base->mapTo(&d, QPoint()).y() < results->mapTo(&d, QPoint()).y()
              && base->mapTo(&d, QPoint()).x() + base->width() <= d.width() / 2 + 20,
          "the base values sit in the left column, above the results");

    // ---- the results table --------------------------------------------------
    CHECK(results->font().family() == UiStyle::monoFont().family(),
          "results in UiStyle's mono, not systemFont(FixedFont)");
    CHECK(results->horizontalHeader()->sectionResizeMode(2) == QHeaderView::ResizeToContents,
          "the Sent column fits its time (\"23:32:03.4...\" was clipped)");
    CHECK(results->horizontalHeader()->sectionResizeMode(3) == QHeaderView::Stretch, "Seen takes the rest");

    // ---- Range and List only where they are read -----------------------------
    QComboBox *mode = nullptr;
    for (QComboBox *c : d.findChildren<QComboBox *>())
        if (c->findText(QStringLiteral("Boundary values")) >= 0) mode = c;
    QLineEdit *list = nullptr, *from = nullptr;
    for (QLineEdit *e : d.findChildren<QLineEdit *>()) {
        if (e->placeholderText().startsWith(QLatin1String("0, 1, 7"))) list = e;
        if (e->text() == QLatin1String("0") && !from) from = e;
    }
    CHECK(mode && list && from, "fixture: the mode box, the list and the range");
    if (!mode || !list || !from) return;
    CHECK(mode->currentText() == QLatin1String("Boundary values") && !list->isVisible() && !from->isVisible(),
          "Boundary: neither Range nor List shows");
    mode->setCurrentIndex(mode->findText(QStringLiteral("Range")));
    CHECK(from->isVisible() && !list->isVisible(), "Range: the range row only");
    mode->setCurrentIndex(mode->findText(QStringLiteral("List")));
    CHECK(list->isVisible() && !from->isVisible(), "List: the list only");
    mode->setCurrentIndex(mode->findText(QStringLiteral("Boundary values")));

    // ---- counts in English, the run row ---------------------------------------
    auto *status = d.findChild<StatusLine *>();
    CHECK(status && status->text().startsWith(QLatin1String("5 values for PKT_TYPE")),
          QByteArray("the plan reads \"5 values for PKT_TYPE\" (") + (status ? status->text().toUtf8() : QByteArray()) + ")");
    QPushButton *start = nullptr;
    for (QPushButton *b : d.findChildren<QPushButton *>())
        if (b->text() == QLatin1String("Start sweep")) start = b;
    CHECK(start && start->property("dlRole").toString() == QLatin1String("primary"), "Start sweep is the primary button");
    CHECK(start && status && qAbs(start->mapTo(&d, QPoint()).y() + start->height() / 2
                                  - (status->mapTo(&d, QPoint()).y() + status->height() / 2)) < 12,
          "and the status reads on the same row as the buttons");

    const QStringList loose = LayoutAudit::orphans(&d);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
