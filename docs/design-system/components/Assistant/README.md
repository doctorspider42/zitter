# Assistant

The assistant is a conversation surface, with secondary controls revealed on demand.
It intentionally uses an ambient spectral accent for page-control feedback, as a
focused exception to the browser chrome's generally flat color treatment.

## Layout

- Right panel: 420 DIP, 20 DIP outer radius, 16 DIP inner padding, 10 DIP stack gap.
- Header: compact 15 DIP sans identity; 28 DIP icon actions with accessible labels.
- Session, backend and model fields: collapsed by default; open from the history
  action or the model chip. Privacy grants open from the separate shield chip.
- Welcome: a static 66 DIP optical orb within a 210 DIP hero, one 27 DIP italic
  serif heading and a 12 DIP supporting line. Three 54 DIP suggestion rows fill
  a draft without sending it or granting browser access.
- Conversation: separate user and assistant messages; 12 DIP bubble padding,
  13 DIP body text, 12 DIP role labels. Browser activity uses quiet status text.
- Composer: 18 DIP radius, 12 DIP padding, unframed 14 DIP editor, 28 DIP utility
  actions and a 36 DIP primary send/stop action. Research is a mode chip.
- Model and access chips sit immediately above the composer. Copy/clear actions
  remain in the collapsed session controls.

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
