// Throwaway: render the main window to PNG in every theme so the look can be
// looked at. Not part of the suite.
//
// Session 118: with traffic. Real capture lines from replay/ are fed in as
// three sources, so the shot shows the window as it is used, not empty.
// Output: $SHOT_DIR (default /tmp)/shot_<theme>.png; SHOT_THEMES=dark,light
// limits which themes are drawn.
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPixmap>
#include <QTableView>
#include <QHeaderView>
#include <QTableWidget>
#include <QScrollBar>
#include <QTimer>
#include <QMainWindow>
#include <QLayout>
#include "mainwindow.h"
#include "messagedispatcher.h"
#ifdef DL_HAVE_SERIAL
#include "serialconsolewindow.h"
#include "serialmanager.h"
#endif
#include <QPlainTextEdit>
#include "searchwindow.h"
#include "lococonsolewindow.h"
#include "dmipanel.h"
#include <QGroupBox>
#include <QTabWidget>
#include <QSplitter>
#include <cstdio>
#include "packetmakerdialog.h"
#include "comparewindow.h"
#include "mergedwindow.h"
#include "trackdiagramwindow.h"
#include "twolocowindow.h"
#include "soswindow.h"
#include "incidentreportwindow.h"
#include "incidentreportdialog.h"
#include "flasherwindow.h"
#include "flasherqueuepage.h"
#include "flashercore.h"
#include "layoutaudit.h"
#include "lococonfigwindow.h"
#include "replaywindow.h"
#include "decodeworkbench.h"
#include "framediffwindow.h"
#include "subpacketwindow.h"
#include "archivesearchwindow.h"
#include "faultpanelwindow.h"
#include "roundtripwindow.h"
#include "runreportwindow.h"
#include "sessionwindow.h"
#include "fieldsweepdialog.h"
#include "fieldplot.h"
#include "speeddistance.h"
#include "brakingpanel.h"
#include "settingsdialog.h"
#include "exportdialog.h"
#include "sessionkeydialog.h"
#include "gototimestampdialog.h"
#include "exporter.h"
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include "logmodel.h"
#include "colorrules.h"
#include "sessionfile.h"
#include "querylineedit.h"
#include <QPushButton>
#include "packetbuilder.h"
#include <QDir>
#include <QTemporaryDir>
#include <QDateTime>
#include "namemap.h"
#include "theme.h"
#include "uistyle.h"
#include "settings.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString dir = qEnvironmentVariableIsEmpty("SHOT_DIR") ? QStringLiteral("/tmp")
                                                                : qEnvironmentVariable("SHOT_DIR");
    const QStringList only = qEnvironmentVariable("SHOT_THEMES").split(QLatin1Char(','), Qt::SkipEmptyParts);

    QStringList lines;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd() && lines.size() < 400) {
            const QString l = QString::fromUtf8(f.readLine()).trimmed();
            if (l.startsWith(QLatin1Char('@'))) lines << l;
        }
    }

    // SHOT_WINDOW=serial: a tool window instead of the main one.
    const QString which = qEnvironmentVariable("SHOT_WINDOW");
    if (!which.isEmpty()) {
        for (Theme t : ThemeUtil::all()) {
            if (!only.isEmpty() && !only.contains(ThemeUtil::toString(t))) continue;
            Settings::setTheme(ThemeUtil::toString(t));
            ThemeUtil::apply(t);
            UiStyle::apply();
            MessageDispatcher disp;
            QWidget *win = nullptr;
#ifdef DL_HAVE_SERIAL
            SerialManager mgr(&disp);
            if (which == QLatin1String("serial")) {
                auto *w = new SerialConsoleWindow(&mgr);
                for (const QString &l : lines.mid(0, 40)) w->findChild<QPlainTextEdit *>(QStringLiteral("serialView"))->appendPlainText(l);
                win = w;
            }
#endif
            if (which == QLatin1String("search")) {
                qint64 ms = 1782558147000LL;
                for (int i = 0; i < lines.size(); ++i)
                    disp.ingestLocal(i % 3 ? 21 : 81, 1, lines.at(i).toUtf8(), ms + i * 137, QString());
                disp.drainNow();
                auto *w = new SearchWindow(&disp, nullptr);
                w->setQueryText(QStringLiteral("@lsrp OR @dmi"), true);
                win = w;
            }
            if (which == QLatin1String("loco") || which == QLatin1String("dmi")) {
                // Window first: these pick up locos from traffic as it arrives.
                if (which == QLatin1String("loco")) win = new LocoConsoleWindow(&disp, nullptr);
                else win = new DmiWindow(&disp, nullptr);
                win->resize(1100, 720);
                win->show();
                qint64 ms = 1782558147000LL;
                for (int i = 0; i < lines.size(); ++i) {
                    disp.ingestLocal(21, 1, lines.at(i).toUtf8(), ms + i * 137, QString());
                    if (i % 50 == 49) { disp.drainNow(); QCoreApplication::processEvents(); }
                }
                disp.drainNow();
                // SHOT_BIG=1: the Loco Console's Big numbers panel shown.
                if (qEnvironmentVariableIsSet("SHOT_BIG"))
                    for (QPushButton *b : win->findChildren<QPushButton *>())
                        if (b->text() == QLatin1String("Big numbers") && !b->isChecked()) b->click();
            }
            if (which == QLatin1String("packet")) win = new PacketMakerDialog(nullptr);
            if (which == QLatin1String("compare")) {
                // Two locos' worth of real traffic, so both panes have a tab.
                qint64 ms = 1782558147000LL;
                for (int i = 0; i < lines.size(); ++i)
                    disp.ingestLocal(i % 2 ? 21 : 81, 1, lines.at(i).toUtf8(), ms + i * 137, QString());
                disp.drainNow();
                win = new CompareWindow(&disp, nullptr);
            }
            NameMap names;
            if (which == QLatin1String("merged")) {
                // Three sources, interleaved: what the window exists for.
                qint64 ms = 1782558147000LL;
                for (int i = 0; i < lines.size(); ++i)
                    disp.ingestLocal(i % 3 == 0 ? 81 : (i % 3 == 1 ? 21 : 22), 1, lines.at(i).toUtf8(), ms + i * 137, QString());
                disp.drainNow();
                auto *w = new MergedWindow(&disp, &names, t, 5000, nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("track")) {
                // A run with RFID tags, signals and location: the whole of
                // one real capture, at its own timestamps.
                // SHOT_CAP=29062026_134509 picks another replay/loco_1_1_ capture.
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("26062026_162418")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap));
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
                        disp.ingestLocal(21, 1, l, ms, QString());
                    }
                }
                disp.drainNow();
                const QString key = disp.knownKeys().value(0);
                auto *w = new TrackDiagramWindow(disp.modelForKey(key), key, QStringLiteral("L1_V1"));
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("twoloco")) {
                // Two real locos recorded at the same time (SHOT_CAP picks
                // the date_time; 27062026_151052 has a rear-end episode).
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("27062026_151052")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                for (int loco = 1; loco <= 2; ++loco) {
                    QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_%1_1_%2.cap").arg(loco).arg(cap));
                    if (!run.open(QIODevice::ReadOnly)) continue;
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
                        disp.ingestLocal(loco == 1 ? 21 : 22, 1, l, ms, QString());
                    }
                }
                disp.drainNow();
                auto *w = new TwoLocoWindow(&disp);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("sos")) {
                // SYNTHETIC @sos (schema/fixtures, tests/sosgen): no LKAVACH
                // build logs SoS yet. SHOT_AT=HH:mm:ss picks the moment.
                for (int loco = 1; loco <= 2; ++loco) {
                    QFile run(QStringLiteral(DL_SRC_DIR "/schema/fixtures/sos_synthetic_loco%1.log").arg(loco));
                    if (!run.open(QIODevice::ReadOnly)) continue;
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
                        disp.ingestLocal(quint8(loco), 1, l, ms, QString());
                    }
                }
                disp.drainNow();
                auto *w = new SosWindow(&disp);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                const QString at = qEnvironmentVariableIsEmpty("SHOT_AT") ? QStringLiteral("10:01:40") : qEnvironmentVariable("SHOT_AT");
                w->showMoment(QDateTime::fromString(QStringLiteral("2026-10-09T") + at, Qt::ISODate).toMSecsSinceEpoch());
                win = w;
            }
            if (which == QLatin1String("incident") || which == QLatin1String("incidentdlg")) {
                // One real run, around its rear-end moment (15:13:10).
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("27062026_151052")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap));
                qint64 lo = 0, hi = 0;
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
                        if (!lo) lo = ms;
                        hi = ms;
                        disp.ingestLocal(21, 1, l, ms, QString());
                    }
                }
                disp.drainNow();
                const qint64 at = qEnvironmentVariableIsEmpty("SHOT_AT")
                    ? QDateTime::fromString(QStringLiteral("2026-06-27T15:13:10"), Qt::ISODate).toMSecsSinceEpoch()
                    : QDateTime::fromString(qEnvironmentVariable("SHOT_AT"), Qt::ISODate).toMSecsSinceEpoch();
                if (which == QLatin1String("incidentdlg")) {
                    win = new IncidentReportDialog(at, lo, hi);
                } else {
                    auto *w = new IncidentReportWindow(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"),
                                                       QStringLiteral("L1_V1"), at, IncidentReport::Options());
                    w->setAttribute(Qt::WA_DeleteOnClose, false);
                    win = w;
                }
            }
            QTemporaryDir flashDir;
            if (which.startsWith(QLatin1String("flasher"))) {
                // Three images (arbitrary bytes: the updater does not parse
                // them) through a profile, as test_flasher does; the VCC
                // address is one nothing answers on, so a run sits on the
                // power-cycle prompt and then gives up.
                auto writeImg = [&](const QString &name, int size) {
                    QFile out(QDir(flashDir.path()).filePath(name));
                    out.open(QIODevice::WriteOnly);
                    QByteArray b(size, '\0');
                    for (int i = 0; i < size; ++i) b[i] = char((i * 31 + 7) & 0xFF);
                    out.write(b);
                    return out.fileName();
                };
                Flasher::ProfileStore store(QDir(flashDir.path()).filePath(QStringLiteral("flasher_profiles.json")));
                Flasher::FlashProfile profile;
                profile.name = QStringLiteral("Bench 2");
                profile.vccIp = QStringLiteral("127.0.0.1");
                profile.port = 9;   // discard: nothing answers
                profile.defaultImages.insert(Flasher::CardInput, writeImg(QStringLiteral("KAVACH_Input_Card_v5.appimage"), 40 * 1450));
                profile.defaultImages.insert(Flasher::CardOutput, writeImg(QStringLiteral("KAVACH_Output_Card_v5.appimage"), 25 * 1450));
                profile.defaultImages.insert(Flasher::CardVcc, writeImg(QStringLiteral("LKAVACH_v1.2.9.appimage"), 30 * 1450));
                profile.updaterWaitSeconds = Flasher::kMinUpdaterWaitSeconds;
                store.upsert(profile, store.names().first());
                store.setActiveName(profile.name);
                store.save();
                auto *w = new FlasherWindow(nullptr, flashDir.path());
                w->queuePage()->selectCard(Flasher::CardInput);
                if (which != QLatin1String("flasher")) {
                    w->resize(1100, 720);
                    w->show();
                    w->startBatch(false);
                    QElapsedTimer t;
                    t.start();
                    const qint64 until = which == QLatin1String("flasherrun") ? 2500 : 60000;
                    while (t.elapsed() < until && !(which == QLatin1String("flashersum")
                                                    && w->currentPage() == FlasherWindow::SummaryPageIndex))
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                }
                win = w;
            }
            if (which == QLatin1String("lococonfig")) {
                auto *w = new LocoConfigWindow(nullptr, flashDir.path());
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("replay")) {
                // Both locos of one real run (SHOT_CAP picks the run).
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("27062026_151052")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                auto *w = new ReplayWindow(QStringList{
                    QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap),
                    QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_%1.cap").arg(cap) });
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("workbench")) {
                auto *w = new DecodeWorkbench(nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                // A real @lsrp line from the capture.
                for (const QString &l : lines)
                    if (l.startsWith(QLatin1String("@lsrp"))) { w->loadBuffer(l); break; }
                win = w;
            }
            if (which == QLatin1String("framediff")) {
                auto *w = new FrameDiffWindow(nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                int side = 0;
                for (const QString &l : lines)
                    if (l.startsWith(QLatin1String("@lsrp")) && side < 3) w->setSide(side++, l);
                win = w;
            }
            static PacketBuilder subBuilder;
            static QVector<Schema::SubEntry> subs;
            if (which == QLatin1String("subpacket")) {
                // SLRP's sub-packet SHOT_SUB (default 0), as Packet Maker opens it.
                subs.clear();
                Schema::SubEntry e;
                e.type = qEnvironmentVariableIsEmpty("SHOT_SUB") ? 0 : qEnvironmentVariableIntValue("SHOT_SUB");
                subs.push_back(e);
                auto *w = new SubPacketWindow(nullptr);
                w->setTarget(&subBuilder.encoder(), QStringLiteral("slrp"), &subs, 0);
                win = w;
            }
            static ColorRules archiveRules;
            if (which == QLatin1String("archive")) {
                // Real capture lines as the records of a .dlr dated today,
                // under a temporary disk-log root; then a search for @lsrp.
                const QString day = QDir(flashDir.path()).filePath(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
                QDir().mkpath(day);
                QFile dlr(QDir(day).filePath(QStringLiteral("21_1.dlr")));
                if (dlr.open(QIODevice::WriteOnly)) {
                    const qint64 start = QDateTime(QDate::currentDate(), QTime(14, 2, 26)).toMSecsSinceEpoch();
                    SessionFile::FileHeader h;
                    h.createdMs = start; h.sourceId = 21; h.kvchId = 1;
                    dlr.write(SessionFile::encodeHeader(h));
                    for (int i = 0; i < lines.size(); ++i) {
                        const QByteArray body = lines.at(i).toUtf8();
                        QByteArray wire;
                        wire.append(char(21)); wire.append(char(101)); wire.append(char(7));
                        wire.append(char(body.size() & 0xFF)); wire.append(char(body.size() >> 8));
                        wire.append(char(1)); wire.append(char(0));
                        wire.append(body);
                        dlr.write(SessionFile::encodeRecord(start + i * 137, wire));
                    }
                    dlr.close();
                }
                const QString savedRoot = Settings::diskLogRoot();
                Settings::setDiskLogRoot(flashDir.path());
                auto *w = new ArchiveSearchWindow(&archiveRules, &names, nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->resize(1100, 720);
                w->show();
                if (auto *q = w->findChild<QueryLineEdit *>()) q->setText(QStringLiteral("@lsrp"));
                for (QPushButton *b : w->findChildren<QPushButton *>())
                    if (b->text() == QLatin1String("Search")) b->click();
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                Settings::setDiskLogRoot(savedRoot);
                win = w;
            }
            if (which == QLatin1String("fault")) {
                // A run that raises and clears NMS faults (SHOT_CAP picks it).
                auto *w = new FaultPanelWindow(&disp, nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->resize(1100, 720);
                w->show();
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("26062026_162418")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap));
                if (run.open(QIODevice::ReadOnly)) {
                    int n = 0;
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                        if (++n % 500 == 0) { disp.drainNow(); QCoreApplication::processEvents(); }
                    }
                }
                disp.drainNow();
                win = w;
            }
            if (which == QLatin1String("roundtrip")) {
                // A real run over the capture's lines as the live log.
                auto *w = new RoundTripWindow(nullptr);
                QVector<LogEntryPtr> entries;
                for (const QString &l : lines) {
                    auto e = LogEntryPtr::create();
                    e->text = l;
                    entries.push_back(e);
                }
                w->setLiveEntries(entries);
                w->resize(1100, 720);
                w->show();
                QMetaObject::invokeMethod(w, "onRun");
                QElapsedTimer t;
                t.start();
                auto *tbl = w->findChild<QTableWidget *>();
                while (t.elapsed() < 8000 && !(tbl && tbl->rowCount() > 0))
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                if (tbl && tbl->rowCount() > 0) tbl->selectRow(0);
                win = w;
            }
            if (which == QLatin1String("runreport")) {
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("26062026_162418")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap));
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                    }
                }
                disp.drainNow();
                auto *w = new RunReportWindow(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"), QStringLiteral("L1_V1"));
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("session")) {
                // Two loco tabs of real traffic, written to .dlr the way
                // Save does and read back: what an operator opens.
                qint64 ms = 1782558147000LL;
                for (int i = 0; i < lines.size(); ++i)
                    disp.ingestLocal(i % 2 ? 21 : 81, 1, lines.at(i).toUtf8(), ms + i * 137, QString());
                disp.drainNow();
                static QTemporaryDir tmp;
                QStringList paths;
                for (const QString &key : {QStringLiteral("21_1"), QStringLiteral("81_1")}) {
                    LogModel *m = disp.modelForKey(key);
                    if (!m) continue;
                    const QString path = tmp.path() + QLatin1Char('/') + key + QStringLiteral(".dlr");
                    QFile f(path);
                    if (!f.open(QIODevice::WriteOnly)) continue;
                    SessionFile::FileHeader h;
                    h.createdMs = ms;
                    h.sourceId = quint8(key.section(QLatin1Char('_'), 0, 0).toInt());
                    h.kvchId = 1;
                    f.write(SessionFile::encodeHeader(h));
                    for (int r = 0; r < m->rowCount(); ++r) {
                        const LogEntryPtr e = m->entryAt(r);
                        if (e && !e->rawBytes.isEmpty()) f.write(SessionFile::encodeRecord(e->epochMs, e->rawBytes));
                    }
                    paths << path;
                }
                static ColorRules rules;
                auto *w = new SessionWindow(&rules, &names, t, nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->loadFiles(paths);
                win = w;
            }
            if (which == QLatin1String("fieldsweep")) {
                // Seeded from a real lsrp frame; a short sweep to the local
                // discard port, so the results table has rows.
                auto *w = new FieldSweepDialog(nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                for (const QString &l : lines)
                    if (l.startsWith(QLatin1String("@lsrp"))) { w->seedFromBuffer(l); break; }
                for (QSpinBox *sb : w->findChildren<QSpinBox *>()) {
                    if (sb->suffix().isEmpty()) sb->setValue(9);
                    else sb->setValue(20);
                }
                w->resize(1100, 720);
                w->show();
                for (QPushButton *b : w->findChildren<QPushButton *>())
                    if (b->text() == QLatin1String("Start sweep")) b->click();
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                win = w;
            }
            // The small dialogs, at their own size (SHOT_SIZE=WxH to force one).
            if (which == QLatin1String("settings")) win = new SettingsDialog(nullptr);
            if (which == QLatin1String("export"))
                win = new ExportDialog(QStringLiteral("/Users/operator/Documents/DLConsole/loco_1_1_export.csv"),
                                       Exporter::CSV, Exporter::ColAll, nullptr);
            if (which == QLatin1String("sessionkey")) win = new SessionKeyDialog(nullptr);
            if (which == QLatin1String("gototime")) {
                const qint64 t0 = QDateTime::fromString(QStringLiteral("2026-06-27T14:02:26"), Qt::ISODate).toMSecsSinceEpoch();
                win = new GotoTimestampDialog(t0 + 60000, t0, t0 + 1800000, nullptr);
            }
            if (which == QLatin1String("braking")) {
                // SYNTHETIC @uba (schema/fixtures): no real capture in replay/
                // carries braking curves. Layout only, not firmware evidence.
                QFile uba(QStringLiteral(DL_SRC_DIR "/schema/fixtures/uba_synthetic.log"));
                if (uba.open(QIODevice::ReadOnly)) {
                    while (!uba.atEnd()) {
                        const QByteArray l = uba.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                    }
                }
                disp.drainNow();
                auto *w = new BrakingPanel(&disp, nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("speeddist")) {
                // A whole real run: speed over absolute location.
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("26062026_162418")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap));
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                    }
                }
                disp.drainNow();
                auto *w = new SpeedDistanceWindow(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"));
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("fieldplot")) {
                // A whole real run, speed and location on one time axis.
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_26062026_162418.cap"));
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                    }
                }
                disp.drainNow();
                auto *w = new FieldPlotWindow(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"));
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->plotField(qEnvironmentVariableIsEmpty("SHOT_FIELD") ? QStringLiteral("TRAIN_SPEED") : qEnvironmentVariable("SHOT_FIELD"));
                if (qEnvironmentVariableIsSet("SHOT_FIELD2")) w->addField(qEnvironmentVariable("SHOT_FIELD2"));
                win = w;
            }
            if (!win) return 2;
            const bool small = which == QLatin1String("settings") || which == QLatin1String("export")
                               || which == QLatin1String("sessionkey") || which == QLatin1String("gototime");
            if (which == QLatin1String("session")) win->resize(1200, 700);   // its own default
            else if (small) {
                const QStringList wh = qEnvironmentVariable("SHOT_SIZE").split(QLatin1Char('x'));
                if (wh.size() == 2) win->resize(wh.at(0).toInt(), wh.at(1).toInt());
                else win->adjustSize();
            } else win->resize(1100, 720);
            win->show();
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 800) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (qEnvironmentVariableIsSet("SHOT_WIDE"))
                for (QWidget *c : win->findChildren<QWidget *>())
                    if ((c->isVisible() || qEnvironmentVariableIsSet("SHOT_HIDDEN")) && c->minimumSizeHint().width() > qEnvironmentVariable("SHOT_WIDE").toInt())
                        printf("  wide: %s %s min=%d w=%d '%s'\n", c->metaObject()->className(), qPrintable(c->objectName()),
                               c->minimumSizeHint().width(), c->width(), qPrintable(c->property("text").toString().left(30)));
            if (qEnvironmentVariableIsSet("SHOT_ROWS"))
                if (auto *mw = qobject_cast<QMainWindow *>(win))
                    if (QLayout *cl = mw->centralWidget() ? mw->centralWidget()->layout() : nullptr)
                        for (int i = 0; i < cl->count(); ++i)
                        {
                            printf("  row %d: min %d x %d\n", i, cl->itemAt(i)->minimumSize().width(), cl->itemAt(i)->minimumSize().height());
                            if (QLayout *sub = cl->itemAt(i)->layout())
                                for (int j = 0; j < sub->count(); ++j) {
                                    QWidget *sw = sub->itemAt(j)->widget();
                                    printf("     %d: %s '%s' min %d\n", j, sw ? sw->metaObject()->className() : "spacer",
                                           sw ? qPrintable(sw->property("text").toString().left(20)) : "", sub->itemAt(j)->minimumSize().width());
                                }
                        }
            if (qEnvironmentVariableIsSet("SHOT_TALL"))
                for (QWidget *c : win->findChildren<QWidget *>())
                    if (c->isVisible() && c->minimumSizeHint().height() > qEnvironmentVariable("SHOT_TALL").toInt())
                        printf("  tall: %s %s minH=%d h=%d '%s'\n", c->metaObject()->className(), qPrintable(c->objectName()),
                               c->minimumSizeHint().height(), c->height(), qPrintable(c->property("text").toString().left(30)));
            if (which == QLatin1String("session")) {
                // A row selected, so the side panel has a frame to show.
                for (QTableView *tv : win->findChildren<QTableView *>())
                    if (tv->isVisible() && tv->model() && tv->model()->rowCount() > 100) { tv->selectRow(5); break; }
                for (QWidget *fw : win->findChildren<QWidget *>())
                    if (QString::fromLatin1(fw->metaObject()->className()) == QLatin1String("FilterBar") && fw->isVisible() && qEnvironmentVariableIsSet("SHOT_DUMP")) {
                        printf("filterbar w=%d minHint=%d hint=%d\n", fw->width(), fw->minimumSizeHint().width(), fw->sizeHint().width());
                        for (QWidget *c : fw->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
                            if (c->isVisible()) printf("   %s '%s' w=%d min=%d hint=%d\n", c->metaObject()->className(), qPrintable(c->property("text").toString()), c->width(), c->minimumSizeHint().width(), c->sizeHint().width());
                    }
                settle.restart();
                while (settle.elapsed() < 300) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                if (qEnvironmentVariableIsSet("SHOT_DUMP"))
                    for (QWidget *c : win->findChildren<QWidget *>())
                        if (c->isVisible() && c->minimumSizeHint().width() > 150 && c->minimumSizeHint().width() < 400)
                            printf("  wide: %s %s min=%d w=%d\n", c->metaObject()->className(), qPrintable(c->objectName()), c->minimumSizeHint().width(), c->width());
            }
            win->grab().save(QStringLiteral("%1/shot_%2_%3.png").arg(dir, which, ThemeUtil::toString(t)));
            // SHOT_DUMP=1: every visible group box, tab widget and splitter
            // with its geometry and minimum hint, to see where height goes.
            if (qEnvironmentVariableIsSet("SHOT_DUMP")) {
                printf("window %dx%d  minHint %dx%d\n", win->width(), win->height(),
                       win->minimumSizeHint().width(), win->minimumSizeHint().height());
                for (const QString &o : LayoutAudit::orphans(win)) printf("  loose: %s\n", qPrintable(o));
                for (QTableView *tv : win->findChildren<QTableView *>()) {
                    if (!tv->isVisible()) continue;
                    QString cols;
                    for (int c = 0; c < tv->model()->columnCount(); ++c)
                        cols += QString::number(tv->isColumnHidden(c) ? 0 : tv->columnWidth(c)) + QLatin1Char(' ');
                    printf("  table %s %dx%d viewport %dx%d rows %d rowH %d hbar %d cols %s\n", qPrintable(tv->objectName()),
                           tv->width(), tv->height(), tv->viewport()->width(), tv->viewport()->height(),
                           tv->model()->rowCount(), tv->rowHeight(0), tv->horizontalScrollBar()->isVisible(), qPrintable(cols));
                }
                if (qEnvironmentVariableIsSet("SHOT_DEEP")) {
                    // Every visible widget whose minimum is at least SHOT_DEEP px wide or tall.
                    const int big = qEnvironmentVariableIntValue("SHOT_DEEP");
                    for (QWidget *c : win->findChildren<QWidget *>()) {
                        if (!c->isVisible()) continue;
                        const QSize m = c->minimumSizeHint().expandedTo(c->minimumSize());
                        if (m.width() < big && m.height() < big) continue;
                        printf("  %-28s %-24s min %4dx%-4d\n", c->metaObject()->className(),
                               qPrintable(c->objectName().left(24)), m.width(), m.height());
                    }
                }
                for (QWidget *c : win->findChildren<QWidget *>()) {
                    if (!c->isVisible()) continue;
                    const char *k = c->metaObject()->className();
                    if (!qobject_cast<QGroupBox *>(c) && !qobject_cast<QTabWidget *>(c)
                        && !qobject_cast<QSplitter *>(c)) continue;
                    const QRect g(c->mapTo(win, QPoint(0, 0)), c->size());
                    const QString title = qobject_cast<QGroupBox *>(c) ? qobject_cast<QGroupBox *>(c)->title() : QString();
                    printf("  %-12s y=%4d h=%4d minH=%4d  %s\n", k, g.y(), g.height(),
                           c->minimumSizeHint().height(), qPrintable(title.left(30)));
                }
            }
            delete win;
        }
        return 0;
    }

    for (Theme t : ThemeUtil::all()) {
        if (!only.isEmpty() && !only.contains(ThemeUtil::toString(t))) continue;
        Settings::setTheme(ThemeUtil::toString(t));
        ThemeUtil::apply(t);
        UiStyle::apply();
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        if (auto *disp = w.findChild<MessageDispatcher *>()) {
            qint64 ms = 1782558147000LL;
            for (int i = 0; i < lines.size(); ++i) {
                const quint8 src = (i % 5 == 0) ? 81 : 21;
                const quint16 kv = (i % 7 == 0) ? 2 : 1;
                disp->ingestLocal(src, kv, lines.at(i).toUtf8(), ms + i * 137, QString());
            }
            disp->drainNow();
        }
        // Let the window run a few status ticks (rates, health, the header)
        // before the picture: what a user sees after a moment, not at t=0.
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 4000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);   // past the lanes' 3 s debounce
        // The visible log table's row 8 (an @lsrp line in this capture): the
        // inspector follows the current row.
        for (QTableView *view : w.findChildren<QTableView *>()) {
            if (!view->isVisible() || !view->model() || view->model()->rowCount() < 10) continue;
            for (int r = 0; r < view->model()->rowCount(); ++r) {
                if (view->model()->index(r, 5).data().toString().startsWith(QLatin1String("@lsrp"))) {
                    view->setCurrentIndex(view->model()->index(r, 0));
                    view->selectRow(r);
                    break;
                }
            }
            break;
        }
        for (int i = 0; i < 20; ++i) { QCoreApplication::processEvents(); }
        w.grab().save(QStringLiteral("%1/shot_%2.png").arg(dir, ThemeUtil::toString(t)));
        if (qEnvironmentVariableIsSet("SHOT_DUMP")) {
            printf("main %dx%d minHint %dx%d\n", w.width(), w.height(), w.minimumSizeHint().width(), w.minimumSizeHint().height());
            for (QTableView *tv : w.findChildren<QTableView *>()) {
                if (!tv->isVisible() || tv->model()->columnCount() < 5) continue;
                QString cols;
                for (int c = 0; c < tv->model()->columnCount(); ++c)
                    cols += QString::number(tv->isColumnHidden(c) ? 0 : tv->columnWidth(c)) + QLatin1Char(' ');
                printf("  table %dx%d viewport %d hbar %d cols %s\n", tv->width(), tv->height(), tv->viewport()->width(),
                       tv->horizontalScrollBar()->isVisible(), qPrintable(cols));
                QHeaderView *hh = tv->horizontalHeader();
                printf("  header minSection %d stretchLast %d lastMode %d defSection %d hintLast %d visualLast %d\n",
                       hh->minimumSectionSize(), hh->stretchLastSection(), int(hh->sectionResizeMode(hh->logicalIndex(hh->count() - 1))),
                       hh->defaultSectionSize(), hh->sectionSizeHint(hh->count() - 1), hh->logicalIndex(hh->count() - 1));
            }
        }
    }
    return 0;
}
