# Zitter

Original Zitter code is licensed under [Apache-2.0](LICENSE). Third-party code
retains its own licenses; see [NOTICE](NOTICE) and the binary license audit.

C++ + Chromium fork + Views. This repository contains a native browser development app and a separate
workspace/profile test harness. The selected macOS ARM64 bundle is a local
non-component Release build with ThinLTO/PGO, signed locally with Developer ID.
It is not a notarized release installer.

## Run the desktop app (macOS)

For daily browsing, use the [optimized local build](#optimized-local-build-macos-arm64)
and open `build/Zitter.app`. The measured results and test coverage are in
[the performance report](docs/performance.md).

The separate [Rust UI / GPU prototype](docs/rust-ui-prototype.md) compares Views,
Rust/Skia and Rust/Metal with the current Chromium fork and isolated test profiles.
The [CPU/GPU profile and binary license audit](docs/ui-profile-and-licenses.md)
explains presentation timing and distribution requirements.
The [implemented Apache/libarchive migration and UI A/B benchmark](docs/open-source-ui-optimization.md)
records the current source license, UnRAR replacement and verified WindowServer frame measurement.

For incremental development, keep the separate Debug/component output. After
preparing the pinned Chromium checkout and installing the overlay below:

```sh
cd .chromium/src
gn check out/Zitter //zitter:zitter
autoninja -C out/Zitter -j 12 zitter
cd ../..
python3 tools/package_app.py --destination build/ZitterDebug.app --no-register
open build/ZitterDebug.app
```

`build/Zitter.app` is the normal app. It uses Chromium's regular bootstrap,
network service, sandboxed helpers and profile services, with Zitter's Views
window. It has no browser-test runner or one-hour test timeout. Cmd+L opens the
address/search palette, Cmd+K opens Command Center for actions, tabs, pins, workspaces and profile history,
Cmd+T adds a tab, and Cmd+Alt+1/2 selects the first/second workspace.
The themed window keeps native traffic lights; a pinned left sidebar holds the
address pill, tabs, Downloads, Settings, workspace switching and editable accent
colors and an optional emoji per workspace (shown instead of its dot); drag its
right edge to resize it. The header has only back/forward/reload.
Split View starts from a tab's context menu (or by dragging a tab onto the right
half of the page). The pair stays one sidebar item (among the pins when one half
is pinned): selecting another tab shows that tab alone, selecting either half
brings the split back. The pair's context menu moves a half left/right, switches
between side-by-side and stacked panes, takes a tab out of the split or closes it;
each visible half also has a
header with its title, that menu (⋯) and a close button; the divider between the
pages is draggable. Rebuild the icon with `python3 tools/build_icon.py`.

The default data directory is `~/Library/Application Support/Zitter`:

- `ZitterWork/` and `ZitterPersonal/`: original persistent Chromium profiles;
- `ZitterProfile-<UUID>/`: additional or imported Chromium profiles;
- `ZitterAppearance.json`: theme, search provider, workspace animations, colors and sidebar pinning;
- `ZitterSession.json`: open HTTP(S) tabs, pin associations and two-pane layouts;
- `ZitterWorkspaces.json`: profile catalogue, workspace IDs, names, assignments,
  pinned folders/links, selection, first-run state and pending profile deletions;
- cache: `~/Library/Caches/Zitter`.

macOS protects the cookie encryption key in the separate **Zitter Safe Storage**
Keychain item. The installer guards a two-string upstream patch for this identity;
it retains Chromium's native random-key generation and encryption. The other
upstream changes are the optional browser-test build dependency, a guarded
address-Autofill capability check for standalone content hosts, and a narrow
media-handler registration hook that retains Chromium's capture policy and
output-protection accounting. Product code is
installed under `src/zitter/`; the production target needs no test-runner hook.

The signed packager uses private APFS clones (copies on other filesystems) and places real helper bundles
inside the outer app, preserving Chromium's framework lookup and sandbox startup.
Repackage after rebuilding. This development app depends on the local build and
uses a pinned Developer ID certificate for local packaging; notarization and
standalone redistribution are not configured yet. The native
bootstrap also has a Linux target, but only macOS ARM64 has been built and run;
a Windows production entry point remains to be implemented.

The header gear, **Cmd+,** and **Zitter → Ustawienia…** open a native preferences
centre with a category sidebar, full-text setting search, descriptive cards and
switches. Narrow windows use a category picker; long sections scroll while search
and save status stay visible. Light, dark and system themes apply throughout.

Ten sections cover appearance, search, privacy, site permissions, content and
readability, passwords/profiles, downloads, languages/spelling, browsing data
and advanced Chromium options. Theme, typed-search provider,
sidebar pinning and workspace animation are app-wide and save automatically to
`ZitterAppearance.json`. All other controls read and write the active Chromium
profile, with managed values disabled:

- HTTPS-only mode, third-party cookie blocking, Do Not Track and hyperlink pings;
- default ask/block policies for camera, microphone, location, notifications and
  clipboard reads (site exceptions and OS permissions still apply);
- default zoom, default/minimum font size, JavaScript, images, popups and sound;
- password saving and automatic sign-in;
- prompting for a download destination and choosing a default folder with the
  native folder picker. The picker retains the original profile identity if the
  active workspace changes before it returns.

Sound changes mute/unmute existing pages and respect site exceptions. Popup
adoption respects the source profile's site policy for windows without a user
gesture. Reload a page if a content/font preference has not taken effect yet.
Permission switches configure Chromium's default policies; camera/microphone
requests use the native consent surface described below and still require OS
permission. A setting alone never grants device access.
Text preferences use an explicit **Zapisz** button or Enter; switches and dropdowns
save immediately. Unsaved text survives control refreshes within the same profile.
Language tags are validated, canonicalized and deduplicated, then saved through
Chromium's language preferences so forced languages retain precedence. Font
families, fixed-width text size, spellchecking and caret browsing use the native
profile services.

The site exception editor accepts HTTP(S) origins, checks valid policies for each
content type, and lists explicit rules with their primary/embedding scopes.
Managed rules are read-only; removing a user rule restores the default policy.
Rule lists support searching by domain and progressive loading.

Browsing-data removal offers hour/day/week/all-time ranges and independent
history, site-data and cache selection. A separate native confirmation names the
profile and selected data. It defaults to Cancel and binds the original profile
identity before any workspace switch. Removal uses Chromium's asynchronous
remover with busy/error feedback and honours the history-deletion policy.
Passwords, download files, workspace pins and other profiles are excluded.

Advanced settings include application-wide DNS-over-HTTPS mode/templates and
hardware acceleration (restart required), plus links to the system proxy and
certificate tools. Strict DNS cannot be saved without a valid nonempty HTTPS
resolver configuration. These options are marked as app-wide in their descriptions.

Links open workspace controls, profile management, saved logins and Arc import.

The Privacy setting **Wstępne połączenia ze stronami** controls Chromium's
prediction preference for the current profile. Address hints prepare an origin
connection while you type, including bare domains, without fetching page content
or submitting search queries. Hints follow the destination workspace's profile
and skip profiles still loading. Unused hints can consume network connections if
you abandon the address; disabling prediction stops these hints. Managed profile
preferences remain read-only.

The theme covers native shell controls, cached sidebar rows, palettes, Peek and
password dialogs. System mode follows OS appearance; explicit modes are scoped
to Zitter's windows. WebContents receive the selected color provider, so sites
with `prefers-color-scheme` support follow the preference without forced page
inversion. Workspace accent colors tint both light and dark frames.

Switching spaces keeps an already-visible left panel open, including in auto-hide
mode; it does not change the pinning preference. Incoming sidebar rows slide in
the direction of the selected workspace. Only the visible list viewport animates;
native page views switch straight to their final bounds. Animations restart
cleanly on rapid switching, can be disabled and respect the OS reduced-motion
preference. Cached pin trees remain intact during transitions. Inactive pinned lists are
prepared incrementally after startup without opening pages. Switching skips
refreshing hidden settings, repeated context-menu tree walks and pin/tab
visibility scans. The list viewport uses a 180 ms compositor transition, starting after the new
viewport has presented its first frame, with render-surface caching. Swipe horizontally over
the sidebar to scrub continuously through spaces. The preview follows finger
distance in either direction: reverse before lifting to cancel, or drag farther
to cross several spaces. Release selects the nearest preview and settles it;
intermediate spaces do not open pages or change profiles. At most three clipped
viewport previews are retained, with resistance at the ends and no wrapping.
Vertical scrolling and gestures over web pages keep their normal behavior.
Cocoa finger-down/up phases re-arm a new gesture without requiring cursor
movement, including when it interrupts settling/inertia. Phase-less wheels
settle after a brief idle interval.

Use the sidebar's workspace dots to switch spaces and its gear button to manage
spaces. **Zapisz nazwę i profil** applies the name and browsing profile; colors
and emoji save immediately. **Nowa przestrzeń…** opens a separate creation mode
with **Utwórz przestrzeń** and **Anuluj**. Settings are grouped and scroll in small
windows. Each space has its own tabs and accent color. Spaces assigned to the
same profile share cookies, logins and persistent site storage. Changing a
space's profile closes its tabs and opens one blank tab in the selected profile.
Removing a space requires a confirmation naming that space, with Cancel as the
default. It removes its tabs and pins, preserves profile data and keeps the active
space when deleting an inactive one; the last space cannot be removed.

Right-click a workspace dot to choose its color or emoji (including clearing the
emoji), open settings, or delete it with confirmation. **Profil przeglądania**
selects its profile, edits the profile name, or creates and assigns a new one. This
also works on inactive workspaces without switching the active page. Long dot
bars scroll horizontally. The profile editor retains Chromium profile identity,
storage and workspace assignments when renaming a profile.

The sidebar keeps **Nowa karta** and the workspace's pins/tabs. Time Machine,
History, Downloads and Settings live in the native **Zitter** menu on macOS.
Right-click the sidebar/tab-list background or a tab to add a named folder.
Right-click an existing folder for **Nowy podfolder…**, **Zmień nazwę folderu…**
and **Usuń folder…**. Removal requires confirmation and deletes the folder,
nested folders and all their pins. Already opened pages remain ordinary tabs.
Folder names and nesting persist with the workspace catalogue; cancellation
makes no changes, and a failed catalogue save rolls back the edit and shows a
retry message.

**Historia** opens from the Zitter app menu, Preferences → Prywatność, the command
palette, or Cmd+Y (Ctrl+Y outside macOS). It shows the active profile's actual
Chromium history, including imported page history, with titles, URLs and local
visit dates. Search filters titles/URLs. Results start with the newest 200 visits;
**Pokaż więcej** loads another 200. Selecting an entry opens it in a new tab in
the same profile. History remains separate from Time Machine snapshots.

**Wyczyść historię…** in that panel offers the last hour, 24 hours, 7 days or all
time. A confirmation names the profile and interval, with Cancel as the default.
Only history is selected by this action; the open list reloads after deletion.
Preferences also retain the broader data-clear controls for cookies and cache.

**Time Machine** is available in the Zitter app menu and in Preferences →
Wygląd. Save a named snapshot of the current workspace's pages, pinned folders,
active pane and split ratio. Restoring creates a recovery point for the current
layout first. Automatic points are also made before closing tabs/changing splits,
before sleeping a workspace, and on session saves at most every five minutes.
Each workspace retains 30 named and 30 automatic snapshots, plus its last 100
closed tabs. Search snapshots/closed tabs and restore them from the same panel.
Snapshots belong to their original browsing profile; they cannot transplant a
session into another profile. Restoration reloads URLs, not forms or the page's
back/forward stack. Cookies, passwords and browsing history are not rolled back.

**Sleep a workspace** from its Time Machine panel to release all its live pages,
including both split panes. Zitter switches to another workspace; selecting the
sleeping space wakes its saved layout. At least two spaces must exist. Sleeping
state survives restarting the app. Save work in web forms before sleeping.

**Configurable shortcuts** live in Preferences → Wyszukiwanie → Skróty
klawiszowe. Select an action, record its chord or type `Cmd+Shift+J`, then save.
An empty chord disables an action. Conflicts and reserved editing/system chords
are rejected. Reset restores an [Arc-inspired scheme](https://resources.arc.net/hc/en-us/articles/20595231349911-Keyboard-Shortcuts): Cmd+S toggles the sidebar,
Cmd+Shift+T reopens a closed tab, Cmd+Option+Left/Right changes spaces,
Control+1…9 selects spaces, Cmd+1…9 selects tabs, Control+Tab cycles tabs,
Control+Shift+= toggles Split View, Control+Shift+- closes it, and
Control+Shift+1/2 focuses a pane. Cmd+Shift+Y opens Time Machine and Cmd+Shift+S
opens snapshot saving. Original Cmd+Option+1/2 space chords remain compatibility
aliases until those actions are remapped. Native menu shortcuts update too.

**Keyword search** is configurable in Preferences → Wyszukiwanie → Wyszukiwarki
i prefiksy. Built-in prefixes are `g` (Google), `w` (Polish Wikipedia), `yt`
(YouTube), `gh` (GitHub), `d` (DuckDuckGo), and `b` (Bing). Type `g query` in the
address bar to use an engine immediately, or choose any provider as the default.
Add/edit/remove providers using an HTTP(S) URL containing one `{query}` marker;
query text is encoded and cannot alter the destination origin. A provider's
prefix is unique, lowercase alphanumeric, and up to 16 characters. At least one
provider must remain. All shortcuts and providers persist in `ZitterTools.json`;
snapshot/closed-tab metadata uses `ZitterTimeMachine.json` in Zitter's data folder.

**Split View** opens two pages side by side using the header's split button.
The sidebar groups the two split tabs side by side in one outlined row, in pane
order. Click either half or the page to choose the pane controlled by the address
bar and back/forward/reload; the sidebar stays open. Pinned panes join this pair
and return to their original location when the split ends. Drag a sidebar tab or pin into the right half of the
page area, or right-click it and choose **Otwórz obok w Split View**. Drag the
separator to adjust the split (20–80%). Toggling the split off keeps both tabs;
closing a pane's tab leaves the surviving pane visible. Opening the current page
beside itself creates an independent tab. Both panes use the
workspace's assigned profile.

Open tabs, the chosen pane, split proportions and layouts are saved automatically
per workspace and restored on launch. Background tabs load when shown. This
restores page URLs, not form contents, sessionStorage or the back/forward stack.
Reassigning a workspace's profile starts a fresh session for that workspace.

**Peek** opens an HTTP(S) link in a temporary native preview above the current
page: hold **Cmd and left-click** (Ctrl on Linux), or right-click a link, tab or
pin and choose **Otwórz w Peek**. Escape or the close button dismisses it. The
preview has its own back/forward buttons; **Otwórz jako kartę** and **Split View**
move the same live page into the workspace, keeping its form state and navigation
history. Closing the preview leaves the source page and tab count unchanged.
Temporary previews are not restored on launch. Changing workspace closes Peek.

Right-click a page/link or the sidebar address pill to choose **Zawsze otwieraj
tę domenę tutaj** or pick a workspace under **Otwieraj [host] w…**. The same menu
removes the rule. Tab/pin context menus offer the shortcut for their own workspace.
Rules match the exact canonical hostname (all ports, without implicitly including
subdomains), persist across restarts, and follow the target workspace's current
profile. Removing a workspace removes its rules.

Rules apply to typed addresses, external HTTP(S) links, pins and explicit
main-frame GET links, including new-tab links. The navigation throttle cancels a
cross-workspace link before its first request and opens it with the target
profile. Forms, frames, redirects and history traversal retain their original
request/context. A routed Peek uses the target profile immediately, names its
workspace in the header and switches there when promoted. Ordinary middle-click
keeps its background-tab behavior. The development bundle advertises HTTP/HTTPS
handling to macOS and uses Zitter's existing app URL bridge.

**Command Center (Cmd+K)** searches actions, open tabs across workspaces, pinned
links (including folder paths), workspace names and the active profile's history.
Type `>` to browse only commands, e.g. `> skroty`, `> snapshot`, `> przenies` or
`> sleep`. Polish names work with or without accents; English action keywords
also work. Enter executes, arrows select and Escape dismisses. The initial view
shows six common actions; `>` reveals the contextual catalogue. Commands include
preferences, profiles/spaces, import, providers/shortcuts, Split, navigation,
closed-tab recovery, quick snapshot saving and background workspace sleep/wake.
Sleep releases pages: save forms first; pages reload on wake.

Move a tab into another workspace of the same profile without recreating its live
page, preserving forms and navigation history. The source retains at least one
page; pinned links stay in the source catalogue. A different-profile destination
instead offers **Otwórz kopię karty**: it opens the URL using that profile's own
cookies and storage. Its source page remains intact. Explicit copy destinations
are honoured even if a domain routing rule names another workspace. Queued
commands revalidate their source workspace/tab and current availability.

A tab/pin result switches to its workspace; a history result opens in the focused
pane. History never crosses profile boundaries. Cmd+L/Cmd+T retain address/search
behaviour, and explicit provider prefixes keep their search action first even in
Command Center. History requests are debounced, asynchronous and deduplicated.

See [the feature audit](docs/feature-audit.md) for remaining everyday-browser
integration gaps and the recommended order of work.

**Downloads** open from the toolbar's download arrow, File → Pobrania, Command
Center, or the configurable Cmd+J shortcut. The movable, resizable native panel
shows the selected profile's active transfers and latest 200 historical items:
filename/source, byte progress, transfer speed and estimated seconds remaining.
Pause, resume (when supported by Chromium), cancel and Show in Finder act on the
original DownloadItem. Closing the panel does not stop transfers; switching
profiles closes the old profile's panel without affecting its downloads. The
list is backed by Chromium's download history rather than Zitter session JSON.
Dangerous/insecure items cannot be resumed or revealed through this panel; it
provides no bypass of Chromium's file protection. Files are never opened
implicitly. Download folder and ask-where-to-save preferences still apply.

The toolbar key opens saved-login metadata for the focused page's profile. Its
window is now draggable and positioned within the current display's work area;
password save/update consent uses a properly parented native sheet.

**Webpage uploads** use a native picker for one file, multiple files, or an entire
folder. File contents and relative folder paths are delivered through Chromium's
file-selection pipeline. The requesting page owns the choice and remembered
folder; switching profiles does not redirect it. Navigation, hiding or destroying
the requesting tab cancels pending delivery, including late picker results.
Folder selection is the user's explicit consent to recursively upload that folder.

**Camera, microphone and screen sharing** use Chromium's policy/permission,
media-device and OS-consent pipeline. Native permission sheets identify the
requesting site, embedding site where different, and browsing profile; Cancel is
the default. Sites can use device constraints/enumerateDevices after consent.
The screen-sharing picker retains Chromium's screen/window sources and adds
live Zitter tabs from the exact same profile, respecting the source filters and
enterprise screenshot policy. Zitter tab rows currently show titles without
live thumbnails. No source is automatically selected.

A nonmodal capture window names the actual requesting origins and profile and
provides **Zatrzymaj**; closing that window also stops capture. Controls remain
available while another workspace is selected. Sleep skips workspaces that are
capturing or being captured, so it cannot silently discard a meeting's pages.
The development bundle declares its camera/microphone purpose strings; macOS
privacy consent is still required on the user's machine.

**Page dialogs and unsaved changes**: native alert/confirm/prompt sheets are
bound to their requesting page and show its origin/profile. Navigation, frame
removal and page teardown dismiss stale dialogs. Background pages cannot open
ordinary JavaScript dialogs. Closing a tab/Peek/workspace, changing its profile,
sleeping, restoring a snapshot and exiting run the pages' standard beforeunload
checks first. A batch proceeds only after all pages agree, so cancelling any
sheet leaves every page in that operation open. The plan is revalidated before
commit, including newly registered handlers. This protects sites that implement
beforeunload; snapshots still do not serialize arbitrary form values. App exit
also retains native consent for in-progress downloads.

Each page now attaches Chromium's password, autofill and session clients.
Successful HTML form logins can show a native **Zapisać hasło?** / **Zaktualizować
hasło?** confirmation. Only accepting saves through Chromium's profile password
store; **Nie teraz**, closing the dialog or switching workspace does not save.
The page's immutable profile owns its credentials. Passwords remain in Chromium's
encrypted store, not Zitter's JSON files. Saved credentials can autofill later
visits and use Chromium's account suggestions. The header password icon (also
**Plik → Hasła tego profilu…**) opens the current save fallback or a metadata-only
overview of saved sites/accounts. Password plaintext is never put into that UI.

History records committed main-frame HTTP(S) visits using Chromium's history
service and navigation metadata, with its existing policy/preference checks and
title handling. Each profile has its own `History` and `Login Data` databases.
Standard single-field form autocomplete is connected too. Full address/card
Autofill UI, password reveal/edit/delete UI, and `chrome://settings` are not part
of this first integration. Embedded pages advertise address/payment UI as
unsupported without changing profile preferences; this avoids invoking Chrome
BrowserWindow-only save bubbles. The guarded installer applies the address
capability check, and payment support is disabled per page through Chromium's
existing embedder API.

**Zarządzaj profilami** creates named, isolated Chromium profiles. An unused
profile can be deleted after confirming removal of its browsing data. Profiles
assigned to any workspace are protected: reassign or remove those workspaces
first. The original company and personal profiles follow the same rules. Chromium
clears browsing data and schedules directory cleanup through its profile-deletion
service; a saved deletion marker survives an interrupted shutdown.

**Importuj z przeglądarki…** opens a browser picker for Arc, Google Chrome,
Microsoft Edge and Brave, followed by checkboxes for the supported data. Choose
separate browsing profiles, bookmarks/pins/folders, browsing history and cookies.
History and cookies are opt-in. Close the importer with the × in its header;
on first launch this keeps the single starter workspace. Arc workspace emoji are
always retained. Disabling separate profiles assigns imported spaces to one
destination profile for that source browser. Opening the panel or changing a
browser/checkbox automatically prepares the selected data. The Import button
becomes available when that read completes; stale scan replies are ignored.

Arc reads local `~/Library/Application Support/Arc/StorableSidebar.json` and
profile names from `User Data/Local State`. It retains Space names, nested pins,
Favorites and emoji, including composite emoji and legacy code points.
Chrome, Edge and Brave read their local Chromium `Local State` and each regular
profile's `Bookmarks` file. Profile display names become workspace names; folder
hierarchies become workspace pins. Sources are read only. Invalid/non-HTTP(S)
links and unsupported items are counted as skipped. Arc Split View becomes a
folder of supported child tabs; unpinned Today tabs are excluded. Passwords are
not imported.

History and cookies are read from snapshots of each selected source profile,
including its SQLite WAL, without writing to the source. History uses Chromium's
page-history importer: HTTP(S) page URLs, titles, visit/typed counts and last-visit
dates within its 90-day retention window. Existing destination history pages are
kept. This does not copy every individual visit or search-provider metadata.
Cookie import preserves expiry, Secure, HttpOnly, SameSite and partition keys;
expired/invalid cookies are counted as skipped. Existing destination cookies take
precedence, so repeating an import does not replace current sessions.

On macOS, encrypted `v10` cookies are decrypted in memory with the selected
browser's existing Safe Storage Keychain entry. Only clicking **Importuj** with
cookies checked can request Keychain access; preview never requests it. Zitter
neither creates nor changes source keys. Denied access, unsupported encryption or
invalid host binding stops preparation before destination changes, with a retry
message. Destination CookieManager applies its normal validation and encryption,
and flushes the cookie store before completion. Some sites may require signing
in again. Data is copied to the matching imported profile, including on a later
import into an existing workspace; merging profiles intentionally merges their
selected browsing data. If a source database changes during copying, close that
browser and retry. Destination data writes start only after the workspace
catalogue has been saved successfully. A destination write failure is reported
without deleting already committed workspaces.

A fresh installation starts with one **Moja przestrzeń** workspace and one
profile. Skipping import keeps that workspace. A successful first-run import
replaces the untouched starter workspace and its profile, so only imported
spaces remain. A failed import preserves the starter and can be retried.
Existing installations and customized workspaces remain intact; later imports
add spaces and skip previously imported ones, retaining local icon changes.
Pins load on demand and retain their hierarchy across restarts.

## Stable local macOS signing

`tools/package_app.py` signs the application, nested helpers, framework and
libraries with the certificate pinned in the git-ignored `.zitter-signing.json`.
Normal packaging commands reuse that identity. The private key stays in macOS
Keychain. To configure another checkout explicitly:

```sh
security find-identity -v -p codesigning
python3 tools/package_app.py --sign-identity '<Developer ID certificate SHA-1>'
```

`ZITTER_CODESIGN_IDENTITY` can override the saved certificate. Missing or invalid
certificates fail packaging rather than falling back to an ad hoc signature.
`--unsigned` is an explicit opt-out for disposable test/benchmark bundles.
The Python `package()` helper used by filesystem tests remains unsigned unless
an identity is passed; the normal command-line entry point requires signing.

Signed packages keep the `dev.zitter.browser` bundle identity across component
and optimized builds. Code is signed from the inside out and verified with
`codesign --verify --deep --strict` before replacing an existing owned bundle.
Private clones prevent signing from changing the compiler's original outputs.
The development marker lives in `Contents/Resources` so it is sealed as data;
legacy marker locations are still recognized when replacing an older bundle.

After the first transition from an ad hoc build, macOS may ask once more for
**Zitter Safe Storage** access. Choose **Zawsze pozwalaj** yourself to authorize
the signed application. Its designated requirement then stays stable across
rebuilds signed by the same Developer ID team. Packaging does not change the
keychain item, its secret or its access controls. This local signing workflow
does not add notarization or enable Hardened Runtime.

## Current verification status

- Standalone C++ product model and activation contracts: built and tested.
- Chromium integration: **built and linked on macOS ARM64** against
  `chromium/revision.json`.
- `StorageIsolationAndSwitching`: **passed** on 2026-10-15 (real Chromium profiles,
  same-origin storage, workspace switching and page-host recreation).
- Local checkout, tools and executable are in the ignored `.chromium/` directory;
  these large build artifacts are not part of the source overlay.
- The native window and Work/Personal button switching were visually verified.
- Workspace shortcuts: **passed** on macOS ARM64, including native AppKit events
  with focus in each page's text input, repeated switching and preserved text.
  `python3 tools/run_prototype.py --verify` runs the automated storage, keyboard and native-shell tests.
- Native Views shell: **built and tested** with an auto-hiding/pinnable sidebar,
  Cmd+L/Cmd+T floating address/search bar and saved workspace accent colors.
- History and password integration is covered by real form/navigation tests.
- No claim of full Chrome extensions, permissions, downloads,
  popups, fullscreen, or DRM support is made by this probe.

The app uses **a dynamic catalogue of regular Chromium Profile objects and independently
owned workspace page hosts in one visible Views window**. A new workspace starts with one
page; saved workspaces restore their tabs. Cmd+T can add pages bound to that workspace's profile. Switching reuses existing page hosts and lazily loads restored pages; it
never changes a live page's BrowserContext. A profile keep-alive outlives its pages.
In the separate browser-test harness, its normal Chrome window is hidden.
The desktop target starts only the Zitter window.

The page-service adapter explicitly connects Chromium clients to Zitter's own
window. It does not implement the complete Chrome TabInterface/BrowserWindow
contract; remaining services still require their own integration.
[docs/prototype.md](docs/prototype.md) records the original hosting experiment.
See [docs/verification.md](docs/verification.md) for the observed test result and
the exact scope of the verification.

## Performance baseline (macOS ARM64)

See [the performance work log](docs/performance.md) for measured baselines and
the order of subsequent changes.

Keep the development build in `out/Zitter`. Create a separate component Release
to isolate the effect of optimization before changing the linking strategy:

```sh
mkdir -p .chromium/src/out/ZitterRelease
cp chromium/release.args.gn .chromium/src/out/ZitterRelease/args.gn
python3 tools/install_chromium_overlay.py .chromium/src
python3 tools/install_archive_backend.py .chromium/src
cd .chromium/src
buildtools/mac/gn gen out/ZitterRelease
buildtools/mac/gn check out/ZitterRelease '//zitter:*'
../depot_tools/autoninja -C out/ZitterRelease -j 1 generate_bindings_all
../depot_tools/autoninja -C out/ZitterRelease -j 8 zitter zitter_prototype
cd ../..
python3 tools/benchmark_browser.py --out .chromium/src/out/Zitter --label debug
python3 tools/benchmark_browser.py --out .chromium/src/out/ZitterRelease --label release
python3 tools/package_app.py --out .chromium/src/out/ZitterRelease --destination build/ZitterRelease.app
```

Build `zitter_prototype` in Debug after installing the overlay too. Finish all
compilation before collecting comparison runs; keep window visibility, other
applications and power conditions comparable. The benchmark opens its own test
window with temporary Chromium profiles and a local HTTP page. It does not use
the daily browser's profiles or internet services.

Each run uses 25 loaded tabs, two excluded warmups per scenario, and 30 measured
trials (override with `--tabs` and `--iterations`). Raw logs, per-trial samples,
median, nearest-rank p95, GN configuration and executable hash are retained in
`build/performance/`. Failed or incomplete runs never produce a results JSON.

- `new_tab_dispatch_ms`: synchronous new-tab creation, selection and navigation dispatch.
- `new_tab_first_visual_paint_ms`: command through the browser's first visually
  nonempty paint notification, including tab creation.
- `split_dispatch_ms`: synchronous opening of a split with an already loaded tab,
  including the pre-change snapshot and shell updates.
- `split_both_renderer_frames_ms`: command through frame submissions from both panes.

Paint/submission notifications are proxies for visual readiness, not physical
display presentation. These are warm-profile shell benchmarks; they do not
measure cold application startup, network latency or representative heavy websites.
Functional verification skips the benchmark unless `--zitter-benchmark` is given.

To measure a regular packaged application with normal renderer helpers instead
of the browser-test executable, build its regular target and package it, then run:

```sh
python3 tools/benchmark_app.py --app build/Zitter.app --scenario navigation --iterations 30 --tabs 25
python3 tools/benchmark_app.py --app build/Zitter.app --scenario startup --iterations 30 --tabs 25
```

The executable must include the opt-in `AppPerformanceProbe`; the driver requires
a marker-owned bundle from `tools/package_app.py`. Variant regular targets can be
packaged with `--binary zitter_preconnect --destination build/ZitterAppCandidate.app
--no-register` (as arguments on one command). Both scenarios use isolated profile
directories and a local HTTP fixture, retaining logs and raw samples. Startup
creates a fresh process and profile directory per trial, restores tabs in two
profiles, and activates the last profile; two process warmups are excluded.
Operating-system caches remain warm. The driver passes Chromium's
`--disable-backgrounding-occluded-windows` test switch identically to all app
variants, so window placement does not suppress rendering. This measures
foreground-style compositor work; ordinary app launches keep normal occlusion
behavior. Startup ready/paint timestamps are observed
through stderr receipt; split visual-state callbacks acknowledge renderer work,
not physical presentation. These samples complement the browser-test metrics
above and must not be mixed with them in a before/after comparison.

For a controlled renderer-pool experiment, `--spare-renderers 2` passes
`--enable-features=MultipleSpareRPHs:count/2` to this run only. Extra browser
arguments are recorded in its JSON. Compare against the default with the same
binary and dataset, and measure renderer memory before adopting a larger pool.
This does not change the app's default configuration.
`--sample-process-memory` optionally records launcher/descendant RSS and renderer
counts every 250 ms. It adds overhead and RSS can double-count shared pages;
compare matching sampled runs. Pool experiments also require complete native
renderer diagnostics, so rebuild the test binary after staging current tests.


`--scenario profiles --profiles 4` measures fresh empty profile initialization
inside a warm test process. `--scenario profile-page` adds one restored local page
in the selected profile and requires a first visual paint measurement as well as
window readiness. Both await all remaining profile leases before recording
post-configuration browser-process memory. Neither measures cold full-process startup.



To test repository changes while preserving a baseline build's installed sources,
stage an isolated candidate:

```sh
python3 tools/stage_performance_candidate.py
```

In the Debug build's `args.gn`, add
`root_extra_deps = [ "//zitter_candidate:zitter_prototype_candidate" ]` (merge
with any existing extra dependencies). Then run:

```sh
cd .chromium/src
buildtools/mac/gn gen out/Zitter
buildtools/mac/gn check out/Zitter '//zitter_candidate:*'
../depot_tools/autoninja -C out/Zitter -j 4 zitter_prototype_candidate
cd ../..
python3 tools/benchmark_browser.py --out .chromium/src/out/Zitter --binary zitter_prototype_candidate --label candidate
```

The candidate has its own target, source directory and fixtures, and reuses
existing Chromium component dependencies. Restaging refuses modified/untracked
candidate files and preserves timestamps for identical files to avoid rebuilding
unchanged sources. Finish compilation before measuring the candidate as well.

`--scenario restoration` measures rebuilding a window from a fixed saved session
with `--tabs` pages per workspace, an active split and two warm profiles. It
records ready time, both first-paint notifications, realized page count and
browser-process resident/physical-footprint bytes (the footprint field is macOS
specific). Memory is sampled after load and persistence settle, outside timing.
It does not measure cold process/profile startup or whole-browser memory across
renderer/GPU/network processes.

For a reference using frozen product sources and the same current benchmark,
stage `--variant zitter_reference --reference-overlay .chromium/src/zitter
--current-tests`, add `//zitter_reference:zitter_prototype_reference` to
`root_extra_deps`, and build `zitter_prototype_reference`. Run each binary with
the same scenario, tab count and iterations. Reference and candidate code are
kept separate; no sources of an in-progress baseline build are replaced.

## Test the independent core

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure
python3 tools/check_boundaries.py
python3 -m unittest discover -s tests -p 'test_*.py'
```

## Live Rust + Metal sidebar prototype

The real workspace now has an optional Rust + Metal tab-list renderer, with live
titles, folders, Split View and existing keyboard/controller routing. Live rows
use scene text without hidden Views Labels, and skip native subtree painting
while Metal is visible. Rust owns folder hierarchy/collapse state, tab grouping
and pinned-pair placement, split tile/folder geometry, viewport projection/
clipping, and vertical scrollbar geometry/drag/snap state. The overflow thumb
is drawn on the shared GPU canvas and remains visible. The same Metal surface draws New Tab and its configurable shortcut
hint, the address host/path with GPU icons and the workspace heading without
native text Labels. Rust also lays out and draws the pin/settings footer and
workspace dots/emoji, including horizontal overflow and scrollbar dragging.
Model updates refresh the scene independently of
native painting. Rust also owns row
click/drag/cancellation and platform key activation through native event, focus
and accessibility adapters. See
[build, isolated demo and validation](docs/rust-metal-sidebar.md).
The ongoing [functional parity checklist](docs/rust-metal-parity.md) distinguishes
preserved native features from controls already migrated to Rust.

## Prepare Chromium

Use a dedicated Chromium checkout. Follow the official prerequisites for your OS:

- [macOS](https://chromium.googlesource.com/chromium/src/+/main/docs/mac_build_instructions.md)
- [Windows](https://chromium.googlesource.com/chromium/src/+/main/docs/windows_build_instructions.md)
- [Linux](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md)

Chromium requires a large checkout and build directory. Check disk requirements
before fetching; these commands do not use installed Google Chrome or its profile.
With `depot_tools` on PATH, create the checkout outside this repository (or in the
ignored `.chromium` directory):

```sh
mkdir -p .chromium
cd .chromium
fetch --nohooks chromium
cd src
git checkout --detach 1a732fcc23b20dd0bcf38f35d72c1fe1ec799d35
gclient sync -D
```

`gclient sync -D` is intended for this dedicated checkout; it can remove obsolete
dependency directories. Windows commands require adapting directory creation to
your shell. Use Chromium's own fetched compiler, GN, and dependency revisions.

Recent Xcode versions ship the Metal shader compiler as an optional component.
If `xcrun metal --version` reports a missing toolchain, install it before building:

```sh
xcodebuild -downloadComponent MetalToolchain
```

From the Zitter repository, install the overlay. The installer checks the exact
revision, refuses to replace unowned or locally modified overlay files, copies
our sources into `src/zitter`, adds **one dependency** to desktop browser_tests, and gives macOS OSCrypt
its own Zitter Keychain service/account names.
It does not stage or commit changes.

```sh
python3 tools/install_chromium_overlay.py .chromium/src --dry-run
python3 tools/install_chromium_overlay.py .chromium/src
python3 tools/install_archive_backend.py .chromium/src
```

From Chromium's `src` directory:

```sh
gn gen out/Zitter --args='is_debug=true is_component_build=true symbol_level=0 blink_symbol_level=0 v8_symbol_level=0 use_siso=false use_remoteexec=false'
gn check out/Zitter //zitter:profile_workspace_browser_tests
autoninja -C out/Zitter -j 1 generate_bindings_all
autoninja -C out/Zitter -j 8 zitter_prototype
out/Zitter/zitter_prototype --gtest_filter=ZitterProfilesBrowserTest.StorageIsolationAndSwitching --test-launcher-jobs=1
```

Windows uses `out\Zitter\zitter_prototype.exe`; Linux requires a graphical session
or the normal Chromium test Xvfb setup. macOS requires the SDK/Xcode specified by
the pinned Chromium revision. The dedicated `zitter_prototype` executable uses Chromium's browser-test runner
without building the entire upstream test collection. This is a desktop target,
not Android or ChromeOS.

Generate Blink bindings separately on the first local build: each per-type
generator creates its own CPU-sized Python worker pool. Running many of these
actions concurrently can oversubscribe the machine even with a modest Ninja
`-j` setting. The `-j 1` step above still uses the generator's internal worker pool.

## Open the manual prototype

From this repository, after building the local checkout:

```sh
python3 tools/run_prototype.py --verify
python3 tools/run_prototype.py
```

Or invoke the executable from Chromium's `src` directory:

```sh
out/Zitter/zitter_prototype --gtest_filter=ZitterProfilesBrowserTest.ManualWindow --zitter-interactive --single-process-tests --enable-pixel-output-in-tests --test-launcher-timeout=3600000
```

Close the Zitter window to finish the test. Test profiles are temporary and
managed by the harness; the manual run is not a persistent daily browser.

- Click Work / Personal or use Cmd+Alt+1 / Cmd+Alt+2 on macOS,
  Ctrl+Alt+1 / Ctrl+Alt+2 on Windows/Linux.
- Both workspaces open the same test-server URL. Save `work` in one and `personal`
  in the other. Read values after switching repeatedly.
- The output order is `cookie|localStorage|IndexedDB|sessionStorage`.
- The address field accepts HTTP(S) URLs. It is a probe control, not a complete
  omnibox or trustworthy security-origin display.

The automated test also destroys and recreates both page hosts: cookie,
localStorage, and IndexedDB values should remain; sessionStorage should disappear.
This is **not** a test of persistence across browser-process restarts.

## Layout

```text
zitter/core/          product model, no Chromium imports
zitter/ports/         runtime contract
zitter/application/   selection commands and failure handling
chromium/prototype/   Chromium profiles and Views window
chromium/BUILD.gn     GN targets installed as src/zitter/BUILD.gn
chromium/revision.json exact upstream revision
testdata/            same-origin storage diagnostic page
tools/               boundary check and guarded overlay installer
tests/               independent contracts and installer checks
docs/prototype.md    ownership, experiment scope and acceptance criteria
```

For the controlled connection experiment, build the staged current test target
and run `tools/benchmark_browser.py --scenario preconnect` with and without
`--preconnect`, using the same binary and sampling options. It models 120 ms
TLS/HTTP round trips with a fixed 600 ms typing pause in both variants; DNS and
TCP accept are local. Results require one reused connection, one page request,
and no speculative page request per trial. See the performance log for the
current experimental/final verification state.

### Optimized local build (macOS ARM64)

`chromium/optimized.args.gn` enables non-component linking, ThinLTO and Chromium's
pinned mac-arm PGO profile. Our regular executable explicitly enables LTO O2;
the native test executable uses the same backend level. The comparisons and
current acceptance status are in [the performance report](docs/performance.md).
This is a locally signed development bundle; it is not a notarized release installer.

After preparing the pinned checkout, stage the current source without replacing
the preserved original overlay:

```sh
python3 tools/stage_performance_candidate.py --variant zitter_optimized
mkdir -p .chromium/src/out/ZitterOptimized
cp chromium/optimized.args.gn .chromium/src/out/ZitterOptimized/args.gn
cat >> .chromium/src/out/ZitterOptimized/args.gn <<'EOF'
root_extra_deps = [
  "//zitter_optimized:zitter_optimized",
  "//zitter_optimized:zitter_prototype_optimized",
]
EOF
cd .chromium/src
export PATH="$PWD/../depot_tools:$PATH"
python3 tools/update_pgo_profiles.py --target=mac-arm update --gs-url-base=chromium-optimization-profiles/pgo_profiles
python3 v8/tools/builtins-pgo/download_profiles.py download --depot-tools ../depot_tools --check-v8-revision
buildtools/mac/gn gen out/ZitterOptimized
buildtools/mac/gn check out/ZitterOptimized '//zitter_optimized:*'
autoninja -C out/ZitterOptimized -j 1 generate_bindings_all
autoninja -C out/ZitterOptimized -j 8 zitter_optimized
cd ../..
python3 tools/package_app.py --out .chromium/src/out/ZitterOptimized --binary zitter_optimized --destination build/Zitter.app
open build/Zitter.app
```

The first full build and LTO links take substantially longer than an incremental
development build. Keep the output directory/cache for subsequent builds. PGO
uses the profile pinned by this Chromium revision, not a locally trained Zitter
profile; the measured compiler comparison changes linking, LTO and PGO together.
