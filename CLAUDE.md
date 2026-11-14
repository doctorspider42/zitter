# Zitter — notes for Claude

## Documentation language

- Write all new documentation in English, including READMEs, guides, design notes,
  plans, reports and agent instructions.
- When updating existing documentation, write added or rewritten content in
  English. Translate the surrounding section when needed to keep it coherent.
- Preserve original quotations, proper names and exact UI labels when another
  language is required for accuracy. This rule does not change the user's
  preferred language for chat replies or the app's localization.

## UI tests

- Run the **full** browser UI suite (`out/ZitterOptimized/zitter_prototype` without a narrow
  `--gtest_filter`) only after the user explicitly agrees, each time. It shows and
  focuses a browser window over and over for ~12 minutes, so the Mac is unusable
  meanwhile. Say how long it takes when you ask.
- Without asking: Rust tests (`out/ZitterOptimized/sidebar_scene_unittests`),
  Python tests (`python3 -m unittest discover -s tests -p 'test_*.py'`) and single
  browser tests named with `--gtest_filter` when a change needs them.

## Checking UI changes

Finish every UI change with the `verify-ui` skill (`.claude/skills/verify-ui/`): run
the packaged app, screenshot the changed view, compare it with its design, fix what
differs, and only then hand over the final screenshot.

## Shared build directory

Several sessions stage into `.chromium/src/zitter` (through `./build.sh`) and build in
`.chromium/src/out/ZitterOptimized`. Before staging or running ninja there, make sure
no other session is building, and agree on the window with the sessions that use it.
Never stage into `.chromium/src/zitter_ui_lab`: it is a frozen benchmark reference.

## Build directories and disk space

- Build in the existing output directories: `out/ZitterOptimized` on macOS;
  `out/ZitterWin`, `out/ZitterWinDev`, `out/ZitterDevInstaller` (zitter-dev-install
  skill) and `out/ZitterWinOfficial` on Windows.
- Create a new `.chromium/src/out/*` directory only after the user explicitly
  agrees, asked each time. A Chromium output directory takes 10–40 GB and hours
  to build and to delete. When asking, give the expected size and build time.
  Delete it when the task ends unless the user wants to keep it.
- Keep `build/` lean: delete test profiles, Dev packages, logs and scratch
  scripts when the task that made them ends. Published release files live on
  GitHub; don't keep local copies of older versions.
- On Windows, delete large trees with `cmd /c rmdir /s /q <dir>`, which removes
  junctions without entering them. Never delete with `robocopy /MIR`: it follows
  junctions, and a junction to `.chromium` in an old worktree once made it wipe
  the Chromium checkout. Before deleting a worktree or anything outside `out/`,
  list its reparse points (`Get-ChildItem -Recurse -Attributes ReparsePoint`).
  Bash `rm -rf` takes hours on Chromium output directories.

## Windows

- The port's decisions, build steps and status live in `docs/windows-port.md`.
- Build env: `DEPOT_TOOLS_WIN_TOOLCHAIN=0`, `vs2026_install` when VS 2026 sits under
  Program Files (x86), depot_tools on PATH; build with `autoninja`. Build dirs:
  `out/ZitterWin` (component, development), `out/ZitterWinRelease` (static),
  `out/ZitterWinOfficial` (ThinLTO + PGO).
- `verify-ui` on Windows uses `scripts/ui_win.py` (`--scale`, `--theme`). Synthetic
  clicks move the user's real mouse; ask before taking over the screen.
- Never click UAC prompts or install anything elevated yourself: the user does.
- Write files with Python or the Write tool, not PowerShell `Set-Content`, which
  mangles Polish text.

## Local releases

Use `.claude/skills/zitter-local-release/SKILL.md` for local release builds and
GitHub publication. The shared workflow is in `docs/local-releases.md`.
