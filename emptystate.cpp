#include "emptystate.h"

#include "uicolors.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QEvent>
#include <QLabel>
#include <QResizeEvent>

namespace EmptyState {
namespace {

const char *kObjectName = "dlEmptyState";

// The overlay itself: a label parented to the view's viewport, kept the
// size of the viewport, shown only while the model has no rows.
class Overlay : public QLabel
{
public:
    explicit Overlay(QAbstractItemView *view)
        : QLabel(view->viewport()), m_view(view)
    {
        setObjectName(QLatin1String(kObjectName));
        setAlignment(Qt::AlignCenter);
        setWordWrap(true);
        // Without this the label eats clicks meant for the view, which is
        // only obvious the first time someone tries to click a row that
        // arrived while the overlay was still on screen.
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setMargin(24);
        applyColour();
        view->viewport()->installEventFilter(this);
        UiColor::onThemeChange(this, [this] { applyColour(); });

        // The view can be given a different model later, so connectModel()
        // is re-run from refresh() rather than only here.
        connectModel();
        refresh();
    }

    void applyColour()
    {
        // Only when it actually changes. setStyleSheet() on a visible widget
        // makes Qt re-polish it, which can deliver a PaletteChange back to
        // this same widget — and a theme callback that re-sets the sheet
        // unconditionally then calls itself until the stack runs out. That
        // is a segfault with no other symptom, which is how it presented.
        const QString want = UiColor::mutedStyle();
        if (styleSheet() != want) { setStyleSheet(want); }
    }

    void connectModel()
    {
        QAbstractItemModel *m = m_view->model();
        if (m == m_model) { return; }
        for (const QMetaObject::Connection &c : m_modelConns) { disconnect(c); }
        m_modelConns.clear();
        m_model = m;
        if (!m) { return; }
        m_modelConns
            << connect(m, &QAbstractItemModel::rowsInserted, this, &Overlay::refresh)
            << connect(m, &QAbstractItemModel::rowsRemoved,  this, &Overlay::refresh)
            << connect(m, &QAbstractItemModel::modelReset,   this, &Overlay::refresh)
            << connect(m, &QAbstractItemModel::layoutChanged, this, &Overlay::refresh);
    }

    void setProvider(std::function<QString()> p) { m_provider = std::move(p); }

    void refresh()
    {
        connectModel();
        if (m_provider) { setText(m_provider()); }
        const QAbstractItemModel *m = m_view->model();
        // A view whose rows are all filtered out is empty for the operator's
        // purposes even though the source model has plenty, so this asks the
        // view's own model — which for a filtered view is the proxy.
        const bool empty = !m || m->rowCount(m_view->rootIndex()) == 0;
        setVisible(empty && !text().isEmpty());
        if (empty) { resize(m_view->viewport()->size()); raise(); }
    }

protected:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (obj == m_view->viewport() && e->type() == QEvent::Resize) {
            resize(static_cast<QResizeEvent *>(e)->size());
        }
        return QLabel::eventFilter(obj, e);
    }

private:
    QAbstractItemView  *m_view  = nullptr;
    QAbstractItemModel *m_model = nullptr;
    std::function<QString()> m_provider;
    QVector<QMetaObject::Connection> m_modelConns;
};

Overlay *existing(const QAbstractItemView *view)
{
    if (!view || !view->viewport()) { return nullptr; }
    // Overlay has no Q_OBJECT, so look it up as the QLabel it is (Qt 6
    // refuses findChild<T> for a T without its own meta-object).
    return static_cast<Overlay *>(
        view->viewport()->findChild<QLabel *>(QLatin1String(kObjectName),
                                               Qt::FindDirectChildrenOnly));
}

}  // namespace

void attach(QAbstractItemView *view, const QString &message)
{
    if (!view || !view->viewport()) { return; }

    Overlay *o = existing(view);
    if (message.isEmpty()) {
        delete o;
        return;
    }
    if (!o) { o = new Overlay(view); }
    o->setText(message);
    o->refresh();
}

void attach(QAbstractItemView *view, std::function<QString()> provider)
{
    if (!view || !view->viewport() || !provider) { return; }
    Overlay *o = existing(view);
    if (!o) { o = new Overlay(view); }
    o->setProvider(std::move(provider));
    o->refresh();
}

QString message(const QAbstractItemView *view)
{
    const Overlay *o = existing(view);
    return o ? o->text() : QString();
}

bool isShowing(const QAbstractItemView *view)
{
    const Overlay *o = existing(view);
    return o && o->isVisible();
}

}  // namespace EmptyState
