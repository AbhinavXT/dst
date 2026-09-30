#include "testutil.h"
#include "savedata.h"
#include "sessionreader.h"
#include "messagedispatcher.h"
#include "settings.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSharedPointer>
#include <QTemporaryDir>

static LogEntryPtr wireEntry(qint64 ms, quint8 src, quint16 kv, const char *text) {
    QByteArray body(text);
    QByteArray wire;
    wire.append(char(src)); wire.append(char(101)); wire.append(char(7));
    wire.append(char(body.size() & 0xFF)); wire.append(char(body.size() >> 8));
    wire.append(char(kv & 0xFF)); wire.append(char(kv >> 8));
    wire.append(body);
    return MessageDispatcher::buildEntry(wire, ms, nullptr);
}

// Saving a tab must produce a .dlr beside the .log that reopens as a real
// recorded session. The failure worth guarding is a sidecar that exists but
// holds nothing — it looks like a recording and is not one.
TEST_SUITE(savedlr)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir");

    // ---- a tab with real datagrams ---------------------------------------
    {
        QVector<LogEntryPtr> snap;
        for (int i = 0; i < 12; ++i) {
            snap.append(wireEntry(100000 + i * 1000, 33, 1, "RAD IN Link 1 Error"));
        }
        const QString logPath = tmp.path() + "/L2_V1_08082026_140530.log";

        QEventLoop loop;
        QString gotLog, gotRaw; int gotRecords = -1;
        auto *w = new SaveData(logPath, "L2_V1", snap, 33, 1, true);
        QObject::connect(w, &SaveData::saveFinished,
            [&](const QString &l, const QString &r, int n) {
                gotLog = l; gotRaw = r; gotRecords = n; loop.quit();
            });
        QObject::connect(w, &SaveData::saveFailed,
            [&](const QString &, const QString &) { loop.quit(); });
        w->start();
        loop.exec();
        w->wait(3000);

        CHECK(gotLog == logPath, "text log path reported");
        CHECK(QFile::exists(logPath), "text log written");
        CHECK(!gotRaw.isEmpty(), "a sidecar path was reported");
        CHECK(QFile::exists(gotRaw), "sidecar exists on disk");
        CHECK(gotRecords == 12, "all twelve datagrams recorded");

        // Same basename, .dlr extension — the pair must stay obviously
        // related in a directory listing.
        CHECK(QFileInfo(gotRaw).completeBaseName()
              == QFileInfo(logPath).completeBaseName(),
              "sidecar shares the log's basename");
        CHECK(gotRaw.endsWith(".dlr"), "sidecar has the .dlr extension");
        CHECK(QFileInfo(gotRaw).absolutePath()
              == QFileInfo(logPath).absolutePath(),
              "written beside the log, not elsewhere");

        // The real proof: it reads back as a session.
        SessionReader r;
        CHECK(r.open(gotRaw), "sidecar opens as a session file");
        CHECK(r.tabKey() == "33_1",
              "file header carries the source, so it reopens self-identified");
        int n = 0;
        bool textOk = true;
        while (r.next()) {
            LogEntryPtr e = MessageDispatcher::buildEntry(
                r.wire(), r.arrivalMs(), nullptr);
            if (!e || e->text != "RAD IN Link 1 Error") textOk = false;
            if (e && e->tabKey() != "33_1") textOk = false;
            ++n;
        }
        CHECK(n == 12, "twelve records read back");
        CHECK(textOk, "each record rebuilds to the original message");
        CHECK(r.status() == SessionReader::Ok, "clean EOF, no corruption");
    }

    // ---- a tab of synthetic rows only ------------------------------------
    // Banners and replayed rows carry no wire bytes. A .dlr holding only a
    // file header would look like a recording and contain nothing.
    {
        QVector<LogEntryPtr> snap;
        for (int i = 0; i < 3; ++i) {
            auto e = QSharedPointer<LogEntry>::create();
            e->epochMs = 200000 + i; e->header.source_id = 33; e->header.kvchId = 1;
            e->text = "*** 5 messages dropped ***"; e->cacheDerived();
            snap.append(e);                       // no rawBytes
        }
        const QString logPath = tmp.path() + "/synthetic.log";

        QEventLoop loop;
        QString gotRaw = "unset"; int gotRecords = -1;
        auto *w = new SaveData(logPath, "syn", snap, 33, 1, true);
        QObject::connect(w, &SaveData::saveFinished,
            [&](const QString &, const QString &r, int n) {
                gotRaw = r; gotRecords = n; loop.quit();
            });
        w->start(); loop.exec(); w->wait(3000);

        CHECK(QFile::exists(logPath), "text log still written");
        CHECK(gotRaw.isEmpty(), "no sidecar path reported");
        CHECK(gotRecords == 0, "and no records claimed");
        CHECK(!QFile::exists(tmp.path() + "/synthetic.dlr"),
              "no empty .dlr left on disk to mislead anyone");
    }

    // ---- raw capture disabled --------------------------------------------
    {
        QVector<LogEntryPtr> snap{ wireEntry(300000, 21, 2, "CAN OUT ok") };
        const QString logPath = tmp.path() + "/nocapture.log";
        QEventLoop loop;
        QString gotRaw = "unset";
        auto *w = new SaveData(logPath, "n", snap, 21, 2, false);
        QObject::connect(w, &SaveData::saveFinished,
            [&](const QString &, const QString &r, int) { gotRaw = r; loop.quit(); });
        w->start(); loop.exec(); w->wait(3000);
        CHECK(QFile::exists(logPath), "text log written");
        CHECK(gotRaw.isEmpty(), "writeRaw=false produces no sidecar");
        CHECK(!QFile::exists(tmp.path() + "/nocapture.dlr"), "and no file");
    }

    // ---- mixed: synthetic rows interleaved with real ones ----------------
    {
        QVector<LogEntryPtr> snap;
        snap.append(wireEntry(400000, 99, 7, "VCC IN nominal"));
        auto banner = QSharedPointer<LogEntry>::create();
        banner->epochMs = 400500; banner->header.source_id = 99;
        banner->header.kvchId = 7; banner->text = "*** dropped ***";
        banner->cacheDerived();
        snap.append(banner);
        snap.append(wireEntry(401000, 99, 7, "VCC IN nominal"));

        const QString logPath = tmp.path() + "/mixed.log";
        QEventLoop loop; QString gotRaw; int gotRecords = -1;
        auto *w = new SaveData(logPath, "m", snap, 99, 7, true);
        QObject::connect(w, &SaveData::saveFinished,
            [&](const QString &, const QString &r, int n) {
                gotRaw = r; gotRecords = n; loop.quit();
            });
        w->start(); loop.exec(); w->wait(3000);

        CHECK(gotRecords == 2,
              "only the two wire messages are recorded, the banner is skipped");
        SessionReader r;
        CHECK(r.open(gotRaw), "mixed sidecar opens");
        int n = 0; while (r.next()) ++n;
        CHECK(n == 2, "and holds exactly the real datagrams");
    }
}

// ---------------------------------------------------------------------------
// The combination that motivated separating the two settings: continuous
// logging switched OFF, but a deliberate Save must still capture the bytes.
// ---------------------------------------------------------------------------
TEST_SUITE(savedlr_nocontinuous)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir");

    const bool origDisk = Settings::diskLoggingEnabled();
    const bool origRaw  = Settings::rawCapture();
    const bool origSave = Settings::saveIncludesRaw();

    // Continuous logging off, continuous raw capture off — but the manual
    // save sidecar on. Nothing is being written in the background at all.
    Settings::setDiskLoggingEnabled(false);
    Settings::setRawCapture(false);
    Settings::setSaveIncludesRaw(true);

    CHECK(!Settings::diskLoggingEnabled(), "continuous logging is off");
    CHECK(!Settings::rawCapture(),         "continuous raw capture is off");
    CHECK(Settings::saveIncludesRaw(),
          "but the save sidecar setting is independent and still on");

    QVector<LogEntryPtr> snap;
    for (int i = 0; i < 5; ++i) {
        snap.append(wireEntry(500000 + i * 1000, 33, 1, "RAD IN Link 1 Error"));
    }
    // Raw bytes must be present in memory regardless of any disk setting —
    // this is what makes the manual save possible at all.
    CHECK(!snap.first()->rawBytes.isEmpty(),
          "entries carry raw bytes even with all disk capture disabled");

    const QString logPath = tmp.path() + "/manual_save.log";
    QEventLoop loop;
    QString gotRaw; int gotRecords = -1;
    auto *w = new SaveData(logPath, "m", snap, 33, 1,
                           Settings::saveIncludesRaw());
    QObject::connect(w, &SaveData::saveFinished,
        [&](const QString &, const QString &r, int n) {
            gotRaw = r; gotRecords = n; loop.quit();
        });
    w->start(); loop.exec(); w->wait(3000);

    CHECK(!gotRaw.isEmpty(),
          "a .dlr IS produced with continuous logging entirely off");
    CHECK(gotRecords == 5, "all five datagrams captured");

    SessionReader r;
    CHECK(r.open(gotRaw), "and it reopens as a session file");
    CHECK(r.tabKey() == "33_1", "self-identifying");
    int n = 0; while (r.next()) ++n;
    CHECK(n == 5, "five records readable");
    CHECK(r.status() == SessionReader::Ok, "clean");

    // The inverse: turning the save setting off must suppress it, even if
    // continuous raw capture happens to be on.
    Settings::setRawCapture(true);
    Settings::setSaveIncludesRaw(false);
    QEventLoop loop2;
    QString gotRaw2 = "unset";
    auto *w2 = new SaveData(tmp.path() + "/suppressed.log", "s", snap, 33, 1,
                            Settings::saveIncludesRaw());
    QObject::connect(w2, &SaveData::saveFinished,
        [&](const QString &, const QString &r, int) { gotRaw2 = r; loop2.quit(); });
    w2->start(); loop2.exec(); w2->wait(3000);
    CHECK(gotRaw2.isEmpty(),
          "save setting off suppresses the sidecar independently of rawCapture");

    Settings::setDiskLoggingEnabled(origDisk);
    Settings::setRawCapture(origRaw);
    Settings::setSaveIncludesRaw(origSave);
}
