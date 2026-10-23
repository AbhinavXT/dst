#include "radiochartdialog.h"

#include "logmodel.h"
#include "statusline.h"
#include "uicolors.h"

#include <QDateTime>
#include <QDateTimeEdit>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
constexpr qint64 kAroundMs = 30 * 1000;   // the default window: the moment ±30 s
}

RadioChartDialog::RadioChartDialog(LogModel *model, const QString &tabName, qint64 centreMs, QWidget *parent)
    : QDialog(parent), m_model(model), m_tabName(tabName)
{
    setWindowTitle(tr("Radio chart — %1").arg(tabName));
    setObjectName(QStringLiteral("radioChartDialog"));
    auto timeEdit = [this](const QString &name) {
        auto *e = new QDateTimeEdit(this);
        e->setObjectName(name);
        e->setDisplayFormat(QStringLiteral("yyyy-MM-dd  HH:mm:ss"));
        e->setCalendarPopup(false);
        return e;
    };
    m_from = timeEdit(QStringLiteral("radioChartFrom"));
    m_to = timeEdit(QStringLiteral("radioChartTo"));
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("radioChartSummary"));
    m_summary->setWordWrap(true);
    m_picture = new QLabel(this);
    m_picture->setObjectName(QStringLiteral("radioChartPicture"));
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(m_picture);
    scroll->setMinimumSize(640, 360);
    auto *note = new QLabel(tr("One arrow per SLRP, LSRP and ARP in the order logged. ✕: the receiver would not "
                               "process it (the clause beside it). Dashed: a silence on one link."), this);
    note->setWordWrap(true);
    note->setStyleSheet(UiColor::mutedStyle());

    m_status = new StatusLine(this);
    m_png = new QPushButton(tr("Export PNG…"), this);
    m_png->setObjectName(QStringLiteral("radioChartPng"));
    m_png->setDefault(true);
    m_svg = new QPushButton(tr("Export SVG…"), this);
    m_svg->setObjectName(QStringLiteral("radioChartSvg"));
    auto *close = new QPushButton(tr("Close"), this);

    auto *form = new QFormLayout;
    form->addRow(tr("From:"), m_from);
    form->addRow(tr("To:"), m_to);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_status, 1);
    buttons->addWidget(m_png);
    buttons->addWidget(m_svg);
    buttons->addWidget(close);
    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_summary);
    lay->addWidget(scroll, 1);
    lay->addWidget(note);
    lay->addLayout(buttons);

    setRange(centreMs - kAroundMs, centreMs + kAroundMs);
    connect(m_from, &QDateTimeEdit::dateTimeChanged, this, &RadioChartDialog::rebuild);
    connect(m_to, &QDateTimeEdit::dateTimeChanged, this, &RadioChartDialog::rebuild);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_png, &QPushButton::clicked, this, [this]() { exportAs(QStringLiteral("png"), tr("PNG image (*.png)")); });
    connect(m_svg, &QPushButton::clicked, this, [this]() { exportAs(QStringLiteral("svg"), tr("SVG drawing (*.svg)")); });
}

void RadioChartDialog::setRange(qint64 fromMs, qint64 toMs)
{
    const QSignalBlocker b1(m_from), b2(m_to);
    m_from->setDateTime(QDateTime::fromMSecsSinceEpoch(fromMs));
    m_to->setDateTime(QDateTime::fromMSecsSinceEpoch(toMs));
    rebuild();
}

void RadioChartDialog::rebuild()
{
    m_chart = RadioChart::build(m_model, m_tabName, m_from->dateTime().toMSecsSinceEpoch(),
                                m_to->dateTime().toMSecsSinceEpoch());
    const bool ok = m_chart.error.isEmpty();
    m_summary->setText(ok ? m_chart.summary() : m_chart.error);
    m_png->setEnabled(ok);
    m_svg->setEnabled(ok);
    m_picture->setPixmap(ok ? QPixmap::fromImage(RadioChart::toImage(RadioChart::draw(m_chart))) : QPixmap());
    m_picture->adjustSize();
}

void RadioChartDialog::exportAs(const QString &suffix, const QString &filter)
{
    const QString name = QStringLiteral("Radio_%1_%2.%3")
                             .arg(m_tabName, QDateTime::fromMSecsSinceEpoch(m_chart.fromMs).toString(QStringLiteral("yyyyMMdd_HHmmss")), suffix);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save the radio chart"), QDir::home().filePath(name), filter);
    if (!path.isEmpty()) exportTo(path);
}

bool RadioChartDialog::exportTo(const QString &path)
{
    QString err = m_chart.error;
    if (err.isEmpty() && RadioChart::save(m_chart, path, &err)) {
        m_status->ok(tr("Wrote %1: %2 frames, %3 the receiver would not process")
                         .arg(QFileInfo(path).fileName()).arg(m_chart.messages.size()).arg(m_chart.dropped()));
        return true;
    }
    m_status->fail(tr("Not exported: %1").arg(err));
    return false;
}
