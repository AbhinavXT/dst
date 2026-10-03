#include "exportdialog.h"
#include "uicolors.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

ExportDialog::ExportDialog(const QString &defaultPath,
                           Exporter::Format defaultFormat,
                           Exporter::ColumnFlags defaultColumns,
                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export current tab"));
    setMinimumWidth(560);

    auto *root = new QVBoxLayout(this);

    // ---- File path row ---------------------------------------------
    {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(tr("Save to:"));
        m_path = new QLineEdit(defaultPath);
        auto *browse = new QPushButton(tr("Browse…"));
        connect(browse, &QPushButton::clicked, this, &ExportDialog::onBrowse);
        row->addWidget(lbl);
        row->addWidget(m_path, 1);
        row->addWidget(browse);
        root->addLayout(row);
    }

    // ---- Format (radio) --------------------------------------------
    {
        auto *grp = new QGroupBox(tr("Format"));
        auto *row = new QHBoxLayout(grp);
        m_radCsv  = new QRadioButton(tr("&CSV"));
        m_radJson = new QRadioButton(tr("&JSON"));
        if (defaultFormat == Exporter::JSON) m_radJson->setChecked(true);
        else                                  m_radCsv ->setChecked(true);
        row->addWidget(m_radCsv);
        row->addWidget(m_radJson);
        row->addStretch();
        root->addWidget(grp);

        connect(m_radCsv,  &QRadioButton::toggled, this, &ExportDialog::onFormatChanged);
        connect(m_radJson, &QRadioButton::toggled, this, &ExportDialog::onFormatChanged);
    }

    // ---- Column include checkboxes ---------------------------------
    {
        auto *grp = new QGroupBox(tr("Columns to include"));
        // Name and explanation in two columns (session 151): they were one
        // string padded with spaces, which lines up only in a monospace font.
        auto *layout = new QGridLayout(grp);
        layout->setColumnStretch(1, 1);

        auto mkCb = [&](const QString &label, const QString &what, bool initial) {
            auto *cb = new QCheckBox(label);
            cb->setChecked(initial);
            auto *note = new QLabel(what);
            note->setStyleSheet(UiColor::mutedStyle());
            const int row = layout->rowCount();
            layout->addWidget(cb, row, 0);
            layout->addWidget(note, row, 1);
            return cb;
        };

        m_cbTime      = mkCb(tr("Time"), tr("time_iso; JSON also adds time_ms"),
                             defaultColumns & Exporter::ColTime);
        m_cbSource    = mkCb(tr("Source"), tr("CSV: 33_1; JSON: source_id + kvch_id"),
                             defaultColumns & Exporter::ColSource);
        m_cbFriendly  = mkCb(tr("Friendly name"), tr("from friendly_names.csv"),
                             defaultColumns & Exporter::ColFriendly);
        m_cbDirection = mkCb(tr("Direction"), tr("in / out / blank"),
                             defaultColumns & Exporter::ColDirection);
        m_cbSeverity  = mkCb(tr("Severity"), tr("info / warn / error"),
                             defaultColumns & Exporter::ColSeverity);
        m_cbMessage   = mkCb(tr("Message"), tr("the decoded text payload"),
                             defaultColumns & Exporter::ColMessage);
        m_cbRaw       = mkCb(tr("Raw bytes (hex)"), tr("JSON only; CSV ignores this"),
                             defaultColumns & Exporter::ColRawBytes);

        root->addWidget(grp);
    }

    // ---- Buttons ----------------------------------------------------
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Export"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    // Set initial extension hint based on default format.
    onFormatChanged();
}

QString ExportDialog::path() const
{
    return m_path->text();
}

Exporter::Format ExportDialog::format() const
{
    return m_radJson->isChecked() ? Exporter::JSON : Exporter::CSV;
}

Exporter::ColumnFlags ExportDialog::columns() const
{
    Exporter::ColumnFlags f = 0;
    if (m_cbTime     ->isChecked()) f |= Exporter::ColTime;
    if (m_cbSource   ->isChecked()) f |= Exporter::ColSource;
    if (m_cbFriendly ->isChecked()) f |= Exporter::ColFriendly;
    if (m_cbDirection->isChecked()) f |= Exporter::ColDirection;
    if (m_cbSeverity ->isChecked()) f |= Exporter::ColSeverity;
    if (m_cbMessage  ->isChecked()) f |= Exporter::ColMessage;
    if (m_cbRaw      ->isChecked()) f |= Exporter::ColRawBytes;
    return f;
}

void ExportDialog::onBrowse()
{
    // Pick a save path. The QFileDialog filter shows both formats so
    // the user can switch via the file extension if they want; we'll
    // also auto-fix the extension after they pick.
    const QString filter = (format() == Exporter::JSON)
        ? tr("JSON files (*.json);;CSV files (*.csv);;All files (*)")
        : tr("CSV files (*.csv);;JSON files (*.json);;All files (*)");

    const QString picked = QFileDialog::getSaveFileName(
        this, tr("Choose export path"), m_path->text(), filter);
    if (picked.isEmpty()) return;
    m_path->setText(picked);

    // If the user picked a .json file but has CSV selected (or vice
    // versa), update the radio to match. Their pick is the source of
    // truth; the format radio is just a hint.
    if (picked.endsWith(".json", Qt::CaseInsensitive)) {
        m_radJson->setChecked(true);
    } else if (picked.endsWith(".csv", Qt::CaseInsensitive)) {
        m_radCsv->setChecked(true);
    }
}

void ExportDialog::onFormatChanged()
{
    updateExtensionForFormat();

    // Slightly grey out the "Raw bytes" checkbox when CSV is selected,
    // since CSV ignores it. Keep it functional (the flag still flows
    // through and JSON exports will honor it) — just visually hint
    // that this option only matters for JSON.
    const bool isJson = (format() == Exporter::JSON);
    m_cbRaw->setEnabled(isJson);
    if (!isJson) {
        m_cbRaw->setToolTip(tr("CSV ignores this column. "
                                "Switch to JSON to include raw bytes."));
    } else {
        m_cbRaw->setToolTip(QString());
    }
}

void ExportDialog::updateExtensionForFormat()
{
    // Rewrite the filename extension to match the active format. Only
    // touches the extension; the directory and base name are preserved.
    const QString cur = m_path->text();
    if (cur.isEmpty()) return;

    QFileInfo fi(cur);
    const QString want = (format() == Exporter::JSON) ? "json" : "csv";
    const QString have = fi.suffix().toLower();
    if (have == want) return;

    // Strip any extension and append the right one. If there's no
    // existing extension, just append.
    const QString stem = fi.completeBaseName();
    const QString dir  = fi.absolutePath();
    m_path->setText(QString("%1/%2.%3").arg(dir, stem, want));
}
