#include "testutil.h"

#include "capturedecoder.h"
#include "fieldinspector.h"
#include "logentry.h"
#include "logmodel.h"
#include "rowcard.h"

#include <QFile>

// =============================================================================
//  Session 334 — the hover card on a log row: the frame's type and size, its
//  CRC, the inspector card's key values, and whether a receiver would
//  process it. Real frames: replay/loco_1_1_27062026_170217.cap.
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
    e->header.message_id = 7;
    e->header.message_len = quint16(text.size());
    e->text = text;
    e->cacheDerived();
    return e;
}
}  // namespace

TEST_SUITE(session334)
{
    const QString lsrp = firstLine("@lsrp_"), dmi = firstLine("@dmi_");
    CHECK(!lsrp.isEmpty() && !dmi.isEmpty(), "fixture: a real LSRP and DMI line");
    if (lsrp.isEmpty() || dmi.isEmpty()) return;

    for (const QString &line : { lsrp, dmi }) {
        const CaptureLine cap = CaptureDecoder::parseLine(line);
        const QVector<FieldRow> rows = CaptureDecoder::describe(cap);
        const QString card = RowCard::html(line);
        QStringList missing;
        int shown = 0;
        for (const QString &want : FieldInspector::keyFields())
            for (const FieldRow &r : rows)
                if (r.field.trimmed().compare(want, Qt::CaseInsensitive) == 0) {
                    ++shown;
                    if (!card.contains(FieldInspector::tileCaption(want).toHtmlEscaped())
                        || !card.contains(FieldInspector::tileValue(r.value).toHtmlEscaped()))
                        missing << want;
                    break;
                }
        const QByteArray name = cap.typeToken.toUpper().toUtf8();
        CHECK(card.startsWith(QStringLiteral("<b>%1 · %2 B</b>").arg(cap.typeToken.toUpper()).arg(cap.bytes.size())),
              name + ": the card names the packet and its size");
        CHECK(shown > 0 && missing.isEmpty(), name + ": every key value the frame carries, captioned (" + QByteArray::number(shown)
                                                    + " shown; missing " + missing.join(QLatin1Char(',')).toUtf8() + ")");
        CHECK(!cap.crcChecked || card.contains(cap.crcOk ? QStringLiteral("CRC pass") : QStringLiteral("CRC fail")),
              name + ": and its CRC");
    }
    CHECK(RowCard::html(QStringLiteral("Link 1 up, 3 retries")).isEmpty(), "a text row: no card");
    CHECK(RowCard::html(QStringLiteral("@lsrp_1_1 2026-06-27T17:02:18 4202")).isEmpty(), "a capture line with no bytes: no card");

    // In the table: the message column's tooltip is the card, the header
    // tuple kept under it; a text row keeps the tuple alone.
    LogModel model(nullptr, 100);
    model.appendEntry(entryOf(lsrp));
    model.appendEntry(entryOf(QStringLiteral("Link 1 up, 3 retries")));
    const QString tip = model.index(0, LogModel::ColMessage).data(Qt::ToolTipRole).toString();
    CHECK(tip.startsWith(RowCard::html(lsrp)) && tip.contains(QStringLiteral("source=21")),
          "an LSRP row's tooltip: the card, then source / kvch / msg_id");
    const QString textTip = model.index(1, LogModel::ColMessage).data(Qt::ToolTipRole).toString();
    CHECK(textTip.startsWith(QStringLiteral("source=21")), "a text row's tooltip: as before");
}
