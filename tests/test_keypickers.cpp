#include "testutil.h"

#include "decodeworkbench.h"
#include "packetmakerdialog.h"
#include "sessionkeystore.h"
#include "crypto/kavachmac.h"

#include <QComboBox>
#include <QDateTime>
#include <QLineEdit>

// =============================================================================
//  The two pickers, end to end.
//
//  A store-level test proves the sets exist; it does not prove either window
//  offers them, keeps the operator's choice across a repopulate, or actually
//  uses the chosen one. Both windows subscribe to changed(), which fires on
//  every observed frame, so a mistake here is a combo that resets itself under
//  the operator mid-edit.
// =============================================================================

namespace {

QString authLine(int n, const QDateTime &a, const QDateTime &b,
                 const QByteArray &k0, const QByteArray &k1, int loco)
{
    auto revTime = [](QByteArray &p, const QDateTime &t) {
        p.append(char(t.time().hour()));
        p.append(char(t.date().day()));
        p.append(char(t.date().month()));
        p.append(char(t.date().year() - 2000));
    };
    QByteArray p; revTime(p, a); revTime(p, b); p += k0; p += k1;
    return QStringLiteral("@auth_keys%1_%2_1 %3 100 %4")
               .arg(n).arg(loco).arg(QDateTime::currentDateTime().toString(Qt::ISODate))
               .arg(QString::fromLatin1(p.toHex()));
}

QString randLine(quint16 lr, quint16 sr, int loco)
{
    QByteArray p;
    p.append(char(lr & 0xFF)); p.append(char(lr >> 8));
    p.append(char(sr & 0xFF)); p.append(char(sr >> 8));
    return QStringLiteral("@rand_num_%1_1 %2 101 %3")
               .arg(loco).arg(QDateTime::currentDateTime().toString(Qt::ISODate))
               .arg(QString::fromLatin1(p.toHex()));
}

// The picker in either window: the combo whose first entry is the id-0
// sentinel ("Auto …" in the workbench, "(none …" in the packet maker). Found
// by what it holds rather than by object name, so it keeps working if the
// widget is renamed.
QComboBox *keyCombo(QWidget *w)
{
    for (QComboBox *cb : w->findChildren<QComboBox *>()) {
        if (cb->count() < 1) { continue; }
        const QVariant d0 = cb->itemData(0);
        if (d0.type() != QVariant::Int || d0.toInt() != 0) { continue; }
        const QString t0 = cb->itemText(0);
        if (t0.startsWith(QLatin1String("Auto ")) || t0.startsWith(QLatin1String("(none"))) {
            return cb;
        }
    }
    return nullptr;
}

}  // namespace

TEST_SUITE(keypickers)
{
    SessionKeyStore ownStore;                 // session 96: the test's own, not a global
    SessionKeyStore &store = ownStore;
    store.clear();

    const QByteArray K1 = QByteArray::fromHex("1234567890abcdef1234567890abcdef");
    const QByteArray K2 = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime s1a = now.addDays(-10), s1b = now.addDays(10);
    const QDateTime s2a = now.addDays(20),  s2b = now.addDays(40);

    store.observeLine(CaptureDecoder::parseLine(authLine(1, s1a, s1b, K1, K2, 1)));
    store.observeLine(CaptureDecoder::parseLine(authLine(2, s2a, s2b, K2, K1, 1)));
    store.observeLine(CaptureDecoder::parseLine(randLine(0x1111, 0x2222, 1)));
    CHECK(store.snapshots().size() == 1, "one key set to offer");

    // ---- Decode Workbench --------------------------------------------------
    {
        DecodeWorkbench wb(nullptr, &store);
        QComboBox *cb = keyCombo(&wb);
        CHECK(cb != nullptr, "the workbench has a session-key picker");
        if (cb) {
            CHECK(cb->count() == 2, "Auto plus the one captured set");
            CHECK(cb->itemData(0).toInt() == 0, "Auto is the id-0 sentinel");
            CHECK(cb->itemData(1).toInt() == store.snapshots().first().id,
                  "and the set carries its id");

            // A frame signed under set #1, pasted in, checked against set #1.
            const KeySnapshot k = store.snapshots().first();
            const QByteArray body(24, '\x33');
            QByteArray frame = body;
            frame += KavachMac::cbcMacWire(body, k.result.sessionKey);
            frame += QByteArray(4, '\0');
            wb.loadBuffer(QStringLiteral("@slrp_1_1 %1 200 %2")
                              .arg(now.toString(Qt::ISODate))
                              .arg(QString::fromLatin1(frame.toHex())));

            cb->setCurrentIndex(1);                 // choose the set explicitly
            CHECK(cb->currentData().toInt() == k.id, "the choice sticks");

            // A new set arrives: the combo must grow WITHOUT resetting the
            // operator's choice out from under them.
            store.observeLine(CaptureDecoder::parseLine(randLine(0x9999, 0x8888, 1)));
            CHECK(cb->count() == 3, "the new set appears in the picker");
            CHECK(cb->currentData().toInt() == k.id,
                  "and the operator's choice survives the repopulate");
        }
    }

    // ---- Packet Maker ------------------------------------------------------
    {
        PacketMakerDialog pm(nullptr, nullptr, &store);
        QComboBox *cb = keyCombo(&pm);
        CHECK(cb != nullptr, "the packet maker has a captured-key picker");
        if (cb) {
            const int wanted = store.snapshots().first().id;
            const int idx = cb->findData(wanted);
            CHECK(idx > 0, "the captured set is offered");
            cb->setCurrentIndex(idx);

            // Choosing a set fills the key field, which is what the builder
            // signs with — so the effect is visible and still editable.
            QLineEdit *keyEdit = nullptr;
            for (QLineEdit *e : pm.findChildren<QLineEdit *>()) {
                if (e->maxLength() == 32) { keyEdit = e; break; }
            }
            CHECK(keyEdit != nullptr, "the session-key field is there");
            const KeySnapshot *k = store.snapshot(wanted);
            CHECK(keyEdit && k
                  && keyEdit->text().toLower()
                         == QString::fromLatin1(k->result.sessionKey.toHex()),
                  "choosing a set copies its key into the field");
        }
    }

    store.clear();
}
