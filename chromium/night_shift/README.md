# Night Shift engine

Night Shift uses Dark Reader's Dynamic theme engine under the MIT license.
The pinned npm release is `darkreader@4.9.135` and the matching source revision
is `dbf2a1029131280d9dab21ed29f1dfdeb51a6cc7`. The assets in `vendor/` are
checked in so Zitter builds do not fetch or execute npm packages.

The generated bridge uses the upstream site-fix parser, URL matching and
native-dark site list. The API bundle dispatches a local event in place of
creating an inline script. The extension's main-world script listens for
that event and installs the upstream CSSOM proxy, including on pages that
block inline scripts with Content Security Policy.
The API's local resource-fetch shim handles only Dark Reader's fetch messages.
Other runtime messages keep Chromium's normal promise-based behavior.
Linked stylesheets also subscribe to CSSOM change events. Animation-frame
polling remains limited to inline styles when the proxy is unavailable.

Zitter registers Night Shift as a built-in component extension for regular
profiles. Its settings page, opened from the panel's settings row or the
extension's Options, controls Night Shift for the whole app: on or off, all profiles or
selected ones, and the default look (brightness, contrast, sepia, grayscale and
whether to override a site's own dark theme). Zitter passes the profiles to the
page in its `state` query parameter. The page reports each change by replacing
its URL fragment with `#apply=<json>`; Zitter reads it only from this
extension's `settings.html`, saves it in `ZitterWorkspaces.json` and writes
`enabled` and `defaults` into every profile's extension storage. Until the
settings are saved once, each profile keeps the switch it had before. When
disabled for a profile, Night Shift removes its styles from sites that follow
the profile and keeps running on sites explicitly turned on. It unregisters
its content scripts when the profile is off and has no enabled exceptions.
A site's own tuning overrides only the values it sets; the rest follow the
defaults.

The extension has no toolbar action or popup page. Zitter shows a native moon
button in the header and hosts an extension settings page in the panel beneath
it. On and Off in that panel save an override for the current site. Use profile
setting clears the override and keeps the site's tuning and repairs. The page's
content script follows each change through `storage.onChanged`. The moon can
be unpinned from the extensions menu, where Night Shift keeps its own row.

Sites read Zitter's current theme through Blink's `prefers-color-scheme` media
query. Night Shift applies its own styles independently of this preference.

When CORS prevents access to an already-loaded linked stylesheet,
the extension can read an anonymous CSS response with limits on origin,
response type, redirects, size and lifetime. Other resources use the page's
ordinary CORS rules. Private profiles do not run the component.

The panel's repair action opens a scoped Assistant session that can sample
computed colors and propose typed color rules. The model receives neither page
text nor screenshots through this action. A proposal is tied to the document
that was inspected. Opening the panel starts a temporary comparison: Zitter
calls `zitterNightShiftReview` in the extension's isolated world, which the page
cannot reach. Closing the panel restores the saved state. Saving affects the site in that profile, and Undo
restores the previous saved fix.

Run `python tools/refresh_darkreader.py` to reproduce the assets. The refresh
checks both upstream archives and uses a pinned esbuild version.

## Browser verification

`tools/verify_content_night_shift.py build/ZitterContentLab.app` checks the
bundled extension across ordinary macOS starts of the Content app with
disposable profiles. The Chrome-based browser test
`NightShiftBrowserTest.NativeActionReviewsScopedRepair` was removed with the
Chrome build.

Run `node --test tests/night_shift_policy.test.mjs` for site scoping and typed
repair validation. Run `python tests/night_shift_browser.py <chromium-executable>`
with `websocket-client` installed to exercise the unpacked extension in an
isolated profile. Pass `--original <unpacked-extension-directory>` to run the
same linked stylesheet fixture with the
[Dark Reader 4.9.135 MV3 release](https://github.com/darkreader/darkreader/releases/tag/v4.9.135).

In the Windows Chromium-based Edge check, adding a rule through
`CSSStyleSheet.insertRule()` changed the fixture's themed background from
`rgb(34, 36, 38)` to `rgb(103, 70, 15)` with Night Shift. The original
extension kept the old color after the same mutation. The fixture serves
its scripts under `script-src 'self'`.

A linked stylesheet served from a separate loopback origin without an
Access-Control-Allow-Origin header produced `rgb(34, 36, 38)` in Night Shift.
Dark Reader 4.9.135 left the same background at `rgb(238, 238, 238)`.
