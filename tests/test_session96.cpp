#include "testutil.h"

#include "capturedecoder.h"
#include "sessionkeystore.h"
#include "tabtags.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>

// =============================================================================
//  Session 96: TabTags and SessionKeyStore are no longer process-wide.
//  MainWindow owns one of each and hands them out.
// =============================================================================

TEST_SUITE(session96)
{
    // ---- TabTags: each hub signals only its own listeners ------------------------------------
    {
        TabTags a, b;
        QSignalSpy spyA(&a, &TabTags::changed), spyB(&b, &TabTags::changed);
        const QString key = QStringLiteral("96_1");
        const TabTag saved = a.tag(key);
        a.setColor(key, 2);
        CHECK(spyA.count() == 1 && spyB.count() == 0, "a tag set through one hub tells that hub's windows only");
        CHECK(b.tag(key).color == 2, "but the tag itself is saved, so another hub reads it");
        a.setTag(key, saved);
    }

    // ---- SessionKeyStore: the MAC row needs a store handed in -------------------------------------
    {
        QString slrp;
        const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
        for (const QString &f : dir.entryList({ QStringLiteral("*.cap") }, QDir::Files, QDir::Name)) {
            QFile file(dir.filePath(f));
            if (!file.open(QIODevice::ReadOnly)) continue;
            while (!file.atEnd() && slrp.isEmpty()) {
                const QString l = QString::fromLatin1(file.readLine()).trimmed();
                if (l.startsWith(QLatin1String("@slrp_"))) slrp = l;
            }
            if (!slrp.isEmpty()) break;
        }
        CHECK(!slrp.isEmpty(), "a real @slrp frame");
        const CaptureLine c = CaptureDecoder::parseLine(slrp);
        auto hasMacRow = [](const QVector<FieldRow> &rows) {
            for (const FieldRow &r : rows) if (r.field.contains(QLatin1String("MAC (live)"))) return true;
            return false;
        };
        CHECK(!hasMacRow(CaptureDecoder::describe(c)),
              "no key store handed in (a replay, a recording): no live MAC row");
        SessionKeyStore empty;
        const QVector<FieldRow> rows = CaptureDecoder::describe(c, nullptr, 0, nullptr, &empty);
        bool noKeyYet = false;
        for (const FieldRow &r : rows) if (r.field.contains(QLatin1String("MAC (live)")) && r.value.contains(QLatin1String("no session key"))) noKeyYet = true;
        CHECK(noKeyYet, "a store with no keys yet: the row says so");
        SessionKeyStore other;
        CHECK(empty.snapshots().isEmpty() && other.snapshots().isEmpty(), "two stores share nothing");
    }
}
