# Zitter — agent instructions

## Documentation language

- Write all new documentation in English, including READMEs, guides, design notes,
  plans, reports and agent instructions.
- When updating existing documentation, write added or rewritten content in
  English. Translate the surrounding section when needed to keep it coherent.
- Preserve original quotations, proper names and exact UI labels when another
  language is required for accuracy. This rule does not change the user's
  preferred language for chat replies or the app's localization.

## UI design

Keep the UI elegant, modern, and consistent with `docs/design-system`. Use compact
layouts and a clear hierarchy of actions instead of heavy admin-style forms.
Review UI changes in light and dark themes across the affected renderers
(compositor and Metal).

## Checking the running app

To exercise or look at Zitter's UI (clicks, typing, scrolling, screenshots,
animation frames), follow `.agents/skills/drive-zitter/SKILL.md`. On macOS it
drives the app through its automation socket: the user's mouse never moves and
the app stays in the background, so it needs no permission to take over the
screen.

## Build directories and disk space

- Zitter builds only on the Content embedder (`//zitter_content:zitter_content`).
  Build in the existing output directories. macOS: `./build.sh` uses
  `out/ZitterContentBundle` (`--build-only`: `out/ZitterContentDev`);
  `ZITTER_CONTENT_OUT=out/<dir>` picks another existing one. Windows:
  `out/ZitterWinContent`.
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

## Local releases

Use `.agents/skills/zitter-local-release/SKILL.md` for local release builds and
GitHub publication. The shared workflow is in `docs/local-releases.md`.
