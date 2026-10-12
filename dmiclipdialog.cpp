#include "dmiclipdialog.h"

#include "logmodel.h"
#include "statusline.h"
#include "uicolors.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr qint64 kAroundMs = 30 * 1000;   // the default window: the moment ±30 s

QString duration(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    return s < 60 ? QStringLiteral("%1 s").arg(s) : QStringLiteral("%1 min %2 s").arg(s / 60).arg(s % 60);
}
}  // namespace

DmiClipDialog::DmiClipDialog(LogModel *model, const QString &tabName, qint64 centreMs, const QString &loco,
                             QWidget *parent)
    : QDialog(parent), m_model(model), m_tabName(tabName)
{
    setWindowTitle(tr("DMI clip — %1").arg(tabName));
    setObjectName(QStringLiteral("dmiClipDialog"));

    m_loco = new QComboBox(this);
    m_loco->setObjectName(QStringLiteral("dmiClipLoco"));
    for (const QString &k : DmiClip::locosIn(model)) m_loco->addItem(tr("Loco %1").arg(k), k);
    auto timeEdit = [this](const QString &name) {
        auto *e = new QDateTimeEdit(this);
        e->setObjectName(name);
        e->setDisplayFormat(QStringLiteral("yyyy-MM-dd  HH:mm:ss"));
        e->setCalendarPopup(false);
        return e;
    };
    m_from = timeEdit(QStringLiteral("dmiClipFrom"));
    m_to = timeEdit(QStringLiteral("dmiClipTo"));
    m_speed = new QComboBox(this);
    m_speed->setObjectName(QStringLiteral("dmiClipSpeed"));
    for (int s : { 1, 2, 4, 8 })
        m_speed->addItem(s == 1 ? tr("Real time (1×)") : tr("%1× faster").arg(s), s);
    m_scale = new QComboBox(this);
    m_scale->setObjectName(QStringLiteral("dmiClipScale"));
    for (int p : { 100, 75, 50 })
        m_scale->addItem(tr("%1 % (%2 × %3)").arg(p).arg(800 * p / 100).arg(600 * p / 100), p);
    m_scale->setCurrentIndex(1);

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("dmiClipPreview"));
    m_preview->setWordWrap(true);
    auto *note = new QLabel(tr("Each frame is held until the next @dmi, as the DMI holds it. A gap of more "
                               "than 3 s is shortened and the clip says so on that frame."), this);
    note->setWordWrap(true);
    note->setStyleSheet(UiColor::mutedStyle());

    m_status = new StatusLine(this);
    m_export = new QPushButton(tr("Export GIF…"), this);
    m_export->setObjectName(QStringLiteral("dmiClipExport"));
    m_export->setDefault(true);
    auto *close = new QPushButton(tr("Close"), this);

    auto *form = new QFormLayout;
    form->addRow(tr("Loco:"), m_loco);
    form->addRow(tr("From:"), m_from);
    form->addRow(tr("To:"), m_to);
    form->addRow(tr("Speed:"), m_speed);
    form->addRow(tr("Size:"), m_scale);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(m_export);
    buttons->addWidget(close);
    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_preview);
    lay->addWidget(note);
    lay->addStretch(1);
    lay->addLayout(buttons);

    if (!loco.isEmpty()) setLoco(loco);
    setRange(centreMs - kAroundMs, centreMs + kAroundMs);

    connect(m_loco, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DmiClipDialog::refreshPreview);
    connect(m_speed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DmiClipDialog::refreshPreview);
    connect(m_from, &QDateTimeEdit::dateTimeChanged, this, &DmiClipDialog::refreshPreview);
    connect(m_to, &QDateTimeEdit::dateTimeChanged, this, &DmiClipDialog::refreshPreview);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_export, &QPushButton::clicked, this, [this]() {
        const DmiClip::Options o = options();
        const QString name = QStringLiteral("DMI_%1_%2.gif")
                                 .arg(o.locoKey, QDateTime::fromMSecsSinceEpoch(o.fromMs).toString(QStringLiteral("yyyyMMdd_HHmmss")));
        const QString path = QFileDialog::getSaveFileName(this, tr("Save the DMI clip"),
                                                          QDir::home().filePath(name), tr("Animated GIF (*.gif)"));
        if (!path.isEmpty()) exportTo(path);
    });
    refreshPreview();
}

DmiClip::Options DmiClipDialog::options() const
{
    DmiClip::Options o;
    o.locoKey = m_loco->currentData().toString();
    o.fromMs = m_from->dateTime().toMSecsSinceEpoch();
    o.toMs = m_to->dateTime().toMSecsSinceEpoch();
    o.speed = m_speed->currentData().toInt();
    o.scalePct = m_scale->currentData().toInt();
    return o;
}

void DmiClipDialog::setRange(qint64 fromMs, qint64 toMs)
{
    m_from->setDateTime(QDateTime::fromMSecsSinceEpoch(fromMs));
    m_to->setDateTime(QDateTime::fromMSecsSinceEpoch(toMs));
    refreshPreview();
}

void DmiClipDialog::setSpeed(int speed)
{
    const int i = m_speed->findData(speed);
    if (i >= 0) m_speed->setCurrentIndex(i);
}

void DmiClipDialog::setScale(int pct)
{
    const int i = m_scale->findData(pct);
    if (i >= 0) m_scale->setCurrentIndex(i);
}

void DmiClipDialog::setLoco(const QString &key)
{
    const int i = m_loco->findData(key);
    if (i >= 0) m_loco->setCurrentIndex(i);
}

QStringList DmiClipDialog::locos() const
{
    QStringList out;
    for (int i = 0; i < m_loco->count(); ++i) out << m_loco->itemData(i).toString();
    return out;
}

void DmiClipDialog::refreshPreview()
{
    if (!m_preview) return;
    if (m_loco->count() == 0) {
        m_preview->setText(tr("This tab has no @dmi frames: nothing to make a clip of."));
        m_export->setEnabled(false);
        return;
    }
    const DmiClip::Options o = options();
    if (o.toMs <= o.fromMs) {
        m_preview->setText(tr("The end is not after the start."));
        m_export->setEnabled(false);
        return;
    }
    if (o.toMs - o.fromMs > DmiClip::kMaxSpanMs) {
        m_preview->setText(tr("A clip covers at most %1 minutes of log.").arg(DmiClip::kMaxSpanMs / 60000));
        m_export->setEnabled(false);
        return;
    }
    const int n = DmiClip::countFrames(m_model, o);
    m_export->setEnabled(n > 0);
    if (n == 0) {
        m_preview->setText(tr("No @dmi frames of loco %1 in that window.").arg(o.locoKey));
        return;
    }
    m_preview->setText(tr("%1 DMI frame%2 over %3 of log: a clip of about %4.")
                           .arg(n).arg(n == 1 ? "" : "s").arg(duration(o.toMs - o.fromMs))
                           .arg(duration((o.toMs - o.fromMs) / qMax(1, o.speed))));
}

bool DmiClipDialog::exportTo(const QString &path)
{
    const DmiClip::Options o = options();
    m_status->state(tr("Drawing the clip…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const DmiClip::Clip clip = DmiClip::build(m_model, o, [this](int done, int total) {
        m_status->state(tr("Drawing frame %1 of %2…").arg(done).arg(total));
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    });
    QString err;
    const bool ok = clip.error.isEmpty() && DmiClip::writeGif(clip, path, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        m_status->fail(tr("Not exported: %1").arg(clip.error.isEmpty() ? err : clip.error));
        return false;
    }
    m_status->ok(tr("Wrote %1: %2 DMI frames, plays %3%4")
                     .arg(QFileInfo(path).fileName()).arg(clip.dmiFrames).arg(duration(clip.clipMs))
                     .arg(clip.gaps ? tr("; %1 gap%2 over 3 s shortened").arg(clip.gaps).arg(clip.gaps == 1 ? "" : "s")
                                    : QString()));
    return true;
}
