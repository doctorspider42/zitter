# Assistant

The assistant is a conversation surface, with secondary controls revealed on demand.
It intentionally uses an ambient spectral accent for page-control feedback, as a
focused exception to the browser chrome's generally flat color treatment.

## Layout

- Right panel: 420 DIP, 20 DIP outer radius, 16 DIP inner padding, 10 DIP stack gap.
- Header: compact 15 DIP sans identity over a 12 DIP muted session title; 28 DIP
  icon actions with accessible labels: sessions, access (accent when anything is
  shared), new session, pin, close.
- Welcome: a static 66 DIP optical orb within a 210 DIP hero, one 27 DIP italic
  serif heading and a 12 DIP supporting line. Three 54 DIP suggestion rows fill
  a draft (and insert the current tab or Deep research as a chip) without sending
  it or granting browser access.
- Conversation: separate user and assistant messages; 12 DIP bubble padding,
  13 DIP body text, 12 DIP role labels. User messages repeat their chips. Browser
  activity uses quiet status text.
- Composer: 18 DIP radius, unframed 14 DIP editor, a wrapping row of 24 DIP chips
  above it, then the tool row with 28 DIP icon actions for `@`, Deep research and
  Background, the model chip with a chevron and the 36 DIP primary send/stop action.
  The Background action keeps its width during a run and describes its current
  action in its accessible name and tooltip.
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
  - Sessions and access menus: 320 DIP, right-aligned 6 DIP below the header. Access
    rows end in a 32×18 DIP switch (accent when on, outlined when off); a switch that
    would widen a started session is disabled. A danger-tinted **Revoke all access**
    pill closes the menu.
  - Every menu closes on a press outside it, except on its own toggle.
- Background section, between the header and the conversation: a group label with
  the task count, 46 DIP task rows (accent tint, activity dots while running, take
  over and stop/close actions) and consent cards (the element's label on a light
  page strip with an accent ring, then Allow, Skip and Take over the tab pills).
- Page control overlays (browser chrome over the page): a 40 DIP status capsule
  with activity dots, status text, an Esc key cap and **Finish in the background**;
  a 88 DIP consent bubble beside the clicked element (above it when there is room)
  with Allow (↵), Skip (Esc) and **Until the task ends**.
- Unpinned panels auto-hide after a 650 ms pointer-exit delay. Open menus,
  focused drafts, resizing and running tasks keep the panel visible. The left
  edge has an 8 DIP drag target with a subtle 40 DIP grip; preferred width ranges
  from 360 to 760 DIP and persists across hiding/pinning for the current window.

## Color and motion

Dark surfaces use `#111217` and `#1a1c24`; light surfaces use `#faf9f6` and
`#f0efed`. The accent is `#b4a6ff` in dark mode and `#6d54d8` in light mode.
Text and icons retain distinct primary, secondary and disabled roles.

Page control uses a rotating violet–ice–mint sweep, with 38/20/8/1.5 DIP nested
edge strokes at 10/17/35/95 percent opacity. The rotation takes 5.4 seconds at
30 frames per second while active. The page center remains untouched. A small
bottom status capsule carries three activity dots and an Escape key hint.
Reduced-motion mode uses a static frame and no animation timer.

The welcome orb is static: the assistant does not animate or poll while idle.
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
