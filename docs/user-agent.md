# User-Agent

Zitter decides per site how it introduces itself: the `User-Agent` header,
`navigator.userAgent` and, when they match, the client hints (`Sec-CH-UA*`,
`navigator.userAgentData`). Most people never touch it: **Auto** sends plain
Chrome everywhere, and Zitter's built-in catalogue switches the few sites that
need something else.

Design: [Zitter — User-Agent](https://claude.ai/artifact/N9Cqv4fbXVm5Edh2e3V5dx).

## Presets

| Preset | User-Agent | Client hints (when matched) |
| --- | --- | --- |
| Chrome | the reduced Chrome string of this build (`Chrome/157.0.0.0`) | Chromium + Google Chrome |
| Zitter | Chrome's string + ` Zitter/<version>` | Chromium + Zitter |
| Firefox | Firefox one major ahead of Chrome, for this OS | none, like Firefox |
| Safari | Safari 26.1 on macOS | none, like Safari |
| Own (`custom:<name>`) | the user's string | none |

The browser default (`ContentBrowserClient::GetUserAgent`) is the Chrome preset.
Requests outside pages, such as service workers and extensions, use it.

## Which preset a page gets

`ResolveUserAgent` (`chromium/prototype/user_agent_policy.cc`) checks three
things, in this order:

1. **The user's exception** for the site. A host covers its subdomains, and the
   most specific exception wins.
2. **Zitter's catalogue**, in Auto mode only. It ships with the app and is
   never fetched. Entries:
   - `youtube.com`, `youtu.be`, `youtube-nocookie.com` → Zitter
   - `accounts.google.com` → Zitter. Google sign-in rejects browsers that claim
     to be Chrome without being it (see 0.22.2), and YouTube signs in through it.
3. **The mode**: Auto (Chrome), or a fixed preset that ignores the catalogue.

An exception or mode naming a deleted own agent falls back to Auto.

## How it is applied

`UserAgentPage` (`content_embedder/user_agent_page.cc`) is attached to every
`WebContents`. On each primary main-frame, cross-document navigation it
resolves the URL's host. In `DidStartNavigation` it then either:

- keeps the default (Chrome with matched hints), or
- calls `WebContents::SetUserAgentOverride` and
  `NavigationHandle::SetIsOverridingUserAgent(true)`.

Content allows that call during `DidStartNavigation` without reloading. The
committed page, its subresources and its subframes keep the override. Limits:

- A redirect to another host keeps the preset chosen for the first URL.
- A change applies from the next navigation. The panel offers **Odśwież**
  ("Reload").
- `--user-agent` on the command line disables all of this.

## Settings and storage

The profile's site permission store ("Content Site Permissions") holds one
dictionary, `zitter.user_agent`:

```json
{"mode": "auto", "sites": {"stary-portal.pl": "firefox"},
 "custom": [{"name": "Kiosk TV", "ua": "Mozilla/5.0 (SMART-TV; …)"}],
 "client_hints": true, "pinned": false}
```

When `ProfilePermissionController::UserAgent()` reads it, it drops invalid
entries. `SetUserAgent()` rejects invalid settings. Limits:

- 500 site exceptions.
- 20 own agents. Each has a name of at most 40 characters and a printable ASCII
  string of at most 512 characters.

Private windows use the same store rules as the other site settings.

## Interface

- **Extensions menu**: a built-in "User-Agent" row above the footer commands.
  Its second line says how the current site sees Zitter ("Auto · tu jako
  Zitter", "Auto · here as Zitter"). The row's pin puts the button in the
  extensions bar. It is unpinned by default, because most people stay on Auto.
- **Panel** (`chromium/prototype/user_agent_panel.cc`), 296 px like the other
  toolbar panels. It shows:
  - the site;
  - what the site sees, with its source (catalogue, Auto or your exception) and
    the full string behind **Szczegóły** ("Details");
  - the choices for this site;
  - after a change, "Teraz: …" ("Now: …", what the open page still uses) with
    **Cofnij** ("Undo") and **Odśwież** ("Reload");
  - a link to Settings.
- **Pinned button**: shows a dot while the active site has an exception.
- **Settings → User-Agent** (category 12, before Extensions, which moved to
  13) has these sections:
  - the mode;
  - the catalogue;
  - the user's exceptions, each with an equal-width preset picker, plus a line
    to add one;
  - own agents, whose full string appears only in the editor;
  - **Dopasuj Client Hints** ("Match Client Hints"), with a tooltip that
    explains it.

Test and screenshot hooks:

- `--zitter-open-panel=user-agent` opens the panel 4 s after start, and
  `--zitter-open-panel=user-agent-settings` opens the Settings category.
- `RunShortcut("user_agent")` opens the panel.
- The panel's view ids are listed in `user_agent_panel.h`.

## Tests

- `user_agent_policy_unittests`: resolution order, catalogue matching, strings,
  validation and the dictionary round trip.
- `sidebar_scene_unittests`: catalogue counts, keys and glyphs (including the
  new category).
- `tools/verify_content_workspace_session.py`: a local page in Auto mode gets
  matching Chrome headers, `navigator.userAgent` and brands, without Zitter's
  brand.
