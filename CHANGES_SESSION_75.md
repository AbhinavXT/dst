# Session 75 — Named chips for minimised windows; a clear flash-queue selector

## 1. Minimised tool windows: named chips in DLConsole's bottom bar

**What was wrong:** minimised tool windows (Live Loco Console, Firmware
Flasher, …) appeared as small icon-only stubs at the bottom-left of the
window, with no way to tell them apart. Windows draws those stubs for
windows owned by the main window, and DLConsole cannot put text in them.

**Session 74 fixed the wrong thing.** It gave each window a taskbar button;
what was wanted was named chips in DLConsole itself. That change is
**removed** (`taskbarwindows.*` deleted, the `user32` link lines removed).

**Now:** new `minimizeddock.{h,cpp}`, shown at the far left of the main
window's status bar.
- When a tool window is minimised, DLConsole hides it (so Windows draws no
  stub) and adds a **chip with the window's name**.
- **Click the chip** to bring the window back, maximised if it was
  maximised. **✕** closes the window.
- A window that refuses to close (the Flasher mid-flash asks first) keeps
  its chip.
- The chip follows a title change. It disappears if the window is reopened
  another way (e.g. from its menu item) or is destroyed.
- The dock takes no space until something is minimised.
- It is one app-wide filter, so every tool window is covered, including
  future ones. The main window, dialogs, message boxes and menus are
  untouched.

Found while testing, and handled: Qt sends stray Show/Hide events to a
*hidden* window whose state is changed. The first version read the stray
Show as "reopened" and dropped the chip. The dock now leaves a docked
window's state alone until restore, and only treats a Show as real when
the window is actually visible.

## 2. Flash queue: the selected card is obvious

**What was wrong:** every unselected card showed a solid black dot, so the
selected VCC looked no different from the rest.

**Cause:** the queue table has its own style sheet (`background:
transparent`), which made the delegate's palette background transparent.
The style's radio, and then my first redraw too, painted with that
"colour", which comes out black.

**Now the selector is drawn directly**, using the theme's real background
from the application palette:
- **Selected card:** a filled **green** circle with a **✓**, inside a light
  ring so it stands out even on the blue selected-row highlight.
- **Other cards:** an empty circle with a grey outline.
- The green is new `UiColor::selectedMark()`. It starts from a vivid green,
  because `ok()` is darker, tuned for green *text*. `withContrast()`
  adjusts it to at least 3:1 (WCAG's floor for graphical objects) on every
  theme's background, and `test_uicolors` checks that on all seven themes.

## Tests

- **New `minimizeddock` (18 checks):**
  - minimise gives a chip with the window's name and hides the window;
  - the chip follows a title change; clicking restores it;
  - a maximised window comes back maximised;
  - two windows give two chips, told apart;
  - a window reopened another way loses its chip;
  - ✕ closes the window, and a window that refuses keeps its chip;
  - dialogs and the main window are not docked.
- **`flasher`:** renders the real queue page and counts green pixels in
  each selector. The selected card must have them and every other card
  none. This would have caught the black dots.
- **`uicolors`:** the selected mark is ≥ 3:1 on the background in every
  theme.
- **Checked by eye:** the queue in Light and Nord with VCC selected and
  highlighted, as in the bug screenshot; and the main window with two
  minimised tool windows showing as "Live Loco Console" and "Firmware
  Flasher" chips.

## verify.sh

Full run, **14/14 stages green, 0 failed**: validators 11/11, unit suite
127 suites / 3817 checks, menu audit 77 ok (one fewer than session 74: the
taskbar-button check went with the taskbar change), headless smoke alive.
Built with Qt 5.15 only.
