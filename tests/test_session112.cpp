#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"

#include <QSignalSpy>

// =============================================================================
//  Session 112 — the dispatcher: a lookup that created tabs, and NULs.
//
//  1. modelForKey() created a model on a miss. A model made by a lookup is
//     one ingest then finds already there, so the tab was never announced
//     and MainWindow showed none of that source's traffic. Now lookups only
//     look; ensureModel() creates.
//  2. Checked, not changed: an embedded NUL does not cut a line short on
//     its way into a tab (buildEntry converts with an explicit length); only
//     trailing NUL padding is stripped, on purpose.
//
//  Real line: @lsrp from replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {
const QByteArray kLsrp =
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B";
}

TEST_SUITE(session112)
{
    // ---- a lookup no longer makes a tab-less model ---------------------------------
    {
        MessageDispatcher disp;
        QSignalSpy tabs(&disp, &MessageDispatcher::tabRequested);
        CHECK(disp.modelForKey(QStringLiteral("21_1")) == nullptr,
              "looking up a source not yet heard from finds nothing");
        CHECK(disp.modelForKey(QStringLiteral("21_1")) == nullptr, "…and still nothing: the look made none");

        // The source then speaks.
        disp.ingestLocal(21, 1, kLsrp, 1000, QString());
        disp.drainNow();
        CHECK(tabs.count() == 1 && tabs.at(0).at(0).toString() == QLatin1String("21_1"),
              "its first line announces its tab (before: a looked-up model swallowed this, no tab ever)");
        LogModel *m = disp.modelForKey(QStringLiteral("21_1"));
        CHECK(m && m->count() == 1 && m->entryAt(0)->text == QString::fromLatin1(kLsrp),
              "and the line is in it");
        CHECK(disp.modelForKey(QStringLiteral("21_1")) == m, "the same model on every lookup");

        bool created = true;
        CHECK(disp.ensureModel(QStringLiteral("21_1"), &created) == m && !created,
              "ensureModel finds an existing one without making another");
        CHECK(disp.ensureModel(QStringLiteral("99_9"), &created) && created, "and makes a new one when asked");
    }

    // ---- NULs on the way into a tab ---------------------------------------------------
    {
        MessageDispatcher disp;
        QByteArray withNul = kLsrp;
        withNul.insert(10, '\0');                       // inside the line
        disp.ingestLocal(21, 1, withNul + QByteArray(4, '\0'), 1000, QString());   // plus padding
        disp.drainNow();
        LogModel *m = disp.modelForKey(QStringLiteral("21_1"));
        const QString text = m && m->count() ? m->entryAt(0)->text : QString();
        CHECK(text.size() == withNul.size(),
              QByteArray("an embedded NUL does not cut the line short (") + QByteArray::number(text.size())
                  + " of " + QByteArray::number(withNul.size()) + " chars)");
        CHECK(text.endsWith(QLatin1String("ED 59 9B")), "everything after the NUL arrives");
        CHECK(!text.endsWith(QChar(0)), "trailing NUL padding is stripped, as intended");
        CHECK(m && m->entryAt(0)->rawBytes.endsWith(QByteArray(4, '\0')), "the raw bytes keep everything");
    }
}
