#include "testutil.h"

#include "comparewindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "theme.h"
#include "uistyle.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTableView>

#include <algorithm>

// =============================================================================
//  Session 155 — Compare window aborted on open (Qt 6, debug build)
//
//  rebindPane connected lambdas with Qt::UniqueConnection. Uniqueness can
//  only be checked for a member-function slot, so Qt 6 refuses the connect:
//  a debug build asserts and aborts as Tools ▸ Compare Tabs binds its first
//  pane; a release build (the gate's, and the deployment machine's) logs a
//  warning and makes no connection — time-lock scrolling never followed and
//  a selected row never reached the inspector. Qt 5 accepted it and stacked
//  one more handler per source switch. The pane now keeps its two
//  connections and drops them before wiring again. Real traffic:
//  replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {
// receivers() is protected; this only reads it.
struct ReceiverPeek : QScrollBar {
    static int count(const QScrollBar *sb)
    {
        return static_cast<const ReceiverPeek *>(sb)->receivers(SIGNAL(valueChanged(int)));
    }
};

// Qt's refusal of a connect is a warning, not a failure, in release builds.
QStringList g_connectWarnings;
QtMessageHandler g_previousHandler = nullptr;
void captureConnectWarnings(QtMsgType t, const QMessageLogContext &c, const QString &m)
{
    if (m.contains(QLatin1String("QObject::connect"))) g_connectWarnings << m;
    if (g_previousHandler) g_previousHandler(t, c, m);
}
}

TEST_SUITE(session155)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: real traffic");
        qint64 ms = 1782558147000LL;
        int n = 0;
        while (!f.atEnd() && n < 400) {
            const QByteArray l = f.readLine().trimmed();
            if (!l.startsWith('@')) continue;
            disp.ingestLocal(n % 2 ? 21 : 81, 1, l, ms + n * 137, QString());
            ++n;
        }
        disp.drainNow();
    }

    g_connectWarnings.clear();
    g_previousHandler = qInstallMessageHandler(captureConnectWarnings);
    {
        CompareWindow w(&disp, nullptr);   // binds both panes: the abort was here
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1100, 720);
        w.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

        // The two panes, left to right, and the left one's source picker.
        QList<QTableView *> panes;
        for (QTableView *v : w.findChildren<QTableView *>())
            if (v->isVisible() && v->model() && v->model()->columnCount() == LogModel::ColumnCount) panes << v;
        std::sort(panes.begin(), panes.end(), [&w](QTableView *a, QTableView *b) {
            return a->mapTo(&w, QPoint()).x() < b->mapTo(&w, QPoint()).x();
        });
        CHECK(panes.size() == 2, "fixture: two bound panes");
        QComboBox *box = nullptr;
        if (panes.size() == 2)
            for (QComboBox *c : panes[0]->parentWidget()->findChildren<QComboBox *>(QString(), Qt::FindDirectChildrenOnly))
                if (c->count() >= 2) { box = c; break; }
        CHECK(box, "fixture: the left pane's picker offers two sources");

        if (panes.size() == 2 && box) {
            QScrollBar *left = panes[0]->verticalScrollBar();
            QScrollBar *right = panes[1]->verticalScrollBar();
            CHECK(left->maximum() > 0 && right->maximum() > 0, "fixture: both panes scroll");

            // ---- time-lock: scrolling one pane drives the other --------------------
            right->setValue(0);
            left->setValue(left->maximum() / 2);
            QCoreApplication::processEvents();
            CHECK(right->value() > 0,
                  QByteArray("time-lock: scrolling the left pane moved the right (") + QByteArray::number(right->value()) + ")");

            // ---- a selected row reaches the inspector -----------------------------
            // onPaneSelectionChanged relabels the bookmark action for the row:
            // a bookmarked row reads "Remove", which no other path sets.
            if (auto *m = qobject_cast<LogModel *>(panes[1]->model()))
                if (LogEntryPtr e = m->entryAt(3)) e->bookmarked = true;
            QAction *bm = nullptr;
            for (QAction *a : w.findChildren<QAction *>())
                if (a->text().contains(QLatin1String("bookmark"), Qt::CaseInsensitive)
                    && !a->text().contains(QLatin1String("Next")) && !a->text().contains(QLatin1String("Prev"))) { bm = a; break; }
            panes[1]->setCurrentIndex(panes[1]->model()->index(3, 0));
            QCoreApplication::processEvents();
            CHECK(bm && bm->text() == QLatin1String("Remove &bookmark"),
                  QByteArray("selecting a row reached the window (bookmark action: ") + (bm ? bm->text().toUtf8() : QByteArray("none")) + ")");

            // ---- switching sources neither drops nor stacks the handlers ----------
            const int before = ReceiverPeek::count(left);
            const int start = box->currentIndex();
            for (int i = 0; i < 6; ++i) {
                box->setCurrentIndex((box->currentIndex() + 1) % box->count());
                QCoreApplication::processEvents();
            }
            box->setCurrentIndex(start);
            QCoreApplication::processEvents();
            const int after = ReceiverPeek::count(left);
            CHECK(after == before,
                  QByteArray("scroll-bar receivers unchanged by six source switches (") + QByteArray::number(before)
                      + " -> " + QByteArray::number(after) + ")");
            right->setValue(0);
            left->setValue(0);
            left->setValue(left->maximum() / 2);
            QCoreApplication::processEvents();
            CHECK(right->value() > 0, "and time-lock still drives the other pane after them");
        }
    }
    qInstallMessageHandler(g_previousHandler);
    CHECK(g_connectWarnings.isEmpty(),
          QByteArray("Qt refused no connection (") + g_connectWarnings.join(QLatin1String(" | ")).toUtf8() + ")");

    // ---- no lambda is connected with Qt::UniqueConnection --------------------------
    // The gate builds release, where Qt 6's assert is compiled out, so the
    // behaviour check above cannot see the abort itself. This can.
    {
        const QDir src(QStringLiteral(DL_SRC_DIR));
        // From each Qt::UniqueConnection back to its connect(: a lambda
        // capture list followed by ( or { in between is the offence. A
        // single regex cannot do it — the lambda body has its own ';'.
        const QRegularExpression lambdaIntro(QStringLiteral("\\[[^\\]\\n]*\\]\\s*[({]"));
        const QString unique = QStringLiteral("Qt::UniqueConnection");
        QStringList offenders;
        int scanned = 0;
        for (const QString &name : src.entryList({QStringLiteral("*.cpp"), QStringLiteral("*.h")}, QDir::Files)) {
            QFile f(src.filePath(name));
            if (!f.open(QIODevice::ReadOnly)) continue;
            ++scanned;
            const QString text = QString::fromUtf8(f.readAll());
            for (int at = text.indexOf(unique); at >= 0; at = text.indexOf(unique, at + 1)) {
                const int from = int(text.lastIndexOf(QLatin1String("connect("), at));
                if (from < 0 || at - from > 800) continue;
                if (lambdaIntro.match(text.mid(from, at - from)).hasMatch()) { offenders << name; break; }
            }
        }
        CHECK(scanned > 100, "fixture: the sources were found");
        CHECK(offenders.isEmpty(),
              QByteArray("no connect(..., lambda, Qt::UniqueConnection) (") + offenders.join(QLatin1String(", ")).toUtf8() + ")");
    }
}
