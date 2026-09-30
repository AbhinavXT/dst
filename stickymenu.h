#ifndef STICKYMENU_H
#define STICKYMENU_H

// =============================================================================
//  StickyMenu
//  -----------------------------------------------------------------------------
//  A QMenu that does NOT close when a checkable item is toggled.
//
//  WHY
//    Qt closes a menu as soon as any action fires. For a one-shot command
//    that is right. For a list of independent on/off toggles — which
//    columns to show, which panels to open — it is wrong: choosing to hide
//    three columns means opening the same menu three times, and each time
//    the menu shuts before you can see the effect of what you just did.
//
//    Non-checkable items (commands like "Reset panel layout") close the
//    menu as normal, because there the click IS the whole interaction.
//
//  Keyboard behaviour is handled too: Space and Return on a checkable item
//  toggle in place, so the menu is equally usable without a mouse.
// =============================================================================

#include <QMenu>

class StickyMenu : public QMenu
{
    Q_OBJECT

public:
    explicit StickyMenu(const QString &title, QWidget *parent = nullptr);

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
};

#endif // STICKYMENU_H
