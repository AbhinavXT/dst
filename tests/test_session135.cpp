#include "testutil.h"
#include "layoutaudit.h"

#include "lococonfigwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QTableView>
#include <QTemporaryDir>

// =============================================================================
//  Session 135 — UI revamp, tool windows 12: Loco Configuration.
//
//  The send bar took 304 px and left the field table six of its 170 rows in
//  a 720-px window. The summary and vcc_crc share a line, the "no reply"
//  note is one sentence, the blocker line shows only when something blocks.
//  Run under the app's stylesheet, as the app runs (session 134's lesson).
// =============================================================================

TEST_SUITE(session135)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    QTemporaryDir temp;
    CHECK(temp.isValid(), "fixture: a temporary data directory");
    LocoConfigWindow w(nullptr, temp.path());
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(w.isUsable(), "fixture: the LINFO layout loaded from the schema");
    w.resize(1100, 720);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    auto *bar = w.findChild<QFrame *>(QStringLiteral("locoSendBar"));
    QTableView *table = nullptr;
    for (QTableView *t : w.findChildren<QTableView *>()) if (t->isVisible()) table = t;
    CHECK(bar && table, "the send bar and the field table");
    if (bar && table) {
        CHECK(bar->height() <= 270, QByteArray("the send bar is compact (") + QByteArray::number(bar->height())
                                        + " px; was 304)");
        const int rows = table->viewport()->height() / qMax(1, table->rowHeight(0));
        CHECK(rows >= 9, QByteArray("the field table shows more of its fields (") + QByteArray::number(rows)
                             + " rows; was 6)");
    }

    auto *summary = w.findChild<QLabel *>(QStringLiteral("locoSummary"));
    auto *vcc = w.findChild<QLabel *>(QStringLiteral("locoVccCrc"));
    CHECK(summary && summary->text().startsWith(QStringLiteral("376 B · src 28 → dest 2 · msg 120 · loco_info_crc 0x")),
          QByteArray("the summary, short (") + (summary ? summary->text().toUtf8() : QByteArray()) + ")");
    CHECK(summary && summary->toolTip().contains(QLatin1String("376-byte")), "its tooltip spells the size out");
    CHECK(summary && vcc && qAbs(summary->y() - vcc->y()) <= 4, "summary and vcc_crc on one line");
    CHECK(vcc && vcc->text().contains(QLatin1String("set by hand")), "vcc_crc still says it is set by hand");
    auto *noReply = w.findChild<QLabel *>(QStringLiteral("locoNoReply"));
    CHECK(noReply && noReply->isVisible() && noReply->text().contains(QLatin1String("does not reply")),
          "the no-reply note is still in the send bar");
    CHECK(noReply && noReply->height() < 2 * noReply->fontMetrics().height(), "on one line");
    QWidget *send = w.findChild<QWidget *>(QStringLiteral("locoSendButton"));
    CHECK(send && bar && send->y() < bar->height() / 3, "Send sits in the bar's title row, not beside the notes");

    // ---- the blocker line: only when something blocks ---------------------------------------
    auto *blocker = w.findChild<QLabel *>(QStringLiteral("locoBlocker"));
    QLineEdit *ip1 = nullptr;
    for (QLineEdit *e : bar ? bar->findChildren<QLineEdit *>() : QList<QLineEdit *>()) { ip1 = e; break; }
    QCheckBox *on1 = bar ? bar->findChild<QCheckBox *>() : nullptr;
    CHECK(blocker && ip1 && on1, "fixture: the blocker line and target 1");
    if (blocker && ip1 && on1) {
        on1->setChecked(true);
        ip1->setText(QStringLiteral("192.168.25.168"));
        CHECK(!blocker->isVisible(), "nothing blocks: the line takes no room");
        ip1->setText(QStringLiteral("999.1.1.1"));
        CHECK(blocker->isVisible() && blocker->text().contains(QLatin1String("not a valid IPv4")),
              "a bad address: the line says why Send is off");
        ip1->setText(QStringLiteral("192.168.25.168"));
        CHECK(!blocker->isVisible(), "fixed: gone again");
    }

    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366,
          QByteArray("fits a 1366 x 768 laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + ")");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (")
                               + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
