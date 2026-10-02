#include "testutil.h"
#include "layoutaudit.h"

#include "archivesearchwindow.h"
#include "colorrules.h"
#include "namemap.h"
#include "querylineedit.h"
#include "sessionfile.h"
#include "settings.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateEdit>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

// =============================================================================
//  Session 140 — UI revamp, tool windows 17: Search recorded sessions.
//
//  The From box shared the query's stretching grid column; Cancel was a
//  disabled full-width bar whenever no search ran; the Time column read
//  "2026-10-02 …"; the status said "hit(s) … record(s) … file(s)". Now:
//  query + Search on one row, dates and filters compact on the next, Cancel
//  in Search's place only while searching, measured columns, plain English.
//  Fixture: a .dlr dated today whose records are real capture lines from
//  replay/loco_1_1_27062026_140226.cap, under a temporary disk-log root.
// =============================================================================

TEST_SUITE(session140)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    QTemporaryDir root;
    CHECK(root.isValid(), "fixture: a temporary disk-log root");
    {
        QStringList lines;
        QFile cap(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        if (cap.open(QIODevice::ReadOnly))
            while (!cap.atEnd() && lines.size() < 200) {
                const QString l = QString::fromUtf8(cap.readLine()).trimmed();
                if (l.startsWith(QLatin1Char('@'))) lines << l;
            }
        const QString day = QDir(root.path()).filePath(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
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
        }
        CHECK(lines.size() == 200, "fixture: 200 real capture lines as records");
    }
    const QString savedRoot = Settings::diskLogRoot();
    Settings::setDiskLogRoot(root.path());

    ColorRules rules;
    NameMap names;
    ArchiveSearchWindow w(&rules, &names);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1100, 720);
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    QPushButton *search = nullptr, *cancel = nullptr;
    for (QPushButton *b : w.findChildren<QPushButton *>()) {
        if (b->text() == QLatin1String("Search")) search = b;
        if (b->text() == QLatin1String("Cancel")) cancel = b;
    }
    CHECK(search && search->isVisible() && cancel && !cancel->isVisible(), "idle: Search shows, Cancel takes no room");
    for (QDateEdit *d : w.findChildren<QDateEdit *>())
        CHECK(d->width() < 260, QByteArray("a date box is a date's width (") + QByteArray::number(d->width()) + " px)");

    if (auto *q = w.findChild<QueryLineEdit *>()) q->setText(QStringLiteral("@lsrp"));
    if (search) search->click();
    auto *status = w.findChild<StatusLine *>();
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 10000 && !(status && status->text().contains(QLatin1String(" from "))))
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(status && status->text().contains(QLatin1String("8 hits from 200 records across 1 file.")),
          QByteArray("the result reads as English (") + (status ? status->text().toUtf8() : QByteArray()) + ")");
    CHECK(search && search->isVisible() && cancel && !cancel->isVisible(), "finished: back to Search");

    auto *table = w.findChild<QTableWidget *>(QStringLiteral("archiveResults"));
    CHECK(table && table->rowCount() > 0, "hits are listed");
    if (table && table->rowCount() > 0) {
        const QString time = table->item(0, 0)->text();
        CHECK(table->fontMetrics().horizontalAdvance(time) + 8 <= table->columnWidth(0),
              QByteArray("the whole time fits its column (") + time.toUtf8() + ")");
        for (int c = 0; c < table->columnCount(); ++c) {
            QFont hf = table->horizontalHeader()->font();
            hf.setWeight(QFont::DemiBold);
            const QString head = table->horizontalHeaderItem(c)->text();
            CHECK(c == 3 || QFontMetrics(hf).horizontalAdvance(head) + 16 <= table->columnWidth(c),
                  QByteArray("header \"") + head.toUtf8() + "\" fits");
        }
        CHECK(table->horizontalHeader()->defaultAlignment() & Qt::AlignLeft, "headers align left");
    }

    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366, "fits a laptop");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
    Settings::setDiskLogRoot(savedRoot);
}
