---
name: drive-zitter
description: Drive the running Zitter app the way a person would (click, hover, drag, scroll, press keys, type) and look at the result through screenshots and animation contact sheets, without touching the person's mouse or keyboard focus. Use it whenever you need to exercise or reproduce something in Zitter's UI, check how a view looks after an interaction, test a flow end to end, inspect an animation or transition, or confirm a fix in the real app. Also use it when the user says "kliknij", "przetestuj", "sprawdź czy działa", "zobacz animację", "przejdź przez flow", "odtwórz buga" or asks how something behaves in the app. On macOS and Windows this works with the app in the background and leaves the system cursor in place.
---

# Drive Zitter

The app has an automation channel for agents. `ui.py launch` starts it with
`--zitter-automation`, and every `ui.py` command then goes through
`<workspace root>/automation.sock` straight into the app's widgets and web pages. On Windows this file names a local pipe restricted to the current user:

- **The person's mouse never moves** and their app keeps the keyboard. A Zitter
  being driven doesn't bring itself to the front.
- **Screenshots come from the window's compositor**, web pages included. The
  window's child windows (the sidebar, panels) are drawn on top, so the image
  shows the app as a person sees it, even while other windows cover it.

So you can run as many interactions as you need without asking the user first.
That is unlike `--cursor`, `shot --screen` and the `verify_content_*` checks,
which take over the screen.

All commands: `python3 .claude/skills/verify-ui/scripts/ui.py <command>`, run from
the repository root (written `ui.py` below).

## 1. Have a current app running

```bash
python3 .claude/skills/verify-ui/scripts/ui.py launch            # keeps its workspace root
python3 .claude/skills/verify-ui/scripts/ui.py launch --fresh    # empty spaces, tabs, settings
```

- `launch` runs `build/ZitterContentLab.app` in `$TMPDIR/zitter-content-probe-ui`,
  never in the user's own profile.
- If you changed code, build first: use the `zitter-dev` build job if this session
  has it, else `.claude/skills/verify-ui/scripts/build_app.sh`. See CLAUDE.md on
  the shared build directory. An app built before the automation channel (before
  0.9.7) has no socket; rebuild it.
- `--open-panel <view>` opens a view at start without any clicks (`ui.py launch
  --help` lists them). `--theme dark|light` and `--scale 2` set the theme and
  display scale.
- `--front` lets the app come to the front. Use it only when the user wants to
  watch.

## 2. Find what to act on

```bash
ui.py windows                    # every window: id, title, visible, bounds
ui.py tree --window sidebar      # named views: x y width height (window points), role, name
ui.py tree --window main --all   # unnamed views too
```

Window names: `main` ("Zitter"), `sidebar` ("Zitter — Karty"), `panel` (space
settings). Any other window: part of its title (`--window Paleta`) or its id.

Prefer acting **by name**. Names are accessible names, so they follow the UI
language: Polish by default, for example "Adres strony", "Opcje karty",
"Przestrzeń: Work". Tabs are named by their page title ("New tab"). When `tree` shows no name for the target, use window
coordinates taken from `tree` bounds or from a `shot --scale 1` of that window
(image pixels = window points).

## 3. Act

```bash
ui.py click --name "Adres strony"                 # exact name (ignoring case), else a part of it
ui.py click --name "Opcje karty" --index 1        # several match: the error lists them, pick one
ui.py click 120 300 --window sidebar              # window points
ui.py click --name "New tab" --double
ui.py rightclick --name "New tab"                 # context menu: its own window, "menu"
ui.py tree --window menu; ui.py click --name "Wycisz kartę" --window menu
ui.py click --name "Pobrane" --window sidebar --hover 800   # hover first (tooltips)
ui.py move 40 80 --window sidebar                 # hover state only
ui.py drag --name "New tab" --to-name "Przestrzeń: Personal"
ui.py drag 40 80 --to 40 260 --window sidebar --steps 20
ui.py scroll 600 400 --window main --dy 600       # positive dy: towards the end of the content
ui.py type "example.com"                          # into the focused field (views or page)
ui.py key return
ui.py key t --mod cmd                             # shortcuts; also escape, tab, up, f5, ...
```

- Every pointer command prints what it hit: the class, the view name, and
  `"web": true` over a page. Check it and don't assume.
- Each command waits `--settle` seconds afterwards (default 0.3). Raise it after
  something slow, such as a page load (`key return --settle 3`).
- `type` needs a focused text field. Click the field first; a panel that opens
  with its field focused, such as the command palette, works too.
- `key` and `type` go to the window that would be key: the frontmost menu or panel,
  else the main window. Pass `--window` when that guess is wrong.
- Zitter's context menus (tabs, folders, spaces) are drawn by the app in an
  untitled window. `--window menu` finds the open one, and `tree`, `click`, `shot`
  and `key escape` work in it like anywhere else.

## 4. Look

```bash
ui.py shot build/screens/step.png                        # main window + sidebar, 1 px per point
ui.py shot build/screens/step.png --scale 0.5            # half size: enough to read most UI
ui.py shot build/screens/panel.png --window panel --scale 2   # Retina detail
ui.py shot build/screens/row.png --window sidebar --crop 0,40,248,60
```

Then read the PNG.

**Save context.** Most checks need only `--scale 0.5`. Use `--crop` (window points)
when only one region matters. Use `--scale 2` only for pixel-level detail: hairlines,
type rendering, 1 px misalignment. Keep files in `build/screens/` (ignored by git)
and delete them when the task ends.

### Animations and transitions

```bash
ui.py frames build/screens/anim.png --window main --then-name "Dodaj kartę" \
    --count 8 --interval 40 --crop 0,0,248,300
```

- `frames` runs the `--then-*` action, then takes `--count` frames `--interval` ms
  apart. They land on one contact sheet, each labeled `#n +ms`.
- Starting actions: `--then-click X Y` (with `--window`, window points),
  `--then-name N` or `--then-key K` (with `--mod`).
- Defaults: `--scale 0.5`, up to 4 columns (`--columns`). `--delay` waits before the
  first frame. `--separate` writes one file per frame.
- Frames follow the compositor, so 16 ms steps are the floor. Two frames with almost
  the same time can show the same image.
- Identical frames mean there was no animation (or it ended before the first frame);
  try `--interval 16`.

## Recipes

**Open a page and check it**
```bash
ui.py click --name "Adres strony"; ui.py type "example.com"; ui.py key return --settle 3
ui.py shot build/screens/page.png --scale 0.5
```

**Reproduce a bug step by step.** Act, then `shot`. Repeat with the smallest steps
that show the problem. Report the steps as `ui.py` commands, so the user or the
next agent can replay them.

**Before and after a fix.** Take the same `shot` (same window, scale and crop) on
the old and the new build. Compare them, and send both with `SendUserFile` when
the user should see the difference.

**Many steps from one script.** The socket takes one JSON object per line and
answers each with one line. See `automation()` in `ui.py`. Commands: `ping`,
`windows`, `tree`, `click`, `move`, `drag`, `scroll`, `key`, `type`, `shot`,
`frames`. Their arguments match the `ui.py` flags (`window`, `name`, `index`, `x`,
`y`, `button`, `count`, `modifiers`, `path`, `scale`, `crop`, `alone`, ...).

## Limits

- **The macOS menu bar and system windows** (file pickers, alerts, permission
  prompts) aren't part of Zitter's windows: `shot` doesn't show them and the socket
  can't click them. Ask the user before you fall back to `--cursor` and
  `shot --screen`, which use the real mouse and screen. A blocking system dialog
  can also hold commands until it closes.
- Drag and drop between apps (files from Finder) isn't possible.
- **Windows:** use `ui.py` for compositor captures and in-app input. Ask before using `--cursor` or `shot --screen`.
- A clicked control edits the test workspace (selected tab, emoji, a new space).
  `launch --fresh` resets it.

## Checking the channel itself

`python3 tools/verify_content_automation.py`, about 10 s. It uses its own probe
root and passes in the background. Run it after changing
`chromium/content_embedder/ui_automation_driver.inc` or `ui.py`. On Windows pass the built `zitter.exe` to the check.
