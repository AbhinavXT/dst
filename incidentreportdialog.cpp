#include "incidentreportdialog.h"

#include "uicolors.h"

#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

static const char *kEditFormat = "yyyy-MM-dd HH:mm:ss.zzz";

IncidentReportDialog::IncidentReportDialog(qint64 seedMs, qint64 minMs, qint64 maxMs, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Incident report"));
    setModal(true);

    if (minMs > maxMs) { const qint64 t = minMs; minMs = maxMs; maxMs = t; }
    if (seedMs < minMs) seedMs = minMs;
    if (seedMs > maxMs) seedMs = maxMs;

    auto *root = new QVBoxLayout(this);

    {
        const QDateTime lo = QDateTime::fromMSecsSinceEpoch(minMs);
        const QDateTime hi = QDateTime::fromMSecsSinceEpoch(maxMs);
        const bool sameDay = (lo.date() == hi.date());
        const QString fmt = sameDay ? QStringLiteral("HH:mm:ss.zzz") : QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz");
        auto *hint = new QLabel(tr("This tab spans  %1  →  %2").arg(lo.toString(fmt), hi.toString(fmt)));
        hint->setStyleSheet(UiColor::mutedStyle());
        root->addWidget(hint);
    }

    auto *form = new QFormLayout;

    m_edit = new QDateTimeEdit;
    m_edit->setDisplayFormat(QString::fromLatin1(kEditFormat));
    m_edit->setCalendarPopup(true);
    // The moment is allowed to sit anywhere; only the editor's own range needs
    // widening a little past the tab so an incident right at either end is
    // still pickable with room either side.
    m_edit->setDateTimeRange(QDateTime::fromMSecsSinceEpoch(minMs - 24 * 3600 * 1000LL),
                             QDateTime::fromMSecsSinceEpoch(maxMs + 24 * 3600 * 1000LL));
    m_edit->setDateTime(QDateTime::fromMSecsSinceEpoch(seedMs));
    form->addRow(tr("Moment:"), m_edit);

    m_before = new QSpinBox;
    m_before->setRange(1, 24 * 3600);
    m_before->setValue(60);
    m_before->setSuffix(tr(" s"));
    form->addRow(tr("Before:"), m_before);

    m_after = new QSpinBox;
    m_after->setRange(1, 24 * 3600);
    m_after->setValue(60);
    m_after->setSuffix(tr(" s"));
    form->addRow(tr("After:"), m_after);

    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Build report"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    m_edit->setFocus();
}

qint64 IncidentReportDialog::atMs() const { return m_edit->dateTime().toMSecsSinceEpoch(); }
qint64 IncidentReportDialog::beforeMs() const { return qint64(m_before->value()) * 1000; }
qint64 IncidentReportDialog::afterMs() const { return qint64(m_after->value()) * 1000; }
