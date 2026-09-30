# Session 74 — More themes, and named minimised windows

## 1. Five new themes, every one held to the contrast floors

**View ▸ Theme** lists them all. The same list is in Settings. The choice
is saved as before.

| Theme | Kind | Body text | What it's for |
|---|---|---|---|
| Light (Ayu) | light | 8.1 : 1 | unchanged |
| **Sepia** | light | 11.5 : 1 | warm paper and brown ink; calm for long reading |
| **Sage** | light | 11.3 : 1 | soft green-grey; restful, no pure white |
| **High contrast** | light | 21 : 1 | black on white, **7 : 1 for every colour**; bright cabs and sunlight |
| Dark (Ayu) | dark | 10 : 1 | unchanged |
| **Nord** | dark | 10.4 : 1 | cool arctic blue-grey |
| **Mocha** | dark | 12.1 : 1 | Catppuccin Mocha: warm dark, pastel accents |

**How contrast is guaranteed, not eyeballed:**
- Each new theme has its own hand-tuned ok / warning / error / muted /
  accent colours (`uicolors.cpp`). Borrowing Ayu's would have failed on a
  sepia background.
- `tests/test_contrastaudit.cpp`, `test_uicolors.cpp` and the flasher's
  badge check now loop over **every** theme (`ThemeUtil::all()`), not just
  Light and Dark. For each theme they check:
  - every text-on-surface palette pair (4.5 : 1);
  - disabled and placeholder text (3 : 1);
  - every semantic colour on the window, base, alternate row and button
    (4.5 : 1);
  - body text ≥ 7 : 1, the row stripe, and the frame greys.
  - **High contrast is held to 7 : 1 (AAA)** for text and every semantic
    colour.
- The palettes were designed with a script running the same checks before
  any code was written. Nord's published red (#BF616A, 3.3 : 1 on a button)
  and one muted grey (4.46 : 1) were lifted until they passed.
- The contrast audit went from ~80 checks to 263, all passing.
- Plot series colours keep their hues but are moved lighter or darker until
  they clear 3 : 1 on the current background
  (`UiColor::withContrast`). The two Ayu themes were already above that, so
  they are unchanged.

**Also:**
- **Toggle Dark/Light** (now in View ▸ Theme) switches between your *last*
  light and last dark theme, so Sepia ⇄ Nord works; the first time it's
  Ayu Light ⇄ Ayu Dark.
- The selected tab in Sepia, Sage and High contrast is a tint of the
  theme's own colours. The Ayu formula gave Sepia a clashing cyan. Ayu Light
  is unchanged.
- Code that needs "is this dark" (the log colour rules' light/dark
  variants, the find tint) now asks `ThemeUtil::isDark()` instead of
  `== Theme::Dark`.
- An unknown theme key in `dlconsole.ini` falls back to Light.
- **Checked by eye:** the Live Loco Console rendered in all seven themes
  with real `@linfo` data and an active find.

## 2. Minimised windows now have a name

**Cause:** every tool window (Live Loco Console, Firmware Flasher, Loco
Configuration, Decode Workbench, …) is owned by the main window. On Windows
an owned window gets no taskbar button, so minimised it is parked as a
tiny title-less caption at the bottom-left of the desktop.

**Fix:** new `taskbarwindows.{h,cpp}`, installed once in `main.cpp`.
- An app-wide filter sets `WS_EX_APPWINDOW` on each tool window as it is
  shown. Each gets **its own taskbar button**, with its title and the
  DLConsole icon, and minimises to the taskbar.
- They stay owned by the main window: they still close with it and stay
  above it.
- Only real windows with a parent qualify. Dialogs, message boxes, menus
  and tooltips are untouched, and so is the main window.
- It is one filter rather than code in each window, so future tool windows
  are covered automatically.
- `win32: LIBS += -luser32` added to the three `.pro` files.

**Could not be run here (Linux):**
- The Windows branch was **compiled and linked with MinGW** against the
  real Windows headers and user32, `-Wall -Wextra` clean.
- The decision logic ("which windows get a button") is unit-tested.
- On Windows, please check that minimising the Live Loco Console puts a
  button with its name on the taskbar.

## Files

- **New:** `taskbarwindows.{h,cpp}`, `tests/test_taskbarwindows.cpp`.
- **Changed:**
  - `theme.h` (five themes, `ThemeUtil::all/label/isDark`, palettes);
  - `uicolors.{h,cpp}` (per-theme semantic sets, `setActiveTheme`,
    `activeTheme`, `withContrast`, series contrast);
  - `uistyle.cpp` (selected-tab tint for the new light themes);
  - `logmodel.cpp` (`isDark`);
  - `mainwindow.{h,cpp}` (View ▸ Theme menu, toggle remembers the last of
    each kind);
  - `settingsdialog.cpp` (all themes);
  - `main.cpp`;
  - `tests/test_contrastaudit.cpp`, `test_uicolors.cpp`, `test_flasher.cpp`,
    `menuaudit_main.cpp`, `screenshot_main.cpp`;
  - the three `.pro` files.

## verify.sh

Full run on the final tree, **14/14 stages green, 0 failed**:
validators 11/11, unit suite 127 suites / 3799 checks (+ `taskbarwindows`,
and every colour suite now covering all seven themes), menu audit 78 ok
(View ▸ Theme lists every theme, exactly one ticked, toggle present; the
Live Loco Console gets a taskbar button), headless smoke alive. Built with
Qt 5.15 only; the Windows-only branch was compile-and-link checked with
MinGW.
