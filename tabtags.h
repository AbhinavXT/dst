#ifndef TABTAGS_H
#define TABTAGS_H
// =============================================================================
//  TabTags -- a colour and an optional short label per loco / source.
//
//  Keyed by the tab key ("7_1": source id, kavach id), which is also what
//  the Live Loco Console's loco selector lists. So a tag set on a tab
//  follows that loco everywhere it appears:
//    - the main window's tab       (a coloured dot before the name; the
//                                   name's own colour stays free for tab
//                                   HEALTH -- amber/red when a source goes
//                                   quiet)
//    - its pop-out window          (window icon and title)
//    - that window's minimise chip (the chip shows the window icon)
//    - the Live Loco Console       (selector entries, and the window's icon
//                                   and title for the loco being watched)
//
//  Saved in dlconsole.ini (ui/tabTags/<key>). changed(key) tells every view
//  to redraw.
// =============================================================================
#include <QIcon>
#include <QObject>
#include <QString>

struct TabTag {
    int     color = -1;     // index into UiColor::tagColor(); -1 = no tag
    QString label;          // optional, short ("Brake test")

    bool isSet() const { return color >= 0 || !label.isEmpty(); }
};

class TabTags : public QObject
{
    Q_OBJECT
public:
    static TabTags *instance();

    TabTag tag(const QString &key) const;
    void   setTag(const QString &key, const TabTag &tag);
    void   setColor(const QString &key, int color);
    void   setLabel(const QString &key, const QString &label);

    // A round dot in the tag's colour; a null icon when untagged.
    QIcon dotIcon(const QString &key) const;
    static QIcon dotIconFor(int color);

    // "7_1" -> "7_1 · Brake test" when labelled.
    QString decoratedName(const QString &key, const QString &name) const;

    static const int kMaxLabelLength = 24;

signals:
    void changed(const QString &key);

private:
    explicit TabTags(QObject *parent);
};

#endif // TABTAGS_H
