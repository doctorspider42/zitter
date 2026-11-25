# Assistant

The assistant is a conversation surface, with secondary controls revealed on demand.
It intentionally uses an ambient spectral accent for page-control feedback, as a
focused exception to the browser chrome's generally flat color treatment.

## Layout

- Two presentations. **Ask bar**: up to 640 DIP wide, centred 20 DIP above the
  page's bottom, 10 DIP padding, the latest answer over a composer without the
  mode switch, `@` or model chip; an expand action docks it. **Panel**: 420 DIP,
  20 DIP outer radius, 16 DIP inner padding, 10 DIP stack gap, pinned by default.
- Header: one 30 DIP row. The conversation title (13 DIP semibold, a chevron, a
  search glyph for research conversations) opens the history; 28 DIP icon actions
  follow: access, new conversation, more, close.
- Empty conversation: one 32 DIP italic display line, then one-line 32 DIP
  suggestion pills (hairline `line`, glyph in `ink-muted`, accent on hover), then
  an eyebrow **Recent** with up to three history rows. No orb, no descriptions.
- Conversation: user messages are right-aligned bubbles (accent tint, 14 DIP
  radius with a 4 DIP bottom-right corner, at most 84% wide, chips above the
  text). Answers sit directly on the panel: 13 DIP Markdown, then a copy action
  and the model in muted text. A finished turn's steps collapse into a disclosure
  row; the running turn shows each step.
- Research plan card (`bg-base` well, 14 DIP radius, 14 DIP padding): a title
  line with the state, numbered angle rows (removable while waiting), outlined
  22 DIP fact tags (sources, parallel calls, no tabs or history), then **Start**
  (primary, `⌘↵` key cap) and **Cancel**.
- Research progress card: the loader dots and phase title with the source count,
  three 4 DIP phase bars (plan, sources, report) with labels, one status row per
  angle (hollow dot pending, spinner running, check done, warning failed), a
  tinted “Now: host/path” note and a danger **Stop** pill.
- Consent in the conversation: a warn-outlined card with the element preview,
  the question and **Allow** / **Skip** pills.
- Composer: 16 DIP radius on `bg-base`, `line` hairline (`line-strong` when
  focused), unframed 14 DIP editor, a wrapping row of 24 DIP chips above it, then
  the tool row: a 30 DIP Chat/Research switch, `@`, Background, a gap, the model
  chip and the 36 DIP send/stop action. Research mode tints the well and draws a
  1.5 DIP accent ring; its segment fills with the accent.
- Chips: 6 DIP radius, a 16 DIP letter tile in the page's color (tools: an accent
  glyph), 12 DIP medium text. Pages have an accent tint, tools an outline. An 18 DIP
  × button removes the chip and is named “Remove <title>”.
- Popovers share one surface: 12 DIP radius, raised fill, 1 DIP line, 6 DIP inner
  padding, uppercase 10 DIP group labels. Rows are 32 DIP (one line) or 42 DIP (two
  lines, mono detail for addresses and model IDs), with an 8 DIP radius hover or
  active fill, a trailing check for the current choice and badges in a 1 DIP outline.
  - `@` suggestions: as wide as the composer, 8 DIP above it. A header with the typed
    query and the result count; groups for open tabs, pinned pages and tools; the
    matched part of the title in accent; the active row shows an ↵ key cap; a footer
    explains what the highlighted item shares, with ↑↓ Tab Esc key caps.
  - Model menu: 300 DIP, right-aligned 8 DIP above the composer; a search field,
    an accent note when the session is fixed to its model, groups per backend, the
    default model's badge and a link to Settings.
  - History, more and access menus: right-aligned 6 DIP below the header. More
    holds pinning, scheduled tasks, page actions and the defaults for new
    conversations. Access rows end in a 32×18 DIP switch (accent when on, outlined
    when off); a switch that would widen a started session is disabled. A
    danger-tinted **Revoke all access** pill closes the menu.
  - Every menu closes on a press outside it, except on its own toggle.
- Background section, between the header and the conversation: a group label with
  the task count, 46 DIP task rows (accent tint, activity dots while running, take
  over and stop/close actions) and consent cards (the element's label on a light
  page strip with an accent ring, then Allow, Skip and Take over the tab pills).
- Toolbar button: an accent dot while the assistant works, a badge for unseen
  results or `!` for a waiting consent, the status as its tooltip.
- Page control overlays (browser chrome over the page): a 40 DIP status capsule
  with activity dots, status text, an Esc key cap and **Finish in the background**;
  a 88 DIP consent bubble beside the clicked element (above it when there is room)
  with Allow (↵), Skip (Esc) and **Until the task ends**.
- The left edge has an 8 DIP drag target with a subtle 40 DIP grip; preferred
  width ranges from 360 to 760 DIP and persists across hiding/pinning for the
  current window.

## Color and motion

The panel uses the design system's tokens: `bg-raised` for the panel, `bg-base`
for wells (composer, cards), `ink`, `ink-muted`, `line`, `line-strong`, the
violet accent (`ket`) and `ok`/`warn`/`danger` for states, in both themes.

Page control uses a rotating violet–ice–mint sweep, with 38/20/8/1.5 DIP nested
edge strokes at 10/17/35/95 percent opacity. The rotation takes 5.4 seconds at
30 frames per second while active. The page center remains untouched. A small
bottom status capsule carries three activity dots and an Escape key hint.
Reduced-motion mode uses a static frame and no animation timer.

The assistant does not animate or poll while idle.
The effect lives in browser chrome, accepts no pointer input, and ends when the
task completes, fails, stops or changes profile.

## Visual verification

Use `--zitter-open-panel=assistant-activity` with an owned test profile. The generic
UI launcher seeds `workspace_animations: false` for stable stills; enable it in
that isolated profile when recording the animated effect. Check both light and
dark schemes, and keep the user's ordinary browser profile untouched.
For repeatable menu captures, run `AssistantMenusAnchorAndAccessCanBeRevoked`
with `--zitter-test-assistant-render-dir=<absolute output directory>`. The native
browser test writes the model, sessions, access and `@` menus in both themes
through the actual Views renderer.
