#include "bignumberpanel.h"

#include "settings.h"
#include "uicolors.h"
#include "undolog.h"

#include <QPointer>

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QContextMenuEvent>
#include <QFrame>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QVBoxLayout>
#include <QSettings>

namespace {

const char kTilesKey[] = "lococonsole/bigNumbers";
const int  kColumns    = 4;

QString durationText(qint64 ms)
{
    const qint64 s = qMax<qint64>(0, ms / 1000);
    if (s < 60) return QObject::tr("%1 s").arg(s);
    if (s < 3600) return QObject::tr("%1 min %2 s").arg(s / 60).arg(s % 60);
    return QObject::tr("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}

}  // namespace

// The last minute of a tile's value, drawn small under the number: whether
// it is rising, falling or holding is what a number alone cannot say.
class Sparkline : public QWidget
{
public:
    explicit Sparkline(QWidget *parent) : QWidget(parent)
    {
        setFixedHeight(qMax(18, fontMetrics().height() + 4));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setData(const QVector<QPair<qint64, double>> &points, qint64 nowMs, const QColor &colour)
    {
        m_points = points;
        m_now = nowMs;
        m_colour = colour;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_points.size() < 2) return;
        double lo = m_points.first().second, hi = lo;
        for (const auto &p : m_points) { lo = qMin(lo, p.second); hi = qMax(hi, p.second); }
        if (hi - lo < 1e-9) { lo -= 1; hi += 1; }
        const QRectF r = QRectF(rect()).adjusted(1, 2, -1, -2);
        const double span = BigNumberPanel::kSparkSeconds * 1000.0;
        QPainterPath path;
        for (int i = 0; i < m_points.size(); ++i) {
            const double x = r.right() - (m_now - m_points.at(i).first) / span * r.width();
            const double y = r.bottom() - (m_points.at(i).second - lo) / (hi - lo) * r.height();
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(m_colour, 1.6));
        p.drawPath(path);
    }
private:
    QVector<QPair<qint64, double>> m_points;
    qint64 m_now = 0;
    QColor m_colour;
};

BigNumberPanel::BigNumberPanel(QWidget *parent)
    : QWidget(parent)
{
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(8);
    m_grid->setVerticalSpacing(8);
    m_hint = new QLabel(tr("No big numbers. Right-click a field in any tab below ▸ Show as big number."), this);
    m_hint->setStyleSheet(UiColor::mutedStyle());
    m_refs = loadSaved();
    rebuild();
    UiColor::onThemeChange(this, [this]() { restyle(); });
}

QVector<LiveFieldRef> BigNumberPanel::loadSaved()
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    if (!settings.contains(QLatin1String(kTilesKey))) {
        return LiveFields::defaultBigNumbers();
    }
    QVector<LiveFieldRef> refs;
    for (const QString &text : settings.value(QLatin1String(kTilesKey)).toStringList()) {
        const LiveFieldRef ref = LiveFieldRef::parse(text);
        if (ref.isValid()) {
            refs.append(ref);
        }
    }
    return refs;
}

void BigNumberPanel::save() const
{
    QStringList texts;
    for (const LiveFieldRef &ref : m_refs) {
        texts.append(ref.serialise());
    }
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.setValue(QLatin1String(kTilesKey), texts);
}

void BigNumberPanel::setTiles(const QVector<LiveFieldRef> &refs)
{
    m_refs = refs;
    save();
    rebuild();
    emit tilesChanged();
}

void BigNumberPanel::addTile(const LiveFieldRef &ref)
{
    QVector<LiveFieldRef> refs = m_refs;
    refs.append(ref);
    setTiles(refs);
}

void BigNumberPanel::rebuild()
{
    for (const Tile &tile : m_tiles) {
        tile.frame->deleteLater();
    }
    m_tiles.clear();
    m_grid->removeWidget(m_hint);

    if (m_refs.isEmpty()) {
        m_grid->addWidget(m_hint, 0, 0);
        m_hint->show();
        return;
    }
    m_hint->hide();

    for (int index = 0; index < m_refs.size(); ++index) {
        Tile tile;
        tile.frame = new QFrame(this);
        tile.frame->setObjectName(QStringLiteral("bigTile"));
        tile.frame->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tile.frame, &QWidget::customContextMenuRequested, this, [this, index](const QPoint &pos) {
            showTileMenu(index, m_tiles.at(index).frame->mapToGlobal(pos));
        });
        auto *layout = new QVBoxLayout(tile.frame);
        layout->setContentsMargins(14, 8, 14, 10);
        layout->setSpacing(0);

        tile.label = new QLabel(m_refs.at(index).label.toUpper(), tile.frame);
        QFont labelFont = tile.label->font();
        labelFont.setBold(true);
        labelFont.setPointSizeF(QApplication::font().pointSizeF() * 0.95);
        tile.label->setFont(labelFont);
        layout->addWidget(tile.label);

        // An explicit multiple of the application font, so View > Text size
        // and F11 (presentation) make these bigger too.
        tile.value = new QLabel(QStringLiteral("—"), tile.frame);
        QFont valueFont = tile.value->font();
        valueFont.setBold(true);
        valueFont.setPointSizeF(QApplication::font().pointSizeF() * 3.0);
        tile.value->setFont(valueFont);
        tile.value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        // Session 182: a long value ("4 (Full_Supervision)" at three times the
        // font) set the tile's minimum, and held the Loco Console at 1,242 px
        // on Linux. It may be cut short on a narrow window; the tooltip holds
        // it (set with every value).
        tile.value->setMinimumWidth(80);
        layout->addWidget(tile.value);

        tile.spark = new Sparkline(tile.frame);
        layout->addWidget(tile.spark);

        tile.detail = new QLabel(tile.frame);
        // Wraps rather than setting the tile's width (session 151): on one line,
        // "FRAME_NUM 50577 · arp · 97d 08h ago" set the tile's minimum, and four
        // of them held the Loco Console over 1100 px with Linux's fonts. The
        // tiles then share the row evenly; a long detail takes two lines
        // rather than being cut (a minimum width alone cut it).
        tile.detail->setWordWrap(true);
        layout->addWidget(tile.detail);
        tile.frame->installEventFilter(this);
        tile.frame->setToolTip(tr("Double-click: plot over time · right-click: thresholds and more"));

        m_grid->addWidget(tile.frame, index / kColumns, index % kColumns);
        m_tiles.append(tile);
    }
    for (int column = 0; column < kColumns; ++column) {
        m_grid->setColumnStretch(column, 1);
    }
    restyle();
}

void BigNumberPanel::setValues(const QVector<TileValue> &values)
{
    setValues(values, QDateTime::currentMSecsSinceEpoch());
}

void BigNumberPanel::setValues(const QVector<TileValue> &values, qint64 nowMs)
{
    for (int index = 0; index < m_tiles.size() && index < values.size(); ++index) {
        Tile &tile = m_tiles[index];
        const TileValue &value = values.at(index);
        if (value.missing) {
            tile.value->setText(QStringLiteral("—"));
        } else {
            tile.value->setText(value.text);
        }
        tile.value->setToolTip(value.text);
        tile.stale = value.stale || value.missing;
        tile.level = (value.missing || value.stale) ? 0 : value.level;

        // Time in state: a mode, a direction, a brake state is read as "what,
        // and since when". Measured from when the TEXT changed, so it works
        // for any field; shown for states (an enum name, or not a number).
        if (!value.missing && value.text != tile.lastText) {
            tile.lastText = value.text;
            tile.changedMs = nowMs;
        }
        const bool isState = value.trackState && !value.missing
                          && (!value.hasNumber || value.text.contains(QLatin1Char('(')));
        tile.stateText = (isState && tile.changedMs >= 0) ? tr("for %1").arg(durationText(nowMs - tile.changedMs))
                                                          : QString();
        tile.detail->setText(tile.stateText.isEmpty() ? value.detail
                             : value.detail.isEmpty() ? tile.stateText
                                                      : value.detail + QStringLiteral(" · ") + tile.stateText);

        // Sparkline: measurements only, and only live ones.
        if (value.hasNumber && !value.missing && !isState) {
            if (tile.history.isEmpty() || tile.history.last().first != nowMs) {
                tile.history.append(qMakePair(nowMs, value.number));
            }
        }
        const qint64 cutoff = nowMs - qint64(kSparkSeconds) * 1000;
        while (!tile.history.isEmpty() && tile.history.first().first < cutoff) tile.history.removeFirst();
        const QColor ink = tile.level == 2 ? UiColor::error() : tile.level == 1 ? UiColor::warning()
                                                                                 : UiColor::series(0);
        tile.spark->setData(isState ? QVector<QPair<qint64, double>>() : tile.history, nowMs,
                            tile.stale ? UiColor::muted() : ink);
    }
    restyle();
}

void BigNumberPanel::restyle()
{
    const QPalette pal = palette();
    for (const Tile &tile : m_tiles) {
        tile.frame->setStyleSheet(
            QStringLiteral("QFrame#bigTile { background-color:%1; border:1px solid %2; border-radius:10px; }")
                .arg(pal.color(QPalette::Base).name(), UiColor::frame().name(QColor::HexArgb)));
        tile.label->setStyleSheet(UiColor::mutedStyle());
        tile.detail->setStyleSheet(UiColor::mutedStyle());
        // Stale or missing: the number is muted, so an old value is never
        // mistaken for a live one across the room. A rule's amber or red
        // colours the number AND thickens the border: colour plus weight,
        // so it reads without telling the two colours apart.
        if (tile.stale) {
            tile.value->setStyleSheet(UiColor::mutedStyle());
        } else if (tile.level == 2) {
            tile.value->setStyleSheet(UiColor::errorStyle());
        } else if (tile.level == 1) {
            tile.value->setStyleSheet(UiColor::warningStyle());
        } else {
            tile.value->setStyleSheet(UiColor::style(pal.color(QPalette::Text)));
        }
        if (!tile.stale && tile.level > 0) {
            const QColor edge = tile.level == 2 ? UiColor::error() : UiColor::warning();
            tile.frame->setStyleSheet(
                QStringLiteral("QFrame#bigTile { background-color:%1; border:%2px solid %3; border-radius:10px; }")
                    .arg(pal.color(QPalette::Base).name()).arg(tile.level == 2 ? 4 : 3).arg(edge.name()));
        }
    }
}

void BigNumberPanel::showTileMenu(int index, const QPoint &globalPos)
{
    if (index < 0 || index >= m_refs.size()) {
        return;
    }
    QMenu menu(this);
    QAction *plot = menu.addAction(tr("Plot over time"));
    QAction *rule = menu.addAction(m_refs.at(index).rule.isSet()
                                       ? tr("Colour thresholds… (%1)").arg(m_refs.at(index).rule.describe())
                                       : tr("Colour thresholds…"));
    menu.addSeparator();
    QAction *rename = menu.addAction(tr("Rename…"));
    QAction *left = menu.addAction(tr("Move left"));
    left->setEnabled(index > 0);
    QAction *right = menu.addAction(tr("Move right"));
    right->setEnabled(index < m_refs.size() - 1);
    QAction *remove = menu.addAction(tr("Remove"));
    menu.addSeparator();
    QAction *reset = menu.addAction(tr("Reset to defaults (Speed, Mode, Frame clock, Brake)"));
    QAction *chosen = menu.exec(globalPos);

    QVector<LiveFieldRef> refs = m_refs;
    if (chosen == plot) {
        emit plotRequested(index);
        return;
    }
    if (chosen == rule) {
        editRule(index);
        return;
    }
    if (chosen == rename) {
        bool ok = false;
        const QString label = QInputDialog::getText(this, tr("Rename tile"), tr("Label:"), QLineEdit::Normal,
                                                    refs.at(index).label, &ok).trimmed();
        if (ok && !label.isEmpty()) {
            refs[index].label = label;
            setTiles(refs);
        }
    } else if (chosen == left) {
        refs.swapItemsAt(index, index - 1);
        setTiles(refs);
    } else if (chosen == right) {
        refs.swapItemsAt(index, index + 1);
        setTiles(refs);
    } else if (chosen == remove) {
        const QString gone = refs.at(index).label;
        const QVector<LiveFieldRef> before = m_refs;
        refs.removeAt(index);
        setTiles(refs);
        pushUndo(tr("Remove big number %1").arg(gone), before);
    } else if (chosen == reset) {
        const QVector<LiveFieldRef> before = m_refs;
        setTiles(LiveFields::defaultBigNumbers());
        pushUndo(tr("Reset big numbers"), before);
    }
}

bool BigNumberPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        for (int i = 0; i < m_tiles.size(); ++i) {
            if (m_tiles.at(i).frame == watched) {
                emit plotRequested(i);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BigNumberPanel::editRule(int index)
{
    if (index < 0 || index >= m_refs.size()) return;
    const TileRule current = m_refs.at(index).rule;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Colour thresholds — %1").arg(m_refs.at(index).label));
    auto *form = new QFormLayout(&dialog);
    auto *kind = new QComboBox(&dialog);
    kind->addItems({ tr("None"), tr("Fixed values"), tr("Against another field") });
    kind->setCurrentIndex(int(current.kind));
    auto *direction = new QComboBox(&dialog);
    direction->addItems({ tr("Bad when high"), tr("Bad when low") });
    direction->setCurrentIndex(current.above ? 0 : 1);
    auto *warn = new QDoubleSpinBox(&dialog);
    auto *alarm = new QDoubleSpinBox(&dialog);
    for (QDoubleSpinBox *b : { warn, alarm }) { b->setRange(-1e9, 1e9); b->setDecimals(2); }
    warn->setValue(current.warn);
    alarm->setValue(current.alarm);
    auto *ref = new QLineEdit(current.refSource.isEmpty() ? QStringLiteral("dmi:speed_limit_permissible")
                                                          : current.refSource, &dialog);
    ref->setToolTip(tr("<capture type>:<field>, as a tile's sources are written"));
    auto *margin = new QDoubleSpinBox(&dialog);
    margin->setRange(0, 1e6);
    margin->setValue(current.margin);
    form->addRow(tr("Rule"), kind);
    form->addRow(tr("Direction"), direction);
    form->addRow(tr("Amber at"), warn);
    form->addRow(tr("Red at"), alarm);
    form->addRow(tr("Limit field"), ref);
    form->addRow(tr("Amber within"), margin);
    auto *note = new QLabel(tr("Against another field: amber when within the margin below that field's value, "
                               "red above it. E.g. speed against dmi:speed_limit_permissible."), &dialog);
    note->setWordWrap(true);
    note->setStyleSheet(UiColor::mutedStyle());
    form->addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto sync = [=]() {
        const int k = kind->currentIndex();
        direction->setEnabled(k == 1); warn->setEnabled(k == 1); alarm->setEnabled(k == 1);
        ref->setEnabled(k == 2); margin->setEnabled(k == 2);
    };
    connect(kind, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, sync);
    sync();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    TileRule r;
    r.kind = TileRule::Kind(kind->currentIndex());
    r.above = direction->currentIndex() == 0;
    r.warn = warn->value();
    r.alarm = alarm->value();
    r.refSource = ref->text().trimmed();
    r.margin = margin->value();
    if (r.kind == TileRule::Kind::Relative && !r.refSource.contains(QLatin1Char(':'))) r.kind = TileRule::Kind::None;
    setRule(index, r);
}

void BigNumberPanel::setRule(int index, const TileRule &rule)
{
    if (index < 0 || index >= m_refs.size()) return;
    const QVector<LiveFieldRef> before = m_refs;
    QVector<LiveFieldRef> refs = m_refs;
    refs[index].rule = rule;
    setTiles(refs);
    pushUndo(tr("Change thresholds of %1").arg(refs.at(index).label), before);
}

void BigNumberPanel::pushUndo(const QString &label, const QVector<LiveFieldRef> &before)
{
    if (!m_undo) {
        return;
    }
    QPointer<BigNumberPanel> self(this);
    m_undo->push(label, [self, before]() {
        if (!self) {
            return false;
        }
        self->setTiles(before);
        return true;
    });
}

void BigNumberPanel::reloadAll()
{
    for (QWidget *w : QApplication::allWidgets()) {
        if (auto *panel = qobject_cast<BigNumberPanel *>(w)) {
            panel->m_refs = loadSaved();
            panel->rebuild();
            emit panel->tilesChanged();
        }
    }
}

QString BigNumberPanel::valueTextAt(int index) const
{
    if (index < 0 || index >= m_tiles.size()) {
        return QString();
    }
    return m_tiles.at(index).value->text();
}

QString BigNumberPanel::detailTextAt(int index) const
{
    if (index < 0 || index >= m_tiles.size()) {
        return QString();
    }
    return m_tiles.at(index).detail->text();
}

bool BigNumberPanel::isStaleAt(int index) const
{
    return index >= 0 && index < m_tiles.size() && m_tiles.at(index).stale;
}

int BigNumberPanel::levelAt(int index) const
{
    return index >= 0 && index < m_tiles.size() ? m_tiles.at(index).level : 0;
}

int BigNumberPanel::sparkPointsAt(int index) const
{
    return index >= 0 && index < m_tiles.size() ? m_tiles.at(index).history.size() : 0;
}

QString BigNumberPanel::stateTextAt(int index) const
{
    return index >= 0 && index < m_tiles.size() ? m_tiles.at(index).stateText : QString();
}
