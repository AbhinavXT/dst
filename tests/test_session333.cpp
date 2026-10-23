#include "testutil.h"

#include "capturedecoder.h"
#include "fieldcrumb.h"
#include "fieldinspector.h"
#include "logentry.h"
#include "schemadecoder.h"

#include <QFile>
#include <QLabel>
#include <QTableWidget>

// =============================================================================
//  Session 333 — the breadcrumb over the decoded fields:
//  "Tab › SLRP › subpkt[N] NAME › FIELD", each part but the last a link.
//  Real frames: the first SLRP and ARP of replay/loco_1_1_27062026_170217.cap.
// =============================================================================

namespace {
QString firstLine(const char *prefix)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    while (!f.atEnd()) {
        const QString l = QString::fromLatin1(f.readLine()).trimmed();
        if (l.startsWith(QLatin1String(prefix))) return l;
    }
    return QString();
}

LogEntryPtr entryOf(const QString &text)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 21;
    e->header.kvchId = 1;
    e->text = text;
    e->cacheDerived();
    return e;
}
}  // namespace

TEST_SUITE(session333)
{
    const QString slrp = firstLine("@slrp_"), arp = firstLine("@arp_");
    CHECK(!slrp.isEmpty() && !arp.isEmpty(), "fixture: a real SLRP and ARP line");
    if (slrp.isEmpty() || arp.isEmpty()) return;

    const QVector<FieldRow> rows = CaptureDecoder::describe(CaptureDecoder::parseLine(slrp));
    int header = -1, inside = -1;
    for (int i = 0; i < rows.size() && inside < 0; ++i) {
        if (rows.at(i).field.startsWith(QLatin1String("subpkt["))) header = i;
        else if (header >= 0 && rows.at(i).field.startsWith(QLatin1String("  "))) inside = i;
    }
    CHECK(header >= 0 && inside > header, "fixture: the SLRP has a sub-packet with fields");
    if (inside < 0) return;

    // ---- the parents ------------------------------------------------------------
    CHECK(FieldCrumb::parents(rows, inside) == QVector<int>{ header }, "a sub-packet field's parent is its sub-packet");
    CHECK(FieldCrumb::parents(rows, header).isEmpty(), "a sub-packet has none");
    CHECK(FieldCrumb::label(rows.at(header)).startsWith(QLatin1String("subpkt["))
              && !FieldCrumb::label(rows.at(header)).contains(QLatin1String("@byte")),
          "a sub-packet's part drops its @byte");
    int bodyField = -1;
    for (int i = 0; i < header; ++i) if (rows.at(i).field.startsWith(QLatin1String("  "))) { bodyField = i; break; }
    CHECK(bodyField > 0 && FieldCrumb::parents(rows, bodyField).isEmpty(),
          "a header field before the sub-packets has no parent (the envelope rows are not parents)");
    CHECK(FieldCrumb::parents(rows, -1).isEmpty() && FieldCrumb::parents(rows, rows.size()).isEmpty(),
          "out of range: none");

    // ---- in the inspector -------------------------------------------------------
    FieldInspector fi;
    fi.setDecoder(&kavachSchema());
    fi.setContextLabel(QStringLiteral("Loco 1 VCC"));
    fi.showEntry(entryOf(slrp));
    auto *table = fi.findChild<QTableWidget *>();
    auto *crumb = fi.findChild<QLabel *>(QStringLiteral("fieldCrumb"));
    CHECK(table && crumb, "the inspector has a crumb over its table");
    if (!table || !crumb) return;
    table->selectRow(inside);
    const QStringList parts = fi.crumbParts();
    CHECK(parts.size() == 4 && parts.at(0) == QStringLiteral("Loco 1 VCC") && parts.at(1) == QStringLiteral("SLRP")
              && parts.at(2) == FieldCrumb::label(rows.at(header)) && parts.at(3) == rows.at(inside).field.trimmed(),
          QByteArray("Tab > SLRP > sub-packet > field: ") + parts.join(QStringLiteral(" > ")).toUtf8());

    emit crumb->linkActivated(QStringLiteral("row:%1").arg(header));
    CHECK(table->currentRow() == header && fi.crumbParts().size() == 3, "clicking the sub-packet selects its row");
    emit crumb->linkActivated(QStringLiteral("top"));
    CHECK(table->currentRow() == 0, "clicking the packet goes to its first row");
    bool tabAsked = false;
    QObject::connect(&fi, &FieldInspector::contextActivated, [&]() { tabAsked = true; });
    emit crumb->linkActivated(QStringLiteral("tab"));
    CHECK(tabAsked, "clicking the tab asks for the log table");

    fi.showEntry(entryOf(arp));
    CHECK(fi.crumbParts().size() == 2 && fi.crumbParts().at(1) == QStringLiteral("ARP"),
          "a new frame, no row chosen: Tab > packet");
    fi.clear();
    CHECK(crumb->isHidden(), "nothing shown: no crumb");
}
