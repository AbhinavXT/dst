# Session 76 — Text size, presentation mode, tab colour tags, better pop-outs

## 1. Text size — View ▸ Text size (Ctrl + / Ctrl − / Ctrl 0)

One size for the whole application, from 80 % to 200 %, remembered between
runs. The shortcuts work from any DLConsole window. New `textzoom.{h,cpp}`.

**What scales, together:**
- **The app font:** everything that inherits it.
- **Every widget that sets its own size** (titles, monospace hex and
  values, the flasher's big numbers), keeping its proportion to the rest.
- **Every table's fixed row height,** so larger text is never clipped.
  This covers the log tabs, Live Loco Console, flash queue and field
  tables.
- Windows already open are rescaled in place. Windows opened later come out
  at the same size: `UiStyle::monoFont()` and `Settings::rowHeightFor()`
  apply the factor.
- UiStyle's 9 pt minimum now applies only before zoom starts; otherwise it
  quietly undid 80 % and 90 %.

**Two Qt behaviours found and handled while testing:**
- **With an application style sheet, Qt remembers each widget's font** the
  first time it styles it, and puts that font back every time the sheet is
  re-applied, which zoom must do. Explicitly sized widgets were snapping
  back to their first size on the second zoom. Zoom now updates Qt's
  remembered font (`_q_styleSheetWidgetFont`, the same property in Qt 5
  and 6).
- **"100 % size" is derived per widget and re-derived when something else
  resized it** (e.g. a Row density change), so repeated zooming never
  drifts. Tested with 20 changes back and forth.

## 2. Full screen / presentation — View ▸ Full screen / presentation (F11)

For whichever window is active: the main window or any tool window. New
`presentationmode.{h,cpp}`.
- **In:** full screen; menu bar, status bar and toolbars hidden; text one
  size larger while any window presents (not saved).
- **Out:** F11 again, or **Esc** unless you're typing in a field (Esc in
  the find bar still closes the find bar). Every bar comes back, and the
  window returns to normal or maximised as it was.
- Menu shortcuts keep working while the menu bar is hidden: they are lent
  to the window and handed back afterwards.

## 3. Tab colour tags — right-click a tab ▸ Colour Tag

Eight colours plus an optional short label, remembered per loco. New
`tabtags.{h,cpp}` and `UiColor::tagColor / tagName`.
- **Shown as a dot before the tab name,** plus the label after it
  ("L1_V1 · Brake test"). The name's own colour stays free for **tab
  health** (amber/red when a source goes quiet), so the two never compete.
- **Readable in every theme:** each tag colour is adjusted to at least 3:1
  (the WCAG floor for a graphical object) against the window and the
  background in every theme, and the eight stay distinct. The tests check
  this on all seven themes.

**The tag follows the loco everywhere:**
- its **pop-out window** (icon and title);
- that window's **minimise chip**, which shows the window icon;
- the **Live Loco Console**: dots in the Loco / Ctrl selector, and the
  window's title and icon name the loco being watched ("Live Loco Console —
  7_1 · Brake test").
- **How the Loco Console matches locos:** a tab's key comes from the UDP
  header (source id, kavach id), while the console lists the capture
  line's own loco/ctrl ids. They aren't assumed to be the same numbers.
  Each capture key is mapped to the tab its lines actually arrived on.

## 4. Pop-out windows — "Pop Out to Window", the Detach button, or drag a tab out

`DetachConsoleDialog` is replaced by `TabPopoutWindow`. It's still a **live
copy**: it shares the tab's model, and the tab stays in the main window.
- **Drag a tab out of the tab bar** (up or down past the bar) to pop it
  out where you let go. Dragging *along* the bar still reorders tabs.
- **A proper tool window:** it closes with the main window and minimises to
  a named chip. The old dialog had no owner.
- **Follows Row density and text size, the tabs' hidden columns and column
  widths.** The old dialog used a fixed 20 px row.
- **The tab's tag** is on the window's icon, title and header. It uses the
  tab's own name, not just its key.
- **Follow latest** (starts from the tab's scroll lock), and **Show in main
  window**, which brings the main window forward on that tab.
- **One per tab:** opening it again raises the one that's open.
- **Reopened on start:** pop-outs still open at shutdown come back on the
  next start, in the same place, as soon as that loco's tab appears. One
  you close yourself is forgotten.

Removed: `detachconsoledialog.{h,cpp,ui}` and a stale generated
`tests/ui_detachconsoledialog.h`.

## Tests

- **`textzoom` (17):**
  - app font and explicit sizes scale in proportion;
  - a bold-only font isn't scaled twice;
  - rows 30 → 38 at 125 %, and new tables match;
  - the monospace font scales;
  - no drift after 20 changes;
  - a size set in between becomes the new base;
  - snapping and the ends; 80 % survives a theme change;
  - the presentation boost isn't saved.
- **`presentationmode` (16):**
  - full screen with bars hidden; shortcuts lent and returned;
  - maximised comes back maximised;
  - two windows share one boost;
  - a window destroyed while presenting doesn't leave the boost behind;
  - Esc in a field is left to the field, Esc elsewhere leaves.
- **`tabtags` (23):** store, clear, trim and cap labels; 3:1 contrast and
  eight distinct colours in all seven themes.
- **`tabpopout` (9):**
  - a tool window, sharing the tab's model;
  - row density followed;
  - the label and colour reach the title and icon at once;
  - Show in main window, Follow latest, and closed-by-user.
- **`menuaudit`, through the real main window:**
  - a pop-out open at the last shutdown comes back on start;
  - the tag dot and "L1_V1 · Brake test" are on the tab, and the pop-out
    carries the same name and label;
  - a pop-out you close is forgotten;
  - Detach opens one copy, not two, and it is remembered;
  - Text size Larger/Smaller/Reset shortcuts, and F11, are application-wide.
- **Checked by eye:** the real main window with two tagged tabs at 100 %
  and 125 %, and a tagged pop-out.

## Found along the way

- The pop-out took its title from the dispatcher (the key "21_1") while the
  tab showed the workspace name "L1_V1". It now uses the tab's name.
- A widget's `destroyed()` fires before pointers to it are cleared, so
  presentation mode matches a destroyed window by address.

## verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
131 suites / 3882 checks (+ `textzoom`, `presentationmode`, `tabtags`,
`tabpopout`), menu audit 91 ok (+14, through the real main window),
headless smoke alive. Built with Qt 5.15 only.
