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

## Local releases

Use `.agents/skills/zitter-local-release/SKILL.md` for local release builds and
GitHub publication. The shared workflow is in `docs/local-releases.md`.

## Local Dev installation

Use `.agents/skills/zitter-dev-install/SKILL.md` for side-by-side Windows Dev
installs. After changing browser behavior, UI, or the installer, offer to build
and launch Zitter Dev for a hands-on check if it has not already been launched.
