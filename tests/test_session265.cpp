#include "testutil.h"

#include "rfidtag.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QLineEdit>
#include <QValidator>

// =============================================================================
//  Session 265 — Tag Builder: editing page_x / page_y. Every keystroke was
//  read at once and the box rewritten zero-padded: Backspace put a 0 at the
//  front, a full box took no digit. Now the box keeps what is typed and the
//  tag changes only when it holds 16 hex digits. The tag: 912D's pages, as
//  session 195 uses them (082a2a027fece219 / f65d4fe80d010781).
// =============================================================================

namespace {
// What typing does: the text changes, then textEdited.
void type(QLineEdit *e, const QString &text)
{
    e->setText(text);
    emit e->textEdited(text);
}
}  // namespace

TEST_SUITE(session265)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    TagBuilderWindow w;
    const QString px = QStringLiteral("082a2a027fece219"), py = QStringLiteral("f65d4fe80d010781");
    w.setPages(px, py);
    const QByteArray start = RfidTag::fromPages(px, py);
    QLineEdit *x = w.pageXEdit();

    // Backspace in the middle: 15 digits, kept as typed, the tag unchanged.
    const QString fifteen = QStringLiteral("082a2a07fece219");          // the '2' at position 7 deleted
    type(x, fifteen);
    CHECK(x->text() == fifteen, QByteArray("Backspace: the box keeps the 15 digits typed, no 0 put in front: ") + x->text().toUtf8());
    CHECK(w.tag() == start && w.status()->text().contains(QLatin1String("15 of 16")),
          QByteArray("the tag stays the last complete one, and the status says so: ") + w.status()->text().toUtf8());

    // Typing a digit back in, where it was deleted: 16 again, the tag read.
    const QString back = QStringLiteral("082a2a0a7fece219");          // a different digit inserted
    type(x, back);
    CHECK(x->text() == back && w.pageYEdit()->text() == py, "inserting a digit: the box holds what was typed");
    CHECK(w.tag() == RfidTag::fromPages(back, py) && w.status()->text().contains(QLatin1String("read")),
          QByteArray("at 16 digits the tag is read: ") + w.status()->text().toUtf8());

    // Inserting into a full box: 17 digits accepted while editing, then one deleted elsewhere.
    QString seventeen = back;
    seventeen.insert(4, QLatin1Char('f'));
    int pos = 0;
    QString probe = seventeen;
    CHECK(x->validator()->validate(probe, pos) != QValidator::Invalid, "a full box takes a 17th digit (insert, then delete)");
    type(x, seventeen);
    CHECK(x->text() == seventeen && w.tag() == RfidTag::fromPages(back, py)
              && w.status()->text().contains(QLatin1String("17 of 16")),
          QByteArray("17 digits: kept, not read, said so: ") + w.status()->text().toUtf8());
    QString sixteen = seventeen;
    sixteen.remove(10, 1);
    type(x, sixteen);
    CHECK(x->text() == sixteen && w.tag() == RfidTag::fromPages(sixteen, py) && w.status()->text().contains(QLatin1String("read")),
          QByteArray("one deleted elsewhere: 16 again, read: ") + w.status()->text().toUtf8());

    // A tag pasted with spaces; the spaces left as typed.
    const QString spaced = QStringLiteral("08 2a 2a 02 7f ec e2 19");
    type(x, spaced);
    CHECK(x->text() == spaced && w.tag() == start && w.status()->text().contains(QLatin1String("read")),
          QByteArray("pasted with spaces: read, the box as pasted: ") + w.status()->text().toUtf8());

    // Not hex: refused by the box.
    QString bad = QStringLiteral("08zz");
    pos = 0;
    CHECK(x->validator()->validate(bad, pos) == QValidator::Invalid, "a non-hex character is refused");

    // From a file with its leading zeros left out: padded, read.
    w.setPages(QStringLiteral("82a2a027fece219"), py);
    CHECK(w.pageXEdit()->text() == px && w.tag() == start, QByteArray("setPages pads a value given without its leading zero: ") + w.pageXEdit()->text().toUtf8());
}
