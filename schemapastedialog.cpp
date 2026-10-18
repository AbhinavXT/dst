#include "schemapastedialog.h"

#include "uicolors.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

SchemaPasteDialog::SchemaPasteDialog(const SchemaEdit::Session &session, int table, QWidget *parent)
    : QDialog(parent), m_session(session), m_table(table)
{
    const SchemaDoc::Document &doc = session.doc();
    const SchemaDoc::Node &t = doc.node(table);
    setWindowTitle(tr("Paste into %1").arg(t.value(QStringLiteral("name"))));
    auto *lay = new QVBoxLayout(this);
    QString how;
    if (t.tag == QLatin1String("enum")) {
        how = tr("One value per line: \"3  Spare\", \"3<Tab>Spare\" (two spreadsheet columns), \"1..50  label\" for a range, "
                 "\"otherwise  rsvd(%v)\". A line with no number takes the one after the line before.");
    } else if (t.tag == QLatin1String("flagtable")) {
        how = tr("One flag name per line, in bit order, or \"bit<Tab>name\". New flags go after the last one (bit %1 next).")
                  .arg(t.children.size());
    } else {
        how = tr("One event per line: \"id name [bytes] [signed]\", or tab-separated columns in that order. Bytes is 1 if not given.");
    }
    auto *hint = new QLabel(how, this);
    hint->setWordWrap(true);
    hint->setStyleSheet(UiColor::mutedStyle());
    lay->addWidget(hint);
    m_text = new QPlainTextEdit(this);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setPlaceholderText(tr("Paste here"));
    lay->addWidget(m_text, 2);
    m_replace = new QCheckBox(tr("Replace the whole table (its %n entry(s) go)", nullptr, int(t.children.size())), this);
    m_replace->setToolTip(tr("Off: a value already in the table has its entry changed, the rest are added"));
    lay->addWidget(m_replace);
    m_preview = new QTreeWidget(this);
    m_preview->setRootIsDecorated(false);
    m_preview->setUniformRowHeights(true);
    const QStringList cols = SchemaEdit::entryColumns(t.tag);
    QStringList headers{ tr("Line") };
    for (const QString &c : cols) headers << SchemaEdit::entryHeader(c);
    headers << tr("Does");
    m_preview->setHeaderLabels(headers);
    lay->addWidget(m_preview, 3);
    m_problemLabel = new QLabel(this);
    m_problemLabel->setWordWrap(true);
    m_problemLabel->setStyleSheet(UiColor::warningStyle());
    lay->addWidget(m_problemLabel);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_text, &QPlainTextEdit::textChanged, this, &SchemaPasteDialog::update);
    connect(m_replace, &QCheckBox::toggled, this, &SchemaPasteDialog::update);
    resize(760, 560);
    update();
}

bool SchemaPasteDialog::replace() const { return m_replace->isChecked(); }

void SchemaPasteDialog::update()
{
    const SchemaDoc::Document &doc = m_session.doc();
    m_rows = SchemaEdit::parseRows(doc, m_table, m_text->toPlainText(), replace(), &m_problems);
    const QStringList cols = SchemaEdit::entryColumns(doc.node(m_table).tag);
    m_preview->clear();
    int added = 0, changed = 0;
    for (const SchemaEdit::TableRow &r : m_rows) {
        QStringList texts{ QString::number(r.line) };
        for (const QString &c : cols) texts << r.cells.value(c);
        texts << r.note;
        auto *item = new QTreeWidgetItem(m_preview, texts);
        if (r.skip) {
            for (int k = 0; k < texts.size(); ++k) item->setForeground(k, UiColor::muted());
        } else if (r.existing >= 0) {
            item->setForeground(texts.size() - 1, UiColor::warning());
            ++changed;
        } else {
            ++added;
        }
    }
    for (int k = 0; k + 1 < m_preview->columnCount(); ++k) m_preview->resizeColumnToContents(k);
    m_problemLabel->setText(m_problems.mid(0, 6).join(QLatin1Char('\n'))
                            + (m_problems.size() > 6 ? tr("\n… and %n more", nullptr, int(m_problems.size()) - 6) : QString()));
    m_problemLabel->setVisible(!m_problems.isEmpty());
    QStringList parts;
    if (added) parts << tr("add %n", nullptr, added);
    if (changed) parts << tr("change %n", nullptr, changed);
    m_ok->setText(parts.isEmpty() ? tr("OK") : (replace() ? tr("Replace with %n", nullptr, added) : parts.join(QStringLiteral(", "))));
    m_ok->setEnabled(added + changed > 0);
}
