#include "reviewpackdialog.h"

#include "settings.h"
#include "statusline.h"
#include "uicolors.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>

namespace {
constexpr qint64 kDefaultSpanMs = 10 * 60 * 1000;   // From / To start on the last 10 minutes

QString mb(qint64 bytes)
{
    return bytes < 1024 * 1024 ? QStringLiteral("%1 kB").arg((bytes + 1023) / 1024)
                               : QStringLiteral("%1 MB").arg(double(bytes) / (1024 * 1024), 0, 'f', 1);
}
}  // namespace

ReviewPackDialog::ReviewPackDialog(const QVector<ReviewPack::Capture> &captures, const ReviewPack::Input &base,
                                   QWidget *parent)
    : QDialog(parent), m_captures(captures), m_base(base)
{
    setWindowTitle(tr("Export review pack"));
    setObjectName(QStringLiteral("reviewPackDialog"));

    // Who and where: remembered, because the same team sends the next one.
    QSettings ini(Settings::iniPath(), QSettings::IniFormat);
    m_site = new QLineEdit(ini.value(QStringLiteral("reviewpack/site")).toString(), this);
    m_site->setObjectName(QStringLiteral("reviewPackSite"));
    m_site->setPlaceholderText(tr("where the test ran, e.g. Lingampalli yard, line 3"));
    m_tester = new QLineEdit(ini.value(QStringLiteral("reviewpack/tester")).toString(), this);
    m_tester->setObjectName(QStringLiteral("reviewPackTester"));
    m_loco = new QLineEdit(ini.value(QStringLiteral("reviewpack/loco")).toString(), this);
    m_loco->setObjectName(QStringLiteral("reviewPackLoco"));
    m_what = new QPlainTextEdit(this);
    m_what->setObjectName(QStringLiteral("reviewPackWhat"));
    m_what->setPlaceholderText(tr("What happened, and what the reviewer should look at: "
                                  "\"EB at 14:03 with no reason on the DMI, scenario 4.2, second run\""));
    m_what->setFixedHeight(m_what->fontMetrics().lineSpacing() * 4 + 12);

    m_tabs = new QListWidget(this);
    m_tabs->setObjectName(QStringLiteral("reviewPackTabs"));
    qint64 first = 0, last = 0;
    for (const ReviewPack::Capture &c : m_captures) {
        int raw = 0;
        for (const LogEntryPtr &e : c.rows) {
            if (!e || e->rawBytes.isEmpty()) continue;
            ++raw;
            if (!first || e->epochMs < first) first = e->epochMs;
            last = qMax(last, e->epochMs);
        }
        auto *it = new QListWidgetItem(c.name == c.tabKey || c.name.isEmpty()
                                           ? tr("%1 — %2 frames").arg(c.tabKey).arg(raw)
                                           : tr("%1 (%2) — %3 frames").arg(c.name, c.tabKey).arg(raw),
                                       m_tabs);
        it->setData(Qt::UserRole, c.tabKey);
        it->setFlags(raw ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::ItemIsUserCheckable);
        it->setCheckState(raw ? Qt::Checked : Qt::Unchecked);
        if (!raw) it->setToolTip(tr("No raw frames in this tab: nothing to put in a .dlr."));
    }
    m_tabs->setMaximumHeight(m_tabs->fontMetrics().lineSpacing() * 6 + 12);

    m_all = new QRadioButton(tr("Everything in the chosen tabs"), this);
    m_all->setObjectName(QStringLiteral("reviewPackAll"));
    m_range = new QRadioButton(tr("From / To:"), this);
    m_range->setObjectName(QStringLiteral("reviewPackRange"));
    m_all->setChecked(true);
    auto timeEdit = [this](const QString &name) {
        auto *e = new QDateTimeEdit(this);
        e->setObjectName(name);
        e->setDisplayFormat(QStringLiteral("yyyy-MM-dd  HH:mm:ss"));
        e->setCalendarPopup(false);
        return e;
    };
    m_from = timeEdit(QStringLiteral("reviewPackFrom"));
    m_to = timeEdit(QStringLiteral("reviewPackTo"));
    const qint64 to = last ? last : QDateTime::currentMSecsSinceEpoch();
    m_from->setDateTime(QDateTime::fromMSecsSinceEpoch(qMax(first, to - kDefaultSpanMs)));
    m_to->setDateTime(QDateTime::fromMSecsSinceEpoch(to + 999));
    auto *spanRow = new QHBoxLayout;
    spanRow->addWidget(m_range);
    spanRow->addWidget(m_from);
    spanRow->addWidget(new QLabel(QStringLiteral("→"), this));
    spanRow->addWidget(m_to);
    spanRow->addStretch(1);

    m_run = new QCheckBox(this);
    m_run->setObjectName(QStringLiteral("reviewPackRun"));
    if (m_base.testRunPath.isEmpty() || !QFileInfo::exists(m_base.testRunPath)) {
        m_run->setText(tr("The test run (none open in the Test Case Catalog)"));
        m_run->setEnabled(false);
    } else {
        m_run->setText(tr("The test run: %1").arg(QFileInfo(m_base.testRunPath).fileName()));
        m_run->setChecked(true);
    }
    auto *also = new QLabel(tr("Always in: each tab's notes, the kavach.xml in force (%1), the colour rules, "
                               "names and field map, the program and Qt versions.")
                                .arg(m_base.schemaFrom.isEmpty() ? tr("built in") : m_base.schemaFrom),
                            this);
    also->setWordWrap(true);
    also->setStyleSheet(UiColor::mutedStyle());

    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("reviewPackCount"));
    m_count->setWordWrap(true);
    m_status = new StatusLine(this);
    m_export = new QPushButton(tr("Export…"), this);
    m_export->setObjectName(QStringLiteral("reviewPackExport"));
    m_export->setDefault(true);
    auto *close = new QPushButton(tr("Close"), this);

    auto *form = new QFormLayout;
    form->addRow(tr("Site:"), m_site);
    form->addRow(tr("Tester:"), m_tester);
    form->addRow(tr("Loco:"), m_loco);
    form->addRow(tr("What to look at:"), m_what);
    form->addRow(tr("Tabs:"), m_tabs);
    auto *span = new QVBoxLayout;
    span->addWidget(m_all);
    span->addLayout(spanRow);
    form->addRow(tr("Span:"), span);
    form->addRow(QString(), m_run);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(m_export);
    buttons->addWidget(close);
    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(also);
    lay->addWidget(m_count);
    lay->addStretch(1);
    lay->addLayout(buttons);

    connect(m_tabs, &QListWidget::itemChanged, this, &ReviewPackDialog::refreshCount);
    connect(m_all, &QRadioButton::toggled, this, &ReviewPackDialog::refreshCount);
    connect(m_from, &QDateTimeEdit::dateTimeChanged, this, [this]() { m_range->setChecked(true); refreshCount(); });
    connect(m_to, &QDateTimeEdit::dateTimeChanged, this, [this]() { m_range->setChecked(true); refreshCount(); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_export, &QPushButton::clicked, this, [this]() {
        const QString site = m_site->text().trimmed().replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")),
                                                              QStringLiteral("_"));
        const QString name = QStringLiteral("ReviewPack_%1%2.zip")
                                 .arg(site.isEmpty() ? QString() : site + QLatin1Char('_'),
                                      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        const QString path = QFileDialog::getSaveFileName(this, tr("Save the review pack"),
                                                          QDir::home().filePath(name), tr("Review pack (*.zip)"));
        if (!path.isEmpty()) exportTo(path);
    });
    // The dialog opens with all fields at their defaults; From / To above
    // ticked "From / To", so start from Everything again.
    m_all->setChecked(true);
    refreshCount();
}

void ReviewPackDialog::setWho(const QString &site, const QString &tester, const QString &loco)
{
    m_site->setText(site);
    m_tester->setText(tester);
    m_loco->setText(loco);
}

void ReviewPackDialog::setWhat(const QString &text) { m_what->setPlainText(text); }

void ReviewPackDialog::setSpan(qint64 fromMs, qint64 toMs)
{
    if (fromMs <= 0 && toMs <= 0) {
        m_all->setChecked(true);
        return;
    }
    m_from->setDateTime(QDateTime::fromMSecsSinceEpoch(fromMs));
    m_to->setDateTime(QDateTime::fromMSecsSinceEpoch(toMs));
    m_range->setChecked(true);
    refreshCount();
}

void ReviewPackDialog::setTabChecked(const QString &tabKey, bool on)
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->item(i)->data(Qt::UserRole).toString() == tabKey)
            m_tabs->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
}

ReviewPack::Input ReviewPackDialog::input() const
{
    ReviewPack::Input in = m_base;
    in.site = m_site->text().trimmed();
    in.tester = m_tester->text().trimmed();
    in.loco = m_loco->text().trimmed();
    in.whatToLookAt = m_what->toPlainText().trimmed();
    in.fromMs = in.toMs = 0;
    if (m_range->isChecked()) {
        in.fromMs = m_from->dateTime().toMSecsSinceEpoch();
        in.toMs = m_to->dateTime().toMSecsSinceEpoch();
    }
    if (!m_run->isChecked()) in.testRunPath.clear();
    in.captures.clear();
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->item(i)->checkState() != Qt::Checked) continue;
        const QString key = m_tabs->item(i)->data(Qt::UserRole).toString();
        for (const ReviewPack::Capture &c : m_captures)
            if (c.tabKey == key) in.captures.append(c);
    }
    return in;
}

void ReviewPackDialog::refreshCount()
{
    const ReviewPack::Input in = input();
    const bool all = in.fromMs <= 0 && in.toMs <= 0;
    int frames = 0, notes = 0;
    qint64 bytes = 0;
    for (const ReviewPack::Capture &c : in.captures) {
        qint64 first = 0, last = 0;
        for (const LogEntryPtr &e : c.rows) {
            if (!e || e->rawBytes.isEmpty() || (!all && (e->epochMs < in.fromMs || e->epochMs > in.toMs))) continue;
            ++frames;
            bytes += e->rawBytes.size() + 16;
            if (!first) first = e->epochMs;
            last = e->epochMs;
        }
        for (const Bookmark &b : c.marks)
            if (b.tabKey == c.tabKey && frames && b.epochMs >= first && b.epochMs <= last) ++notes;
    }
    const bool bad = !all && in.toMs <= in.fromMs;
    m_export->setEnabled(frames > 0 && !bad);
    if (bad) {
        m_count->setText(tr("To is before From."));
    } else if (!frames) {
        m_count->setText(tr("No recorded frames in the chosen tabs and span: nothing to pack."));
    } else {
        m_count->setText(tr("%1 frames from %2 tab(s), %3 note(s)%4 — about %5 before compression.")
                             .arg(frames).arg(in.captures.size()).arg(notes)
                             .arg(in.testRunPath.isEmpty() ? QString() : tr(", the test run"))
                             .arg(mb(bytes)));
    }
}

bool ReviewPackDialog::exportTo(const QString &path)
{
    const ReviewPack::Input in = input();
    QSettings ini(Settings::iniPath(), QSettings::IniFormat);
    ini.setValue(QStringLiteral("reviewpack/site"), in.site);
    ini.setValue(QStringLiteral("reviewpack/tester"), in.tester);
    ini.setValue(QStringLiteral("reviewpack/loco"), in.loco);

    m_status->state(tr("Writing the pack…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const ReviewPack::Written w = ReviewPack::write(path, in);
    QApplication::restoreOverrideCursor();
    if (!w.ok) {
        m_status->fail(tr("Not exported: %1").arg(w.error));
        return false;
    }
    m_status->ok(tr("Wrote %1 (%2): %3 frames, %4 note(s), %5 files. Send it as it is; "
                    "File ▸ Open review pack… opens it.")
                     .arg(QFileInfo(path).fileName(), mb(w.bytes)).arg(w.records).arg(w.notes).arg(w.files.size()));
    return true;
}
