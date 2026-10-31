---
name: verify-ui
description: Finish every Zitter UI change by running the packaged app, screenshotting the changed view, comparing it with its design, and fixing the differences straight away before handing over. Use this whenever you change anything the user sees in Zitter (chromium/prototype — workspace_shell.cc, sidebar_scene.rs, design.rs, sidebar_metal.mm, panels, sidebar, colors, type, spacing), when you implement or polish a design or design-system component, and whenever the user says "włącz", "odpal", "pokaż", "zrób screena", "sprawdź jak wygląda" or asks whether the app matches the design — even if they don't mention screenshots.
---

# Verify UI against the design

Tests prove behaviour, not looks. A panel can pass every test and still be
"rozklekotany": wrong spacing, a truncated label, a field taller than its neighbour, an
old style left behind. The user wants to see the real app, and to see it already
matching the design, so their remarks go to taste rather than to things you could have
caught yourself. So every UI task ends in the running app, with a screenshot, checked
against the design, and fixed until it matches.

## The loop

1. **Build and package.** Make sure no other session is building in the shared
   directory (`pgrep -fl "ninja|build.sh"`; see CLAUDE.md), then:
   ```bash
   zsh .claude/skills/verify-ui/scripts/build_app.sh
   ```
   It stages this checkout into `.chromium/src/zitter_ui_lab`, builds `zitter_ui_lab`,
   `sidebar_scene_unittests` and `zitter_prototype_ui_lab` with Chromium's ninja, and
   packages `build/ZitterRustMetal.app`. Signing needs `ZITTER_CODESIGN_IDENTITY` (from
   `.zitter-signing.json` in the main checkout) or the package step fails. Only the
   packaged app has the design system's fonts; test binaries fall back to system fonts.

2. **Run the tests that cover the change**: Rust tests, plus browser tests named with
   `--gtest_filter`. Count them first with `--gtest_list_tests`. A broad pattern like
   `*Profile*` matches ~195 tests and takes over the screen. Never run the full UI suite
   without the user's explicit OK.

3. **Launch and open the view.**
   ```bash
   python3 .claude/skills/verify-ui/scripts/ui.py launch --open-panel space
   python3 .claude/skills/verify-ui/scripts/ui.py shot build/screens/space-panel.png --window panel
   ```
   `launch` quits the running copy and starts the app with its isolated profile
   (`build/rust-metal-profile`). Prefer launch switches over clicking:
   `--zitter-open-panel=space` opens the space panel by itself.
   - For another view, add a value to that switch next to the `space` case in
     `workspace_shell.cc`, and to `--open-panel` in `tools/run_metal_sidebar.py` and
     `ui.py`.
   - Synthetic clicks (`ui.py click X Y`) move the user's real mouse. They are dropped
     while the app is not frontmost, so use them only for what a switch can't reach.
     Keep in mind a stray click edits the test profile (an emoji, a selected tab).
   - `shot` captures only the app's own window (`screencapture -l`), or for a child
     panel just its area of the screen; it never captures the whole screen. Keep screenshots in `build/screens/`, which git ignores.

4. **Compare with the design. Read it, don't eyeball it.** Find the design first:
   - the artifact the user approved (for the space panel:
     https://claude.ai/artifact/FrS3Q8MLtrxVtwmTWhP23j);
   - its local canvas files, if this session has them;
   - for components and tokens: `docs/design-system/` (README, `tokens.json`,
     `components/`) and the design-system artifact
     https://claude.ai/artifact/M3gpYMX9zs5cpT9Cf33yHx.

   Pull the exact numbers out of the design's source: sizes, paddings, gaps, radii,
   font sizes, text styles and colors. Then go through the screenshot region by region:
   - **Structure:** sections, their order and their labels (eyebrow caps, titles).
   - **Type:** family, size, weight, italic, case.
   - **Geometry:** element sizes, gaps between them, alignment, and equal heights for
     neighbours in one row.
   - **Color:** roles from the palette, for example `accent_soft` tiles, `danger` for
     destructive actions, `line` edges.
   - **Text:** labels truncated with "…", or text overlapping other content.
   - **Fit:** whether the view fits in its window, and what scrolls.
   - **States:** the selected and empty states (a space without an emoji, no
     selection).

   Write the differences down as a short list.

5. **Fix every difference you found, right away.** Don't hand the user a list of what
   you saw and ask whether to fix it. Make the change, rebuild, re-run the affected
   tests (update tests that pin old geometry), relaunch and take a new screenshot. Repeat
   until nothing you can see differs from the design. If something can't match, say why
   in the summary. Examples: a platform limit, such as native menus or shadows the
   scene can't draw; or a design decision only the user can make.

6. **Hand over.** Send the final screenshot with `SendUserFile` and summarize briefly:
   - what you fixed;
   - what still differs and why;
   - what isn't verified.

   Leave the app running so the user can look at it themselves. Then wait for their
   remarks and treat them as the next round of this same loop.

## Windows

The loop is the same; the tools differ. `ui.py` runs `ui_win.py` on Windows.

1. **Build:** `powershell -File .claude/skills/verify-ui/scripts/build_app_win.ps1`
   installs the overlay and builds `zitter.exe` and its tests in
   `.chromium/src/out/ZitterWin`. There is no package: the build directory, with the
   design fonts in its `Fonts\`, is the app.
2. **Tests:** `out\ZitterWin\sidebar_scene_unittests.exe`,
   `editor_text_layout_unittests.exe`, `arc_importer_unittests.exe`; browser tests
   run with `zitter_prototype.exe --gtest_filter=…`.
3. **Launch and shoot:**
   ```bash
   python .claude/skills/verify-ui/scripts/ui.py launch --open-panel app-menu
   python .claude/skills/verify-ui/scripts/ui.py shot build/screens/app-menu.png --as-is
   ```
   `launch` starts `zitter.exe` with the seeded profile `build/win-ui-profile`. The
   sidebar, palette and panels are translucent windows that PrintWindow can't
   capture, so `shot` brings Zitter forward and copies that window's area of the
   screen. Ask the user before taking over their screen, and don't capture while
   another app is in front. `--as-is` skips the Alt press that brings the window
   forward, which also closes open menus. Coordinates are physical pixels.

## Notes

- A sleeping display makes every capture fail ("could not create image"). Check
  `system_profiler SPDisplaysDataType | grep -i asleep`; `caffeinate -u -t 15 &`
  wakes it for the shot.

- Commit as part of the task when the user asked for the change; the screenshot stays
  out of git.
- `ui.py windows` lists the app's windows with their ids and bounds in macOS points. A
  Retina capture is 2× those numbers. Windows are found by title ("Zitter",
  "Zitter — Karty", "Zitter — Ustawienia przestrzeni"); their sizes follow the main window.
- Screen-reading is cheap; reading the design's source is what makes the comparison
  exact. When the design and the design-system tokens disagree, the approved design for
  that view wins; mention it so the tokens can be updated.
