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
   It runs `./build.sh`: it stages the Content embedder into `.chromium/src`
   (`tools/stage_content_embedder.py`), builds `//zitter_content:zitter_content` in
   `out/ZitterContentBundle`, runs the license audit and packages
   `build/ZitterContentLab.app` with a local signature. Then it builds
   `sidebar_scene_unittests` in the same output directory. Only the packaged app has
   the design system's fonts; test binaries fall back to system fonts.

2. **Run the tests that cover the change**: the Rust tests
   (`out/ZitterContentBundle/sidebar_scene_unittests`) and the `tools/verify_content_*.py`
   checks for the area you changed (for example `verify_content_workspace_session.py`
   for the workspace UI). The `verify_content_*` checks open app windows; run only the
   ones the change needs, and ask the user before running many of them.

3. **Launch and open the view.**
   ```bash
   python3 .claude/skills/verify-ui/scripts/ui.py launch --open-panel space
   python3 .claude/skills/verify-ui/scripts/ui.py shot build/screens/space-panel.png --window panel
   ```
   `launch` quits the running copy and starts the app with
   `--zitter-content-workspace` in its own workspace root,
   `$TMPDIR/zitter-content-probe-ui` (the app accepts a root only as a private
   `zitter-content-probe-*` directory in `$TMPDIR`). The root keeps spaces, tabs and
   settings between launches; `--fresh` starts from an empty one. `--theme dark|light`,
   `--raster metal` and `--scale 2` pass the matching app switches. Prefer launch
   switches over clicking: `--open-panel space` passes `--zitter-open-panel=space`,
   which opens the space panel by itself.
   - For another view, add a value to that switch next to the `space` case in
     `workspace_shell.cc`, and to `--open-panel` in `ui.py` and `ui_win.py`.
   - The user's own Zitter has the same bundle name ("Zitter Content Lab"). `ui.py`
     therefore finds windows by the process that owns the test workspace root, never
     by name, so it leaves the user's browser alone.
   - `launch` also passes `--zitter-automation`. The app then takes commands on
     `<workspace root>/automation.sock` and delivers them straight to its own widgets
     and pages. The user's cursor never moves, the app may stay in the background, and
     other windows may cover it. See "Driving the app" below.
   - `shot` copies the window's own composited frame, web pages included. It never
     captures the screen, so it needs no Screen Recording permission and works on a
     covered window. Keep screenshots in `build/screens/`, which git ignores.

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

## Driving the app

Prefer launch switches (`--open-panel`) where one exists. For anything else, drive
the app through `ui.py`. On macOS every command goes through the automation socket:

```bash
python3 .claude/skills/verify-ui/scripts/ui.py tree --window sidebar      # named views, window points
python3 .claude/skills/verify-ui/scripts/ui.py click --name "Nowa karta"   # by accessible name
python3 .claude/skills/verify-ui/scripts/ui.py click 120 300 --window sidebar
python3 .claude/skills/verify-ui/scripts/ui.py rightclick --name "Projekty" --window sidebar
python3 .claude/skills/verify-ui/scripts/ui.py move 40 80 --window sidebar  # hover states
python3 .claude/skills/verify-ui/scripts/ui.py drag 40 80 --to 40 200 --window sidebar
python3 .claude/skills/verify-ui/scripts/ui.py scroll 400 300 --dy 240     # screen points
python3 .claude/skills/verify-ui/scripts/ui.py key escape; ui.py key t --mod cmd
python3 .claude/skills/verify-ui/scripts/ui.py type "zitter.dev"           # into the focused field
python3 .claude/skills/verify-ui/scripts/ui.py shot build/screens/x.png --window panel --crop 0,0,360,200
python3 .claude/skills/verify-ui/scripts/ui.py frames build/screens/anim.png --window sidebar \
    --then-name "Nowa karta" --count 8 --interval 40 --crop 0,0,280,400
```

- **Coordinates.** With `--window`, X Y are window points: the coordinates of a
  `shot --scale 1` image of that window. Without it, X Y are screen points. `tree`
  lists bounds in window points, so `--name` is usually simpler and more robust.
- **Clicks by name** match the accessible name exactly, ignoring case, then as a
  part of the name. When several views match, the error lists them; add `--index N`.
- **Pages.** Clicks, drags and wheel events over a web view go to the page.
  `type` reaches the focused page field too.
- **Saving context.** `shot --scale 0.5` and `--crop x,y,w,h` keep images small.
  `frames` takes `--count` frames `--interval` ms apart into one contact sheet, at
  `--scale 0.5` by default. Each frame is labeled with its time.
  `--then-click X Y`, `--then-name N` or `--then-key K` starts the animation first.
  `--separate` writes one file per frame instead.
- **Timing.** Pointer commands answer at once and dispatch right after, then
  `ui.py` waits `--settle` (0.3 s) so the next shot shows the result.
- **Limits.** Native macOS menus (`NSMenu`) and system dialogs are not part of a
  window's frame. A `shot` misses them, so use `shot --screen` with the app in front.
  The sidebar drawn with `--raster metal` is a separate Metal layer, so the compositor
  shot misses it too. `--cursor` (click) and `--screen` (shot) fall back to the real
  mouse and `screencapture`; those take over the user's screen, so ask before using
  them. On Windows, `ui_win.py` still drives the real mouse.
- **Check:** `python3 tools/verify_content_automation.py` (about 30 s, its own
  probe root). It verifies that clicks, screenshots and frames work and that the
  cursor stays put.

## Windows

The loop is the same; the tools differ. `ui.py` runs `ui_win.py` on Windows.

1. **Build:** `powershell -File .claude/skills/verify-ui/scripts/build_app_win.ps1`
   stages the Content embedder and builds `zitter_content` (`zitter.exe` and
   `zitter_setup.exe`) and the UI tests in `.chromium/src/out/ZitterWinContent`. There
   is no package: the build directory, with the design fonts in its `Fonts\`, is the
   app.
2. **Tests:** `out\ZitterWinContent\sidebar_scene_unittests.exe` and
   `editor_text_layout_unittests.exe`; the Windows Content checks are
   `tools/verify_content_windows_bundle.py` and `verify_content_windows_migration.py`.
3. **Launch and shoot:**
   ```bash
   python .claude/skills/verify-ui/scripts/ui.py launch --open-panel app-menu
   python .claude/skills/verify-ui/scripts/ui.py shot build/screens/app-menu.png --as-is
   ```
   `launch` starts `zitter.exe --zitter-content-workspace` in the workspace root
   `%TEMP%\zitter-content-probe-ui` (`--fresh` empties it first).
   `ZITTER_UI_OUT=out/<dir>` picks another build directory than
   `out/ZitterWinContent`; `--open-panel assistant-settings` opens Settings → AI
   Assistant. The Windows Content app refuses `--zitter-rust-raster=metal`. The
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
