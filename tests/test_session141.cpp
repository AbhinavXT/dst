#include "testutil.h"
#include "layoutaudit.h"

#include "faultpanelwindow.h"
#include "messagedispatcher.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFontInfo>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QToolBar>

// =============================================================================
//  Session 141 — UI revamp, tool windows 18: the Fault panel.
//
//  "Save report…" was a toolbar of one action, a whole row above the
//  panel's own header; it is a button at the end of that header now. Codes
//  read "0x02" (were "0X02"), the code column uses UiStyle::monoFont(),
//  headers align left. Real faults: replay/loco_1_1_26062026_162418.cap,
//  which raises and clears NMS faults.
// =============================================================================

TEST_SUITE(session141)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher disp;
    FaultPanelWindow w(&disp);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1120, 700);
    w.show();
    QFile cap(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_26062026_162418.cap"));
    int n = 0;
    if (cap.open(QIODevice::ReadOnly)) {
        while (!cap.atEnd()) {
            const QByteArray l = cap.readLine().trimmed();
            if (!l.startsWith('@')) continue;
            disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(l.split(' ').value(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
            if (++n % 500 == 0) { disp.drainNow(); QCoreApplication::processEvents(); }
        }
    }
    disp.drainNow();
    QElapsedTimer t;
    t.start();
    auto *table = w.findChild<QTableWidget *>(QStringLiteral("faultTable"));
    while (t.elapsed() < 3000 && !(table && table->rowCount() > 0)) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(n > 9000 && table && table->rowCount() == 4, QByteArray("fixture: the real run leaves 4 faults active (")
                                                          + QByteArray::number(table ? table->rowCount() : -1) + ")");

    CHECK(w.findChildren<QToolBar *>().isEmpty(), "no one-action toolbar row");
    auto *save = w.findChild<QPushButton *>(QStringLiteral("faultSaveReport"));
    CHECK(save && save->isVisible() && save->y() < 60, "Save report… sits in the header row");

    if (table && table->rowCount() > 0) {
        bool lowerX = true, mono = true;
        for (int r = 0; r < table->rowCount(); ++r) {
            const QString code = table->item(r, 4)->text();
            lowerX = lowerX && code.startsWith(QLatin1String("0x")) && code.mid(2) == code.mid(2).toUpper();
            mono = mono && QFontInfo(table->item(r, 4)->font()).fixedPitch();
        }
        CHECK(lowerX, QByteArray("codes read 0x02, not 0X02 (") + table->item(0, 4)->text().toUtf8() + ")");
        CHECK(mono, "codes are monospaced");
        CHECK(table->horizontalHeader()->defaultAlignment() & Qt::AlignLeft, "headers align left");
    }
    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366, "fits a laptop");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
