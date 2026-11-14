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

## Local releases

Use `.agents/skills/zitter-local-release/SKILL.md` for local release builds and
GitHub publication. The shared workflow is in `docs/local-releases.md`.

## Local Dev installation

Use `.agents/skills/zitter-dev-install/SKILL.md` for side-by-side Windows Dev
installs. After changing browser behavior, UI, or the installer, offer to build
and launch Zitter Dev for a hands-on check if it has not already been launched.
