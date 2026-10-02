#include "testutil.h"
#include "layoutaudit.h"

#include "decodeworkbench.h"
#include "rawbytespanel.h"
#include "theme.h"
#include "uistyle.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFontInfo>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QTextEdit>

// =============================================================================
//  Session 137 — UI revamp, tool windows 14: the Decode Workbench.
//
//  Hex, the status and the field table asked for
//  QFontDatabase::systemFont(FixedFont), which on some platforms (the
//  offscreen plugin, some Linux setups) is the proportional UI face: the hex
//  lost its columns. UiStyle::monoFont() (session 117) checks and falls
//  back; it is used here now, and in Replay's readout, the Live Loco
//  Console's table and the raw-bytes panel, which had the same call.
//  The input is five lines, the key combo fits its entries, headers align
//  left. Real frame: an @lsrp line from replay/loco_1_1_27062026_140226.cap.
// =============================================================================

TEST_SUITE(session137)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    QString lsrp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd() && lsrp.isEmpty()) {
            const QString l = QString::fromUtf8(f.readLine()).trimmed();
            if (l.startsWith(QLatin1String("@lsrp"))) lsrp = l;
        }
    }
    CHECK(!lsrp.isEmpty(), "fixture: a real @lsrp line");

    DecodeWorkbench w(nullptr);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.loadBuffer(lsrp);
    w.resize(1000, 700);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    auto *input = w.findChild<QPlainTextEdit *>(QStringLiteral("workbenchInput"));
    auto *table = w.findChild<QTableWidget *>(QStringLiteral("workbenchFields"));
    CHECK(input && table, "the input and the field table");
    if (input && table) {
        CHECK(QFontInfo(input->font()).fixedPitch(), "the hex input is monospaced (columns line up)");
        CHECK(QFontInfo(table->font()).fixedPitch(), "and so is the field table");
        CHECK(table->rowCount() > 20, QByteArray("the frame decoded (") + QByteArray::number(table->rowCount()) + " rows)");
        CHECK(input->height() <= input->fontMetrics().lineSpacing() * 5 + 14, "the input is five lines, not 140 px");
        CHECK(table->height() > w.height() / 2, "the field table gets most of the window");
        CHECK(table->horizontalHeader()->defaultAlignment() & Qt::AlignLeft, "headers align left");
    }
    for (QComboBox *c : w.findChildren<QComboBox *>())
        if (c->toolTip().contains(QLatin1String("auth keys")))
            CHECK(c->width() < w.width() / 2, "the session-key combo fits its entries, not half the row");

    // The same call, fixed in the panels that had it.
    {
        RawBytesPanel panel;
        bool mono = false;
        for (QTextEdit *e : panel.findChildren<QTextEdit *>()) mono = mono || QFontInfo(e->font()).fixedPitch();
        CHECK(mono, "the raw-bytes panel's hex is monospaced");
    }

    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366, "fits a laptop");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (")
                               + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
