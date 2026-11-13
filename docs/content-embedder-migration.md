# Content embedder migration

Zitter will move its browser backend to Chromium's public Content API. The
current Chrome-backed application remains the baseline until the replacement
passes the same product and security checks on macOS and Windows. Builds run
locally. Use a dedicated Chromium checkout at the revision in
`chromium/revision.json`, a dedicated output directory, and disposable profiles
throughout development.

| Milestone | Acceptance criteria |
| --- | --- |
| macOS bootstrap | The parallel `//zitter_content:zitter_content` target has no transitive `//chrome` GN dependency. The lab app creates an ephemeral context, displays a page, and runs JavaScript in a separate renderer. Verify helper launch and sandboxing. |
| Product integration | Host the existing Zitter shell and workspace model on Content, retaining the compositor and Metal paths. Verify two persistent profiles and private storage isolation, navigation and restoration. |
| Browser services | Wire permissions, file and capture pickers, downloads, certificate handling, Safe Browsing, DevTools, history, and printing as needed for current behavior. Reuse suitable Chromium components. Preserve request blocking, document-start scriptlets, and fingerprint protection. |
| Data and compatibility | Verify representative extensions, password storage and autofill with the OS key store, and persistence across Content updates. Check that the private context leaves no persistent browsing state. |
| Windows and cutover | Bring up the Windows launcher and sandbox. Validate packaged helpers, installer and update behavior on both platforms, then switch the production target after parity checks pass. |

## Current cutover status

The production entry point still uses `//chrome`. The parallel macOS
candidate runs the shared Workspace UI in a relocated arm64 bundle, with
compositor and Metal. The standalone macOS GN graph has zero labels under
`//chrome`. Development uses a separate checkout, output and disposable
profile roots.
The macOS build and `--plan` check both the transitive GN targets and
Ninja's declared inputs. The merged Chromium 157 graph has 13,756
dependency targets and only two declared inputs under `chrome/`, the
Chromium version and branding metadata. Other inputs under that directory
fail the build check. The package records the observed metadata inputs.
The resumed macOS build passed the same dependency audit. The full
Python unit suite under Python 3.14 ran 230 cases with 28 skipped and
no failures.
The first Content release is a clean-install cutover. A manual install
with a fresh profile satisfies the initial delivery requirement. On macOS
the Content app starts in its own Application Support directory.
The packaging tests check the production `macos-content` artifact name
alongside the legacy and rehearsal names. The packaging and notarization
unit suites passed with 22 and 12 tests respectively.
The storage migration verifier and profile-trial unit suites passed 57
tests. These exercise fixture and validation code ahead of the packaged
Content-to-Content update trial.
Existing Chrome-backed installations are disposable for this cutover.
The first Content launch can leave their profiles untouched. Chrome
profile import, cross-backend rollback and automatic delivery from the
Chrome-backed version are outside the cutover acceptance criteria.
Release acceptance covers startup from the clean, signed package with
the system credential store, then delivery of a subsequent Content
build through the ordinary update channel. The update must preserve
Content profiles, passwords, and other stored data. Native Windows
delivery is owned by a separate session. This workstream covers macOS
and the shared implementation.

| Area | Evidence | Before cutover |
| --- | --- | --- |
| Workspace and browser services | Relocated macOS runs cover navigation, session restoration, pins and folders, split panes, profile and private-window separation, DevTools, printing, site permissions, certificate handling and fingerprint protection. Signed macOS trials cover the native save panel, selected HTTPS document and media types, quarantine, and interrupted-download recovery. Ad blocking covers the shield diagnostics, tab-scoped rule trials, and approval for saving and replacing profile rules with cosmetic updates on loaded pages. Assistant probes cover an approved, document-scoped ad blocking diagnosis, profile-scoped tab discovery, opening, navigation and closing, page reads, screenshots, editing and consent for clicks and Enter. | Verify the production reputation provider and live Assistant provider authorization. The signed 0.4.11 full-display trials passed. |
| Extensions | Selected signed packages pass installation, update, restore, and profile-scoped Settings removal tests on macOS. Ordinary startup restores approved packages and skips damaged ones. The complete published uBO Lite package blocked an ad request and hid a matching page element in a loopback trial. Bitwarden filled both fields from its popup and inline suggestion on the first restored Work tab. Personal stayed empty. | Include the verified extension implementation in the next notarized package. |
| Profiles and credentials | Disposable macOS trials cover two encrypted profile stores, an empty private context, password autofill, profile-scoped password deletion and virtual WebAuthn. The 0.1.3 to 0.1.4 replacement preserved both profiles with ad hoc outer signatures and a file Keychain. A separate signed fixture verified password autofill across those versions. | Verify password autofill across a system Keychain replacement and validate native passkeys with the managed browser entitlement. Signed system Keychain startup and cookie persistence passed. |
| Windows | The launcher, packaging and migration paths have implementation and fixture coverage. Some Windows sources compiled to x64 COFF. The macOS cross-toolchain generated a 50,470-action Windows x64 plan with zero `//chrome` dependency labels and linked a standalone SDK probe. | Build and run the packaged app on Windows. Exercise the sandbox, installer, updates, native credential services and DPAPI persistence on a clean installation. |
| Release | The 0.1.1 through 0.1.4 macOS arm64 rehearsal DMGs passed notarization and Gatekeeper. The apps carry the production identifier. The 0.1.4 image uses the Content update asset name. | Deliver a subsequent Content build through the ordinary update channel and verify both profiles survive. The signed 0.4.11 replacement with the system Keychain passed. Native Windows installation is tracked separately. |
| Local build cost | Matched macOS non-component builds completed 57,951 Chrome actions and 43,834 Content actions. Content used 22.9% less measured active build time in this run. A separate fingerprint channel narrows edits to its key implementation. | Repeat the cold timings with the machine awake and idle and compare equivalent release builds after feature parity. |

The native Windows checkpoint is tracked separately. It starts with a local
x64 build and the package verifier in a Windows GUI session. Use disposable
profile roots to check the renderer sandbox, Workspace and DPAPI. On macOS,
continue using the relocated bundle for parity work and verify clean startup
from the release-signed installation. Check that subsequent Content
updates retain the resulting profiles. At the current macOS checkpoint,
the console account already has a Content profile root, and the
configured Safe Browsing credential file is absent. The production
trials require a disposable desktop login and a provider credential.

## Main integration checkpoint

An isolated Git merge preview compared the current migration snapshot
with remote main at `0e1ebd8` (Zitter 0.4.10). Main has advanced 21
commits from the migration base `001d99f`. The preview reported eight
content conflicts in the GN configuration, Assistant panel and host,
browser data, extensions bar, and Workspace shell. The preview left
both worktrees and their branches untouched.

A later read of the remote branch confirmed the same main revision.
A fresh snapshot of the migration, including the completed Night Shift
review trial, produced 14 conflicting files. The integration rehearsal
opened that snapshot in a detached worktree and resolved 12 of those
files. The resolutions preserve the Content Night Shift assets, combine
the browser data declarations and menu dispatch, and keep the shared
fingerprint toolbar alongside main's Night Shift and Assistant entries.
The last two conflicts were resolved by keeping the panel on
AssistantHost and adding host methods for the active document, settings
and page actions. The shared settings codec now persists the per-profile
shortcut grant masks and Content handles the existing command to clear
them. The combined sources built on the earlier Chromium pin in the
isolated migration output. GN header checking passed and the graph had
13,757 dependency labels with zero under `//chrome`. The combined app
passed the ordinary-startup Night Shift Work, Personal and restart
trial. The Assistant consent rehearsal also passed on the earlier pin:
with the default page grant disabled, the Night Shift shortcut asked for
access, remembered the explicit grant for Work, and submitted the
restricted request to the local fixture provider. On restart the grant
was used without a new dialog and the default remained off. Provider
availability refreshes now preserve explicit session grants while
changes that narrow a default still revoke access. The activation hook
was confined to Chromium staging. The integration worktree still needs
a build and regression run on the new pin before a merge commit is used
for cutover.

The integrated import now commits Arc favorites to the destination
profile's shared favorites list and publishes them only after the
catalogue write succeeds. A durable `--arc-import` fixture exercised
preview, approval, restart and repeat import on macOS with two
disposable Arc profiles. Each profile kept one distinct favorite.
The favorites were not duplicated into individual spaces and the
fixture source remained byte-for-byte unchanged. The existing
Chromium import trial also passed its checks for source immutability,
profile mapping, encryption, and preservation of existing rows.
These trials used the earlier Chromium pin. The password CSV action
in main is part of the Windows welcome wizard and belongs to the
separate Windows port.

The Content session store now retains GitHub live-folder metadata and
its cached Arc pull-request children. Existing session files without the
optional metadata are accepted. The Arc fixture now checks that the
folder and cached child survive restart and repeat import while profile
favorites remain separate. The expanded fixture passed on the earlier
Chromium pin after updating the preview expectations to include one
GitHub folder and its cached pull request. The trial on Chromium 157
is pending. The Content Workspace now wires folder creation
and editing for persistent profiles. Token writes use OSCrypt with the
destination profile, workspace, and folder IDs inside the encrypted
payload. Pending writes check that the profile and original folder still
match before applying changes. The Content host implements manual and
five-minute refresh for the active workspace. It verifies the encrypted
scope before adding an authorization header, uses the owning profile's
network partition, omits cookies, rejects redirects, and cancels stale
work on workspace or configuration changes. Cached pull requests retain
their pin IDs when possible. The local `verify_content_github_folders.py`
fixture exercises the real request path using disposable profiles and a
loopback API. Only synthetic fixture credentials can be sent to that
endpoint. The trial checks creation, a manual refresh after restart,
stable pin IDs, token replacement and removal, and rejection of a
ciphertext copied from Work into Personal.
The complete loopback trial passed with the locally linked macOS app on
the earlier Chromium pin. It verified that Personal emitted no API
request with the copied ciphertext.

The first Chromium 157 macOS arm64 build completed all 43,877 Ninja
actions locally. On the same Chromium revision, the initial merged
snapshot `7a55a20` passed GN header checking and its transitive graph
contained 13,756 dependencies with zero targets under `//chrome`.
The only files read under `chrome/` were `chrome/VERSION` and
`chrome/app/theme/chromium/BRANDING`. The merged runtime regression
uses a later snapshot and must repeat the audit for that snapshot.

The first Chromium 157 runtime pass verified persistent profiles and
OSCrypt password storage, form autofill, docked DevTools, and Workspace
downloads. The private-window probe failed at native menu dispatch
after both private windows shared the expected storage. The encrypted
import probe reported that the native menu had neither a key nor a main
window. The fingerprint probe reported that the Work popover had not
closed during a profile switch. The test menu now activates the app,
and the fingerprint toolbar hides its client-owned bubble before
requesting asynchronous destruction. The first repair build stopped at
the macOS 13 deployment check because the probe used an AppKit activation
method introduced in macOS 14. The probe now uses
`NSRunningApplication.activateWithOptions:`. The replacement snapshot
passed GN header checking and the graph again had 13,756 labels and zero
`//chrome` targets.
A host restart interrupted the first run during the adblock migration
check, before it returned a result.
On the next Chromium 157 build the private-window, cross-process
fingerprint, and adblock timing and migration probes passed. The
encrypted import probe still reached the native menu before AppKit
reported a key or main window. It now retries the native menu command
for up to four seconds before reporting failure. The signed extension
registry trial passed on the same Chromium 157 build. It verified
same-key updates, profile-scoped approval and removal, optional
permission persistence and revocation, and rejection of changed
archive bytes and oversized archives.
The native menu readiness retry passed the encrypted browser import
probe on Chromium 157. The fixture checked that the Keychain lookup
followed confirmation, rejected invalid source data without publishing
profiles, and restored both imported cookie values in the destination
profiles after restart.

The macOS Chromium 157 suite passed all 14 checks on Zitter snapshot
`1333e04222c9e516b47d1d627a33b8942e663177` and Chromium
`9a31f0acec1854d44e49a73fbf8017598151a583`. It covered profile
persistence, password storage and autofill, private windows, encrypted
import, cross-process fingerprint protection, DevTools, downloads,
adblock timing and storage migration/rollback, Night Shift, the
first-launch import offer, zoom, Arc import, GitHub folders, and signed
extensions. GN header checking passed, with 13,756 transitive dependency
labels and zero `//chrome` targets. The local 0.4.11 rehearsal passed
Developer ID, timestamp, and Hardened Runtime checks on 11 Mach-O paths.
The signed, relocated bundle also passed Workspace, DevTools, sandbox
override rejection, profile isolation, and the private-window lifecycle.
Apple accepted the signed application under submission
`3bc8e3f1-c5ba-45dc-a171-10987764c3c8`. The ticket was stapled and
Gatekeeper accepted the application as Notarized Developer ID. Apple
accepted the arm64 DMG under submission
`9f6693cd-8d0d-4107-946b-c800feb9af18`. The stapled disk image,
mounted app, and quarantined copy passed distribution checks. The
image SHA-256 is `75c1019258313948de4adf1832452186b142d95c4a075e0cf8ad2d7afa16553a`.
The relocated DMG app passed runtime verification with its Developer
ID seal intact, including the signed extension registry and sandboxed
extension utility checks. The signed 0.1.6
and 0.4.11 images passed replacement preflight with matching designated
requirements. The isolated file Keychain replacement preserved Work
and Personal sessions, encrypted cookies, local storage, and both
Shared Dictionary network stores. The runtime replacement used ad hoc
outer signatures to load the fixture. On 2026-11-12, the signed
0.1.6 to 0.4.11 replacement passed on the disposable `tester` account
using its system Keychain. Both notarized bundles retained their
Developer ID signatures. Work and Personal restored their tabs,
local storage, and encrypted cookies, and both Shared Dictionary
stores remained inside their profile network directories. The system
trial did not exercise password autofill. Delivery through the
ordinary update channel remains open.

A separate password upgrade probe used both notarized DMGs with their
original Developer ID seals and an isolated OSCrypt test key. The 0.1.6
app saved a Work credential. The 0.4.11 app autofilled it without
rewriting its ciphertext and retained the existing password prompt
regression coverage. System Keychain password persistence remains
unverified.

The macOS release candidate was rebuilt from `76c5fab` to include the
English label for the Assistant sessions button. The full Python suite
had 202 passing tests and 28 skips. GN header checking passed with zero
`//chrome` targets. The signed runtime probe passed and Apple notarized
the app and DMG. The rebuilt DMG passed the distribution checks and
the signed replacement preflight with 0.1.6. Its SHA-256 is
`f5798a90195b2a9331fdd1d67c70b7ef391c6e96daf4bf055eb8744d85b3504f`.
The final DMG also passed the signed extension registry, sandboxed
extension utility, and private-window probes. Its replacement trial
with the file Keychain preserved both encrypted profiles and Shared
Dictionary stores. The system Keychain and full-display trials used
`1333e04`.
The update metadata probe selected the rebuilt DMG for Content 0.1.6
from a local GitHub-format catalogue and rejected the earlier image's
digest and size when checking the rebuilt artifact.

The updater metadata probe selected the exact notarized 0.4.11 image
for Content 0.1.6 on arm64 using a local GitHub-format catalogue.
It skipped newer prerelease, Chrome-backend, and Windows-only entries,
verified the image digest and size, and rejected wrong architecture,
backend, installed version, digest, and size. The probe used the same
metadata sources as the release snapshot and did not download an asset
from the public feed.

The notarized 0.4.11 DMG passed the synthetic camera and microphone,
tab and synthetic desktop capture, and native macOS picker cancellation
probes in dark Metal. Picker cancellation did not select a source.
On 2026-11-12, four native full-display runs on the disposable
`tester` desktop passed against the notarized 0.4.11 bundle. Each
delivered a video frame and verified track termination after Stop.
Passkey preflight on the same
signed bundle reported that the Apple-managed
`com.apple.developer.web-browser.public-key-credential` entitlement
is absent, so native passkeys remain unverified. The installed
Chrome-backed Zitter bundle also lacks that entitlement and an
embedded provisioning profile. The signature comparison establishes
that the missing entitlement is shared by both bundles. It does not
establish native passkey behavior on either backend.

The notarized 0.4.11 DMG passed the HTTPS loopback reputation trial,
including URL and artifact blocks, resumed requests, and quarantine
for allowed user saves. The production identity rejected the standalone
HTTP artifact fixture at startup. The unbranded Chromium 157 bundle from
the same source snapshot passed all ten standalone artifact scenarios,
including altered bytes and malformed or missing catalogues. A live
reputation provider was not configured on the test Mac.

The 0.4.11 DMG passed Assistant settings and encrypted test credential
persistence on a loopback provider. The same Chromium 157 source
snapshot passed the legacy Assistant migration and rollback fixture in
the unbranded lab after the fixture accounted for the new
`shortcut_access` settings field. The Developer ID bundle rejected
the injected file Keychain fixture. Live provider authorization remains
unverified.

On first ordinary macOS launch, Content now opens the shared browser
import panel. A cancelled process can reopen the pending offer, and a
dismissal or successful catalogue commit clears it. Existing sessions
without the pending marker continue directly into the Workspace.
Imported spaces are added alongside the two built-in Work and Personal
spaces. The argument-free startup fixture passed on the earlier pin.
It reopened a pending offer, dismissed it, and checked that a third
launch bypassed the panel while keeping both built-in profiles. It used
an isolated file Keychain and an import path inside the temporary root.
The first merged Chromium 157 snapshot also completed its 48-action
incremental build.

Content now handles the page zoom keys and Chromium's zoom gesture
callback with main's zoom steps. Configured Workspace shortcuts retain
priority. The macOS `--page-zoom` trial verified zoom in and out, the
gesture callback, persistence for one Work site, reset to Work's
default, and separate Personal and private settings. It sent keyboard
events to the WebContents delegate and called the gesture callback
directly. The first run passed the zoom checks and stopped in the
private native-menu focus exercise. The zoom trial now uses its
existing direct private-window path, while the separate private-window
trial still exercises native menu dispatch. The final zoom trial and
GN header check passed on the earlier pin with zero `//chrome` labels.

Main also pins Chromium `9a31f0acec1854d44e49a73fbf8017598151a583`
(157.0.8103.39); the migration trials used
`1a732fcc23b20dd0bcf38f35d72c1fe1ec799d35`. The integrated candidate
needs a local build against the new pin and a fresh dependency audit.
The integration retains the Content component extension and profile
settings code. Verify its private-window policy, per-site settings,
page zoom, contrast repair consent, the Assistant and extension popup,
and the browser import paths on the merged build. Re-run packaging and update checks with a version newer
than the published release at cutover.

The Chromium 157 staging rehearsal fetched the exact new pin in a
separate source checkout and ran the existing staging script from an
isolated migration snapshot using the new revision declaration. The
patch and overlay stage succeeded. The initial dependency sync refused
the staged changes, so the trial stashed its generated tracked edits
before starting the pinned sync and queued staging again after hooks.
Pinned dependency sync and hooks completed, and the overlay staged
again successfully. GN generated 36,125 targets. The Content graph
contains 13,754 dependencies with zero `//chrome` labels. The only
`chrome/` inputs are `chrome/VERSION` and Chromium branding metadata.
GN header checking passed for the browser framework. The cold Ninja
plan contains 43,877 actions. The local build started in its separate
output, and the current release output still uses the previous pin.

The six Night Shift policy tests passed against main's source. A
disposable CRX containing the same extension code, signed with a
throwaway key, also installed from Content Settings and started its
MV3 module worker in Work. Personal had no installation directory.
The first worker trial timed out because the worker-only probe
required initialized static user-script memory even though Night
Shift declares no static scripts. The probe now checks worker
readiness directly in that mode. Script-dependent probes still wait
for their loader. This trial covers the existing user-extension
runtime. The built-in identity, component lifecycle, dynamic script
activation and Workspace controls still need integration coverage.

A follow-up CRX used the same Night Shift code plus a test-only
startup listener that enabled it in Work and reported registration.
Content registered both persistent scripts in their declared MAIN
and ISOLATED worlds. A loopback page observed Dark Reader styling
in Work and stayed light in Personal. After restarting into Work,
the page was styled again and the worker reported the stored enabled
state and both registrations. The initial trial read those two
reports in the wrong order. Waiting for both made the test pass.
The Workspace controls remain open. Private windows must remain
outside the built-in extension lifecycle.

The migration now stages main's Night Shift runtime files with the
original manifest and vendor license. The macOS Content target places
them in the framework Resources directory. The Windows target declares
copies under the runtime resources directory. On the macOS pin used
for the integrated lab, GN generated both resource targets and listed
13,757 transitive dependencies with zero `//chrome` labels. All six
Night Shift policy tests passed with the imported sources. Ninja
materialized all 15 runtime files in the macOS framework, and every
file matched the staged source byte for byte.

Ordinary macOS startup now configures Night Shift from the framework
bundle, verifies the manifest-derived ID
`kdpnmnbnedpcomicojommdikkdepclio` and creates a component extension
instance for each regular context. Component state is separate from
the managed user-package list. Private contexts skip component
registration. Two ordinary launches with disposable profiles and a
file Keychain fixture verified worker registration on disk in both
Work and Personal under the expected ID and with no user installation
directory for Night Shift. The ordinary extension regression also
passed, including Work-only messaging, persisted state, disabled
state, and handling of a damaged archive.

The Content workspace now reads and persists the Night Shift profile
configuration with main's field names and bounds, and publishes the
effective settings to each regular profile's extension storage. The
component worker receives its startup event so it can restore dynamic
scripts and apply them to open tabs. New regular profiles receive the
same defaults and scope rules when created. The local arm64 build
passed the following ordinary-startup test with disposable profiles
and a file Keychain. The first restored Work tab became dark,
Personal remained light, and Work became dark again after restart.
The seeded profile selection and brightness setting survived all
three launches. The dependency graph remained at 13,757 labels with
zero `//chrome` targets.

```sh
python3 tools/verify_content_night_shift.py \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

The macOS Night Shift menu opens the bundled settings page with the
current profile list, configuration, theme and language. Content
accepts settings changes only from the committed bundled options
document opened by the menu, filters removed profile IDs, and writes
through the same session and profile storage used at startup. The
local build and GN include check passed, and the dependency graph
remained free of `//chrome` targets. The native menu trial opened and
rendered the profile controls in a disposable ordinary-startup app.
The disposable UI trial invoked the real menu item and activated the
settings page's enable control in the renderer. The page's fragment
update reached the host, and the session file recorded the enabled
value. The renderer activation hook was confined to the isolated
Chromium staging tree. Native pointer delivery was outside this test.

The page context menu now exposes Night Shift site modes for HTTP
and HTTPS documents in regular profiles. It keeps the storage key
scoped to the site and profile and discards the write if its original
document is gone. A disposable macOS trial invoked the Darken site
command through the actual menu model with Night Shift disabled at
profile level. The loopback site became dark in Work, remained light
in Personal, and became dark again in Work after restart. The trial
hook was applied only to Chromium staging.

A separate ordinary-startup macOS trial confirmed that the loopback
site became dark in Work with Night Shift enabled. The app then
opened a private window from Work and loaded a different path on the
same origin. The private page stayed light throughout the observation
window. Browser-side assertions also verified an off-the-record
context, no enabled Night Shift extension resources, and no access to
the site-mode action. The trial hook was applied only to Chromium
staging.

The page context menu also opens the bundled settings editor for the
current site's brightness and contrast. The editor updates only those
fields in the active profile and resets them to inherited defaults
without removing the mode or any repair. A disposable ordinary-startup
trial opened the editor through the real menu model and exercised
its controls in the renderer. Work saved brightness 113% and contrast
87%, Personal showed its own 100% defaults on the same origin, Work
restored both values after restart, and Reset removed both overrides.
The trial instrumentation was confined to Chromium staging.
The site settings editor can now review a staged repair, compare it
with the saved appearance, discard it, explicitly save it for the
site, and undo the last save. The worker targets the initiating
document by extension document ID. Preview changes expire and the
editor requests cleanup on close. In a disposable macOS trial a
synthetic draft changed the fixture caption color without writing
the site record. Before restored the old color. Discard removed the
draft. Save persisted it across a restart, and Undo removed it.
The review test seeded the draft in extension session storage, with
no Assistant provider involved.

The site menu now also starts a restricted Assistant request for the
visible document. The browser samples computed colors in its isolated
world, keeps selectors local, and exposes only numeric target IDs and
computed colors. Proposals accept a fixed set of color properties and
bounded color values. A successful proposal stages a draft in the
extension's session storage for the originating document and consumes
the request ID. The Assistant turn disables web access, background
execution, and the other browser tools.

An ordinary-startup macOS trial used the real Assistant panel and
native helper with a disposable loopback Ollama-compatible server. The
server verified that the request carried only the page permission and
an empty browser context. A generic read-page call was rejected. The
restricted inspect call returned target IDs without selectors, and the
following propose call reported a staged draft. The fixture's first
response omitted the required answer field and used object arguments.
Encoding the provider's documented response shape made the retry
pass. Menu activation was instrumented in Chromium staging only.
A combined macOS trial then followed the same Assistant-produced
draft through the real site menu and Night Shift review page. The
preview changed the fixture caption color without a persisted repair.
Before restored the old color, Save persisted the repair, and Undo
removed it after an ordinary restart. The trial used a deterministic
loopback provider and disposable profiles. Its hooks triggered the
menu and page controls and observed the storage and rendered color
without seeding a draft. A live model quality trial and Windows
startup wiring remain open.

## Representative MV3 extensions

These publisher packages are compatibility fixtures for Content's
extension host. Zitter ships its own password manager and autofill,
which have separate verification. The Bitwarden trial exercises
extension messaging, grants, renderer isolation and form filling.
Bitwarden is installed as a third-party extension in disposable test
profiles. It is outside the bundled extension set and has no role in
Zitter’s built-in password manager.

The compatibility audit used publisher release ZIPs for Chromium. The
measurements are for the release ZIPs before CRX signing. The installer
accepts at most 32 MiB per CRX, 2,048 entries, 96 MiB of expanded
contents and 16 MiB per file. The utility enforces the remaining
expanded-size budget during extraction. The browser independently
validates the returned sizes and paths.

| Extension | ZIP bytes / entries / expanded bytes / largest file | Compatibility gaps visible in the published manifest |
| --- | --- | --- |
| [SponsorBlock 6.1.7](https://github.com/ajayyy/SponsorBlock/releases/tag/6.1.7) | 1,845,183 / 202 / 4,651,654 / 436,740 | Action popup, options, web-accessible resources, `scripting`, YouTube subdomain scopes and an optional wildcard host scope. |
| [Dark Reader 4.9.135](https://github.com/darkreader/darkreader/releases/tag/v4.9.135) | 850,821 / 88 / 3,217,605 / 842,042 | Action popup, options, commands, `scripting`, `fontSettings`, optional `contextMenus`, and all-site script and host scopes. |
| [uBO Lite 2026.1110.1816](https://github.com/uBlockOrigin/uBOL-home/releases/tag/2026.1110.1816) | 9,081,087 / 1,029 / 32,883,766 / 2,166,597 | Needs declarative network rules, `userScripts` and offscreen documents. The package also uses `activeTab`, `scripting` and all-site access. |
| [Bitwarden 2026.10.2](https://github.com/bitwarden/clients/releases/tag/browser-v2026.10.2) | 25,759,183 / 277 / 92,823,716 / 13,868,896 | Declares an action, side panel, commands, offscreen documents, clipboard, request/auth handling, optional native messaging, and broad HTTP, HTTPS and file script scopes. |

A runtime trial used the notarized 0.1.4 macOS arm64 app directly from
its read-only DMG. SponsorBlock and Dark Reader release contents were
repacked as CRX3 with disposable developer keys. The package's manifest
received only the corresponding public key. Both packages reached the
sandboxed extraction utility and were rejected by manifest validation
before preview or installation. The trial used separate temporary
profiles and did not run any extension code.

A later Developer ID build installed a signed fixture with over 92 MiB
of expanded contents and 1,050 extra entries, then restored and updated it
in the regular profile. The registry suite ran with light compositor and
dark Metal. It rejected signed packages exceeding each of the four size
limits before publishing an installation. The browser also checks
required and optional API declarations before handing the manifest to
Chromium. This prevents the manifest parser from aborting when the core
permission registry recognizes an API whose feature is absent in Content.

The same build extracted the publisher's uBO Lite and Bitwarden
Chromium packages in the sandboxed utility. The trial verified the
publisher asset digests, repacked the files with disposable CRX3 keys,
and used separate temporary profiles. Both packages were rejected for
unsupported manifest capabilities before installation. An earlier
Bitwarden run aborted when Chromium parsed its clipboard permission.
The preflight check changed this to a normal rejection.

SponsorBlock has the fewest missing capabilities among the tested extensions. Its archive
fits the existing limits, and its required site scopes are confined to
YouTube and the service endpoint. Zitter opens supported packages' options pages from the owning profile's
Settings. The host also opens declared action popups in a separate
panel. Trials with the published SponsorBlock package covered live
YouTube playback and a completed automatic skip. The host admits
web-accessible subresources for declared HTTP and HTTPS sites.
The larger packages require additional browser APIs before activation.

The Content host accepts Chromium's `options_ui` and `options_page`
declarations for extension-owned resources. The signed install review
shows the path. Settings exposes the Options action when the package is
enabled in the active profile. It opens a tab in that profile. The
navigation throttle requires the exact declared URL, a browser-initiated
primary main-frame navigation, and an enabled extension in the target
context. Renderer-initiated navigation and redirects to the options page
are blocked. The browser opens the options page as a tab, including
manifests that request inline presentation. Developer ID trials in light
compositor and dark Metal checked that another profile hid the action, opened the
page from Settings in the owning profile, and confirmed its extension
origin, dedicated renderer and access to the extension's local storage.
The test hides the reusable Settings widget between steps so it can
reopen after the earlier permission dialog trial. The same signed suite
also verified that the extension worker can call `runtime.openOptionsPage`
and reuse its existing options tab. The browser resolves the destination
from the calling extension's verified manifest and profile.

The installer accepts an MV3 `action` with an enabled default state and
a valid, extension-owned `default_popup`. The review shows the popup
path. Settings opens it as a dedicated extension popup in the owning
regular profile. The popup navigation gate admits only a browser-started
primary frame load of that exact URL. The panel closes when the active
Workspace changes or the browser opens the extension's options. Toolbar
placement, dynamic `chrome.action` methods and actions without a
declared popup remain open. The Developer ID registry suite passed in
light compositor and dark Metal. It opened the panel through Settings
and verified the popup document's origin, dedicated renderer and
extension-local storage in the owning profile. The English strings
compiled separately and the signed relink reused the prior Rust library.

The Content host accepts static and dynamic `web_accessible_resources`
for named HTTP and HTTPS site patterns. Dynamic entries can also declare
`<all_urls>`. The install review shows the paths, eligible sites, and
whether the URLs are dynamic. A regular web frame receives an extension
resource factory bound to its browser-known origin. For each GET, the
browser checks the live frame, committed origin, request initiator and
enabled extension in that profile. Chromium generates the dynamic host,
resolves it to the enabled extension, and checks the declared path and
audience. The embedder permits the GUID redirect only to the same
resource on that extension's canonical host. An unattended update cannot
add or change a resource exposure entry. Top-level and frame navigations
to these resources and extension-ID audiences remain outside the
supported paths.

Developer ID trials in light compositor and dark Metal read the static
fixture asset and fetched a second resource from the URL returned by
`chrome.runtime.getURL`. They rejected the stable ID for that dynamic
resource, an undeclared path under the dynamic host, and the static asset
from another origin. They also rejected an all-site static resource
declaration and an unattended update that would expose a new file.
GN header checks passed. The English translation source compiled
separately from the signed relink.

The Content host exposes MV3 `scripting.executeScript`, `insertCSS`
and `removeCSS` through Chromium's shared scripting API. Workspace tab
lookup is scoped to the calling regular profile. Chromium's browser and
renderer checks enforce document permissions. Signed macOS trials in
light compositor and dark Metal exercised JavaScript and CSS in an
authorized loopback document and rejected script execution in a tab
without host access and in a tab from a different profile. Dynamic
content-script registration remains open. The regenerated macOS GN
dependency list had 13,755 labels and zero under `//chrome`.

A later trial repacked the complete [SponsorBlock 6.1.7 Chromium
release](https://github.com/ajayyy/SponsorBlock/releases/tag/6.1.7) with a
disposable developer key and the corresponding manifest public key.
The Developer ID build with `chrome.scripting` reached sandboxed
extraction and rejected the manifest before the review. The release
declares a document-start script in the `MAIN` world, a homepage URL,
and dynamic web-accessible resources for `<all_urls>`. The Content host
now admits the homepage metadata and a `MAIN` script with the same
restricted host patterns used by isolated scripts. Review identifies
the page JavaScript environment. Unattended updates must preserve each
script's execution world. Signed fixture runs in light compositor and
dark Metal confirmed the declared script ran in the page world, where
extension storage bindings were unavailable. Both rejected an unattended
update that moved a script from `ISOLATED` to `MAIN`. The English label
compiled separately from the signed relink.

Chromium adds `current_locale` when it resolves the signed manifest's
localized strings. The Content host now accepts that metadata and
retains the rest of the manifest checks. Both renderer runs showed the
localized name in the fixture preview without creating an installation.
With the dynamic resource and localization changes, the complete
SponsorBlock 6.1.7 archive reached the Developer ID build's install
review. The preview reported its popup, options page, script worlds and
resource scope. A subsequent disposable Settings trial installed that
package in Work, started its service worker and observed its local
extension storage directory. Personal had no installation. The trial
used a loopback workspace and disabled external name resolution. It
stopped after worker activation. Its install handler opens an
extension-owned help page through `tabs.create`. The Content host now
allows that API to open the caller's own resources in the owning
profile. A second signed Settings run recorded both Chromium's
browser-side commit and the renderer's provisional load commit for the
published package's `/help/index.html`. The trial used a fresh profile,
loopback fixture pages, and disabled external name resolution.

A separate Developer ID run used the complete publisher package and a
fresh profile, then navigated to a public YouTube watch page without
signing in. Chromium's trace recorded the YouTube renderer commit, the
extension's background worker and both declared content scripts. The
segment lookup API and packaged player icons returned HTTP 200. The
trial establishes script injection and resource and API access on the
live site.

The playback trial installed the same package through Settings with the
Developer ID build, then restored it from disk in a locally ad hoc signed
build of the same framework. The disposable profile opened the public
[YouTube playback sample](https://www.youtube.com/watch?v=wl2IugfRMDk&t=65s)
with audio muted and autoplay enabled for the trial. SponsorBlock's
[segment lookup](https://sponsor.ajay.app/api/skipSegments/c8d4b?category=sponsor)
reported a sponsor segment from 66.715 to 80.91 seconds. In the YouTube
renderer, Chromium completed the initial seek to 65 seconds and then
completed the automatic seek to 80.91 seconds. The earlier install-only
attempt ended before the player had time to produce a completed seek.

The Content browser client now uses Chromium's shared embedder support
for the product string, HTTP user agent and renderer user-agent
metadata. The signed extension registry trial in light compositor and
dark Metal verified that a loopback server received the exact value
exposed by `navigator.userAgent` and that `navigator.userAgentData`
reported the Chromium brand. GN header checking passed. The generated
macOS graph contains 13,755 dependency labels and zero under `//chrome`.

The Content host exposes `chrome.storage.sync` through Chromium's
extension storage frontend, backed by a separate on-disk store for
each extension in its owning profile. Zitter disables account sync in
the Chrome-backed browser, so this namespace persists locally. Chromium
applies its sync byte, per-item, item-count and write-rate limits. The
namespace participates in the normal extension-storage deletion path.
The Developer ID fixture in light compositor and dark Metal checked
that an oversized item was rejected and remained absent, that the sync and local areas held independent data,
and that the sync counter survived restart and a signed version update.
It also checked the owning profile's store path and absence of the
sync store in Personal and the private context.

### Dark Reader compatibility

The Dark Reader 4.9.135 Chromium MV3 archive uses
`chrome.fontSettings.getFontList` with a callback in its popup. The
Content host now exposes that method to privileged extension contexts
with the required permission. It uses Chromium's asynchronous system
font enumeration and returns the font IDs and localized display names.
The signed install review discloses the access. Content scripts and
private contexts cannot call it. The Settings fixture exercised the
callback and checked a nonempty, well-formed system font list in the
owning profile's popup in light compositor and dark Metal. The English
review label was compiled separately from the signed relink.

The host accepts required all-site host permissions and declarative
scripts, including the MAIN world. It compiles `<all_urls>` to
`*://*/*` for those fields, so Chromium applies the grant to HTTP and
HTTPS only. It retains the signed package bytes. Web Store origins are
blocked by the script policy and the network origin access list.
Optional wildcard host requests still require a bounded site selection.
A Developer ID Settings trial installed a signed all-sites fixture.
The MAIN script set a global visible to the page on a second loopback
origin. The worker fetched a response across origins and saved a result
in Work. Personal had no installation. Signed Settings regressions in
light compositor and dark Metal checked that the preview shows the
effective web scope, explicit file and FTP declarations are rejected,
and an unattended update cannot broaden the required host scope.

The host accepts author metadata and checks
`minimum_chrome_version` against the linked Chromium engine. Chromium's
MV3 manifest handler validates an explicit extension-page CSP and its
extension protocol serves that policy. An update that changes the
effective policy requires approval. Settings shows the previous and new
policies for that update. Web Store origin blocks are applied when the
extension is activated, including installation from Settings and restore.
The Developer ID fixture passed in light compositor and dark Metal. It
rejected an invalid or too-new engine version, a CSP allowing
`unsafe-eval`, and an unattended update changing CSP. In the installed
popup, the browser reported a CSP violation and blocked an inline style
under the manifest's custom `style-src 'none'` rule. The English review
strings compiled separately from the signed relink.

A Developer ID trial used the Dark Reader 4.9.135 publisher MV3 archive
with its original CSP and minimum engine version. For this diagnostic
preview, the manifest omitted the commands and optional context menu
declarations and added the disposable signing key. It reached the install
review and showed the parsed CSP.

The Content host now uses Chromium's command parser for MV3 shortcuts
and exposes `commands.getAll` and `commands.onCommand`. The install
review discloses suggested keys. Foreground shortcuts are resolved in
the active regular profile. Zitter's own shortcuts retain priority.
Duplicate extension chords, global declarations and commands without a
suggested key remain unassigned. Named events go to the owning extension
with the active tab metadata filtered by its existing permissions.
Changes to declared commands require update approval. The Developer ID
fixture passed in light compositor and dark Metal. It queried a bound
and an unassigned command, received the named event in the worker, and
opened the popup with an action shortcut after closing Settings. The
suite exercised the registered accelerator binding through the browser
dispatch method. It rejected a shortcut change on the unattended update
path. The English review string compiled separately from the signed
relink.

Signed packages can request `activeTab`. Clicking the extension popup
action or invoking its action shortcut on an eligible HTTP or HTTPS tab
uses Chromium's tab-specific permission granter. It associates the
browser and renderer with the same tab ID. The grant stays in the
regular owning profile. The host excludes the Chrome Web Store and
clears grants on extension disable or tab detachment. Chromium revokes
the grant when the tab navigates to a different origin. Signed light
compositor and dark Metal trials opened the popup by shortcut on an
otherwise ungranted loopback origin. The fixture verified access was
absent before the action, then read the tab URL and executed a script
through the popup. The browser checked that the grant was gone after
the same tab committed a different origin.

Named foreground shortcuts and extension context menu clicks also grant
access before dispatching the worker event. The named shortcut uses the
selected tab, and the menu uses the tab that owns the clicked frame. Media
keys skip the grant. Signed light compositor and dark Metal trials
exercised the action popup, a menu click inside a cross-origin frame, and
a named shortcut on a separate origin. The worker read the granted
top-level URL and injected a script after each of the latter two
gestures. The menu event withheld the cross-origin frame URL.

A second Developer ID preview of the publisher package kept the
original command declarations and omitted only the optional context
menu permission before adding the signing key. It reached review and
showed its suggested shortcuts.

The Content host accepts optional `contextMenus` permission and exposes
`create`, `removeAll`, and `onClicked` in privileged extension contexts.
Enabled extensions can add page entries and one level of submenu items.
Registrations live in the owning regular browser context and survive a
worker restart within that context. Private contexts and Web Store pages
do not expose these entries. A click rechecks the document, active tab,
extension and permission, then filters URL and tab fields through the
extension's existing grants. The Developer ID fixture passed in light
compositor and dark Metal. It selected a child item through the actual
Workspace menu model and observed the worker's result without exposing
an ungranted page URL. Other item types, deeper nesting, update/remove
operations, and persistence across browser restart remain open.

The complete Dark Reader 4.9.135 publisher package reached review and
installed in Work with its original manifest plus the disposable CRX
public key. The worker started and created its local storage. The
first runtime reported an unrecognized `navigate-to` CSP directive, an
unavailable `runtime.setUninstallURL` call and a missing
`chrome.action.setIcon` binding.

The Content host now provides `action.setIcon` for packaged image
paths, plus `setBadgeText` and `setBadgeBackgroundColor`. Chromium's
extension image loader decodes the icon and the action manager stores
the appearance per profile and tab. The browser ignores superseded
icon loads and clears tab-specific state at a new document or tab
closure. Settings displays the current dynamic icon and badge for the
selected tab. The Content resource pack includes Chromium's extension
browser icons, including the action manager's fallback icon. Signed
light compositor and dark Metal fixture runs checked that the action
row displayed the decoded package icon and the requested badge.

With those changes, a Developer ID run installed the original Dark
Reader archive in a disposable Work profile and navigated to a loopback
fixture. The page observed Dark Reader's mode marker and measured a
background change from white to `rgb(24, 26, 27)`. The test disabled
external name resolution. Chromium still warns about `navigate-to`.

The Content host stores `runtime.setUninstallURL` per profile and
extension installation. It accepts HTTP and HTTPS URLs without embedded
credentials and treats an empty value as clearing the record. The Settings
confirmation displays the destination origin. Successful removal opens
the approved URL in a tab owned by the profile that held the extension.
A Developer ID trial with the original Dark Reader archive saved
`https://darkreader.org/goodluck/` and applied the theme again without
contacting that destination. A paired fixture verified profile isolation,
persistence across restart, rejection of invalid URLs, and clearing in
light compositor and dark Metal. The Settings probe confirmed removal,
fetched the loopback uninstall page in the owning profile, preserved the
other profile's installation and worker, and reinstalled the removed
package. It ran in Work with light compositor and Personal with dark
Metal. The signed incremental test bundle reused the prior Rust archive,
so it did not exercise the updated English confirmation text.

### Offscreen documents

Content registers Chromium's native offscreen document API and
`runtime.getContexts`. The owning regular profile manages document
lifetime. The navigation throttle allows the native host to load the
requested extension page and blocks primary redirects. Extension
activation publishes the ready state after the loaded event so that
Chromium can start the document. Offscreen documents report no Workspace
tab or window ID.

The local Developer ID build passed the signed paired-extension probe
in Work with light compositor and Personal with dark Metal. Each run
verified same-profile worker messaging, rejection of external and
duplicate document requests, and context metadata and removal. Disabling
the extension destroyed its document and left the peer profile's
document active. The suite also exercised re-enable, restart, Settings
removal and reinstall. Publisher-package compatibility remains a
separate release check.

### User scripts

The Content host registers Chromium's native `userScripts` APIs and
delegated availability check. Settings exposes a separate opt-in for
each extension and regular profile. The native script store and the
projected runtime preferences persist script registrations, world
configuration and approval. Uninstall clears approval and removes the
extension's stored scripts.

The Developer ID probe passed in Work with light compositor and
Personal with dark Metal. It used the Settings switch, registered and
updated a script, configured and reset a world, and verified that the
script survived extension re-enable and browser restart. The peer
profile's worker had no `userScripts` API. The suite also passed its
offscreen, uninstall and reinstall checks. A follow-up Developer ID
run in both renderer configurations verified the script's effect on
a loopback page in the approving profile. The same page in the peer
and private profiles had no script effect. Revoking approval through
Settings blocked injection on a fresh navigation.

Content projects explicit `split` and `spanning` declarations to
`not_allowed` in the runtime manifest. It retains the signed archive
and leaves private contexts outside extension registration. The
follow-up signed fixture used `incognito: split` and checked both the
effective manifest and private page behavior.

A fresh preview of the publisher's uBO Lite 2026.1110.1816 Chromium
archive passed sandboxed extraction and permission preflight, then
failed the manifest capability policy on its `storage.managed_schema`
declaration. A diagnostic copy with only that declaration removed
reached review with the original network rules. The diagnostic also
found that DNR indexing refused the standard macOS `/var/folders`
temporary-directory alias. Review staging now resolves the temporary
directory before creating the package. The updated manifest
translation unit compiled. The signed diagnostic run used a
canonical temporary directory supplied in the environment.

The diagnostic copy also installed and restored in Work. The
publisher's worker wrote its ruleset configuration there and left
Personal untouched. Its logs reported missing
`scripting.registerContentScripts` and
`scripting.unregisterContentScripts`. The full-package trial below
followed the implementation of managed-storage schema support and
native dynamic content-script bindings.

### Managed storage and dynamic content scripts

The standalone host validates `storage.managed_schema` against the
packaged file with Chromium's policy schema parser. The declaration
implies the storage permission. The managed namespace returns empty
policy and rejects writes. The embedder has no enterprise extension
policy provider.

The native dynamic content-script bindings use the existing
profile-local script store. The paired Developer ID runs in light
compositor and dark Metal covered schema-declared storage, read
defaults, write rejection, dynamic registration, enumeration, update,
unregister, and page execution. The script's updated state survived
restart when the update explicitly requested persistence. The Chromium
revision did not retain persistence after the fixture omitted that flag.
The test also exercised the existing user-script approval and
revocation, offscreen, uninstall and reinstall paths.

The complete uBO Lite 2026.1110.1816 archive, repacked as CRX3 with
only a disposable signing key added to its manifest, passed preview
using the normal macOS temporary directory. It installed and restored
in an isolated regular profile. The worker persisted its ruleset
configuration and registered its MAIN and isolated scriptlets, popup
script and specific cosmetic script. In a loopback network trial, the
ad host and control host both received requests without the extension.
With the extension active, the ad endpoint stopped receiving requests
and the control endpoint continued to receive them. The same trial
mapped `nnn.ng` to the fixture server and rendered an element matching
the archive's EasyList selector `#hfgad1`. The element remained visible
in the baseline and the extension hid it after navigation with its
specific cosmetic script registered. DNS mappings kept both cases on
the fixture server.
The macOS dependency check still reports 13,755 target labels and
zero under `//chrome`.

The wider signed registry suite exposed an activation race in its
legacy navigation probe. After stale worker cleanup, a new renderer
could start while the extension appeared enabled and renderer
metadata was still pending. The probe now publishes the native
activation before navigating. The rerun passed the registry suite,
including restart, signed review and installation, malformed managed
schema rejection, permission grants and revocation, and isolated
profile persistence.

### Extension clipboard and Bitwarden

The install review accepts and discloses `clipboardRead` and
`clipboardWrite`. Content grants those permissions to the focused,
visible primary document of an enabled extension in its owning
regular profile. It checks the extension renderer's process
registration, active API permission and the document's Permissions
Policy. Paste rechecks the document at the browser boundary. Ordinary
sites continue to use the profile's clipboard settings.

The signed registry suite passed with both permissions declared in
its fixture. The install review and worker reported both grants.
The opened action popup queried the read and sanitized-write
permissions without a user gesture and received `granted` for both.
This probe did not access the system pasteboard. The ordinary site
clipboard permission suite also passed in the signed build.

The Chrome ZIP for Bitwarden 2026.10.2 matched the publisher's asset
digest. The complete package, repacked with a disposable CRX3 key,
passed sandboxed extraction and stopped at the manifest permission
preflight. Its remaining declarations include `idle`, `sidePanel`,
`webNavigation`, `webRequest`, `webRequestAuthProvider`,
`notifications`, and optional `nativeMessaging` and `privacy`.
The manifest also contains sandboxed overlay pages and file-URL
content scripts. The package's background and offscreen scripts
contain clipboard operations, so the focused-popup permission test
does not cover Bitwarden's offscreen clipboard flow.

### Offscreen extension clipboard

Content supports the legacy `document.execCommand('copy')` and `paste`
fallback used by Bitwarden's offscreen document. The renderer checks
the calling extension context. The browser also checks the active API
permission, registered renderer process, owning regular profile and
the exact native offscreen document created for the `CLIPBOARD`
reason. It checks write eligibility again at commit. The async
Clipboard API retains Chromium's document-focus rule.

Developer ID builds passed the paired extension regression in light
compositor and dark Metal. Separate ad hoc builds ran the same suite
with `--extension-offscreen-clipboard` and an isolated AppKit
pasteboard. The authorized document read the fixture string and
replaced it. A document created for DOM parsing and the peer
installation without clipboard permissions could neither read the
string nor change the board. The app requires the test pasteboard
library before starting this probe. These runs used disposable profiles
and did not use the general pasteboard.

The complete Bitwarden package still requires its remaining manifest
capabilities and a publisher-package runtime test.

### Extension idle API

Content uses Chromium's native `IdleManager` and extension functions
for idle queries and detection thresholds. The manager attaches to each
regular profile's event router. The permission belongs to an extension
installation, while the measured idle time comes from the operating
system. The private context has no extension idle manager.

The signed Work and Personal fixture runs queried the API and set the
threshold in the permitted worker. The peer installation used the same
signing key without the idle permission, and its worker had no idle API.
A follow-up used Chromium's test idle-time provider to move from active
to idle at the configured threshold. Both renderer configurations
delivered the native event to the permitted service worker and passed
the extension lifecycle checks. The trial did not lock the desktop.
The dependency check still reports zero transitive `//chrome` targets.

Bitwarden's background script uses `idle.setDetectionInterval` and
`idle.onStateChanged`. It also uses `webNavigation.getFrame`,
`getAllFrames`, `onCommitted`, `onCompleted` and `onErrorOccurred`.
The existing Chromium implementation uses Chrome tab lookup and
tab-strip observers. Content already supplies profile-scoped Workspace
tab lookup and Chromium frame IDs for its other APIs. The navigation
port needs to connect those services to Content navigation and load
callbacks, and verify event and frame isolation across profiles.

### Extension web navigation

Content exposes `webNavigation.getFrame` and `getAllFrames` through
the owning Workspace's tab table and Chromium document IDs. A lookup
by document ID checks the owning browser context and any supplied tab
and frame IDs. It returns active HTTP and HTTPS documents in regular
Workspace tabs. It excludes private contexts, fenced frames and inner
WebContents.

Workspace tab observers publish start, commit, DOM content, completion,
error, history-state and fragment events through the owning profile's
native event router. The event URL feeds Chromium's listener filters.
The staged bindings use the schema from the pinned Chromium checkout,
as the existing tabs bindings do. The macOS build graph continues to
have zero transitive `//chrome` targets.

The signed registry suite passed in Work with light compositor and in
Personal with dark Metal. Both runs verified filtered commit and load
completion, a child frame with the correct parent document ID, frame
and document lookup, rejection of a mismatched frame ID, delivery of a
failed child navigation, and history and fragment events in the main
document and its child. Tab replacement and navigation target creation
events remain outside the implemented event set.

The first Metal attempt ran out of temporary disk space. Repeated
save-error notices then replaced a dialog delegate before closing its
widget. The shell now closes the prior widget before replacing the
delegate. Both signed suites passed on a RAM disk. The follow-up did
not reproduce disk exhaustion.

### Extension side panel

The signed manifest can declare a packaged `side_panel.default_path`.
Install review displays the path and `sidePanel` permission. The API
stores default options and action behavior per profile, and tab
overrides on the owning Workspace tab. Opening through the API requires
a user gesture and a regular-profile target. The tab hosts a right-side
view and a close button. The browser pins its primary navigation to the
approved extension resource and denies redirects. Disabling the
extension closes its open panels.

The Developer ID registry suite passed in Work with light compositor
and Personal with dark Metal. Each run read the manifest defaults,
set tab options, rejected an external panel URL and an open request
without a gesture, then opened the docked extension page with test
activation and closed it through the API. The panel loaded the signed
resource in the selected tab's profile.

Window-targeted calls currently resolve to the active Workspace tab.
Keeping a window-wide panel mounted across tab switches remains open.
The staged schema comes from the pinned Chromium checkout. The build
graph has zero transitive `//chrome` targets.

### Extension HTTP authentication

The Content host uses Chromium's request event router for signed extensions
with `webRequest` and `webRequestAuthProvider`. Regular-profile HTTP and HTTPS
server challenges can reach the owning profile's router. Chromium checks the
extension's host access before dispatching the event. When the router returns
no credentials and does not cancel the request, Content uses the native
authentication prompt where one is available. Proxy and private requests
continue through the existing authentication path. The embedder binds
Chromium's request completion interface for extension workers and pages.

The Developer ID suite passed with Work in light compositor and Personal
in dark Metal. On initial launch and after restart, the local server
received the fixture credentials exactly once, for the allowed host in
the owning profile. The ungranted host, the other regular profile and the
private context each reached the server without credentials. The same
suite covered offscreen documents, user scripts, removal and reinstall.
The dependency and header checks passed with zero transitive `//chrome`
targets. A workflow trial with the publisher's Bitwarden package remains
open.

### Bitwarden publisher package

The macOS trial used the publisher's 2026.10.2 Chromium ZIP repacked as a
CRX3 with a disposable developer key. The repacked archive contained the
same 200 files. The only manifest change was the signing key. A Developer ID
build with extension HTTP authentication extracted the CRX in its sandboxed
utility and rejected the package before installation on a disposable profile.

The first preflight rejected `nativeMessaging`. Chromium's permission
parser had silently skipped permission names absent from its registry.
The Content preflight now collects those names and reports them alongside
registered APIs that this host does not provide. It checks required and
optional declarations before publishing an installation.
The rebuilt Developer ID candidate reported required `notifications`
and optional `privacy` and `nativeMessaging` in the same preview.
The extraction succeeded and neither profile acquired an installation.

The publisher's worker calls `notifications.create` and
`notifications.clear` and listens for notification and button clicks.
The package also references the browser privacy settings for saving
passwords and autofilling addresses and cards. A follow-up needs to
validate the sandboxed overlay pages, the file-URL script declarations,
and the separate HTTP and HTTPS wildcard grants before running the worker
and exercising a disposable vault.

### Native messaging on macOS

Content reuses Chromium's native message framing and port dispatcher.
The macOS launcher reads host manifests from `NativeMessagingHosts`
beside the Workspace profiles, with a system fallback under
`/Library/Application Support/Zitter/NativeMessagingHosts`. The
disposable Workspace probe uses its own manifest directory. The launcher
requires an exact matching extension origin in `allowed_origins`,
a valid host name and an absolute executable path. Only enabled
extensions with an active `nativeMessaging` permission can connect
from a regular profile. The launched process receives the extension
origin as an argument. Native-initiated reconnect is outside this path.

The Developer ID suite passed with Work in light compositor and
Personal in dark Metal, before and after restarting. The fixture
used one-shot messaging and sent two messages over an open port.
It rejected a host manifest listing another extension, a missing host
and an invalid host name. The peer worker without the permission could
not see the native messaging methods. No fixture host remained running
after the suite. The macOS GN graph still has zero transitive
`//chrome` targets.

The Bitwarden 2026.10.2 publisher package passed the API permission
preflight in the same build. It was rejected by the subsequent
manifest capability check before installation. Its manifest includes
separate HTTP and HTTPS wildcard grants and file-URL content-script
patterns.

### Bitwarden installation on macOS

Content now accepts separate HTTP and HTTPS wildcard patterns without
changing either scheme. A manifest may declare the `file:///*` script
pattern with its file scheme disabled by Chromium. Install review
discloses that local file access is disabled and omits inactive file
patterns from the network grants.

Bitwarden 2026.10.2 passed signed preview and installation through
Settings on the Developer ID build. The publisher contents were
repacked with the disposable signing key. The installed service worker
started in Work with light compositor and Personal with dark Metal.
Each run used a fresh Workspace and confirmed that the peer profile did
not acquire an installation. The worker reported WebAssembly support
and read the empty managed configuration. The launch used loopback
pages and disabled external DNS resolution. The paired extension suite
also passed in both renderer configurations.

A vault workflow, optional permissions and Bitwarden's desktop native
host still need runtime trials.

### Extension privacy preferences

The pinned `chrome.privacy` schema exposes the three service settings
used by Bitwarden. `passwordSavingEnabled` uses Chromium's extension
preference map and store above the profile's user preference. The
override is stored with the extension's profile preferences and stops
applying when the extension is disabled. Clearing the override restores
the user's preference. The embedder sends change events only to enabled
extensions with the permission in that regular profile. Private scopes
are rejected.

The Developer ID suite passed in Work with light compositor and Personal
with dark Metal. The permitted worker disabled password saving, received
the change event, observed the override after re-enabling the extension
and restarting the browser, then cleared it and observed the original
value. The peer worker without the permission could not see the API.
GN header and dependency checks passed with zero transitive `//chrome`
targets.

Content has no native address or payment-card autofill service. Those
settings report disabled and `not_controllable`. Bitwarden's Chromium
worker tries to write those settings before the password setting, so its
combined browser-autofill switch remains a compatibility gap. The trial
uses a signed fixture with a required permission. Bitwarden's optional
permission flow still needs a publisher-package trial.

### Extension notifications on macOS

The staging tool compiles the schema from the pinned Chromium WebIDL.
Content registers the `notifications` permission for regular macOS
profiles and uses UserNotifications for basic notifications. It assigns
an opaque system identifier for each notification and associates it with
the calling profile, extension ID and extension-provided notification
ID. The API can create, replace, list and clear entries. The native
delegate routes clicks, button actions and dismissals to the owning
extension. Disabling the extension or shutting down the profile clears
its entries.

The Developer ID trial passed in Work with light compositor and Personal
with dark Metal, before and after restarting. The authorized worker
queried the macOS permission, listed its empty store, checked clear and
update misses, and received an error for an incomplete create request.
The peer worker without the permission could not see the API. The trial
did not request notification authorization.
A separate ad hoc build exercised the native delegate through an isolated
replacement for the macOS notification center. The fixture checked
create, update, list, clear, click, button click and dismissal across two
regular profiles on initial launch and after restart. Both installations
used the same extension ID and notification ID. The fixture confirmed
that the two system identifiers differed and the button event returned
to the owning worker. This trial did not display a system banner or
change the account's notification settings. A live Notification Center
trial and authorization denial remain open. The macOS implementation
currently uses the app icon and supports the basic template.

### Sandboxed extension pages

Content admits `sandbox.pages` declarations for exact packaged files.
Chromium applies its sandbox CSP when serving those pages. Install review
shows the sandbox paths and policy. Changes to either require approval
when updating an installed extension.

An extension page can embed its own declared sandbox page. The sandbox
renderer stays outside the privileged extension process map. A factory
bound to the committed frame checks the opaque origin's precursor and
allows package resources only from that enabled extension in the owning
profile. The Developer ID suite passed in Work with light compositor and
Personal with dark Metal, including restart. In both profiles the frame
ran its packaged script, reported an opaque origin and had no access to
the parent document or `chrome.runtime`. The trial covered an extension
parent.

The Developer ID build also loaded Bitwarden's published
`overlay/menu-button.html` as a dynamic web-accessible resource from a
regular loopback tab in both Work with light compositor and Personal
with dark Metal. The frame's packaged script registered its custom
element. The frame kept an opaque origin and could not access the
parent document or `chrome.runtime`. The trial rejected the canonical
URL for the dynamic-only resource and rejected embedding the
non-sandboxed `overlay/menu.html` resource.

The browser validates the embedding origin, enabled extension, declared
sandbox path and web-accessible resource before allowing the frame. Only
the same-resource redirect from the dynamic host to the canonical host
may proceed. Runtime context enumeration skips uncommitted and opaque
frames. The probe inserted the frame itself while Bitwarden was signed
out. Automatic overlay activation and filling from an unlocked vault
remain open. The published Bitwarden worker called `management.getSelf`
successfully in the signed Work trial. It returns the calling extension's
metadata and active permissions and reports a locally installed CRX as
`normal`. The signed fixture checks that an extension without the
`management` permission cannot enumerate other installations.

A local Vaultwarden 1.37.5 build served its configuration API and the
published 2026.10.1 web vault on loopback from a fresh SQLite database.
The web client refused HTTP during account registration. A follow-up
bound Vaultwarden to HTTPS on IPv4 loopback, created and signed into a
disposable account, and saved an encrypted login item for a separate
loopback form. The current Developer ID Content candidate installed and
activated the published Bitwarden package in an isolated Work profile.
The web client used a dedicated browser profile and pinned the fixture
certificate for that process. Content rejects certificate bypass flags.
A disposable Content Lab build reused the download probe's leaf trust
limited to IPv4 loopback. The trust was supplied only to Work. Work
loaded the vault over HTTPS and its API returned HTTP 200. Personal
rejected the same certificate with `ERR_CERT_AUTHORITY_INVALID`.
From Work Settings the published Bitwarden action opened its popup,
but the popup remained on the loading indicator. Its console reported
a missing `chrome.extension` namespace when reading `getBackgroundPage`
and an unimplemented `chrome.tabs.getCurrent`. Content now exposes
the Manifest V3 compatibility method in extension documents and returns
`null` because the background worker has no Window. `tabs.getCurrent`
returns the caller tab's permission-filtered metadata or no value for a
popup. The published Bitwarden popup reached its login form after these
changes.

In the follow-up Work trial, the temporary build trusted the same
loopback leaf in the extension's network partition. The published
popup signed into the local vault, displayed the matching item, and
filled the username and password through its Fill button. The form
checked both values and reported success. The fixture trust code was
built from temporary source copies. A follow-up reopened Work and
confirmed that Bitwarden retained the account in a locked state after
restart. Unlocking with the test password restored the vault item and
its popup suggestion. Inline suggestions were off by default. After
enabling them and focusing the login field, two error documents appeared
in the form's accessibility tree instead of the inline suggestion UI.
A browser-side diagnostic recorded two dynamic-host requests for
`overlay/menu.html`, both initiated by the regular loopback document.
The publisher declares this bootstrap page as a dynamic web-accessible
resource. Its manifest sandbox covers only `overlay/menu-button.html`
and `overlay/menu-list.html`. The bootstrap uses `chrome.runtime` to
open a port, so loading it as one of those opaque sandboxed pages would
break its protocol. The current subframe throttle rejects the bootstrap
at its manifest sandbox check. The earlier direct-load rejection
verified that same restriction. The compatibility work needs to
validate Chromium's web-accessible extension subframe rules for this
bootstrap, including the owning profile, dedicated extension process,
parent-origin matching, dynamic redirect, and nested opaque frames.

A subsequent temporary build admitted the dynamic web-accessible
bootstrap only for a matching HTTP or HTTPS parent, kept its canonical
redirect on the same resource, and admitted manifest-sandboxed children
from the extension parent. The published package displayed both nested
documents and the inline unlock prompt from the ordinary form. After
unlocking in the extension popup and reloading the form, the inline
list displayed `No items to show`. The popup had previously matched
the same form and filled both fields. The messaging delegate now
constructs `sender.tab` using the receiving extension's existing tab
permissions. The registry trial verified both delivery of the URL to
an authorized receiver and omission of restricted fields for a receiver
without access. The publisher still showed an empty list after that
change. Its background log identified a missing
`chrome.windows.WINDOW_ID_CURRENT` in the active-tab lookup used to
populate suggestions. Content now supplies the integer window ID
constants. The first build omitted their schema types and Chromium
rejected the bindings in the renderer. With the corrected schema,
the published Bitwarden package showed the matching vault item in
the inline list after unlock. Selecting the item filled both fields,
and the loopback form verified the values. Focusing the same form in
Personal produced no Bitwarden suggestion and left both fields empty.
The light compositor registry suite passed with the corrected constants
and the current-window query assertion.
Browser-side instrumentation confirmed that the published bootstrap
committed in the extension process, separate from its HTTP parent.
Both nested documents committed with opaque origins in sandboxed
renderers outside the extension process map, separate from the
bootstrap and the main page. A direct canonical bootstrap iframe was
blocked in Work and in Personal. The verified navigation and loader
changes are now in the Content source. The publisher regression probe
also checks process separation and rejects direct canonical URLs for
the bootstrap and sandboxed button. The probe passed on the rebuilt
local app in Work with light compositor and Personal with dark Metal.
The completed integrated arm64 build repeated the publisher overlay
probe successfully with both profile and renderer combinations.
After the disposable RAM disks were lost, the local Vaultwarden fixture
was rebuilt. Its pinned web client created one test account and saved
one encrypted login item for the separate loopback form. The form is
ready to check both fields. The first integrated trial rejected the
Vaultwarden fixture certificate, which declared `CA:TRUE`. After
reissuing it as a loopback leaf with `CA:FALSE`, the disposable Work
browser opened the vault over HTTPS and Personal showed the certificate
error. The extension's worker and popup still rejected the same leaf.
Content gives the installed extension a separate storage partition, and
the reused download fixture provides the leaf only to the default
partition. A follow-up diagnostic build adds the same IPv4-loopback leaf
to the one Bitwarden fixture partition in Work. This build logged in to
the local vault, completed sync, and showed one matching suggestion in
the extension popup. After enabling inline suggestions and reloading
the form, focusing either field did not show the inline menu. The
extension also logged unsupported `contextMenus.create` properties.
Content now supports normal items, separators, and the enabled flag
used by the publisher. The macOS registry fixture verified a separator
and a disabled item in the rendered submenu and dispatched the enabled
item. A subsequent interactive trial with the menu fix restored the
publisher package in a disposable Work profile, synchronized the local
vault, and showed one matching suggestion. Selecting it from the popup
reported that the item could not be filled, both on the restored tab and
after reloading it. The same form in Personal showed no suggestion and
left both fields empty when focused. The menu fix's effect on inline
suggestions remains unverified.

The worker also logged an undefined `addListener`. The publisher's
tab-tracking setup subscribes to `windows.onFocusChanged` and
`tabs.onReplaced`, which are absent from the Content bindings. Trace
the worker's subscription and the page-details request and response on
the same restored tab to establish which failures affect autofill.

The ordinary macOS startup verifier now covers that message sequence
with a signed fixture that has explicit loopback host permission. On a
restored Work tab the content script contacted its worker at document
start. The worker resolved the tab with `tabs.get`, injected a packaged
script into the sender frame with `scripting.executeScript`, and sent a
tab message without a frame filter. The injected script sent a separate
runtime message back. The worker verified the sender tab ID, page URL
and top-level frame ID before persisting the run count. The integrated
arm64 bundle passed the sequence on two Work launches. It also passed
the Personal, disabled extension and damaged package checks. The
verifier uses a file-backed Keychain fixture.

A disposable copy of the publisher package with diagnostic logging
showed the first restored Work tab had ID zero. Bitwarden's tab helpers
treated that ID as absent, skipping the page-details message. On the
same browser build, the publisher package without instrumentation
filled both fields on a new Work tab with a positive ID. The fixture
verified both values and received its `/verified` request. Personal
showed no suggestion and left the same form empty.

Content now allocates positive extension tab IDs. The ordinary startup
verifier failed against the previous bundle on the new positive-ID
assertion and passed against the rebuilt integrated arm64 bundle. The
signed extension registry suite also passed on that bundle. The
instrumented publisher repeated the page-details sequence on the first
restored tab with ID one. With monitoring explicitly started on the
synthetic form, its worker received one frame and two fields.

The rebuilt arm64 bundle then passed the complete publisher autofill
trial using the local vault and the temporary Work-only loopback leaf.
The unmodified Bitwarden extension code filled both fields from the
popup on the first restored Work tab. After enabling inline suggestions
and reloading that tab, focusing the password field displayed the same
vault item. Selecting it filled both fields again. The fixture verified
both values and recorded two successful `/verified` requests. Focusing
the same form in Personal showed no suggestion and left both fields
empty. The standard output was rebuilt after restoring the two staged
files used for the certificate fixture.

The registry regression traced the previous permission failure to the
shared host-pattern check allowing an optional `*://*/*` request.
Content still accepts that declaration. It rejects the wildcard-scheme
pattern when requested or restored as an optional grant. Explicit HTTP
and HTTPS wildcard patterns remain eligible for the normal consent flow.
The light compositor and dark Metal registry suites then passed the grant, revocation,
restart, popup, options, and install review checks. The review fixture
now checks the file-access-blocked field that the browser reports.

The broader regression exposed a timeout on the native messaging port
after a successful overlay trial. The macOS launcher now schedules
reading and writing on independent sequences so a waiting read cannot
delay an outgoing message. The signed native messaging suite passed in
both renderer configurations after that change.

## Navigation reputation

The macOS Content startup creates Chromium's local Safe Browsing database
when the existing Safe Browsing key file is configured. The Content
throttle checks HTTP and HTTPS frame navigations and their redirects
in regular and private browser contexts. The database updates without
sending visited URLs. A local prefix match can trigger a full-hash lookup.
V5 keeps an in-memory full-hash cache per browser context, including a
separate cache for each private session. V4 uses Chromium's shared
in-memory full-hash cache.
A confirmed match blocks the request and displays a browser-owned error
page. If a check times out, the navigation continues.

The browser uses a dedicated, in-memory network context with its HTTP
cache disabled. Its loader accepts the four Chromium V4 and V5 update
and lookup paths on the HTTPS Safe Browsing API origin, attaches the
configured key only there, omits credentials and disallows redirects.
Extended reporting is off. This integration covers macOS document
navigations. In the pinned Chromium checkout,
`BrowserURLLoaderThrottle::WillStartRequest` checks only document
destinations, and `RendererURLLoaderThrottle::WillStartRequest`
skips the URL check. Both live under
`components/safe_browsing/content`. Script and image lookups therefore
fall outside the Safe Browsing parity target for this checkout.
Windows startup is tracked separately.

The local macOS GN and Ninja input check still finds 13,755 transitive
GN dependencies, zero labels under `//chrome`, and only the previously
allowed Chromium version and branding inputs under `chrome/`. GN header
dependency checks passed. The relinked Developer ID build passed the
two-profile Workspace restart trial with no reputation provider configured.
The Developer ID signed Content Lab passed the local-list navigation
verifier with Chromium V4 and V5 in both a disposable encrypted Work
profile and a disposable private context. In each run the safe page
loaded, its iframe was blocked, and the blocked URL and a redirect to
it committed the browser-owned warning in the top-level frame. The
loopback server received no request for the blocked URL. The probe
uses Chromium's synthetic phishing entry with list updates disabled
and no provider key. The verifier requires the Content Lab identity.

```sh
python3 tools/verify_content_navigation_reputation.py "/path/to/Zitter Content Lab.app"
```

The signed verifier also runs the V4 protocol in Work and the V5
protocol in both Work and a private context against a local provider.
The browser accepts a checksummed list update with a four-byte prefix,
requests confirmation, and blocks after the provider replies with the
matching full hash. The V5 response includes the requested lists in
order, with a Rice-encoded prefix in the phishing list. Each V5
confirmation request contains exactly the expected four-byte prefix.
Both protocols send the test key and omit cookies, referrer,
authorization, the full hash, and the visited URL. Each run makes one
list request and one confirmation request across the three blocked
routes. The test remaps the allowed provider routes to loopback HTTP
and uses the browser's isolated network context. It does not exercise
TLS or the production key and service.

The retained pre-reputation build timed out before issuing any fixture
requests. Its framework lacks the probe switch. The verifier now
reports the protocol, profile and observed traffic on timeout. A
separate snapshot of the retained non-component lab bundle loaded
the safe page and requested the blocked URL during the first V4 Work
case, then timed out. That snapshot did not pass the navigation
reputation test. The completed integrated arm64 build passed all seven
loopback cases. V4 and V5 blocked direct, redirected, and subframe
navigations with the seeded lists and with the local protocol provider.
The production key file was absent in the local test environment.

The current arm64 lab build repeated all seven seeded-list and
loopback-provider cases successfully. The notarized 0.1.5 production
bundle rejected the standalone test-provider override before opening
the disposable profile directory. Its production identity cannot
exercise the local provider fixture through that entry point. The
normal production startup still requires a provider credential and a
live end-to-end test.

Before cutover, verify list updates and full-hash requests with the
production provider. Google's public Safe Browsing API is for
non-commercial use. Commercial distribution requires a separate
agreement to use that API or a provider such as Google Web Risk.
Confirm the distribution model before provisioning credentials.
The current macOS Content startup reads a private key file from the
account or an explicit absolute path. With no key file it skips
starting the navigation reputation service. The clean-install trial
must therefore exercise the intended credential provisioning and
verify that protection starts on the release's normal launch path.
[Google's Safe Browsing terms](https://developers.google.com/safe-browsing/terms)
[Google's Safe Browsing usage and warning requirements](https://developers.google.com/safe-browsing/reference/Appropriate.Usage)

## Build the macOS lab

The project build script supports the standalone macOS target:

```sh
./build.sh --content
./build.sh --content --run --zitter-rust-raster=metal
```

It initializes a local non-component Release output with ThinLTO and PGO
disabled when the output has no arguments yet. It stages the Content
overlay, runs GN and Ninja, audits the linked licenses, and produces
`build/ZitterContentLab.app` with an ad hoc seal and bundled notices.
`ZITTER_CONTENT_OUT` selects another output path relative to the Chromium
checkout. `ZITTER_BUILD_JOBS` and `ZITTER_PYTHON` retain their usual meanings.
Set `ZITTER_CONTENT_PACKAGE_DIR` to write the resulting app or ZIP to
another directory while reusing the existing Chromium build output.
Set `ZITTER_CONTENT_PACKAGE_SCRATCH` to an existing directory on a local
volume to stage and seal the bundle there. The build verifies the copy
on the destination volume before replacing the previous app.
Use `./build.sh --content --archive` to keep a compact local artifact
at `build/ZitterContentLab-local.zip`. This mode verifies the app's ad
hoc signature after extracting the ZIP in its scratch directory.
The current arm64 archive was built through Ninja and verified after
extraction with its ad hoc signature intact. The sandbox guards,
Workspace, DevTools, signed-extension and profile suites passed. Its
sealed metadata records 13,755 GN dependencies and the two allowed
Chromium metadata inputs under `chrome/`. The archive SHA-256 is
`da4c75548678830b9b83409d6f322ede1bcc12fbf8aff945794b6a9079f8b97a`.
The arm64 build used a disposable RAM volume for signing and
verification after the local system volume ran out of space while
copying the expanded app.
The run mode opens the Workspace with profiles and a random fixture
encryption key under a private temporary directory. Closing the process
removes that directory. Developer ID distribution uses the separate
preparation and notarization flow.

The arm64 build script passed packaging, launch, and normal AppKit Quit
through `--content --run`. The resulting local bundle also passed
the dark Metal live navigation verifier.

From the Zitter migration worktree, place the dedicated Chromium checkout at
`.chromium/src`. Stage the standalone target directly, without running the
Chrome overlay installer.

```sh
python3 tools/stage_content_embedder.py .chromium/src
mkdir -p .chromium/src/out/ZitterContent
cp chromium/release.args.gn .chromium/src/out/ZitterContent/args.gn
printf '\nuse_thin_lto = false\nchrome_pgo_phase = 0\n' \
  >> .chromium/src/out/ZitterContent/args.gn

cd .chromium/src
buildtools/mac/gn gen out/ZitterContent \
  --root-target=//zitter_content:zitter_content --fail-on-unused-args
buildtools/mac/gn check out/ZitterContent \
  //zitter_content:browser_framework_shared_library \
  --root-target=//zitter_content:zitter_content
buildtools/mac/gn desc out/ZitterContent //zitter_content:zitter_content deps \
  --all --root-target=//zitter_content:zitter_content
third_party/ninja/ninja -C out/ZitterContent -j 6 zitter_content
open 'out/ZitterContent/Zitter Content Lab.app'
```

The target asserts that its dependency tree contains no `//chrome` targets.
`./build.sh --content` also checks the full GN dependency listing before
running Ninja, including dependencies of executables used as build tools.
It records the label count and a SHA-256 digest of the sorted dependency
labels in the sealed bundle and in the Content DMG build record. The lab reuses
Chromium's macOS launcher source for its Seatbelt bootstrap. It owns the
Content delegate, browser context, window and bundle, and links neither the
Content Shell app nor its browser test library.

From the Chromium checkout, launch the smoke mode in a macOS GUI session.

```sh
'out/ZitterContent/Zitter Content Lab.app/Contents/MacOS/Zitter Content Lab' \
  --zitter-content-smoke-test
```

Expect `Zitter Content Lab smoke passed` and normal process exit. The marker
requires the page's JavaScript to set its title and the renderer to have a PID
distinct from the browser. The renderer checks that Seatbelt is active at
startup. Use an external timeout for automated runs. Test service process
sandboxing and policy coverage separately.

## Relocated macOS bundle

The development target can still use a component build. For a
relocation test, take the framework's GN runtime dependency list and
put its component libraries inside a disposable copy of the app.
The verifier places them in `Contents/Frameworks/Runtime` so the
libraries' own runpaths stay inside the bundle. It redirects the
launcher, framework, and helper runpaths, checks the Mach-O load
commands, and seals only the temporary copy with an ad hoc signature.
It starts the browser and Workspace outside the checkout, inspects the
dyld image trace, and runs the persistent and private profile probes.

```sh
(cd .chromium/src && buildtools/mac/gn desc out/ZitterContent \
  //zitter_content:browser_framework_shared_library runtime_deps \
  --root-target=//zitter_content:zitter_content) > /tmp/zitter-content-runtime-deps
python3 tools/verify_content_bundle.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app' \
  --component-runtime-deps /tmp/zitter-content-runtime-deps
```

The relocated app passed both smoke tests with a separate sandboxed
renderer. The verifier embedded 482 component libraries and checked
the ad hoc seal, then reopened Work and Personal with separate
encrypted cookie stores and verified that private storage started
empty. A copy of the unmodified component app failed the runpath
check as expected. These runs used disposable profile data and the
fixture encryption key.

For a bundle without component dylibs, use a separate local output
directory. This keeps the development output and any concurrent
Chrome-backed builds independent.

```sh
python3 tools/stage_content_embedder.py .chromium/src
mkdir -p .chromium/src/out/ZitterContentBundle
sed 's/^is_component_build = true$/is_component_build = false/' \
  chromium/release.args.gn > .chromium/src/out/ZitterContentBundle/args.gn
printf '\nuse_thin_lto = false\nchrome_pgo_phase = 0\n' \
  >> .chromium/src/out/ZitterContentBundle/args.gn
(cd .chromium/src && buildtools/mac/gn gen out/ZitterContentBundle \
  --root-target=//zitter_content:zitter_content --fail-on-unused-args)
(cd .chromium/src && third_party/ninja/ninja -C out/ZitterContentBundle \
  -j 8 zitter_content)
python3 tools/verify_content_bundle.py \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

The verifier checks the framework's helper bundles, resource pack and
graphics libraries before launching. Every inspected Mach-O image must
support the launcher's architectures. The Vulkan ICD must point to the
adjacent packaged SwiftShader library with a relative path. The
Workspace and profile probes
run outside the build tree with inherited dyld overrides removed.
The smoke tests verify the renderer executable path by PID because
concurrent processes can interleave lines in a shared dyld trace.
The verifier also runs the bundled DevTools frontend with the dark
Metal Workspace and checks the same relocated loader and renderer paths.
Before the normal smoke tests, both package verifiers check that browser
startup refuses `--no-sandbox`, `--disable-gpu-sandbox`,
`--single-process`, `--in-process-gpu`, and
`--browser-subprocess-path`. It also refuses renderer, GPU, and utility
launcher prefixes. Each launch must return status 1, produce no renderer
or success marker, and leave the isolated temporary directory empty.
The previous macOS build accepted an empty GPU launcher override. The
rebuilt, relocated macOS app rejected all eight switches and passed the
ordinary sandboxed renderer smoke test. The Windows package still needs
to run these checks natively.
Pass `--crx-signer /path/to/crx3_build_action` to
also exercise signed extension installation and updates, including
unpacking in a sandboxed utility process. The signer builds the test
packages outside the app.

The non-component macOS graph passed GN generation and include checking
with 13,590 dependency labels and zero `//chrome` labels. The cold arm64
build completed all 42,425 Ninja actions in 2 hours 23 minutes at eight
jobs with ThinLTO and PGO disabled. It produced a 456 MiB app on disk.
This measures the initial staged snapshot. Restaging the latest sources
then required 14 Ninja actions in 26 seconds.

The non-component Workspace smoke exposed dangling references while
rebuilding profile rows and during shutdown. The shell now drops those
references before destroying the views, and the detached tab owner
outlives its non-owning reference. The follow-up build ran seven actions
in 34 seconds. The relocated arm64 bundle passed both smoke tests,
reopened Work and Personal with separate encrypted cookies, and kept
private storage empty. The only bundled dylibs were the Vulkan loader
and SwiftShader. The component bundle also passed the payload and
architecture checks, which rejected copies missing the GPU helper or
resource pack.

The final relocated package passed the signed extension install and
update suite, including the sandboxed utility. Separate runs passed
the dark Metal Workspace smoke and the DevTools probe for docking,
the Console and DOM inspection. The docked DevTools view now hosts
the borrowed frontend without keeping a pointer to its private context.

The current 465 MiB arm64 build passed the full verifier with
`--crx-signer .chromium/src/out/ZitterContent/crx3_build_action`.
The results cover both Workspace startup configurations, the bundled
DevTools frontend, signed extension install, restore and update, and
reopening separate encrypted Work and Personal profiles with a fresh
private context. The dependency graph has 13,720 labels and zero under
`//chrome`. An ICD fixture rejected bare, parent-relative and absolute
library paths.

The relocated non-component app also passed the legacy migration
verifiers for split sessions, cookies, localStorage, history, Clipboard,
password autofill and selected extension data. They used disposable
profiles, the mock Keychain and the installed legacy app for fixture
creation and rollback. The split test exposed stale sidebar view
references during a list rebuild. The shell now clears its caches
before destroying those views. The extension migration verifier accepts
`--crx-signer` so the signer can run from its build directory while
the app runs outside the checkout.

Native Windows packaging, production signing and notarization, installer
integration, and updates remain cutover checks.

### Prepare a Developer ID candidate

Use an existing non-component macOS bundle and an empty working
destination. The license audit runs against the standalone GN root
target. It preserves metadata diagnostics for review.

```sh
python3 tools/audit_licenses.py --app '/path/to/Zitter Content Lab.app' \
  --out .chromium/src/out/ZitterContentBundle \
  --target //zitter_content:zitter_content --destination /path/to/audit
python3 tools/notarize_app.py --prepare-only --content-version 0.1.0 \
  --app '/path/to/Zitter Content Lab.app' --destination /path/to/candidate \
  --third-party-licenses /path/to/audit/THIRD_PARTY_LICENSES.txt
python3 tools/verify_content_bundle.py --preserve-signature \
  '/path/to/candidate/work/Zitter Content Lab.app'
```

The preparation mode copies the bundle, sets its version, installs
release notices and signs it from the inside out. It keeps the
`dev.zitter.content-lab` identifier. This mode prepares an archive
without submitting it to Apple or publishing release assets.

The arm64 candidate passed strict signature verification. All 11 Mach-O
paths had a timestamped Developer ID signature and Hardened Runtime.
The relocated signed bundle passed the sandbox, Workspace, DevTools and
persistent and private profile checks. The license audit matched all 11
binaries to link rules and retained nine metadata diagnostics for review.
After testing the signed bundle, submit the prepared archive under
the separate Content Lab identity. The same command resumes polling.

```sh
python3 tools/notarize_app.py --submit-content-candidate \
  --app '/path/to/Zitter Content Lab.app' --destination /path/to/candidate
python3 tools/package_dmg.py --content-candidate \
  --app '/path/to/candidate/work/Zitter Content Lab.app' \
  --destination /path/to/candidate-dmg
```

The DMG packages the stapled candidate and keeps the Content Lab
application name, volume name and signing identifier. Both tools name
the candidate artifacts separately from the production release.

Apple accepted the arm64 Content Lab archive without reported issues.
The stapled app passed ticket validation and Gatekeeper as
`Notarized Developer ID`. The signed extension suite passed again
on that stapled app using disposable profiles. Apple also accepted the
separately signed DMG. The DMG and its mounted app passed ticket and
Gatekeeper validation. A quarantined copy passed `syspolicy_check
distribution`. The app copied from the final DMG passed sandbox,
Workspace, DevTools and persistent and private profile checks. These
runs used the Content Lab identity. Production update and first-launch
behavior under the production identity remain open.

### Check for macOS updates from Content

Ordinary Content startup connects the Workspace update controls to the
same macOS updater as the Chrome-backed host. The updater reads stable
release metadata, chooses the newest DMG for its backend and architecture,
and checks the asset
size and SHA-256 digest before offering to reveal it in Finder. The
macOS host also passes the bundle version to About, including private
windows, so the native menu and the updater report the same version.
The local navigation probe verified the native About version in light
compositor and dark Metal.

Production Content DMGs use the
`Zitter-v<version>-macos-content-<architecture>.dmg` naming convention.
Content requires that name before offering either a release link or a
download. Chrome continues to select the original macOS asset names.
Existing rehearsal DMGs keep their original filenames and can be used
by the local replacement verifier.

The macOS metadata probe passed for both backends. A mixed fixture
catalogue selected and verified the signed 0.1.3 rehearsal image under
the Content asset name despite a newer Chrome installer. The Chrome
selection rejected that Content image. The public feed responded
without authentication and contained no Content installers at the time
of this check. Delivery through that feed still needs a published
Content release.

The Content signing and DMG preparation tools now require build
provenance inside the application bundle before creating an installer.
They validate the Content backend, source and Chromium revisions, GN
arguments and dependency hashes, and the allowlist for declared inputs
under `chrome/`. The DMG build record carries those fields. The
mounted 0.1.3 app passed Developer ID, Hardened Runtime, and provenance
checks. A separate fixture rejected missing or malformed metadata and
an unexpected `chrome/browser` input.

The production `package_dmg.py --content-release --prepare-only` path
also passed using the notarized app from the 0.1.3 rehearsal image. It
verified the app's existing seal and ticket, built and signed a new
drag-to-Applications image on a local RAM volume, and recorded the
Content backend, 13,755 audited dependency labels, and the two
permitted Chromium metadata inputs. `hdiutil verify` accepted the
prepared image. This run stopped before DMG submission and removed
its scratch copy after verification.

The updater rechecks the cached DMG on a background thread when the user
chooses to reveal it. A failed recheck replaces the Finder action with
the release page link. The Content cache lives under its own profile
root. The user installs the
app from the DMG. The update controls honor the saved automatic check
and download preferences.

The local `mac_release_metadata_probe` exercises the same release
selection and file verification code as the updater. It checks that
Windows-only releases, drafts, and prereleases cannot displace a stable
macOS release. It also checks native architecture preference, the
manual release link when the asset URL or digest is invalid, and the
absence of an offer for an already installed version. A three-byte file
with a known SHA-256 digest passes verification. Changing its size or
content causes verification to fail. A symbolic link to the same valid
file is rejected. The probe also exercises the updater's redirect check.
It accepts HTTPS URLs on the three permitted GitHub hosts and rejects
invalid URLs, HTTP, embedded credentials, non-default ports, and other
hosts. The updater cancels the download when this check rejects a redirect.

The cache probe verifies that a valid download replaces the cache entry
and a corrupt download leaves the verified entry intact. It checks that
both rejected downloads and failures to create the cache directory clean
up the temporary file. The same probe passed with `--download-scratch`
pointing to an HFS+ RAM volume while the cache remained on APFS. When a
direct rename fails, the updater copies the download to a temporary
file beside the cache entry, verifies that copy, and publishes it by rename.

```sh
cd .chromium/src
buildtools/mac/gn gen out/ZitterContentBundle --root-target='//zitter_content:*'
third_party/ninja/ninja -C out/ZitterContentBundle mac_release_metadata_probe
out/ZitterContentBundle/mac_release_metadata_probe
```

Pass a release catalog, installed version, native architecture and local
DMG to verify the release asset with the updater's production code:

```sh
out/ZitterContentBundle/mac_release_metadata_probe \
  /path/to/releases.json 0.1.1 arm64 /path/to/Zitter-v0.1.2-macos-arm64.dmg
```

The 0.1.2 rehearsal used a local catalog in the GitHub API format with
the final DMG's recorded checksum. The verifier selected the arm64
installer for 0.1.1 and verified the image. Incorrect size, incorrect
digest, and a symlink to the same image were rejected. Version 0.1.2
and x86_64 did not select the asset. This verifies selection and file
integrity independently of network delivery and the Finder action.

The arm64 Content app built and linked locally. The ad hoc package
passed strict signature verification and the renderer smoke test.
The Chrome host adapter passed a Clang syntax check. GN header checking
passed and the Content graph still contains zero `//chrome` labels
among 13,755 dependencies. The renderer smoke does not exercise the
release feed, download, or Finder action. Those remain to be verified
through the signed production startup path. The packaged build also
passed the Content-to-Content replacement rehearsal below.

The local 0.1.1 arm64 rehearsal includes the updater and its verification
before revealing a cached DMG. Before submission, the Developer ID app
passed the sandbox startup guards, dark Metal Workspace and DevTools
smoke tests, and Basic and Digest authentication probes. Apple accepted
the app under `343a4dcf-42e7-448d-bfa7-360f308754ba` and its DMG under
`b3dd8b4b-7349-45e4-b2ed-daea81dd3e61`. The final image and mounted
application passed ticket and Gatekeeper checks. The downloaded-app
quarantine assessment passed.

An app copied from the image opened twice without arguments using a
disposable file Keychain and a browser-only ad hoc signature for fixture
injection. The run checked exclusive profile ownership, Work and Personal
restoration, and both Shared Dictionary stores. The 0.1.0 and 0.1.1
Developer ID apps have the same designated requirement. System Keychain
authorization and delivery from the release feed remain open.

The unchanged 0.1.1 Developer ID bundle also passed the signed extension
registry suite. It installed and restored a fixture CRX, accepted a
same-key update, rejected incompatible and stale packages, and enforced
the worker's approved host grants across isolated profiles.

The production bundle verifier passed against both the signed app and
the final DMG. It copies the bundle outside the checkout and retains
the existing signature. It covers helper and sandbox checks, the
packaged Assistant, Workspace, DevTools, two persistent profiles, and
the private-window session. The DMG run included the extension suite.
[Local Content release procedure](local-releases.md#macos-prepare-and-verify-a-content-candidate)

The local 0.1.2 arm64 candidate includes ordinary extension restore and
damaged-package recovery. Its sealed build metadata records 13,755
dependencies and a dirty source checkout. The Developer ID app passed
the relocated bundle suite with the extension registry enabled. The
0.1.1 and 0.1.2 apps share a designated requirement. With their
signatures intact, the password prompt verifier saved a password in
0.1.1 and confirmed 0.1.2 could autofill it using the disposable probe key.
Apple accepted the 0.1.2 app under
`d5bddf17-9df5-45be-9717-5a97e8bdb5a5`. Its stapled ticket and
Gatekeeper assessment passed. The ordinary bundle replacement verifier
also passed from 0.1.1 to 0.1.2 with outer ad hoc signatures and a
disposable file Keychain. Both workspaces restored their tabs, local
storage and encrypted cookies across the replacement.
Apple accepted the 0.1.2 DMG under
`119b5549-9148-4f97-bb0e-412f20898ebe`. The final image and mounted
app passed signature, ticket and Gatekeeper checks, and a quarantined
app copy passed distribution assessment. The relocated bundle suite
also passed from the final image with the extension registry enabled.
The local DMG SHA-256 is
`b93b94d46bb1ac017a57b58d528420e5b1bf1707ed0b2fe3de3a41832ab36800`.

The local 0.1.3 arm64 rehearsal used the current archive from the isolated
checkout and retained its sealed build provenance. Apple accepted the
app under `462c9e41-52b7-476d-b138-45f29131e803` and the DMG under
`b65e4e0f-0e62-4f18-ac32-04edcd5e45fa`. Gatekeeper, ticket validation,
and the quarantined-app check passed. The relocated bundle suite passed
from the final DMG with the signed extension registry enabled. A
separate ordinary-start fixture restored the approved extension and
skipped a damaged installation. The 0.1.2 and 0.1.3 DMGs passed the
signed replacement preflight. The local release catalog test selected
and verified the 0.1.3 image for 0.1.2. The DMG SHA-256 is
`2b4da04fd3f0d0234378036e8313fad614a10a6c1947c9639b7897c27c12a2c1`.
The 0.1.2 to 0.1.3 runtime replacement passed with the file Keychain
fixture, including both profile locks and the network dictionary paths.

The local 0.1.4 signing rehearsal reused the existing non-component
output and rebuilt the affected browser framework objects before
relinking on a RAM volume. Preparation used the same Developer ID
certificate as 0.1.2. The relocated production-identity bundle passed
signature and Hardened Runtime verification, sandbox override checks,
Workspace and DevTools startup, two encrypted profile fixtures, and
private-window isolation. The signed Workspace download fixture
recovered an interrupted transfer with quarantine and discarded a
separate abandoned checkpoint. The ordinary HTTPS save picker and
its crash checkpoint still need signed end-to-end coverage.

The standard local build plan generated successfully with 13,755
dependencies, zero `//chrome` labels, and only the expected Chromium
version and branding inputs under `chrome/`. The output's missing
Ninja command log caused the dry run to schedule 43,873 actions. The
signing rehearsal used the targeted relink, and its temporary bundle
was removed after verification. The rehearsal did not submit to Apple
or produce a distribution DMG.

The resumed local arm64 non-component build uses `out/ZitterContentBundle`
with ThinLTO and PGO disabled. On that output, the dependency audit
found 13,755 labels, zero under `//chrome`, and only
`chrome/VERSION` and `chrome/app/theme/chromium/BRANDING` as Ninja inputs
under `chrome/`. The sorted label digest is
`958d7e4d28f71c1e20f4b57ecbeb22859a7f979fa9f8f683379052e1c436518b`.
GN header checking passed for `//zitter_content:browser_framework_shared_library`.

A second 0.1.4 rehearsal relinked the current updater and browser code
from the isolated non-component output. It passed the signed runtime
checks before submission. Apple accepted the app
(`8a5c873f-81e0-46c2-bad4-af5c6a422a12`) and the DMG
(`8afe79af-4aa7-4f7e-be89-6018ff55a9a5`). The mounted app and a
quarantined copy passed Gatekeeper distribution checks. Fresh startup
from the final DMG passed twice with the file Keychain and verified
both network dictionary stores. The 0.1.3 and 0.1.4 packages have the
same designated requirement.
The runtime replacement from 0.1.3 to 0.1.4 passed with the file
Keychain fixture after ad hoc re-signing the outer bundles of the
disposable copies. It retained both profiles' tabs, local storage,
encrypted cookies, and network dictionary locations. With the original
release signatures, the same fixture stopped at the isolated startup
guard because the injected file Keychain was unavailable. The release
signature checks passed independently. Runtime replacement with the
original signatures still needs a disposable account and its system
Keychain.
An additional password trial saved a credential with the signed 0.1.3
app and autofilled it with the signed 0.1.4 app in the same disposable
profile, using the verifier's isolated key fixture. The 0.1.4 image
also passed its password manager and time-range clearing trials. The
system Keychain authorization trial remains separate.
The signed 0.1.4 app also passed the Workspace download restart and
discard trial with quarantine required. Its earlier production startup
guard rejected the standalone transport probe before navigation. Repeat
that transport check on a newly signed candidate with the later
disposable download entry point.

The retained rehearsal package is
`.chromium/content-cutover-0.1.4.a783c864/Zitter-v0.1.4-macos-content-arm64.dmg`
with SHA-256
`f730915849f4682f60c527645a01de664e4550c6d988e90b5abd76667a1c518d`.
The build record identifies the dirty worktree. The package has not
been published.

A subsequent read-only check of the updater's public GitHub catalog
found no Content macOS DMG among the 15 stable releases in its
20-release response. Local validation accepted the retained 0.1.3
and 0.1.4 DMGs as a signed replacement pair, including their stapled
tickets, increasing versions, and matching designated requirement.
The rebuilt updater metadata probe selected and verified the retained
0.1.4 image against a local catalog in the GitHub API format. The same
probe found no match in the actual public response. The ordinary
delivery trial still needs a published Content asset.

### Rehearse a Content-to-Content update

`tools/verify_content_upgrade.py` replaces the application at the same
installation path while keeping a disposable profile root. The earlier
bundle starts without arguments and saves two workspaces. Each profile
creates a distinct persistent cookie and local storage value at the
same loopback origin. The replacement starts without arguments, restores
the active workspace and both sets of tabs, and reads back both profiles'
values. A read-only SQLite check compares the encrypted cookie rows
before and after replacement.

The macOS arm64 rehearsal passed from the 0.1.0 production-identity
candidate to the local Content build staged as 0.1.1. The two staged
executables use ad hoc outer signatures and a private file Keychain
whose fixture item trusts both executables. This exercise verifies
profile compatibility across the bundle replacement. Release delivery
and authorization by the system Keychain for successive Developer ID
builds remain separate checks.

To test a signed replacement with the system Keychain, run the verifier
in a disposable, logged-in macOS account with neither a Content profile
root nor its Safe Storage item:

```sh
python3 tools/verify_content_upgrade.py --system-keychain --network-dictionaries \
  /path/to/Zitter-v0.1.2-macos-arm64.dmg \
  /path/to/Zitter-v0.1.3-macos-arm64.dmg
```

The verifier also accepts application bundles. For image inputs it
validates both image tickets and stages each app from a read-only mount
before checking Developer ID signatures, app tickets and matching
designated requirements. Run with `--validate-only` in place of
`--system-keychain` to perform those checks without launching either app
or querying the Keychain. Pass `--scratch` to stage both bundles on
another local volume when disk space is limited.

The upgrade verifier runs the same startup, profile selection and
exclusive lock checks on both builds. With `--network-dictionaries`, it
also checks that both Shared Dictionary stores stay under their profile's
`Network` directory before and after replacement. System Keychain mode
checks creation and retention of the Keychain item without requesting
its password. The replacement reads the encrypted fixture cookies in
both profiles.
The signed copies from the 0.1.1 and 0.1.2 DMGs passed staging
validation on the development Mac. A separate run from the same images
restored both profiles and encrypted cookies with the file Keychain
fixture. The startup and Shared Dictionary checks passed on both builds.
The runtime portion still needs a disposable account and retains its
test profiles and Keychain entry there.

The arm64 0.1.5 rehearsal used the integrated non-component output and
Developer ID signing with Hardened Runtime. Its dependency audit
reported 13,755 labels and zero under `//chrome`. The retained
0.1.4 DMG and the new 0.1.5 app passed the replacement verifier with
ad hoc outer signatures and the isolated file Keychain. Work and
Personal restored their tabs, local storage, encrypted cookies and
Shared Dictionary directories. A signed relocation run passed the
Workspace and DevTools smoke tests. Its extension Settings trial
twice timed out before issuing a permission request because the
Workspace window did not become active. Showing it as active at
startup did not resolve the timeout. The installation probe now
restores page focus after closing Settings and waits for the browser
window to become active before issuing each test gesture. The
permission prompt still requires an active window and explicit consent.
The rebuilt Developer ID candidate passed the Settings trial, including
cancel, grant, host access revocation, and persistence across restarts.
Its full signed extension registry run passed. The 0.1.4 to 0.1.5
password prompt trial also passed on the two Developer ID builds
using the disposable probe key. It autofilled the earlier Work
password and verified update, cancel, navigation, hiding the prompt,
and the password manager.

Apple accepted the rebuilt 0.1.5 app
(`bb856dab-8f25-47a1-9229-c1358ffb8ed1`) and DMG
(`1933414f-2da1-48a0-84cd-6609e3603ecd`). The image and mounted app
passed ticket and Gatekeeper checks. A quarantined copy passed the
distribution check. The local rehearsal DMG has SHA-256
`f6507cbdbd04d86442b07ce8c861e5ce3da846e15f82684829d4eca9e6e3bd2d`.
Its build record identifies the dirty worktree. The DMG has not been
published to the update feed.
The 0.1.4 and 0.1.5 DMGs passed the signed replacement preflight with
matching designated requirements. The final 0.1.5 DMG passed the
relocated Workspace, DevTools, private-window and sandbox checks with
its signature preserved. A separate argument-free startup run passed
both cycles and checked both network directories with the file
Keychain fixture. The active macOS account already has a Content
profile root. The system Keychain run still needs a separate desktop
login with an empty Content profile.

The rebuilt updater metadata probe selected the exact notarized 0.1.5
Content arm64 DMG for an installed Content 0.1.4, using a local GitHub
API-format catalog with newer Chrome-backed macOS and Windows-only
entries. It verified the recorded size and SHA-256 against the image.
The same file was rejected with an incorrect catalog digest or size
or when supplied through a symbolic link. It was not selected for
x86_64, the Chrome backend, or an existing Content 0.1.5 installation.
The catalog was local. Delivery through the published feed and
installation using the system Keychain remain open.

The local arm64 0.1.6 rehearsal includes the bounded Ollama connection
check and the streaming answer view lifetime fix. Its GN graph still
has 13,755 dependencies and zero `//chrome` labels. The relocated
Developer ID app passed the Workspace, DevTools, sandbox, private
session and packaged Assistant checks before submission. Apple
accepted the app (`18526406-b3e2-436b-86d6-454494b42470`) and DMG
(`cdfec6b5-54a8-47e8-92c2-80ba600d19c5`). The image, mounted app and
quarantined copy passed their distribution checks. The DMG SHA-256 is
`d46730e56d6dfb18aac81cc499c159fc57750a7d10fe6b7e33074da4e8a9ccc0`.
The 0.1.5 and 0.1.6 DMGs passed the signed replacement preflight.
The file Keychain replacement run restored both profiles, their
encrypted cookies and Shared Dictionary stores. Python unittest
discovery ran 230 tests, with 202 passing and 28 skipped. The image
is a local rehearsal of the dirty worktree and remains unpublished.

The 0.1.5 and 0.1.6 images, their checksums and the system Keychain
verifier were staged together in a local shared directory for the
disposable desktop account. The handoff's read-only mode verified both
checksums, both image and application tickets, and matching designated
requirements. Its system Keychain mode refused the active development
account before launching either app because that account already has
a Content profile root. The positive run still requires a disposable
desktop login.

The signed 0.1.0 candidate saved a form password, then the signed 0.1.1
candidate autofilled it from the same disposable Work profile. The
verifier checked that autofill preserved the encrypted database value
and then exercised editing, cancellation, navigation, hiding the
prompt, and the profile-scoped password manager. Both apps retained
their Developer ID signatures and used the disposable probe key.

```sh
python3 tools/verify_content_password_prompt.py --theme dark --renderer metal \
  --password-manager --previous-content-app /path/to/0.1.0/Zitter.app \
  /path/to/0.1.1/Zitter.app
```

### Rehearse the production bundle identifier

The macOS browser and helpers derive Chromium's base bundle identifier
from the outer app. They accept `dev.zitter.content-lab` and
`dev.zitter.browser`. The production identifier is limited to an
ephemeral Workspace smoke test or a loopback Workspace probe using a
private directory created directly under the temporary directory.
The loopback probe can use Chromium's mock Keychain or a private file
Keychain supplied by the existing test library. The explicitly
prepared-profile mode has the same restrictions in this rehearsal.
It requires a matching layout marker and an existing session file
before taking the profile lock. Keychain requests without either
fixture are rejected. The shared file fixture limits the legacy key
lookup to that file.

To exercise the guard, use a disposable copy of the locally built app,
replace the Content Lab identifier prefix in its browser, framework
and helper Info.plists with `dev.zitter.browser`, and reseal the copy.
Launch the executable directly without installing the bundle. The
rehearsal retains the Content Lab executable and helper names.

The ad hoc signed arm64 rehearsal rejected a default launch, a root
outside the disposable probe directory, and a system Keychain request. It
passed the ephemeral Workspace smoke and the two-profile navigation
test, including exclusive profile locking and rejection of an invalid
session. The same identity also passed the split Workspace storage
migration and login migration with the mock Keychain. Both used
synthetic legacy profiles. The storage trial covered cookies,
localStorage, history, Clipboard rules and rollback. The login trial
checked distinct Work and Personal autofill after restart, unchanged
ciphertext and reopening the restored credential in the legacy app.
Keychain requests without a fixture were rejected before writing to
the temporary profile. GN include checking
passed with 13,755 dependency labels and zero under `//chrome`.
The arm64 production-identity rehearsal also passed both
`--shared-keychain` verifiers with an ad hoc signed Content copy.
The legacy copy generated the encryption key in a disposable
macOS Keychain file. Content opened that key read-only through the
fixture and passed storage, split restoration, login autofill and
rollback. Missing or denied keys and a damaged login database were
rejected before the profiles changed. The test used the production
bundle identifier and real Keychain APIs with a fixture access list.

The storage verifier's `--prepared-root` mode exercised the explicit
handoff with the production identifier and a private shared Keychain
file. The same mode passed replacement, LaunchServices startup and
rollback with the mock Keychain. Wrong layout markers and missing
session files were rejected before locking or loading a page. This
covers the prepared-profile handoff in the temporary installation
trial.

The macOS production identity can opt into ordinary startup through
the signed `ZitterUsePreparedWorkspace` Boolean in the app's Info.plist.
With no user arguments the app uses
`~/Library/Application Support/Zitter Content`. An empty directory starts
a fresh Workspace with the Content key provider and a `fresh-keychain`
layout marker. A recognized fresh root reopens with that provider.
An explicitly prepared `keychain` layout still requires its session and
uses the existing-only legacy key provider. A nonempty directory with an
unrecognized layout is refused. A normal Content Lab build leaves the
opt-in absent.

An ad hoc macOS copy under the production bundle identifier started
without arguments from an empty, disposable root. A private file
Keychain replaced the account Keychain for the trial. The app created
two separate profile network directories, reopened the fresh layout
after process restart, refused a concurrent instance, and rejected an
unmarked nonempty root and an override without the fixture. The
release-signed clean installation with the system Keychain remains open.
The fresh-start verifier accepts a production-identity trial app or DMG.
Its default mode reseals a disposable copy for the private file Keychain.
For the signed trial, run this in a disposable, logged-in macOS account
with no existing Content profile root or Content Safe Storage item:

```sh
python3 tools/verify_content_fresh_startup.py --system-keychain \
  '/path/to/Zitter-Content-Identity-Trial-v0.1.0-macos-arm64.dmg'
```

For a production Content DMG, also pass `--content-release`. Pass
`--scratch` to stage the bundle on another local volume. The system mode
requires the disposable account to own the macOS console session,
preserves the bundle signature, validates the image and app tickets,
and verifies Developer ID, timestamp, and Hardened Runtime on the
nested code. It then checks the profile directories, exclusive lock,
and Keychain entry. It sends a normal AppKit Quit to the process it launched,
checks the saved Work and Personal session, changes the active workspace
offline, and checks that the next launch restores it. It leaves the
test profiles in that account and does not read or print the Keychain
password.

The 0.1.3 DMG passed both fresh-start cycles with the private file
Keychain and initialized both network dictionary stores. The 0.1.2
and 0.1.3 DMGs and their bundled apps passed ticket, nested signature,
and designated requirement checks. The account Keychain run is
pending a disposable desktop login.

The AppKit quit test exposed a crash when Quit arrived before startup
selected a workspace. The host now defers that request until after
selection. The corrected local arm64 build completed both quit and
restart cycles with the private file Keychain and restored the active
workspace selected in the offline session.

To prepare an isolated signed candidate with this setting, add
`--production-identity-trial` to the `notarize_app.py --prepare-only
--content-version` command above. The preparer switches the identifiers
in the copied browser, framework and helpers before signing, and writes
the startup Boolean only to the outer bundle. Use the same trial flag
with `--submit-content-candidate` to resume its notarization.

For the Content distribution build, use `--content-release` in place of
`--production-identity-trial` in both notarization commands. Preparation
sets the display name to Zitter before signing and writes
`work/Zitter.app`. Pass the stapled app to
`tools/package_dmg.py --content-release` to produce
`Zitter-v<version>-macos-<architecture>.dmg` with a `Zitter.app` payload
and the Applications shortcut. Use `--content-release` with the fresh-start
verifier when reading this image. Keep distribution preparation in a
separate destination from each lab or production-identity trial.

The arm64 candidate containing clean startup passed the Developer ID
and Hardened Runtime checks, the Workspace smoke test, and the dark
Metal navigation probe. Apple accepted submission
`473ad753-9f7e-4952-8a3c-e0991c54e6c4` without issues, and Gatekeeper
accepted the stapled app. Apple also accepted the signed DMG under
submission `24cd79ee-848f-4209-bd29-bc3c7d8734b3`. That image predates
the early Quit fix and is superseded for the clean-install trial. The
corrected trial image passed image and app ticket checks, Gatekeeper,
quarantine assessment, and the fresh-start verifier with the private
file Keychain. The separately branded `Zitter.app` passed strict
Developer ID checks and the Workspace smoke test. Its private copy
also passed both argument-free startup and quit cycles. The branded
arm64 DMG passed Apple notarization, Gatekeeper and quarantine
assessment. Its checksum matched the packaging output, and the
fresh-start verifier passed on the copy extracted from the image
using the private file Keychain.

The current source adds HTTP and HTTPS URL declarations to the
distribution plist and queues AppKit URL requests until the regular
Workspace is ready. It uses the existing Workspace link routing and
session path. The completed branded DMG predates this change.
The LaunchServices verifier uses a distinct fixture-only bundle
identifier, a private Keychain file, and a profile root in the temporary
directory. The app copy needs a location outside the system temporary
directory for registration. LaunchServices recognized the copy as an
HTTP handler there. The arm64 cold and warm LaunchServices test now
passes: the startup URL and a second URL sent to the running app both
commit in separate regular Workspace tabs and survive normal Quit.
The `--https` run uses two loopback TLS servers with independent
self-signed certificates. LaunchServices found the HTTPS handler and
delivered both URLs to the same process. Each server saw a TLS handshake
and received no HTTP request. Both URLs were saved in separate tabs.
This passed with a 0.1.2 app copy under the fixture identity and a private
file Keychain. Pass `--scratch` to stage the test bundle on another local
volume without moving the disposable profile.

For the system default test, install the signed Content app on a fresh
disposable macOS account and select it in **System Settings → Desktop &
Dock → Default web browser**. Quit the app before running the verifier.
[Apple's default-browser instructions](https://support.apple.com/en-ie/102362)

```sh
python3 tools/verify_content_external_urls.py --system-default /Applications/Zitter.app
```

This mode checks the Developer ID signatures, notarization and the
default application path for both schemes before launching. It uses the
system default route for the cold HTTP URL and warm HTTPS URL. It checks
that both tabs persist and the app creates its Safe Storage item. The
test retains its disposable profile and Keychain item. The development
account passed signature verification and stopped before launch because
the candidate was not the selected default. The signed launch still
requires a fresh disposable account.
The host installs its Get URL Apple Event handler during
`applicationWillFinishLaunching`, after which the URL queue feeds the
existing Workspace routing. Installing only after AppKit finished
launching lost the initial URL. A separate test also passed by sending
the events directly to the candidate PID. Both used an ad hoc copy,
the distinct probe identifier and a private file Keychain.

```sh
python3 tools/verify_content_external_urls.py '/path/to/prepared/Zitter.app'
python3 tools/verify_content_external_urls.py --apple-event '/path/to/prepared/Zitter.app'
```

Default-browser selection and delivery to the release-signed production
identifier on an isolated account remain open.

The same verifier's `--arguments` mode passed on the local macOS arm64
build. The opt-in startup accepted two HTTP URLs as positional arguments,
loaded them in separate regular Workspace tabs, and saved both after
normal AppKit Quit. The process rejected a credential-bearing HTTP URL
and a file URL before writing profile data. Content validates the
full URLs after Chromium registers its schemes and before opening the
Workspace root.

On macOS the Content network sandbox grants access to each profile's
network directory, which now contains its persistent Shared Dictionary
database and cache. Other persistent storage partitions use distinct
paths under that same profile's network directory. In-memory contexts
still use Chromium's in-memory dictionary storage. The fresh-start
verifier's `--network-dictionaries` check found the database and cache
for both Work and Personal after two launch and quit cycles. The
command-line navigation fixture also passed without the earlier
Shared Dictionary write errors. These checks cover store initialization
and regular navigation.

The local arm64 HTTP fixture also passed actual dictionary transfer.
Work fetched a cacheable `Use-As-Dictionary` response, offered the
dictionary SHA-256 in `Available-Dictionary`, and decoded a Zstandard
`dcz` response. After normal quit and restart, Work offered the same
dictionary without fetching it again and decoded another response.
Personal on the same origin offered no dictionary and read the plain
response. This run used an ad hoc app copy and a private file Keychain.

```sh
python3 tools/verify_content_shared_dictionary.py '/path/to/prepared/Zitter.app'
```

The fixture needs the local `zstd` executable to create its compressed
response. Additional persistent storage partition paths and the
release-signed run on the system Keychain remain open.

The earlier prepared-profile arm64 candidate passed strict Developer ID checks
on all 11 Mach-O paths and completed LaunchServices startup, prepared
Workspace migration and atomic rollback in the disposable installation
slot. Its argument-free launch rejected a trial-root override without
the private Keychain fixture. For the verifier's
`--ordinary-startup --shared-keychain` mode, the environment override
is accepted only with a disposable root and the injected private-file
Keychain fixture.

The arm64 rehearsal rejected the override without that fixture. With
the fixture, the app opened the active Personal tab without command-line
arguments and the loopback page reported the migrated cookie,
localStorage and Clipboard decision. The verifier terminates this
startup instance after receiving the result. Apple accepted the
Developer ID archive containing this startup mode without reporting
issues. The stapled app passed Gatekeeper. Repeating the private-file
Keychain trial against the stapled app stopped at the isolated-probe
guard. The release signature enables Hardened Runtime and library
validation, and the trial relies on an injected fixture library. This
run did not reach the Keychain lookup or ordinary startup. Keep the
release entitlements and verify the default Application Support path
and the system Keychain in an isolated macOS account without injection.

The installer handoff can prepare both the rollback snapshot and the
startup directory from an offline copy:

```sh
python3 tools/profile_migration_trial.py prepare-macos-upgrade \
  '/path/to/offline-profile-copy' '/path/to/rollback-trial' \
  '/path/to/new/Zitter Content'
```

The command publishes a new `keychain` Workspace only if it can restore
the session. It refuses existing destinations and legacy profile
ancestors. If conversion fails, it retains the verified rollback
baseline and leaves the startup destination absent. This handoff is
an offline rehearsal. The arm64 rehearsal generated the startup workspace through this
handoff and opened it without arguments using the shared private
Keychain fixture. The startup page confirmed the migrated Personal
cookie, localStorage and Clipboard decision. The missing-session
regression verified that rollback remained available after refusal.

### Rehearse replacement and rollback

Run the storage migration verifier with both bundles carrying the
production identifier:

```sh
python3 tools/verify_content_storage_migration.py --replace-app --split \
  /Applications/Zitter.app '/path/to/production-identity/Zitter Content Lab.app'
```

The verifier requires both bundles and their copies to pass strict
signature verification against an Apple Developer ID Application
requirement. The signed bundle identifier must be the production
identifier, and the certificate's team must match the installed legacy
app. A regression test confirms that an ad hoc signature with the
production identifier is refused. The verifier stages the bundles
in a private temporary installation directory and seeds the legacy data
from `Zitter.app` and prepares the migrated profiles before replacement.
On macOS it atomically exchanges the two bundle paths using
`renamex_np` with `RENAME_SWAP`. It opens the migrated profiles, then
exchanges the same bundle paths again and opens the restored snapshot
from the legacy executable location. The trial also exchanges the
bundles back if signature verification, launch, or another check raises
after replacement. A regression test exercises both signature and
launch failures using real directory exchanges. The storage trial uses
Chromium's mock Keychain and checks the input snapshots after rollback.
The atomic exchange and LaunchServices check passed with the notarized
production-identity rehearsal. A newer Developer ID app with the signed
startup opt-in also passed `--replace-app --prepared-root --split` after
notarization. That run used the mock Keychain. The older notarized app
predates the prepared Workspace startup mode.
The verifier also accepts the DMG as its second argument in
`--replace-app` mode. It verifies the image, mounts it read-only and
checks the extracted app's existing seal before running the same
replacement trial. With `--prepared-root`, the verifier uses the same
upgrade preparation function as `prepare-macos-upgrade` before it swaps
the bundles. The verifier supplies the mock Keychain layout. The command
line upgrade uses the existing Keychain layout. Both require a restorable
Workspace session and keep the verified rollback snapshot.
The current macOS updater downloads and verifies the DMG, then reveals
it in Finder. The distribution path still needs an installation step
that prepares the offline profile before replacing the app.
The arm64 trial passed with the installed legacy Developer ID bundle
copied to the scratch slot and an ad hoc signed Content candidate.
Both launches used the same scratch app path. The rollback reopened
the restored profile in the legacy browser and verified the original
cookie, localStorage and Clipboard decision. A candidate with the
Content Lab identifier was rejected.

The arm64 trial also passed with the Content copy signed using
Developer ID and Hardened Runtime under the production identifier.
All 11 Mach-O paths had a secure timestamp. After replacement,
LaunchServices opened a fresh instance from the scratch install path
and the app reported the production identity smoke marker. The same
path passed migration and rollback. The rehearsal uses the mock
Keychain and a direct bundle swap.

Apple accepted the Developer ID app carrying the production identifier.
The stapled copy passed Gatekeeper as `Notarized Developer ID`. For a
disposable distribution rehearsal, use the separately named image:

```sh
python3 tools/package_dmg.py --content-candidate --production-identity-trial \
  --app '/path/to/stapled/Zitter Content Lab.app' --destination /path/to/trial-dmg
```

The image retains the signed production identifier inside the app and
uses a separate image identifier and filename. For the GN version
`0.1.0.0`, the image uses `0.1.0` in its filename and artwork.
Apple accepted the rehearsal DMG. Its ticket, the mounted app's ticket,
Gatekeeper and the quarantined copy passed validation. The app extracted
from that image also passed LaunchServices startup, split Workspace
migration and rollback in the disposable installation slot. These
launches used the guarded trial mode and mock Keychain. The follow-up
arm64 image with the signed prepared startup opt-in also passed image
and app ticket validation, Gatekeeper, quarantine assessment, and the
direct `--replace-app --prepared-root --split` migration and rollback
trial with the mock Keychain. Apple accepted the image under submission
`4a5732a1-037d-40b0-bfec-be5fbc9e274b`. Production channel installation
and ordinary startup with the production Keychain remain open.

### Verify an existing macOS signature

Use `--preserve-signature` to exercise a copy of an already signed
non-component bundle. The verifier checks the nested seal before launch
and again after the probes. It retains the existing signature and
entitlements. It checks the Mach-O load commands and the renderer helper
path without requesting dyld tracing, which hardened runtime can restrict.
Component repackaging is incompatible with this mode because it modifies
the signed contents.

```sh
python3 tools/verify_content_bundle.py --preserve-signature \
  --crx-signer /path/to/crx3_build_action '/path/to/Zitter Content Lab.app'
```

The existing ad hoc bundle passed the browser, Workspace, DevTools,
signed extension and profile probes. The verifier rejected a copy with
a changed framework resource before launching it. Developer ID signing,
notarization, and first launch from the distributed DMG still require
verification on a release candidate.

The same verifier can exercise the installed copy from a disk image:

```sh
python3 tools/verify_content_bundle.py --preserve-signature '/path/to/candidate.dmg'
```

The image must contain `Zitter Content Lab.app` and the
`Applications` shortcut. The verifier checks the image checksum,
mounts it read-only, copies the app to a disposable directory and
detaches the image before running the app with its existing signature.
A local compressed image with the ad hoc signed arm64 bundle passed
the sandbox, Workspace, DevTools, and persistent and private profile
checks. An image missing the shortcut was rejected. The local image
did not exercise Developer ID, notarization tickets, or Gatekeeper.

## Local build measurement

Preview the next local Content build with the usual build settings:

```sh
./build.sh --content --plan
```

The command stages the sources, generates the configured GN output,
prints up to 15 pending actions and their total, and exits before
compiling or packaging. To preview an existing output directly:

```sh
python3 tools/stage_content_embedder.py .chromium/src \
  --preview-build out/ZitterContentBundle
```

The output path is relative to the Chromium checkout. The preview uses
the configured output's Ninja regeneration rule to refresh GN if
needed, then counts actions in a dry run of `zitter_content`. It
performs no compilation or linking. Generated outputs that stay the
same can reduce the work during a real build. The action count does
not predict wall time.

For development, `./build.sh --content --build-only` builds the GN target
without signing or packaging. Its default output is `out/ZitterContentDev`,
initialized as a component Release build with ThinLTO and PGO disabled.
Preview it with `./build.sh --content --plan --build-only`. Set
`ZITTER_CONTENT_OUT` to reuse an existing local component or debug
configuration. Packaging still requires the non-component Release output.
Both build modes enforce the dependency check and use local Ninja.
The existing macOS component output passed the preview and dependency
check with 10,119 pending actions and zero targets under `//chrome`.
Build-only on the up-to-date non-component output completed with no
work, and the packaging command rejected the component configuration.

On the idle macOS arm64 non-component output, touching one staged
file at a time produced these dry-run counts:

| Staged input | Scheduled actions |
| --- | ---: |
| `workspace_lab.cc` | 4 |
| `lab_clients.h` | 5 |
| `workspace_lab.h` | 8 |
| Chromium's `renderer_preferences.h` | 1,029 |

The source-file run also forced GN regeneration and still reported all
four post-regeneration actions. Restoring the staged files' modification
times returned the output to zero actions.

A cold macOS arm64 dry run compared the production Chrome-backed
`//zitter:zitter` target with the current `//zitter_content:zitter_content`
target. Both used Chromium revision `1a732fcc23b20dd0bcf38f35d72c1fe1ec799d35`,
the bundled Ninja, and identical GN arguments in empty output directories.
The configuration was component Release. Symbols, ThinLTO, PGO, Siso, and
remote execution were disabled. The Chrome checkout was an APFS copy of
the staged production source with its build outputs excluded.

| Target | Ninja actions | C++ compile actions |
| --- | ---: | ---: |
| Chrome-backed Zitter | 57,264 | 43,688 |
| Content Zitter | 43,134 | 33,758 |

The current Content target scheduled 24.7% fewer actions. The targets
still differ in feature coverage and packaging. Ninja's dry run counts
actions without executing them. The separate non-component experiment
below includes completed builds.

### Local compiler cache

On macOS arm64, ccache 4.14.1 bypassed both executions of the
`workspace_lab.cc` compile command from the non-component output. It
reported that it could not use Clang modules. The calls took 12.41 and
11.66 seconds. With all module arguments removed from that command, the
first compilation took 13.09 seconds and a direct cache hit restored the
identical object in 0.04 seconds. The test used a temporary cache and
temporary object outputs.

A complete cache experiment needs a separate Content output with
`cc_wrapper` set to the local ccache executable and
`use_clang_modules = false`. Measure cold and warm builds there before
changing the development default. The single-object run did not measure
linking, Rust, generated sources, or the cold cost of a full build
without modules.

### Matched non-component elapsed-time run

The measurement uses separate staged Chrome and Content checkouts, both
pinned to the same Chromium revision. The GN, Ninja, and Clang binaries
match byte for byte. Each fresh output uses the arguments from the
non-component macOS Content bundle. Symbols, ThinLTO, PGO, Siso, and
remote execution are disabled. Ninja builds run sequentially at six jobs
on the same Apple M3 Pro. The timed cold interval starts after GN
generation. The Content checkout is a frozen APFS copy of the staged
sources with all build outputs excluded.

The fresh outputs planned 57,951 actions for Chrome and 43,834 for
Content. The first preflight passed a GN label directly to Ninja and
stopped before any compilation. The runner then used the generated
Ninja aliases, repeated the dry runs, and started the timed Chrome build.
One-job, low-priority Content verification builds for the sandbox,
TLS, prepared workspace, native menu, profile and site zoom, site
settings removal, download history, favicon, saved passwords, and the
WebAuthn transport, visibility, virtual authenticator, extension
host grants, permission queries, alarm checks, alarm import, download
history import, ad blocking diagnostics, network and cosmetic rule trials,
and Assistant settings, credential migration, runtime packaging, panel,
history and Ollama setup work overlapped the Chrome cold run, along with short
runtime probes. They used the development checkout and its relocated
bundles.
The frozen benchmark sources remained unchanged.

After each cold build, the benchmark requires an idle target and times
the same comment edit to the byte-identical `arc_importer.cc`. It
also counts the dry-run actions for the respective checkout's
`renderer_preferences.h`. The test restores each file and its original
modification time. Compare the full link and bundle steps for each target
alongside the action count. The two targets retain their existing
differences in feature coverage and packaging.

| Measurement | Chrome | Content |
| --- | ---: | ---: |
| Completed cold Ninja actions | 57,951 | 43,834 |
| Cold time measured by the benchmark timer | 3 h 43 min 41 s | 2 h 52 min 31 s |
| Cold wall time including system sleep | 3 h 43 min 41 s | 3 h 33 min 32 s |
| One leaf edit, including link and package steps | 33.12 s | 26.09 s |
| Leaf edit actions | 2 | 4 |
| Renderer preferences header actions, dry run | 1,141 | 1,029 |
| C++ and Objective-C++ actions in that header plan | 1,012 | 969 |

Both cold commands exited successfully. The benchmark timer excludes
sleep on this Mac. The Content run crossed three sleep intervals totaling
about 41 minutes in the macOS power log, consistent with the difference
between the benchmark timer and Ninja's timestamps. The Content run used
24.4% fewer actions and 22.9% less active time on this pair. Wall time
including those sleep intervals was 4.5% shorter. The warm commands
returned to idle after each source restoration. These are measurements of
the frozen development configurations on one machine. Differences in
feature coverage, packaging and competing work limit attribution.

During the resumed local arm64 non-component build at eight jobs, the
Ninja status lines for actions 20,000 through 20,999 all referred to V8.
At action 21,624, the partial Ninja command log recorded about 137
summed job-minutes for V8 object files and 106 for objects under
`obj/third_party`. These are overlapping command durations from a
partial build, so they cannot estimate time remaining or compare release
configurations. Preserve the output directory and its Ninja logs for
incremental runs to avoid repeating this work.

A follow-up repeated the same leaf edit three times per target, with
alternating order and an idle-sleep assertion. It started and finished
each repetition with an idle Ninja plan. Chrome took 33.11, 31.38 and
32.00 seconds, with a median of 32.00 seconds. Content took 25.87,
13.57 and 25.25 seconds, with a median of 25.25 seconds. The observed
median difference was 21.1%. The spread in the Content timings was
primarily in compilation and linking. Bundle copy steps added less
than 0.1 seconds in each repetition.

### Earlier non-component warm measurements

For a warm local measurement, use the same pinned source revision and
six Ninja jobs. Confirm each output is idle with `ninja -n`, append a
temporary comment to the byte-identical `arc_importer.cc`, dry-run the
plan, then run the complete GN target and record elapsed wall time.
Restore the file and its modification time before testing the shared
`renderer_preferences.h` header with a dry run. Confirm that Ninja
reports no work after restoring each file. Keep the Chrome output in a
separate APFS copy of the staged source and build directory. Run any
regeneration and rebuilds needed to make the copy idle before timing it.

The two existing non-component macOS arm64 configurations produced the
following results on an Apple M3 Pro. The source file and the shared
header were byte-identical across the checkouts.

| Measurement | Chrome with ThinLTO and PGO | Content without ThinLTO or PGO |
| --- | ---: | ---: |
| Leaf edit elapsed time | 99.23 s | 24.44 s |
| Leaf C++ compile from Ninja log | 0.95 s | 1.04 s |
| Relink from Ninja log | 88.45 s | 18.38 s |
| Leaf actions executed | 2 | 4 |
| Shared-header actions planned | 1,141 | 1,031 |
| Shared-header C, C++, and Objective-C++ compilations planned | 1,012 | 971 |

The Content target relinked its browser framework and copied it into the
app. The Chrome target relinked its main executable. The measurements
cover one run of each configuration and exclude Chrome clone preparation.
The optimization settings and link targets differ, so the elapsed-time
difference cannot be attributed to the backend alone. The header edit was
only dry-run. Ninja reported no work after restoring both source files.

After switching the default Content build to the dedicated fingerprint
channel, a comment appended to `zitter_fingerprint_key.cc` scheduled
eight Ninja actions, including one C++ compile, the framework relink,
snapshot generation and bundle copies. This dry run started from an idle
non-component output after the schema rebuild. The earlier shared
`renderer_preferences.h` edit scheduled 1,031 actions for Content.
The new measurement covers changes confined to the key implementation.

The initial macOS component build completed 34,186 Ninja actions in about
2 hours 17 minutes at six jobs, with ThinLTO and PGO disabled. This is a cold
build of the minimal lab and does not give a like-for-like comparison with
the current production build.

During the first macOS component build, 798 of 32,647 valid Ninja
dependency records referenced a renderer preferences header. The count
includes both source headers and generated Mojo headers, with overlaps
counted once. It describes the completed compilation records at that
point. It does not include new sources, regeneration, or relinking.
When the staged GN inputs change, `ninja -n` may only report regeneration.
After regeneration, Ninja initially scheduled 8,622 actions for the first
combined build. It reduced the count to 2,220 by the first link attempt after
checking whether generated outputs had changed. The count includes adding the
Workspace, its services and dependencies, and rebuilding affected Chromium
code. It does not isolate the cost of the fingerprint patch or any individual
header.

The password probe dry run listed 23,950 actions in an empty output and
1,275 after reusing the component build artifacts in a separate APFS clone.
The build with reuse completed all 1,275 actions in 13 minutes 15 seconds
at two jobs while the Workspace build was running. The empty output was
measured only with a dry run. The clone operation reported a transient file
that disappeared during the active build. Clone an idle output and
regenerate GN before reuse.

## Profile storage probe

After staging and building the profile probe, run this from the Zitter
migration worktree in a macOS GUI session.

```sh
python3 tools/verify_content_profiles.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The probe first checks that a missing key stops startup before profile
creation. It then launches six browser processes against one loopback origin. It seeds
distinct persistent cookies and `localStorage` in Work and Personal and
reads them after restart. It also seeds a private session and opens a fresh
one, which must see empty storage. Each navigation checks the HTTP cookie
header and the values seen by JavaScript. Only the two durable directories
may remain alongside the disposable key fixture. The probe reads both
cookie databases after exit and requires an OSCrypt ciphertext entry with
an empty plaintext value.

The probe uses synthetic data in directories it creates under the system
temporary directory. Chromium's Network Service encrypts cookies through
OSCryptAsync using a random key fixture shared across launches. The network
process receives access only to the profile's Network directory. Before
general browsing in persistent profiles, replace the fixture with the
platform key provider. Verify that copies of existing profiles can read
the legacy Keychain and DPAPI formats before changing any production data.
This probe covers sequential
launches. Simultaneous profiles, workspace switching, and private window
lifetime need a separate integration check.

The profile sources passed GN include checking and Clang syntax checking
against the pinned revision. The linked macOS app passed all six launches,
including the missing-key guard and encrypted cookie checks.

The same probe exercises the profile's Content permission delegate through
`navigator.storage.persisted()` and `persist()`. Only the Work profile's
active primary document at the exact fixture origin is granted persistent
storage. Personal, private browsing, and an iframe at the same origin
must report denial. Capabilities without an implemented host flow
remain denied. The permission integration passed GN and
Clang checks and the linked macOS probe passed the grant and denial
assertions. Native consent UI, saved settings and OS permission checks
remain open.

### Clipboard permissions

Content admits sanitized asynchronous clipboard writes from a secure,
focused primary document only while the browser still has transient
user activation. A renderer declaration by itself is insufficient.
The write grant is evaluated per request. The site's saved Block
decision denies programmatic access. Custom-format writes, workers
and embedded frames have no grant through this permission delegate.

The browser-side paste hook also checks the active frame, focus, visible
WebContents and recent user interaction before the clipboard broker can
read data. This protects the existing user-initiated paste path,
including paste into ordinary non-HTTPS forms, independently of the
async permission status. Native menu and shortcut coverage remains a
separate runtime check.

```sh
python3 tools/verify_content_clipboard.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The disposable macOS probe intercepts the actual renderer write at the
clipboard host without committing it to the system clipboard. It checks
a button-initiated plain-text write, a denied request without a gesture,
a same-origin iframe with test-injected activation, and a custom-format
write after a button gesture. It also checks that the browser-side paste
gate denies a hidden page after interaction. All four cases passed
on macOS. Persistent and private profile isolation and Workspace restart
regressions also passed. The shared permission and browser-client units
compiled to Windows x64 COFF objects. Native Windows runtime verification remains
pending. The macOS component graph still has 13,603 dependency labels
and zero `//chrome` labels.

### Saved Clipboard site settings

Workspace opens the shared site panel for eligible secure origins. Its
Clipboard row supports Allow, Block, and Ask. The panel captures a weak
reference to the exact active document. A callback from a previous
navigation or another selected tab cannot change permissions.

Work and Personal store their decisions separately in `Content Site
Permissions`. A rule is keyed by the full canonical origin including
scheme and port. Ask removes the explicit decision. Private contexts
keep their own decisions in memory. The loader rejects malformed or
unsafe preference files before creating the Workspace profiles.

Allow grants programmatic read permission in a focused and visible
primary document, subject to Permissions Policy. Status queries and
permission change notifications use the same Permissions Policy check
as the Clipboard request. Block denies
programmatic reads and writes. Normal user-initiated paste still uses
the browser's interaction check. Clipboard reads recheck the destination
before returning bytes after an asynchronous platform operation.
Permission observers in an open page receive changes when the user edits
or resets a decision. At Ask, a read requested by the focused, visible
primary document with transient user activation opens a Workspace dialog.
The dialog names the full origin and consumes that activation. Allow for
this site saves an explicit rule in the current profile. In a private
Workspace the rule stays in memory. Dismissal leaves Ask unchanged. A tab
or profile switch, a new document navigation, or closing the Workspace
cancels the pending request.

```sh
python3 tools/verify_content_site_permissions.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app' \
  --theme light --renderer compositor
```

The fixture uses Work and Personal at the same origin with opposite
decisions, observes live permission status changes, reopens the browser,
and resets only Work. It also tests a separate loopback port, a private
profile at the original origin, and refusal of a malformed rules file.
It checks that a saved Allow still yields denied status when the
document's header disables Clipboard or when a same-origin iframe
explicitly requests delegation. Both cases preserve the saved rule.
The site panel passed in both themes on the compositor and Metal.
The prompt verifier requests the permission from a loopback document
and checks dismissal, late acceptance after navigation or profile switch,
and a saved grant that reopens only in Work.

The unchanged 0.1.1 Developer ID app and the relocated signed Content
0.1.4 DMG passed the durable Clipboard rules and prompt suite in dark
Metal. Use `--workspace-only` on the
production-identity bundle to exercise the Workspace path. The default
suite also launches the standalone private probe on a lab bundle.
Local runs exposed a focus race in the test sequence. The verifier now
starts the independent private probe before the rapid Workspace restarts.
The Workspace prompt probe waits for focus before sending test
activation and waits for Content to observe activation before asking
for permission. The rebuilt arm64 lab passed the complete dark Metal
suite, including a renderer read from the isolated AppKit pasteboard
after consent and refusal to start that read without its fixture.

On macOS, add `--read-clipboard` to exercise the renderer's
`navigator.clipboard.readText()` promise. The verifier compiles and loads
a test-only library that substitutes a unique AppKit pasteboard in the
disposable browser process. It seeds a fixed string on that pasteboard.
The app refuses the read probe when the library is absent. The probe
clicks a fixture button, accepts the Workspace prompt, and checks that
the renderer receives the fixed string through Chromium's native
clipboard backend. The test does not read or write the general
pasteboard. The relocated arm64 bundle passed with both themes and
renderers. Native Windows clipboard execution remains a separate gate.

### Camera and microphone

The Workspace handles `getUserMedia` with Content's device list and
`MediaStreamUI`. Only a user-activated request from the selected,
focused primary document can open the site prompt. The request must
match the document's trustworthy origin and its camera and microphone
Permissions Policy. The host accepts camera and microphone capture,
rejects unsupported media request types, and matches any requested
device IDs against the available devices.

The dialog offers Allow once. The decision is scoped to the requesting
document and stream. The Workspace handles one capture stream at a time.
The indicator names the origin and offers Stop.
Leaving the tab, switching profiles, navigating to a new document or
closing the Workspace cancels consent and stops capture. The site has
no saved grant to reuse. On macOS the app asks AVFoundation for system
permission after site consent. Windows relies on the capture service
and operating system when opening the selected device.

```sh
python3 tools/verify_content_site_permissions.py --media-capture \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

The probe uses Chromium's fake camera and microphone with the real
Workspace consent dialog. It checks explicit denial, policy denial
without a prompt, two live tracks and a rendered video frame, and
the end of both tracks after Stop or a profile switch. The relocated
non-component macOS bundle passed all four cases in light and dark
with the compositor and Metal. Actual hardware, native OS consent and
denial, and native Windows execution remain unverified. Durable camera
and microphone site settings remain a separate migration gate.

### Share a tab in the current profile

The Workspace handles `getDisplayMedia` for video from a loaded HTTP or
HTTPS tab in the requesting profile. The selected, focused primary
document needs a trustworthy origin, transient activation, and the
`display-capture` Permissions Policy. The prompt names the requesting
origin. The picker identifies each source by its tab number, origin
and page title. It respects requests to exclude the requesting tab and
offers only tabs in the same BrowserContext. A site cannot supply an
alternate source ID.

The host revalidates both documents after consent. The capture indicator
names the requesting origin and the selected tab and offers Stop. Leaving
the requesting tab, switching profiles, navigating either tab to a new
document, closing the source tab or closing the Workspace revokes capture.
The host stores no grant. When the site requests audio, the picker
offers an unchecked option to include sound from the selected tab.
The host uses Content's WebContents loopback for that exact source and
labels local playback suppression when the site requests it. The
indicator identifies whether audio is included. Tab audio does not
authorize the microphone or use system loopback. See the [W3C Screen Capture draft](https://www.w3.org/TR/screen-capture/#dom-mediadevices-getdisplaymedia)
for the API's activation, consent and optional audio requirements.

```sh
python3 tools/verify_content_site_permissions.py --tab-capture \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

The relocated non-component macOS bundle passed all ten scenarios in
light and dark with the compositor and Metal. The loopback fixture verifies
explicit refusal, policy refusal, exclusion of the current tab when the
only other page is in Personal, Stop from the
indicator, and stop after a Work-to-Personal switch. It selects a second
Work tab with a loaded Personal decoy and checks that only the expected
choices are offered. It verifies the actual pixels from both Work sources
and the renderer's `ended` event after the background source closes or
navigates. The audio scenarios verify that the checkbox is absent when
audio was not requested and unchecked by default when offered. With
explicit consent, the requesting renderer measures the 997 Hz tone
generated by the background source. Both tracks must end after Stop
or source navigation. The probe also checks that tab audio does not
grant microphone access. Each case uses fresh profiles and real
Chromium tab capture without fake devices or OS desktop capture.

### System window and screen sharing on macOS

On macOS 14 and newer, the Workspace can offer a system window or
display alongside tabs. It asks for site consent before opening
Chromium's native ScreenCaptureKit picker, where the user selects
the specific surface. It revalidates the requesting document after
the system selection and grants video for the returned session only.
Zitter disables source replacement in an active native picker session.
The screen option is absent when the request excludes monitors.
Tab audio is disabled for system sources.

The synthetic window and monitor tests use Chromium's fake desktop
source with the real Workspace consent dialog. They verify frame
delivery, the window and monitor settings reported to the renderer,
monitor exclusion, video-only capture, and the Stop control.
The macOS matrix passed these tests and the ten tab scenarios in
light and dark with the compositor and Metal.

```sh
python3 tools/verify_content_site_permissions.py --native-desktop-cancel \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

This probe requests a system window, waits for a native picker session,
then cancels before choosing a surface. It requires rejection in the
renderer. The probe passed in the relocated arm64 app on macOS 27.2.
No actual desktop source was captured. Real window and display delivery,
OS-level permission handling, desktop audio, support for macOS
versions below 14, and native Windows window and screen sharing remain
open.

The unchanged 0.1.1 Developer ID app passed four synthetic camera and
microphone cases, ten tab capture cases, and three synthetic desktop
cases in dark Metal. The system picker cancellation also returned
`NotAllowedError` without selecting a source. The relocated signed
Content 0.1.4 DMG also passed all four fake camera and microphone
cases, ten tab capture cases and three synthetic desktop cases in
dark Metal with disposable Workspace profiles. The relocated 0.1.4
app also opened the native desktop picker on macOS 27.2 and returned
`NotAllowedError` after cancelling without selecting a source.
The notarized 0.1.5 image repeated this native cancellation check in
dark Metal.

For the interactive positive case, use a disposable macOS desktop
session and choose only the loopback fixture window or a display
containing test content in the native picker:

```sh
python3 tools/verify_content_site_permissions.py --native-desktop window \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
python3 tools/verify_content_site_permissions.py --native-desktop screen \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

The fixture uses disposable profiles and a loopback server. It checks
the selected surface type, a live video frame, and track termination
after the Workspace Stop control. It does not transmit or save frames.
The initial interactive window run reached the system picker and timed
out without a source selection. A subsequent run with a manual
selection of the synthetic Workspace window verified delivery of
a native window frame and track termination after Stop.
The notarized 0.4.11 bundle subsequently passed four full-display trials on a disposable macOS desktop.
The rebuilt lab passed the existing tab and synthetic desktop
capture regression suite.

### Clipboard permission migration and rollback

The offline trial can convert durable, exact-origin Clipboard decisions
from each legacy profile's `Preferences` file. The importer verifies
the candidate file against the baseline manifest before writing `Content
Site Permissions` into a new profile directory. It retains Allow and
Block, drops explicit Ask, and preserves scheme and non-default port.
It rejects unsupported scopes, temporary or auto-revocable decisions,
related-site rules, and non-Ask defaults. The tool checks for managed
settings recorded in `Preferences`. Operating system policy, site
permission embargoes, and other permission categories are outside this
trial.

Use an offline copy with no browser lock. The two target directories
must be fresh and outside the trial. Create their parent directory
before running the import.

```sh
python3 tools/profile_migration_trial.py prepare "$offline_root" "$trial"
python3 tools/profile_migration_trial.py copy-clipboard-permissions \
  "$trial" work-profile "$content_root/workspace-work"
python3 tools/profile_migration_trial.py copy-clipboard-permissions \
  "$trial" personal-profile "$content_root/workspace-personal"
python3 tools/profile_migration_trial.py rollback "$trial" "$rollback_root"
```

The macOS probe creates Allow and Block through the installed legacy
Zitter settings handler in separate disposable profiles. It assembles
the offline fixture with those decisions assigned to Work and Personal.
It verifies that Content reads the two decisions after launch, keeps
another port at Ask, and clears only Work on reset. It compares the
rollback to the baseline before opening it in the legacy browser, which
reports the original Work decision. The source and both trial copies
remain byte-identical. The probe uses the page's permission state and
does not read the platform clipboard.

```sh
python3 tools/verify_content_clipboard_migration.py \
  /Applications/Zitter.app '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

## DevTools console transport

The Content Workspace has an Assistant host for the existing panel and a
configured local Ollama model. Cutover requires the remaining providers,
credential-backed requests, provider settings, page and action consent,
and the ad blocking diagnosis and repair flow.

The existing assistant console reader is shared by the Chrome host and the
Content lab through `//zitter:devtools_console`. It attaches directly to the
approved document's Content agent host and sends only the two console and
diagnostic subscription commands. The client has no remote listener, browser
agent privileges or local file access. The Chrome host still checks document
consent before constructing the reader.

The Work profile probe reads a buffered console marker from the loopback page.
It then navigates the same tab to a new document on the same origin and requires
the old reader to reject the result. The marker and replacement fixture passed
the local server and script checks. The linked Work profile run passed the
live protocol check.

### Assistant consent for console, DOM and hard reload

The Assistant can read the selected page's console or inspect its DOM
with tab and page access. DOM edits and hard reload also require browser action access.
Each operation prompts through the shared Assistant panel. The Content
host binds the one-time approval to the operation, its bounded arguments,
the current document and URL, and the Assistant settings generation.
The host rechecks the selected page and document before returning
asynchronous results.

Console reads reuse the restricted reader above. DOM inspection and
edits reuse the shared isolated-world page script. The script filters
protected elements, limits the number of matches and restricts edits to
supported operations on a single eligible element. Hard reload uses
Chromium's cache bypass and repost handling. Content observes the
specific top-level navigation and waits for its new document to finish
loading before reporting success. An interrupted navigation, error page,
or later document change cannot be reported as a completed reload.

The loopback provider probe declines and approves separate console
requests, inspects a selector that also matches protected content,
applies and reads back a text edit, and changes the URL before approving
a pending DOM request. It verifies that the expired request returns
no DOM data. The relocated macOS arm64 app passed in both themes on
compositor and Metal, followed by the legacy rollback check. GN include
checking passed, with 13,743 dependency labels and zero under
`//chrome`.

The reload probe declines one request and approves two. The loopback
server checks that only the approved requests bypass the cache. The
first returns a page with a fresh title, and the second returns HTTP 204
to verify that Content reports the uncommitted reload as an error. The
relocated macOS arm64 app passed in both themes on compositor and Metal,
followed by the legacy rollback check. The separate native Find and
Hard Reload regression passed in dark Metal.

### Local DevTools frontend

F12 in the Content Workspace opens or closes DevTools for the active tab.
It opens docked. Chromium's dock controls can move the same frontend to a
separate window and dock it again. When docked, the frontend reports the
inspected page bounds, which the host clips to the current tab pane. The
frontend loads from the bundled Chromium resources in a
separate off-the-record BrowserContext. Its host accepts messages only from
the bundled frontend document and attaches to the selected tab through
`DevToolsAgentHost`. Switching tabs closes the previous attachment. The
client uses Chromium's untrusted target mode and disables local file reads,
local file writes, and privileged operations. The host uses no remote
debugging listener, remote frontend, or network resource proxy.

Run the frontend smoke in a macOS GUI session with a timeout.

```sh
'out/ZitterContent/Zitter Content Lab.app/Contents/MacOS/Zitter Content Lab' \
  --zitter-content-workspace-smoke-test --zitter-content-devtools-smoke-test
```

The test expects `Zitter Content DevTools frontend ready` and the Workspace
smoke marker. It requires the bundled frontend to request the attached
tab's identifier, checks that Chromium rejects enumeration of other targets,
and uses the frontend's dock controller to undock and dock again. After
redocking it verifies the native view bounds of the inspected page, opens
the Console panel, and requires the renderer to acknowledge
`Overlay.setInspectMode` with `searchForNode` on a child session. It then
sends mouse events to the inspected renderer and checks that Elements
selects the fixture heading. The probe calls the same frontend actions as
the Workspace shortcuts and injects mouse events at the render widget.
The macOS smoke passed in both themes with compositor and Metal. GN
listed 12,603 transitive dependency labels with zero `//chrome` targets.

The Windows package verifier runs this smoke in the dark compositor
Workspace from the extracted executable with a system-only PATH. It
requires both the Workspace and DevTools markers. The dispatch and
marker checks passed against a relocated macOS bundle. A verifier test
also rejects a launch that emits the Workspace marker alone. Native
Windows execution remains pending.

The Console and Inspect commands dispatch Chromium's frontend actions
after the inspected target connects. The embedder supplies Content's
pass-through DevTools manager delegate so forwarded commands use the
receiving child session. Without it, renderer commands can fall through
to the browser-only parent session and return a method-not-found error.
The untrusted session still reports unavailable methods in the Profiler,
Animation, Autofill, and ServiceWorker domains. Source-map fetching,
native keyboard and mouse event routing, Windows, and visual verification
remain open.

## First checkpoint

On the pinned Chromium revision, GN generated the macOS component target and
passed the framework include check. The full dependency listing contained
12,126 labels, including host tools, with zero `//chrome` targets. A Clang
syntax check used the lab target's compiler flags and generated headers
available from a same-revision build. The dedicated build linked and produced
the app and helpers. The first launch showed two additional requirements.
Register a native Screen before creating WebContents, and give each helper
an rpath to the component output so it can load Seatbelt before entering
`main()`. After those fixes, the minimal macOS lab passed the launch
smoke test and exited normally.
The component build reported a duplicate ANGLE Objective-C class and a
`waitpid` error during the smoke. Their impact on longer runs has not been
checked.

After the bootstrap passes, put a Content-backed page host behind
`WorkspaceShell`'s existing state and callbacks. `WorkspaceWindow` still
owns Chrome profile leases and service adapters. Move only the parts needed
for page hosting and profile selection first. Test persistent cookies and
site storage across restart in two disposable profiles, then check that a
private context neither shares nor restores that data.

## Migration and rollback trial

Use a disposable offline copy of a Zitter data directory or a synthetic
fixture. Keep both browsers closed while copying. The trial tool validates
the saved workspace-to-profile mapping and creates separate baseline and
candidate directories. Its manifest records the file hashes and preserves
the original layout, including Local State, cookie and login databases,
and their sidecars. The tool rejects browser locks, symbolic links,
Windows reparse points such as junctions, and special files. It checks
for links while traversing the inventory and again before following
entries during a copy.

```sh
python3 tools/profile_migration_trial.py prepare /path/to/offline-copy /path/to/new-trial
python3 tools/profile_migration_trial.py rollback /path/to/new-trial /path/to/new-rollback-copy
```

Use only the candidate for migration. Rollback verifies the baseline
against its manifest and creates a new directory for the old backend.
The command refuses an existing destination or a modified baseline.

Synthetic tests cover recovery after candidate changes, corrupt baseline
refusal, source and destination guards, invalid profile references, and
the legacy workspace layout. Tests that simulate Windows file attributes
also cover a reparse root, a junction encountered during the snapshot
copy, and a linked cookie directory added to the candidate. Rejection
leaves the destination absent. The trial keeps the encrypted stores in
their original format. Full-profile compatibility with the platform key
provider remains open. The extension verifier below checks an
application-level rollback for the local extension store.

### Coordinated Workspace catalogue import

`import-workspace-profiles` copies cookies, localStorage, navigation
history, and the profile password stores. It converts Clipboard preferences,
the password saving preference, and Workspace sessions. The verified
catalogue can contain additional profiles and workspaces, including
multiple workspaces assigned to the same profile. Additional identifiers
are mapped consistently in the catalogue, session, link rules, Time
Machine, and Assistant ownership. Favorites in an unassigned profile
remain in that profile. The importer marks absent built-in workspaces as
removed and retains the active workspace from the legacy catalogue.
For a built-in profile missing from the legacy catalogue, it initializes
an empty directory because the Content backend still reserves both
built-in profile slots. The reserved profile can still appear in profile
management. All profiles are staged beside the destination and published
together. An existing destination or conversion failure leaves the
rollback baseline available.

A macOS arm64 run opened a three-profile offline fixture through the
prepared Workspace path in the notarized app with the mock Keychain.
The imported Research tab acquired its own cookie and localStorage and
reopened both after restart. The importer regression also verified a
shared profile assignment, an unassigned profile's favorite, a routed
host, and a timeline frame under the remapped identifiers. Native
Windows execution and representative offline user profiles remain open.

The `--ordinary-startup --shared-keychain --single-personal-startup`
storage verifier removes Work from a copy of its offline fixture and
launches the prepared app without arguments. The ad hoc macOS run
reopened Personal with its migrated cookie, localStorage, and Clipboard
decision through a private file Keychain and did not request a Work tab.

The importer carries over theme, search engine, sidebar settings,
animation and folder preferences, Time Machine, and update preferences.
It remaps workspace colors and emoji to the imported workspace IDs and
copies the verified browser tools settings. Normal startup reads the
imported theme through the shared Workspace settings loader. The
`--appearance light` and `--appearance dark` storage trials passed
on compositor and Metal without a command-line theme override. They
checked the theme, sidebar pinning, search engine, workspace color and
emoji in the running shell and verified legacy rollback.

```sh
python3 tools/profile_migration_trial.py import-workspace-profiles \
  "$trial" "$content_root" --layout mock-keychain
```

The layout selects the profile directory names and session filename for
the corresponding Content mode. It does not transfer the encryption key.
The default lab launch requires a fresh destination directly under the
system temporary directory. The combined import verifier uses the mock
Keychain. In two Content launches, Work and Personal opened their own
cookies and localStorage and reported Allow and Block for Clipboard at
the same loopback origin. After rollback, the installed legacy browser
reopened Work's storage and retained its Clipboard decision. Unit tests
also cover swapped profile associations and a failure during the second
profile copy.

When `ZitterSession.json` exists, the importer verifies it and the saved
workspace configuration against the snapshot. It retains the active
workspace, selected tab in each workspace, tab order, URLs and titles.
A workspace absent from the saved session opens a blank tab and retains
its saved pins. The import retains the sleeping state of inactive
workspaces. It rejects URLs that Content cannot restore, such as `zitter://rss/` or URLs with embedded
credentials. Extension data is opt-in. Other profile data still requires
separate migration steps.

The macOS verifier supplies a synthetic legacy-format session with two
tabs per workspace and Personal active. Content opens only the selected
tab in each workspace, in the expected order, on two launches using the
converted session. Deferred tab titles survive both launches. Before the
legacy Work checks, the verifier compares the rollback byte-for-byte
against the snapshot and then selects Work in the disposable rollback copy.


#### Opening a prepared workspace

The importer writes a versioned `ContentWorkspaceLayout` marker before
publishing the new root. To reopen an import in a persistent directory
outside the direct temporary fixture layout, pass the explicit prepared
workspace flag together with the workspace mode and root. Use a canonical
absolute path in a dedicated directory. The lab rejects an unexpected
layout, a linked root or marker, and a root at or below a legacy profile
directory. On macOS it also requires owner-only permissions. Startup still
checks the workspace data and takes its exclusive lock.

For a macOS import of data encrypted with the legacy Keychain, use the
`keychain` layout and the two Keychain switches below. The legacy Keychain
provider is read-only.

```sh
python3 tools/profile_migration_trial.py import-workspace-profiles \
  "$trial" "$content_root" --layout keychain
"$app" --zitter-content-workspace \
  --zitter-content-workspace-root="$content_root" \
  --zitter-content-open-prepared-workspace \
  --zitter-content-workspace-keychain --zitter-content-workspace-legacy-keychain
```

On Windows, import with `--layout windows-dpapi`, then pass the workspace
mode, root, and prepared workspace switches. The DPAPI checks still run
under the original Windows account. The macOS verifier uses a synthetic
profile and supports `--prepared-root` with the mock or private file
Keychain.

```sh
python3 tools/verify_content_login_migration.py \
  /Applications/Zitter.app path/to/verify_login_database \
  --content-app 'path/to/Zitter Content Lab.app' --prepared-root --shared-keychain
```

The relocated macOS bundle passed with both the private file Keychain
and the mock Keychain. It required the explicit flag, refused a wrong
layout and a legacy ancestor before locking, and retained both profiles’
credentials across restarts and rollback. The direct temporary fixture
launch passed as well.
The Windows migration verifier checks the explicit flag and marker before
exercising DPAPI in a prepared directory. Its native run is pending.

#### Migrated workspace pins and profile favorites

The importer keeps workspace pin order, folder hierarchy, profile
favorites and the link from an open tab to its pin. It rejects duplicate
IDs, missing or invalid parents, unsafe target URLs and references to
pins outside the tab's own workspace. It preserves closed pins even
when the legacy profile has no saved tab session.

Content renders the restored pins through the shared sidebar. Selecting
an open pin focuses its existing tab. Selecting a closed pin opens its
saved address in the matching BrowserContext. Closing that tab leaves
the pin available to open again. The session keeps those associations
together with tab selection and split layout.

```sh
python3 tools/verify_content_storage_migration.py --pins --split --shared-keychain \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
```

The linked macOS test opens a nested Work pin and verifies its renderer
profile. It checks that the open Work pin is not duplicated and that the
Personal favorite survives across launches. The same test restores
horizontal and vertical splits and verifies the legacy rollback. The
relocated macOS app passed in both themes with the compositor and Metal,
and the split probe also passed with the shared file Keychain.

The Content sidebar can pin and unpin tabs, move leaf pins within the
workspace and into profile favorites, and edit and reset a saved URL.
It also creates, renames, moves and removes folders. Folder moves use
Zitter's shared `PinTree` validation for cycles and depth and preserve
the subtree and sibling order. Removing a folder can promote its
children to the parent or remove the subtree. Removed open pins become
ordinary tabs with the same WebContents. Reordering tabs remaps the
selected tab and split indices while preserving the pane order. Reset
navigates the existing pin tab or opens it at the saved target. Pin and
folder changes queue an atomic Workspace session write while the
application runs. These writes and the final shutdown snapshot use
one sequence, so an older snapshot cannot overwrite a newer one.

```sh
python3 tools/verify_content_storage_migration.py --edit-pins --split --shared-keychain \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
```

The edit probe places a pin among folder siblings, moves it to profile
favorites, unpins it while preserving the active page and split, and
edits a separate pin's URL. It resets the open pin to that URL and
checks that subsequent launches reopen the new target. It also moves a
nested folder, refuses a cycle and a move into profile favorites,
exercises both folder deletion modes, and checks that a live split pane
survives subtree deletion. The edited folder name survives restart.
The relocated macOS arm64 bundle passed in both themes with the
compositor and Metal using the shared file Keychain.

```sh
python3 tools/verify_content_storage_migration.py --crash-pins --split --shared-keychain \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
```

The crash probe waits for the reply to the final pin mutation's write
and sends SIGKILL to the disposable browser process group before
normal shutdown. It requires the changed folder title and pin URL,
both profiles' pin associations, the selected workspace and both
splits to reopen from the acknowledged snapshot. The relocated arm64
bundle passed the forced-exit trial with the shared file Keychain and
the ordinary shutdown trial with the mock Keychain. Changes still queued
at the time of an interruption may be lost. Native Windows execution
remains open.

#### Sleeping workspaces

The importer retains an inactive workspace's sleeping flag. The active
workspace starts awake, matching the legacy app. Content restores tab
descriptors, pins, selection and split layout for the sleeping workspace
without creating its WebContents. Selecting it wakes it and loads only
the selected tab or visible split panes.

Command Center offers wake and sleep actions for the workspace.
Its Sleep command checks for active capture and page beforeunload
handlers. If the active workspace can sleep, Content selects the other
workspace first. It saves the last restorable address and title for
each tab, releases its WebContents, and queues the new session snapshot.
The BrowserContext and profile services stay alive.

```sh
python3 tools/verify_content_storage_migration.py --sleep-workspace --pins --split --shared-keychain \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
python3 tools/verify_content_storage_migration.py --sleep-before-unload --split \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
```

The macOS migration probe passed in light and dark with compositor and
Metal. It submits the wake and sleep commands through Command Center,
checks that Work starts without WebContents, loads in its own BrowserContext,
and releases its pages on sleep. It compares the saved selection, pins and
split layout after each restart and verifies legacy rollback. The
beforeunload variant passed on macOS and required that
declining preserves the active workspace and accepting releases its
pages. The macOS tab audio capture probe passed and required that a
sharing workspace stay awake.

For navigation history, the importer verifies the profile's `History`
database and any SQLite sidecars against the snapshot. It accepts schema
70, the version used by the pinned Chromium revision. In the staged
copy, SQLite checks database integrity, checkpoints the WAL if present,
and retains eligible completed download records as history. The
separate `Favicons` database is migrated per profile.

The macOS verifier seeds distinct visits in the installed legacy app
using disposable profiles. After import and restart, Content lists each
visit only in the matching profile's history panel, and the database
retains its title, timestamp, and navigation transition. Other schema
versions and native Windows history migration remain unverified.

The importer copies each profile's `Login Data` database and its SQLite
sidecars byte-for-byte after checking them against the trial manifest.
It writes an explicit legacy `credentials_enable_service` decision into
`Content Password Preferences` and uses the Content default when the
legacy preference is absent. The copy is limited to the local profile
store. Access to the production key and migration of Windows DPAPI or
App-Bound Encryption data remain separate gates.

### Page source

Content opens page source through Chromium's built-in `view-source:`
navigation from the page context menu, configured shortcut, or macOS
Developer Tools menu. The command uses the last committed HTTP or
HTTPS page and opens in the active Workspace and its browser context.
The command is unavailable for error pages, unsupported MIME types,
existing source documents, and pages reached through a POST. The
source request is browser-initiated and follows the normal Content
network and certificate checks.

Source tabs are transient. Workspace session and Time Machine snapshots
store a blank slot for them. Closing one does not add a recoverable
history entry, and moving one across profiles is unavailable. This
keeps the underlying URL from reopening as an ordinary document during
restore, history recovery, or a profile move.

The Developer ID macOS verifier selected the command from the native
menu. It checked Chromium's committed source mode, the visible
`view-source:` address, and the original browser context. It also checked
that a source tab saved no underlying URL, produced no closed-tab entry,
and rejected another source command. Light compositor and dark Metal
passed. The native menu action was exercised in the find and reload
suite.

### Fullscreen

Content uses the shared Workspace shell for document fullscreen and
the native Views window for window fullscreen. The View menu and
configured shortcut toggle window fullscreen. A visible, active
Workspace tab can enter document fullscreen through Chromium's normal
permission and gesture checks. Entering hides sibling panes and the
Workspace controls. Escape, a tab or Workspace change, tab removal,
or native window fullscreen exit ends document fullscreen. The host
restores the prior window mode and pane layout. A visible, active
Peek can enter through the same permission checks. The host temporarily
moves its existing view into the main pane host and returns it to
the Peek panel on exit. The active Workspace and the Peek page's
browser context stay in place.

The signed macOS verifier enters window fullscreen from the native
menu, then uses the active page's Fullscreen API with user activation.
It checks the committed document state and pane bounds, switches
Workspaces to exit, and verifies that window fullscreen remains until
the menu toggles it off. Light compositor and dark Metal passed. The
signed incremental link reused the existing Rust archive. The new
English menu translation compiled separately.

The signed Peek verifier opens a Personal document while Work remains
active, enters fullscreen through the page API, then exits to the
Peek panel. It checks that the existing document survives, the page's
back stack and Personal context remain available, and Work stays
selected. It then exercises Peek back and forward, promotion into a
tab and split, and canceling and accepting beforeunload. Both the light
compositor and dark Metal passed. The ordinary tab fullscreen test
passed again in both configurations.

### Split Workspace sessions

The importer converts both the legacy left/right pair and the ordered
split list. It keeps two to four distinct tab indices, their weights,
and the orientation. Missing weights in the ordered format use equal
shares. Content accepts sessions without split state as before. Invalid
indices, duplicate panes, or invalid weights reject the import.

The Content host restores each visible group with the shared Workspace
pane layout. It keeps the other profile's views hidden, shows the group
only when one of its members is selected, and saves the group with the
rest of the session. Focusing another pane changes the active tab and
address. Closing a pane removes its tab from the group and updates the
remaining tab indices.

```sh
python3 tools/verify_content_storage_migration.py \
  /Applications/Zitter.app '.chromium/src/out/ZitterContent/Zitter Content Lab.app' --split
```

The fixture imports a horizontal Work pair from the old format and a
vertical Personal pair from the ordered format. It checks that both
members of the visible group are loaded in the correct profile, that the
inactive group is deferred, and that each launch saves the original pane
order, weights, orientation, and selected tab. The migration and
rollback ran with the mock Keychain. Native Windows execution is
tracked separately.

Content handles the configured numbered tab and Workspace commands,
Workspace cycling, and all four Split View focus commands. The
macOS Window menu dispatches Workspace cycling through the focused
Workspace window. Tab cycling uses the shared sidebar implementation
for the active renderer. Numbered commands reject missing tabs or
Workspaces. Workspace switching commands are unavailable in private
windows.

The signed macOS split and tab command verifiers passed with light
compositor and dark Metal. The split verifier selects panes three
and four in a four-pane group, returns focus to the original page, and
tests both directions through the native Window menu. The tab verifier
selects numbered tabs and the third Workspace and rejects out-of-range
commands. Both verifiers retain their existing beforeunload, pin, and
cross-profile assertions. Numbered shortcuts are tested at command
dispatch.

On macOS, the initial dark Metal trial exposed part of a Work frame
when switching from a loaded horizontal split to a vertical Personal
split. The host now detaches the outgoing native views from the window
and retains their WebContents. When switching back, it reattaches the
same views with their existing documents and navigation state.

The visual regression delayed both Personal loopback responses by four
seconds. In light and dark on both compositor and Metal, Personal
showed blank panes until its own pages painted. In dark Metal and light
compositor, switching back displayed the existing Work pages without new
HTTP requests. Closing a pane after reattachment in the light compositor
left the other tab visible. The automated split migration
and ordinary Workspace session probes also check that the inactive
profile's views are detached on macOS. Persisted divider interaction
remains open.

### macOS cookie and localStorage migration

`copy-browser-storage` stages the cookie database, its SQLite sidecars,
and the `Local Storage` directory into a fresh Content profile. It accepts
legacy `Cookies` at the profile root or in `Network` and writes the files
to Content's `Network` directory. It verifies the copied files against
the trial manifest and rejects ambiguous layouts and orphan sidecars.
The operation preserves ciphertext and does not open either store.

```sh
python3 tools/profile_migration_trial.py copy-browser-storage \
  "$trial" work-profile "$content_root/mock-keychain-work"
python3 tools/profile_migration_trial.py copy-browser-storage \
  "$trial" personal-profile "$content_root/mock-keychain-personal"
python3 tools/verify_content_storage_migration.py \
  /Applications/Zitter.app '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The macOS verifier uses the installed legacy app to create a persistent
HttpOnly cookie and a localStorage value in each of two disposable roots.
It assigns those stores to Work and Personal before making the offline
snapshot. Content uses Chromium's mock Keychain provider in a dedicated
Workspace mode. The mode requires a temporary root and a loopback probe,
and uses its own profile, settings, and session paths. In two fresh Content
processes, the loopback server received each profile's distinct cookie,
JavaScript could not read that HttpOnly cookie, and the page read the
expected localStorage value. The `v10` ciphertext stayed byte-identical.
Rollback reproduced the baseline and the legacy app reopened Work's
cookie and localStorage. The source and both trial copies kept their
original file inventories and hashes.

Before creating either persistent macOS Workspace context, the host
checks a temporary copy of each cookie database and its sidecars. The
check requires the pinned schema version 24 and an available encryption
provider. It rejects plaintext rows and validates the host hash in every
encrypted row. For `v10` CBC, re-encryption with the active key must also
reproduce the ciphertext. This rejects cookies Chromium can decode with
its compatibility key for an empty password. The check leaves the profile
databases untouched. If either store fails, the browser exits with status
1 before creating either context.

The verifier tries valid `v10` rows encrypted with an unrelated key and
with Chromium's empty-password compatibility key. In each case only
Personal has the altered row. Neither context reaches the loopback
server, and the inventories of both profiles stay unchanged. The same
verifier then opens both profiles with the correct mock Keychain key.

Production still needs safe access to the existing Keychain item and
a coordinated full-profile import. Older schema versions, recovery from
hot rollback journals, and Windows DPAPI and App-Bound Encryption are
separate compatibility gates.

### Extension data migration and legacy reopen

A copied profile can transfer a selected extension's
`Local Extension Settings/<id>` LevelDB into a new Content profile
directory. The trial tool resolves the legacy profile ID using the saved
workspace mapping, checks the store against the snapshot, and validates
that LevelDB's `CURRENT` names a manifest within the store. It refuses a
changed source, linked path, or existing destination. The copy includes
database sidecars and leaves the baseline available for rollback.

```sh
python3 tools/profile_migration_trial.py copy-extension-storage \
  /path/to/new-trial work-profile <extension-id> /path/to/new-content-profile
```

The local macOS verifier opens the installed legacy Zitter with a
temporary profile, mock Keychain, and loopback DevTools. The legacy
extension writes a counter and a marker through `chrome.storage.local`.
The verifier closes the legacy browser, snapshots Work and Personal,
and transfers only the Work store. Content installs a signed fixture
with the same manifest key and extension ID. Its worker reads the
legacy marker, advances the counter from 41 to 42, and leaves Personal
without an extension store. The legacy browser then opens a fresh
rollback copy and reads the original counter and marker. The source
and offline candidate stores retain their original hashes.

```sh
python3 tools/verify_content_extension_migration.py /Applications/Zitter.app \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The verifier requires the local CRX signing helper, OpenSSL, and a
Node.js runtime with WebSocket support. It uses a separate legacy
worker fixture so the legacy browser leaves the counter under the
verifier's control. The end-to-end verifier and six migration-tool tests
passed on macOS. This covers one extension's local storage and an actual
legacy reopen. Migrating the extension registry, permission
grants, other extension databases, and native Windows execution remain
open.

### Assistant settings migration

The offline Workspace importer copies the known Assistant settings from
the verified legacy configuration into the prepared Content session,
including the provider catalog, selected models, permission defaults and
background settings. It preserves recognized OSCrypt `v10` and `v20`
credential bytes as an opaque Base64 field. Unknown fields and
unrecognized credential formats are omitted. The importer creates a
session for these settings even when the legacy tab session is absent.

Both browser hosts use the same Assistant settings codec. The normal
Content Workspace reads and writes the settings alongside its session.
Private windows have an empty session and do not load or inherit the
Assistant settings. A regular persistent Content Workspace can open the
panel when Assistant is enabled and an enabled provider has an available
model, including when those settings were migrated.

```sh
python3 tools/verify_content_storage_migration.py \
  --assistant-settings --shared-keychain /Applications/Zitter.app \
  'path/to/Zitter Content Lab.app'
```

Both browser hosts share the API credential decoder. The relocated
macOS trial imports a synthetic credential encrypted in the legacy
OSCrypt `v10` format. On both launches, Content uses the Workspace's
Keychain-backed OSCrypt instance to recover the expected value. The
probe also rejects invalid Base64 and truncated ciphertext. The verifier
checks that the plaintext is absent from process output. It verifies
catalog upgrade, custom model and permission preservation, unchanged
ciphertext, and rollback to an unchanged legacy snapshot. Both the
shared file Keychain and the separately seeded file Keychain passed.

The settings codec and Content Workspace translation units compiled to
Windows x64 COFF. Production Keychain identity, native Windows DPAPI
and App-Bound credential access, and a linked Chrome host regression
remain open.

### Packaged Assistant runtime

The Chrome and Content targets build the same native `zitter_assistant`
runtime. The macOS bundle places it beside the launcher and reuses the
framework's ICU data. The Windows Content runtime manifest includes
`zitter_assistant.exe` beside `zitter_content.exe`. Both bundle verifiers
run the packaged executable outside its build directory against a
loopback Ollama fixture. They require a provider response without an
authorization header. The macOS verifier also checks its Mach-O
architecture and runpaths and verifies the nested signature.

The non-component relocated macOS bundle passed the provider check and
the browser, Workspace, DevTools and profile probes. Seven runtime
contract tests passed, including RPC authorization, rejection of
browser origins, tool continuation and permission changes. All 27
component dylibs used by the helper are present in the framework's GN
runtime dependency set. The relocated component helper passed the same
provider check after its runpaths were confined to the bundle. The
current full component browser bundle has yet to be rebuilt and tested.
The standalone Windows GN graph includes the
helper and has zero `//chrome` labels. This graph was generated on macOS
with toolchain stubs for inspection. Native Windows build and execution
remain open.

### Workspace Assistant panel

The shared Assistant panel uses a browser host interface. Content opens
the panel through the Workspace shortcut or edge when the regular
persistent session has an enabled Ollama configuration. The host shares
tab titles and sanitized web addresses only for the current workspace
and only with tab discovery permission. The `list_tabs` tool follows the
same rule. The Content host does not grant page, history, or browser
action access. Changing profiles stops active work, discards helper
runtime state, and selects a conversation for the new profile. New
conversations use a separate, atomically written Content session file.

The relocated macOS trial sent a message through the panel, received a
`list_tabs` request from the local model fixture, and displayed the
answer. It checked that the initial model context and the tool result
contained only Work tabs, that disabled tab discovery returned no
context, and that the selected Personal conversation did not show the
Work reply. The verifier reopened the Work conversations and ran
through all four theme and renderer combinations. It also exercised
panel teardown with dangling-pointer checking enabled.

The Chrome host adapter passed syntax checking. The Content panel and
host compiled to Windows x64 COFF. Native Windows execution, a linked
Chrome host regression, real-account provider checks, and browser page
tools remain open.

#### Configuring providers in Content

Assistant Settings expose Ollama, OpenAI, Codex, and Claude Code in a
regular persistent profile. The connection check runs through the
packaged Assistant helper. Codex and Claude Code use the shared native
executable discovery code and accept an absolute executable override.
OpenAI keys use the Workspace's OSCrypt instance. The settings view
receives only whether a key is stored. Changes to credentials or provider
toggles stop active work and reset the helper. Ollama can discover local
models and supports manual model names. Fresh settings keep tab discovery
and web access off until enabled.

If a provider check completes while the Workspace is busy saving its
catalogue or finishing another edit, Settings clears the pending state
and lets the user retry. The regression probe verifies that a discarded
discovery result adds no models and that a subsequent check succeeds.
It passed in light compositor and dark Metal on macOS.

```sh
python3 tools/verify_content_workspace_session.py \
  'path/to/Zitter Content Lab.app' --assistant-settings
```

The verification uses disposable profiles, an Ollama loopback fixture,
and native executables standing in for Codex and Claude Code. The CLI
fixtures accept the connection prompt with no browser context and reject
an injected OpenAI key. The verifier checks that a synthetic key is
encrypted in the settings file, recovered through the Content credential
callback on restart, and removed by the settings command. The relocated
macOS bundle passed in light and dark with compositor and Metal. The
shared-Keychain migration and rollback probe also passed with a delayed
Ollama response. That probe explicitly selects the local fixture model and
exercises the composer while its caret timer is disabled during a turn.
The shared Assistant panel and updated Content host translation units
cross-compiled to Windows x64 COFF. Actual provider authentication and a
native Windows run remain release gates. Assistant action consent, history
access, and background work remain separate parity work.

The package verifier checks a loopback Ollama response, the missing OpenAI
key error and rejection of a non-loopback Ollama endpoint. It clears
inherited OpenAI credentials for the provider checks. All three cases
passed against the relocated macOS helper. The dark Metal settings trial
also passed again, including restart and encrypted key removal.

A real Ollama 0.40.16 instance on IPv4 loopback ran
`smollm2:135m` with a disposable model directory and cloud features
disabled. The 0.1.5 signed helper reached the server but its connection
check exhausted the 20-second deadline while the model continued
generating. The Ollama connection check now uses a short instruction,
requires an `OK` answer with no actions, and limits generation to
48 tokens. The rebuilt helper returned success in 0.94 seconds and
the server reported the real model loaded. The packaged helper
fixture also passed against the rebuilt local bundle. This validates
the Ollama connection path. The change postdates the notarized 0.1.5
image.

The Developer ID helper from the notarized 0.1.5 app also completed
two regular authenticated `/turn` requests against a real local
`qwen2.5:1.5b` model. The second answer recalled the synthetic marker
from the first turn, with no browser actions in either response. The
helper rejected requests with a missing bearer token or an Origin
header. Session deletion completed through the same local protocol.
The rebuilt Workspace separately passed the dark Metal Assistant
settings suite, including restart and encrypted key removal, using
fixture providers.

The first live Ollama turn through the Workspace panel reached the
model and then aborted the browser while rendering the completed
answer. The dangling-pointer detector identified
`AssistantPanel::RenderConversation`. It destroyed the streaming
view before releasing the panel's reference to it. The panel now
clears the reference first. The rebuilt macOS app displayed two
successful turns with the same local model in a disposable Work
profile. The second answer recalled the synthetic marker and the
Workspace quit with status zero. Browser permissions and background
work were disabled for the trial. Authentication with the other
providers remains open.

The Assistant migration fixture now splits the final reply across two
NDJSON frames and checks that the panel renders an interim answer
before replacing it with the completed answer. The first matrix run
timed out during the broader action sequence. A repeat passed all
four theme and renderer combinations with the shared file Keychain,
including the Assistant credential checks, scoped consent and
storage rollback.

#### Reading Workspace pages

Content uses the same isolated-world page reader as the Chrome host.
Reading requires both tab discovery and page access. A session can read
the selected tab or target a listed tab from the current profile. Content
loads a deferred tab without selecting it. It verifies the Workspace,
profile context, settings generation, and committed document before
returning content, and strips queries and fragments from the result URL.

The Assistant Settings probe enables page access through the preference
and verifies it after restart. The migration probe asks the packaged
helper to read an active and a deferred tab through a loopback provider.
It checks ordinary page text and controls, omission of marked private
elements and password fields, and rejection of missing grants and a tab
from another profile. Both probes passed in light and dark with compositor
and Metal, and migration rollback passed. The shared reader and affected
Content sources cross-compiled to Windows x64 objects. The macOS graph
and the Windows x64 graph generated on macOS with toolchain stubs both
have zero `//chrome` labels. The Chrome adapter passed a syntax check.
Native Windows execution remains open.

#### Clicking page controls

Content supports the Assistant `click` tool in the current visible tab.
It requires tab, page, and action access. New settings request native
confirmation by default. Explicit approval is single-use and bound to
the document, URL, element, and settings generation. The isolated reader
checks that the element still has the approved label and that the click
point reaches it. Positioning the confirmation prompt leaves the page
scroll position alone. Content sends native mouse input and rejects
covered targets. Session-wide approval uses the existing Assistant consent
UI.

The shared-Keychain migration probe asks its loopback provider to propose
three clicks with model-supplied approval fields. It declines the first,
approves the second through the native consent button, and renames the
target before approving the third. The fixture requires one trusted mouse
click, a skip, and rejection of the changed target. It passed in light and
dark with compositor and Metal, followed by rollback. The page-tool
helper and the affected Content host sources cross-compiled to Windows
x64 objects. The Chrome adapter passed a syntax check.

Other page mutations, general mouse and keyboard tools, background
actions, and native Windows execution remain open.

#### Editing page controls

Content supports `type` for an identified text field or contenteditable
editor, `insert_text` for the caret or selection of the focused editor,
and `select` for a listed HTML select control. These tools require tab,
page, and action access and operate in the current tab. The isolated
world rechecks the control when it acts, including private ancestors
across same-origin frames and open shadow roots. Focused insertion uses
the renderer's editing command so Unicode text travels together and line
breaks do not send Enter key events. Protected fields and opaque focused
frames reject edits.

The loopback migration probe replaces and appends Unicode text in a text
field and a rich editor, changes an HTML selection, then checks the page's
resulting values. It also turns a previously listed control into a
password field and tests both a targeted edit and a focused insertion.
The probe passed on macOS in light and dark with compositor and Metal,
followed by rollback. The shared page helper and Content host sources
cross-compiled to Windows x64 objects. The macOS dependency graph has
zero `//chrome` labels. Editor discovery without focus and native Windows
execution remain open.

#### Sending page keys

The Content host supports `press_key` in the current tab with tab, page,
and action access. It uses the shared key parser and sends events to the
renderer without handing unhandled keys back to browser accelerators.
The isolated focus check refuses protected fields and opaque frames.
Enter outside a multiline editor, or Enter with a sending modifier,
requests native confirmation under the existing consent preference. A
single-use approval binds the key to the document and focused element.

The loopback migration probe checks a protected field, a reserved
shortcut, and a trusted End key. It declines an Enter request, approves
the next request to submit a local form, and changes focus before
approving another request. The fixture requires exactly one trusted
submission. The key checks passed in light and dark with compositor and
Metal. The shared helper and Content host sources cross-compiled to
Windows x64 objects, and the Chrome adapter passed a syntax check. The
macOS GN graph has zero `//chrome` labels. Native Windows execution and
broader editor coverage remain open.

#### Capturing page screenshots

Content accepts `screenshot` with tab and page access for tabs in the
current profile. It can load and render a deferred tab without selecting
it. The shared capture helper holds a Chromium capture lease, waits for
the renderer to submit a visual state, and returns a JPEG up to 1280
pixels wide. The document and URL are checked before copying and again
before returning pixels. Content also rechecks Workspace, profile, and
permission scope. Screenshots contain the rendered viewport, including
visible material that the text reader omits from its structured result.

The loopback migration probe requests a screenshot of the active tab and
another of a deferred tab, then checks distinct JPEG attachments and
viewport metadata in the provider request. A separate in-browser probe
changes the current tab's URL during capture and checks that the image is
discarded and the capture lease is released. The migration checks passed
in light and dark with compositor and Metal, followed by rollback. The
shared helper and Content host compiled to Windows x64 objects, and the
Chrome adapter passed a syntax check. The macOS graph has zero `//chrome`
labels. Native Windows execution remains open.

#### Targeting tabs across Workspaces

Assistant tab discovery includes awake Workspaces that share the current
browser profile. Workspace names and current status are scoped to the
same profile. Sleeping Workspaces are listed by name and their tabs are
excluded from discovery and targeting. Page reads and screenshots can
load a same-profile tab in the background without selecting its
Workspace. The host checks the target Workspace and browser context
again before returning text or pixels. Explicit tab activation requires
tab and action access. Page mutations still require the selected tab.

The migration fixture adds two Content-native Workspaces to a disposable
copy of the imported session. It reads, captures and activates a tab in
the additional Work Workspace and checks that its server sees the Work
profile cookie. It also tests rejection of a sleeping tab and a tab from
Personal. The fixture passed in light and dark with compositor and
Metal, followed by rollback. The Content host compiled to Windows x64
objects. The macOS graph has zero `//chrome` labels. Native Windows
execution remains open.

#### Opening pins through Assistant

Assistant discovery exposes HTTP(S) pins from awake Workspaces in the
current profile. A Workspace pin carries its Workspace ID in the
reference. A shared profile favorite appears once and opens in the
current Workspace. Folders, sleeping Workspaces, other profiles, and
URLs with credentials cannot be opened through these references.

Opening a pin requires tab and action access. A `read_page` request
also needs page access. Both `read_page` and `activate_tab` can open the
pin and return its tab reference. A subsequent read can use that
reference to obtain content. The host uses the native pin-opening path,
which reuses an already open pinned tab.

The migration probe checks discovery, refusal without action access,
refusal of folders and out-of-scope pins, opening a closed Research pin,
reopening the same tab, and selecting an already open shared Work
favorite. It verifies that the newly opened pin persists across restarts
without duplicate tabs. The probe passed in light and dark with
compositor and Metal, followed by rollback. The Content host compiled to
Windows x64 objects. The macOS graph has zero `//chrome` labels.
Native Windows execution remains open.

#### Pinning and unpinning Assistant tabs

Assistant `pin_tab` and `unpin_tab` require tab and action access. They
resolve an open tab in an awake Workspace of the current profile and
use the native pin commands. A new pin belongs to the tab's Workspace.
Repeating `pin_tab` on an existing pin preserves its placement. Unpinning
an open tab keeps the tab at its current index. Shared favorites follow
the native profile-wide unpin behavior.

The loopback migration probe pins the Research tab from Work, verifies
the new Workspace pin in discovery, repeats the request, and unpins it.
It checks that the tab and both original pins remain present and that
the saved session matches the expected layout after restart. It also
checks refusal without action access and for a tab from Personal.
The probe passed in light and dark with compositor and Metal, followed
by rollback. The Content host compiled to Windows x64 objects, and the
macOS graph has zero `//chrome` labels. Native Windows execution remains
open.

The separate native shared-favorites probe also passed. It unpinned one
shared favorite and verified that both Workspaces retained their tabs.

#### Opening, navigating and closing Assistant tabs

Assistant `open_tab`, `navigate` and `close_tab` require tab and action
access. Opening adds a visible tab to the current Workspace. Navigation
selects the target in its awake Workspace within the current profile.
The host accepts HTTP(S) URLs without embedded credentials and waits for
the requested Chromium navigation entry to commit and finish loading.
The response contains the tab reference, title and final URL with the
query and fragment removed. The host checks the browser context,
Workspace and Assistant session again before returning it. Closing uses
the normal Workspace close path, including any before-unload prompt.
The tool reports that it requested closure, and tab discovery can confirm
whether the tab is still open.

The loopback Assistant migration probe checks that a file URL is refused,
a newly opened tab has its own reference, a redirect preserves that
reference and returns a redacted URL, and a 204 response returns an error
without reporting the previous page. It verifies that closing removes only
the requested tab and that a Personal tab cannot be navigated or closed
by the Work conversation.

The relocated macOS arm64 bundle passed the probe in both themes with
compositor and Metal, using a shared file Keychain and disposable profiles.
The verifier reopened the migrated profiles and confirmed legacy rollback.
The separate package verifier passed with the development signature,
including the sandbox, DevTools, signed extensions and profile isolation.
GN include checking passed. The graph contained 13,743 dependency labels,
zero under `//chrome`.

#### Assistant history and downloads

The Assistant History setting grants access to the current profile's
browsing history and download summaries. It defaults to off for new
configurations. Both the setting and the conversation's history grant
must allow the request. Private windows have no Assistant host.

Content queries Chromium History for up to 30 matching visits and lists
up to 25 downloads in reverse start order after the profile's download
manager loads its records. Download results contain the basename, state,
progress and size. HTTP(S) URLs returned by either tool omit credentials,
query strings and fragments. Asynchronous work checks the active profile
and Assistant configuration again before returning results.

The migration probe gives Work and Personal different history and
download records. The loopback provider checks the Work response,
including the redacted download URL and basename. The in-browser probe
checks refusal when the conversation has no history grant. The separate
Assistant settings probe toggles the History control and verifies that
the choice survives restart. Both probes passed in the relocated macOS
arm64 app in light and dark with compositor and Metal. The migration
probe also verified rollback. The graph retained 13,743 dependency
labels with zero under `//chrome`.

#### Assistant browser commands and Workspace organization

Assistant browser commands use the same explicit command list as the
Chrome host. They run the existing Workspace actions, including opening
and closing Split View and saving a Time Machine snapshot. Workspace
and root pin-folder creation use the normal catalogue and pin writers.
The Workspace tool returns the new ID after the catalogue write succeeds.

These tools require browser action access. Moving a tab also requires tab
discovery, and both Workspaces must use the current browser profile. The
destination accepts a Workspace ID or an unambiguous name. Moving uses
the existing tab transfer, preserving the live WebContents and leaving
the selected tab alone when the source is in another Workspace. The
Assistant move refuses to take the source Workspace's only tab.

The Assistant settings probe exercises the command allowlist, split
handling, folder creation and the asynchronous Workspace creation reply.
It moves a live Work tab from an inactive Workspace, checks its identity,
and inspects the saved layout. It also checks rejection without action
or tab access and rejects a move to Personal. The relocated macOS arm64
bundle passed in light and dark with compositor and Metal. The existing
tab commands and split actions regressions also passed. GN include
checking passed with zero `//chrome` labels in the Content dependency graph.

#### Assistant ad blocking inspection

The Assistant can inspect an awake tab in the current profile with tab
and page access. It uses the shared ad-candidate scanner and the same
page-scoped block counts and recent requests as the Content site shield.
The result includes the effective blocking state and Chromium's
registrable domain for rule hints. It keeps an IP address or local host
name intact when no registry applies. The document, URL and Assistant
settings are checked before the result is returned.

The shared scanner omits candidates that contain marked private content.
HTTP(S) addresses reported for resources omit credentials, query strings
and fragments. Inspection reads the page without reloading it or changing
rules.

The migration probe inspects a sponsored fixture, a request blocked by a
migrated Work rule and a permitted third-party resource. It checks rule
scope, URL redaction and omission of a candidate with private text.
The in-browser probe checks refusal without page access and for a tab
from Personal. The relocated macOS arm64 bundle passed in light and dark
with compositor and Metal, followed by the rollback check. The Chrome
adapter passed a Clang syntax check. The Content graph retained 13,743
dependency labels with zero under `//chrome`.

#### Assistant ad blocking rule trials

With tab, page and browser action access, the Assistant can test a batch
of network and cosmetic rules through `adblock_add_rules` with
`check: true`. The selected page must support a safe reload. All rules
must pass validation within the site's scope before the temporary engine
is installed. The engine is scoped to the tab and exact origin. The
Assistant waits for the reload to commit before reporting the trial as
ready for inspection.

A trial uses the profile rules as its baseline and leaves the saved
profile preferences intact. Leaving the origin, changing ad blocking
settings, or failing the requested reload discards the matching trial.
Scriptlets remain excluded from the temporary engine because new windows can
inherit renderer preferences.

The migration probe tries a cosmetic and a network rule together, opens
a second tab on the same origin, and checks that only the trial tab
uses the batch. It also checks that a mixed batch with a rule for another
site and a save with an out-of-scope replacement cause no reload or preference
changes.
The relocated macOS arm64 app passed in both themes on compositor and
Metal, followed by rollback. The separate native ad blocking trial
and site shield regression also passed. The Content dependency graph retained
zero `//chrome` labels.

#### Assistant ad blocking rule saves

Saving a batch requires a separate, one-time user approval, even when click
confirmation is disabled. Approval binds the rules and their replacements to
the selected document, URL and current Assistant access. Each replacement must
match a saved rule in the active profile and be scoped to the same site.
Cosmetic selectors must match the document and avoid protected fields and
subtrees before the prompt and again after approval. The save writes the
merged profile rules in one update and discards the tab's trial. Cosmetic
filtering updates on loaded documents after the profile engine finishes
rebuilding. Reloading applies document-start scripts and restores
content changed by earlier imperative rules. The consent prompt summarizes the rule types and
replacements. Visual outlines and immediate application to the loaded
document are tracked below.

The longer Assistant probe reproduced a Chromium compositor assertion when
the scrolled conversation layer shrank past its previous raster priority
rectangle. The last partial tile could still have an in-range index while
its new bounds fell outside that rectangle. Content staging resets the
tiling when the resized layer no longer intersects the previous priority
rectangle and asks Chromium to recompute tile priorities.

The relocated macOS arm64 app passed the local provider probe in light and
dark themes on compositor and Metal, followed by rollback. The probe
rejected a selector enclosing a protected field, exercised denial and
approval after the URL changed, and ignored a model-supplied confirmation
token. It checked the exact Work rules after every run, including
replacement after relaunch, and verified that Personal's rules stayed
unchanged. The native site shield and tab-scoped rule trial regression
also passed in the same appearance matrix. GN found 13,743 dependency
labels and zero under `//chrome`.

#### Assistant ad blocking diagnosis

The Assistant asks for one-time approval before reloading the selected web
page with the diagnostic probe. The action requires tab, page and browser
action grants and rejects form resubmission. The browser checks the approved
document and URL and associates the probe with the token of the new main
document before commit. The renderer consumes the probe once, at document
start in the page world. The injection does not use renderer preferences,
so it cannot be inherited by a child frame or another window.

The browser discards the report if the reload is redirected, interrupted, or
followed by a document or permission change. After a six-second observation
window, the Assistant reads a bounded report through its isolated world.
The report can include bait reads, failed loads, detector globals, scroll
locks, blocking walls and canvas draws. The conversation trace stores counts
for diagnostic lists. The probe belongs to that document for its lifetime
and writes no profile settings.

The relocated macOS arm64 bundle passed the local provider round trip and
rollback in light and dark themes on compositor and Metal. The fixture
observed the probe in the first inline script of the approved reload.
Declining did not reload the page. The child frame and a subsequent hard
reload both ran without the probe. The native Assistant contract suite also
passed all 28 tests using the Content output's test-only
`assistant_cli_fixture` target. The site shield, document-start scriptlets,
network filtering, cosmetics, and private Workspace regressions passed
on the same bundle. GN found 13,755 dependency labels and zero under
`//chrome`.

#### Assistant rule preview

The native rule-save prompt asks Content to show the validated cosmetic
targets on the selected document. The preview uses a separate constructed
stylesheet and random DOM markers. The approval token stays in the native
flow and the isolated world. The preview preserves the page's inline styles,
limits itself to 500 rendered targets and checks protected fields again
before highlighting. If every target is off screen, it reveals the first
one.

Denial, stopping the Assistant, consuming the approval, changing the
selected page, changing Assistant settings or starting a main-frame
navigation clears the preview. Document page-hide and visibility handlers
also remove its stylesheet and markers. Saving still requires a fresh
selector check.

The relocated macOS arm64 bundle passed the provider probe and rollback in
light and dark themes on compositor and Metal. The fixture recorded the
dashed preview and its removal after denial, a URL change and acceptance.
It checked that the original inline outline stayed solid throughout and
became the effective outline again after cleanup. Only the intended public
target received a marker. A distinct selector on each relaunch kept the
preview trial independent of the rules saved in previous runs. GN include
checking passed with 13,755 dependency labels and zero under `//chrome`.
The same bundle passed the native site shield and rule-trial matrix,
document-start scriptlet, network filtering, cosmetic and private Workspace
regressions.

#### Applying profile rules to loaded documents

When the profile engine finishes rebuilding, each active HTTP or HTTPS
document reports its current class names and IDs without changing its
styles. The browser derives the current site and generic rules with their
exceptions, then asks the renderer to replace the old cosmetic stylesheets,
hiding markers, masks and selector caches in one operation. This also
removes hiding left by a replaced rule. Async scan replies are tied to the
document and a cosmetic update generation, so earlier results cannot
reintroduce stale rules after a newer update.

The reset leaves the page's own inline styles intact. Earlier DOM removal,
style writes and other imperative actions require a reload to restore
their input. Document-start scriptlets apply on the next load, and
requests already blocked are retried by reloading the page.
Back/forward cache restoration starts a new cosmetic update using the
current profile rules.

The relocated macOS arm64 bundle passed the local provider probe in both
themes on compositor and Metal, followed by rollback. Before saving, the
probe checked that the prior rule hid its target and the new target was
visible. After saving, it checked that the new target was hidden, the old
target was visible and an added cosmetic exception exposed its target.
The first run replaced a procedural text rule, and the subsequent runs
replaced plain CSS rules saved in the prior launch. These checks used the
loaded document without a reload.

The same bundle passed the native site shield and trial matrix, the
document-start scriptlet, network filtering, cosmetic and private Workspace
regressions. GN include checking passed with 13,755 dependency labels and
zero under `//chrome`.

#### Workspace history cache

Workspace tabs opt in to Chromium's back/forward cache. Chromium still
decides whether a document can be stored and restored. The extension
observer flushes a tab's cache on extension load or unload. Network rule
invalidation requests from the extension components flush loaded
Workspace tabs.

```sh
python3 tools/verify_content_adblock_cache.py 'path/to/Zitter Content Lab.app'
```

The macOS arm64 probe passed in light and dark themes on compositor and
Metal in a relocated bundle with disposable Workspace profiles. It
changed rules while a main document and its cross-site frame were
cached, then verified both document identities, Chromium's restore
flag, the persisted `pageshow` event and the new cosmetic results. A
second return verified that a top-level site exception cleared hiding
in both documents. The server received one request for each document
throughout both returns.

The same build passed Workspace navigation, signed extension
management, cross-process fingerprint protection, document-start
scriptlets and private Workspace ad blocking. GN include checking
passed with 13,755 dependency labels and zero under `//chrome`.
The signed extension Settings probe also checks cache eviction:

```sh
python3 tools/verify_content_workspace_session.py --extension-manage \
  --crx-signer 'path/to/crx3_build_action' 'path/to/Zitter Content Lab.app'
```

Before each toggle, the probe stores a document in Chromium's history
cache in each of two disposable profiles. It checks that the extension's
owning profile loses its cached document on enable and disable, and that
the other profile retains its cached document and running extension
worker. The relocated macOS arm64 bundle passed seven transitions over
two launches in each light and dark compositor and Metal configuration,
with Work and Personal both exercised as owner. This covers extension
registry load and unload. Native Windows remains open.

The standalone installer accepts manifest-declared static
`declarativeNetRequest` rulesets. It indexes rules from the signed CRX
before publication. On restart it verifies the signed installation and
discards the previous generated index. Activation rebuilds that index
from the verified package and loads its checksums into Chromium's rules
monitor. Each regular profile has its own request proxy for navigation
and subresource factories. Private profiles bypass the extension proxy.

The embedder persists the rules monitor's `ExtensionPrefs` records in
each owning profile. It restores only ruleset fields for verified
installations and writes changes atomically on a sequenced worker.
The signed Settings probe wrote different static and dynamic checksums
for the same extension ID in Work and Personal, along with the enabled
static ruleset and badge setting. It restored those values through
Chromium's `PrefsHelper` on restart. It discarded an injected install
path and an unregistered extension's record, then verified that uninstall
removed the owning profile's rule preferences and preserved the peer's.
The probe passed with Work on light compositor and Personal on dark
Metal as the owner. It exercises the preference layer with fixture
values.

The signed macOS registry probe used an enabled static rule to block a
loopback fetch in the owning profile while the same request reached the
server in the peer profile. It toggled the owner through Settings and
checked the result across two launches. The probe also rejected a signed
package with invalid rules before publication. It passed with Work on
light compositor and Personal on dark Metal as the owner.

The embedder exposes Chromium's native `declarativeNetRequest` JavaScript
functions. A signed extension worker used them to enable a second static
ruleset, disable an individual static rule, and add dynamic and session
rules. The owner and peer pages made five loopback requests to check
the effect of those changes on the network. The worker checked that
dynamic rules and both static selections survived restart, and that
session rules were empty before it added them again. The probe also
exercised disable, re-enable, uninstall and fresh reinstall. The
Developer ID bundle passed with Work on light compositor and Personal
on dark Metal. Reindexing retains disabled rule IDs when the ruleset
checksum is unchanged.

The signed macOS probe also opened the blocked destination directly and
through an HTTP redirect in the owner, peer and an off-the-record
browser context. The owner received Chromium's `ERR_BLOCKED_BY_CLIENT`
in both cases. The peer and private navigations committed successfully.
The loopback server received exactly the four destination requests from
those two contexts. A private page completed all five subresource
requests, and the private profile acquired no extension or ruleset
files. The probe passed with Work on light compositor and Personal on
dark Metal.

Dynamic rule recovery uses the JSON rules as the authoritative record.
Before activation, the embedder rebuilds the generated index on
Chromium's extension file sequence and refreshes the preference
checksum. An installation marker allows recovery if the first checksum
write was interrupted. An older installation can establish the marker
from an existing checksum record. A new installation discards rule data
and selections left behind by a previously marked installation.

The signed macOS fixture reproduced two interrupted-write snapshots.
Work on light compositor restored a dynamic block rule after the
checksum preference was removed. Personal on dark Metal applied a new
allow rule from JSON while the index and checksum still described the
old block rule. Both trials also exercised recovery of older state
without the marker and verified that reinstall ignored leftover rules
and preferences after uninstall. The tests recreated the file states
directly.

The Content staging overlay makes Chromium's rules monitor evict
back/forward cache entries in the BrowserContext whose rules changed.
The hook covers matcher installation, removal and changes to individual
static rules. A signed worker separately changed dynamic rules, session
rules, disabled static rule IDs and enabled static rulesets while both
regular profiles held cached history entries. In each case the owning
profile lost its cached entry and the peer retained its entry. The
fixture passed with Work on light compositor and Personal on dark Metal.

The signed fixture also exercised three additional DNR actions. A
same-origin redirect changed the requested resource. A header rule set a
request header observed by the server and a response header observed by
the renderer. A high-priority `allowAllRequests` rule allowed the
subresource in a child frame while a lower-priority block rule still
blocked the parent's request for that resource. The peer and private
contexts received the original responses and headers. Both macOS
renderer trials passed.

The signed fixture also tested `upgradeScheme`. A loopback server
accepted HTTP and TLS on the same port. The owning profile attempted TLS
and Chromium rejected the fixture's untrusted certificate. The peer and
private contexts committed HTTP pages. The same test requested the
redirect and header endpoints under a second loopback hostname outside
the extension's granted hosts. Those requests and responses retained
their original resource and headers in all three contexts. The signed
macOS trials passed with Work on light compositor and Personal on dark
Metal.

### Assistant conversation and task import

The offline importer reads the version 1 Assistant history from the
snapshot manifest and remaps legacy profile IDs using the same Work and
Personal associations as the copied profile stores. It preserves
conversation messages and task definitions. Imported tasks are paused,
and their next run is cleared. The import requires fresh action
confirmation, closes unanswered prompts, and keeps the display labels
for old tab and pin chips while clearing their expired references.
Entries without a mapped profile are omitted. Unsupported file
versions, altered snapshots and histories beyond the panel's
200-conversation restore limit stop publication of the prepared root.

The relocated macOS trial restored both legacy conversations and kept
them in separate profiles across all four theme and renderer
combinations. It rejected a Personal conversation selection from Work,
then showed that conversation after switching to Personal. It checked
that an overdue imported task stayed paused, reopened both profiles
after saving new Work conversations, and verified rollback against the
unchanged legacy snapshot. The importer tests also cover swapped
profile associations, stale references, modified snapshot data and
the conversation limit.

### Legacy extension grants and disabled state

The offline copy command accepts `--include-install-state` for the
fixture. It reads the selected entry from the snapshot-verified
`Secure Preferences` or `Preferences` file and exports a narrow
`ContentLegacyExtension.json` record. The record carries the extension ID,
active and granted API and host permissions, and the saved disable
reasons. It omits the legacy installation path and unrelated preferences.

Content compares the signed manifest's effective permissions with both
legacy sets before publishing the package. It requires an exact match
for the supported representation. Withheld permissions, incognito grants,
and optional or structured permissions need a separate migration policy.
The installer records the import file's hash beside the active version.
A missing or changed record blocks restore. A saved disable reason keeps
the fixture in Work's disabled registry set, where the navigation gate
blocks the extension page and startup skips the worker and content scripts.

The macOS verifier seeds and reopens an enabled legacy extension, rejects
a signed replacement requesting `unlimitedStorage`, and opens a second
legacy copy where Chromium's management API disables the extension. It
imports that state, then checks install and restart with the extension
still disabled and its LevelDB unchanged. It also tries a restart after
removing the import record. The macOS migration verifier and seven
migration-tool tests passed. The Content graph still has 13,566
dependency labels and zero `//chrome` labels.

### Selected extensions in the Workspace import

The coordinated importer accepts explicitly selected extension IDs per
profile. It copies each verified `chrome.storage.local` database and
writes its grants and disable reasons into the matching profile. Both
profiles and all selected extension records are staged together. A
changed store or unsupported grant record prevents the destination from
being published.

```sh
python3 tools/profile_migration_trial.py import-workspace-profiles \
  "$trial" "$content_root" --layout mock-keychain --work-extension "$extension_id"
```

Use `--personal-extension` to stage a selection for Personal. The
macOS loopback verifier opens the selected profile through the signed
fixture installer after the mock Keychain check. Separate runs passed
for Work and Personal. Each rejects a signed
package requesting more permissions, reads the original storage in
Content and after rollback, and verifies that the disabled extension
stays disabled after restart. A unit test stages the same extension ID
with different data and disable reasons in each profile and checks
that changing Personal's store prevents publication of both profiles.
The simultaneous activation trial below uses two signed packages with
fresh storage.

```sh
python3 tools/verify_content_extension_migration.py --extension-profile personal \
  '/path/to/Zitter.app' '/path/to/Zitter Content Lab.app' \
  --crx-signer /path/to/crx3_build_action
```

Repeat `--work-extension` or `--personal-extension` to select multiple
IDs for that profile. A multi-selection writes
`ContentLegacyExtensions/<id>.json` for each ID. The Content host
uses the verified signing key to choose the receipt and binds that
receipt to the signed installation. It rejects mixed single-record and
indexed layouts. It still reads the original single-record format. A
first install with a pre-existing extension store requires its
permissions receipt.

```sh
python3 tools/verify_content_extension_migration.py --multiple \
  --extension-profile personal '/path/to/Zitter.app' \
  '/path/to/Zitter Content Lab.app' --crx-signer /path/to/crx3_build_action
```

The verifier seeds different values in two extensions in one legacy
profile, reopens both after rollback, then disables the second in the
legacy app. In Content, it checks the receipts and the storage copies,
then verifies each signed installation separately. The enabled
extension reads and advances its original counter. The disabled
extension keeps its registration and storage without running its
worker. The verifier rejects swapped, missing, or ambiguous receipts,
including removal of the whole receipts directory before first install.
The macOS trial passed for Work and Personal against the released
Zitter app. The updated manifest adapter compiled to Windows x64 COFF.

### macOS password database probe

The standalone `verify_login_database` target uses Chromium's
`LoginDatabase` and the lab's OSCrypt setup. It creates synthetic login
databases under a private temporary directory and copies them after close.
It checks both the AES-GCM fixture format and Chromium's macOS `v10` format
with a fake Keychain. It enables Chromium's cleanup feature while disabling
automatic deletion for the database. A read with an unrelated key must
report encryption failure, and a newly initialized encryptor with the
correct key must still read the original credential. The probe also
compares the original and candidate ciphertext after the failed read.

Stage the latest sources in the isolated Chromium checkout and copy the lab's
GN arguments to a separate output before building.

```sh
cd .chromium/src
mkdir -p out/ZitterContentLoginProbe
cp out/ZitterContent/args.gn out/ZitterContentLoginProbe/args.gn
buildtools/mac/gn gen out/ZitterContentLoginProbe \
  --root-target=//zitter_content:verify_login_database --fail-on-unused-args
third_party/ninja/ninja -C out/ZitterContentLoginProbe verify_login_database
out/ZitterContentLoginProbe/verify_login_database
```

The default invocation generates its own databases. A separate mode
accepts a disposable legacy copy and uses the fake Keychain. Both modes
verify ciphertext, wrong-key failure, and recovery. The legacy verifier
uses the old browser's password manager to save one synthetic Work
credential in its profile store. It closes the browser, prepares the
offline trial, and opens a fresh copy of Work with Content's
`LoginDatabase`. The wrong key cannot read or delete the credential.
The correct fake Keychain key reads it, and both checks leave the
`v10` password ciphertext unchanged.

```sh
python3 tools/verify_content_login_migration.py /Applications/Zitter.app \
  '.chromium/src/out/ZitterContentLoginProbe/verify_login_database'
```

The verifier reopens a rollback copy in the old browser and checks that
its password manager lists the restored credential. It compares the
source and offline candidate snapshots after the run. The verifier's GN
graph has 6,560 dependency labels and zero `//chrome` labels. Form
autofill after rollback timed out even though the old manager listed
the entry. Access to the production Keychain item and native Windows
execution still require verification.

Add `--content-app` to test the coordinated Workspace import and form
autofill with the mock Keychain.

```sh
python3 tools/verify_content_login_migration.py /Applications/Zitter.app \
  '.chromium/src/out/ZitterContentLoginProbe/verify_login_database' \
  --content-app '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The installed legacy app seeds a different credential for each profile
at the same loopback origin and disables password saving for Work.
The verifier checks the imported saving decision and launches the Content
Workspace twice per profile. Each renderer autofills only its profile's
credential. The ciphertext is unchanged after both runs, and the
rollback matches the snapshot before the legacy password manager reopens it.

### Password stores in the Content browser process

Each persistent `ProfileContext` lazily opens its own Chromium
`PasswordStoreBuiltInBackend` in the profile's `Login Data` file. It uses
the same process-owned OSCrypt service as the network encryption provider.
Private contexts expose no store. Startup rejects symlinked database files
and SQLite sidecars in the lab profile directories. The database disables
automatic removal of undecryptable entries and has no account store.

The linked macOS app passed the browser-process store probe below. It writes
different synthetic credentials for Work and Personal at the same origin
and checks both values again in fresh browser processes. It checks that
private contexts return no password store, that the two databases contain
fixture-key ciphertext, and that database and journal symlinks are rejected
without altering their target. The application GN graph still has 13,566
dependency labels and zero `//chrome` labels. The Windows startup passes
its DPAPI-backed OSCrypt service to the same profile code. Windows GN
generation stops before compilation on this host because the Windows SDK
is absent.

```sh
python3 tools/verify_content_profiles.py --password-store \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

### Password autofill in the Content renderer

Persistent Content pages now attach a `PasswordManagerClient` and Chromium's
`ContentPasswordManagerDriverFactory`. The renderer uses Chromium's
`PasswordAutofillAgent` and `AutofillAgent`. Only the primary main frame can
bind the password driver. Private pages have no browser-side client. The
client checks the committed origin against the form origin and requires
HTTPS with a valid certificate and no active insecure content, or IPv4
loopback HTTP. Chromium still owns form parsing, credential matching,
and its user-gesture gate for password values.

```sh
python3 tools/verify_content_password_forms.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The linked macOS app passed the form probe after seeding distinct synthetic
Work and Personal credentials at the same loopback origin. It checked both
passwords after browser restarts and trusted clicks. A private window, the
same host on a different port, and a same-origin iframe kept their fields
empty. The app's dependency listing has 13,602 labels and zero `//chrome`
labels. This probe uses a fixture key and loopback HTTP. It leaves trusted
HTTPS, production Keychain access, and native Windows verification open.

The shared Chromium fetcher can copy HTTP credentials to HTTPS. Verify the
compatibility of that behavior on migration copies before cutover.

### Workspace password save and update prompt

The Content workspace attaches a nonmodal password prompt to its persistent
tabs. The prompt uses Zitter's existing text and panel rendering. It shows
the site, login name, and workspace profile. A save or update requires the
prompt's affirmative action. The browser checks the origin and visibility
when offering the prompt and again before a save or site blocklist action.
Starting a new main-frame document, hiding the tab, or closing the page
discards the offer. Private contexts have no password client. The lab
disables network leak checks while its password protection service is absent.

```sh
python3 tools/verify_content_password_prompt.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The probe uses trusted mouse and keyboard events to submit synthetic forms.
It accepts a save and an update through the visible dialog. It cancels a
second update and discards later offers by navigating and switching
workspaces. It checks the encrypted database after each run and verifies
the updated value through autofill after restart. The linked app passed in
both light and dark themes with the compositor and Metal. The prompt must
inherit the workspace theme, stay inactive and nonmodal, and render the
login text on the Metal surface when requested.
The linked macOS application still has zero `//chrome` dependencies in
its 13,602-label GN graph.

Folding and reopening offers, manual fallback, testing the site blocklist
action, choosing between multiple logins, and password generation remain
open. The workspace prompt was exercised with
the fixture encryption key and loopback HTTP. The production Keychain and
native Windows execution still need coverage. OS reauthentication before
autofill is also pending.

### Profile password saving preference

The workspace's existing password saving switch reads and writes the active
profile's Chromium preference. Work and Personal use separate
`Content Password Preferences` files with Chromium's asynchronous `JsonPrefStore`.
Disabling the switch dismisses pending prompts and invalidates submitted
form state. Autofill continues to use the encrypted login database. Private
contexts expose no switch or password preferences file.

When there is no explicit Content value, the embedder reads
`credentials_enable_service` from a copied legacy `Preferences` file without
writing that file. An explicit Content value takes precedence. Invalid or
unreadable preference files disable saving and leave the setting unavailable.

```sh
python3 tools/verify_content_password_prompt.py --password-preference \
  --legacy-app /Applications/Zitter.app \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The optional old-browser probe uses its Settings WebUI in a disposable
profile to turn off password saving, then verifies the copied preference
and reopens an offline rollback in the old browser. On macOS the installed
old app and the linked Content app passed this trial. The Content probe
read the imported setting, exercised the Settings switch, and checked
restart persistence, profile isolation, autofill, and malformed preference
handling. It also compared the legacy file and existing password ciphertext
after the trial. The Content setting probe also passed in light and dark
themes with the compositor and Metal. Both processes use temporary profile
roots and test encryption keys. The Content graph still has 13,602 labels
and zero `//chrome` labels.

### Profile password manager panel

The shared Zitter password panel accepts a profile label, a Chromium
password store, and an authenticator factory. The Chrome shell supplies
its existing profile store and device authentication factory. The Content
shell opens the active profile's store from the password manager command
and destroys the panel when switching profiles. Private contexts cannot
open the panel. The panel supports search, local password checks, adding,
editing, confirmed deletion, and the list of sites blocked from saving.

The Content macOS authenticator requests device owner authentication through
LocalAuthentication for each reveal or copy. The panel invalidates pending
authentication when leaving the details page, refreshing store results, or
closing the panel. On refresh it identifies the selected entry by the
database primary key instead of retaining a list index. Interactive macOS
authentication success and import from the password manager remain open.

```sh
python3 tools/verify_content_password_prompt.py --password-manager \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The loopback probe uses a disposable profile and test encryption key. It
checks a masked credential, an authentication denial, and a late mock
authentication reply after leaving the details page. It adds, edits, and
deletes a synthetic entry through the shared panel. It then switches from
Work to Personal and checks that the old panel was closed, Personal has no
Work entries, and the original Work ciphertext was preserved. The linked
macOS app passed the panel probe in light and dark themes with the
compositor and Metal. The app's dependency graph still has 13,602 labels
and zero `//chrome` labels. The Chrome adapter passed a Clang syntax check
with the patched Chromium headers. The legacy browser suite was not run.

### Windows password reveal and copy

The Windows Content panel supplies its own authenticator. It starts Windows
Hello using the password panel's native window on Windows 11 and the
platform's earlier verification API on Windows 10. Specific unavailable
results open the system credential dialog. The credential buffer must pass
an interactive LSA logon and the returned user SID must match the current
process account. The adapter clears and frees the buffer. Cancellation,
failed verification, a changed owner window, and late replies from a closed
panel cannot disclose a password. The authenticator does not reuse a previous
successful request.

The automatic panel and preference verifier now accepts a native Windows
executable. It uses a disposable profile and checks the DPAPI-backed
`v10` password ciphertext. After saving the first login, the Windows probe
substitutes an unrelated key wrapped by DPAPI for the same user. It expects
the login-store check to reject startup without changing the databases,
their sidecars, the saved session, or the substituted key. It restores the
original key and continues through the save and update flow. Automatic panel
tests inject a controlled authentication reply. They do not invoke Windows
Hello or the Windows credential dialog.

```powershell
python tools/verify_content_password_prompt.py --password-manager --password-preference .chromium/src/out/ZitterContentWin/zitter_content.exe
```

Add `--interactive-reauth` to leave the seeded workspace open after the
automated checks. In Work, open Passwords and select the synthetic login.
Cancel Reveal password and check that it stays masked. Try again and verify
that it appears only after a successful prompt. Repeat on a Windows machine
where Hello is unavailable to exercise the credential dialog and account
check. The verifier deletes the temporary profile when the workspace exits.

The linked macOS app passed the password manager and preference regression.
The Windows authenticator, workspace integration, and entry point
cross-compiled to x64 COFF objects using Microsoft SDK 10.0.26100.0 and
MSVC 14.44 headers. The workspace compile check also identified a macOS-only
`ViewsDelegate` override, now guarded to that platform. Full Windows GN
generation on this host stops during Visual Studio setup because
`WINDOWSSDKDIR` is absent. The Windows executable still needs to be linked,
and both authentication paths need runtime verification on Windows.

## Shared presentation

The print sheet and the existing Chrome print adapter now share
`//zitter:print_options`, a standalone GN target with no Chrome dependencies.
The JavaScript dialog code takes a callback to resolve a display name from
its `BrowserContext` when the dialog opens. The Chrome host supplies its
existing profile lookup. The Content host can supply its own profile label
without changing the dialog or its beforeunload flow.

GN generated and checked the print options target independently. Clang
syntax checking passed for the print sheet, print adapter, page dialog,
page services, workspace lifecycle and workspace window sources using
the pinned build flags. The existing browser tests for native JavaScript
dialogs, beforeunload and print preview still need a linked build of these
changes.

On macOS, both hosts can now use `//zitter:workspace_presentation`. This
target contains the existing `WorkspaceShell`, Rust sidebar scene,
compositor and Metal drawing, and AppKit frame and keyboard adapters.
It leaves browser services with the host. The assistant labels, RSS tab
URLs, and timeline timezone helper moved to presentation code.

GN generated the presentation target and the existing Chrome shell target.
The presentation include check passed, and its full graph had 12,427
dependency labels with zero `//chrome` targets. A symbol check across 21
C++ and Objective-C++ objects found no unresolved Zitter presentation
symbols.

The Windows presentation target also uses the shared GN template. The host
supplies the end-session callback and locale PrefService. The Chrome host
still calls its existing session-ending handler and uses the same Local
State locale key. Jump List keeps the existing `user-data-dir` switch on
the wire. The Windows-only frame and Jump List sources require validation
on a Windows toolchain.

## Content workspace smoke

The standalone app links the same macOS presentation target through the
shared GN template. In smoke mode it creates a scripted Views window, a
`WorkspaceModel` and `WorkspaceController`, and a WebView for each of two
temporary browser contexts. The controller activates Work and then Personal.
Both pages must run JavaScript in different renderer processes, and the
Personal tab must be drawn using the selected presentation mode. The
controller then returns to Work and checks that its original page and
renderer are still present and that Personal is hidden.

In a macOS GUI session, run each theme and renderer combination with an
external timeout. Each run should report `Zitter Content workspace smoke passed`.

```sh
app='.chromium/src/out/ZitterContent/Zitter Content Lab.app/Contents/MacOS/Zitter Content Lab'
for raster in compositor metal; do
  for theme in light dark; do
    "$app" --zitter-content-workspace-smoke-test \
      --zitter-rust-raster="$raster" --zitter-content-workspace-theme="$theme"
  done
done
```

The smoke mode uses separate temporary contexts and does not accept user
input. The profile probe above covers persistence, while the workspace smoke
covers in-process selection, distinct renderers, and presentation. GN's
combined include checks and a full dependency listing passed with 12,574
labels and zero `//chrome` or Zitter adblock vendor targets. Clang syntax
checking passed for the workspace host and bootstrap integration. Full
linking exposed missing explicit dependencies on the Blink component and
the color mixers, and an unstaged native text-edit buffer hook. Staging now
applies the existing editor hook and gives changed files fresh timestamps so
Ninja recompiles them when they are copied after an earlier build. GN still
reports 12,603 dependency labels and zero `//chrome` targets for the
combined macOS app. The relink completed and the minimal bootstrap smoke
passed with the combined bundle. The shared host now initializes the Views
`LayoutProvider` before constructing menus and supplies Content's compositor
factory through its Views delegate. The linked Workspace smoke passed in
light and dark with both compositor and Metal. Visual review remains
pending.

## Interactive Content workspace on macOS

Launch the same WorkspaceShell with input enabled and disposable Work
and Personal contexts.

```sh
app='.chromium/src/out/ZitterContent/Zitter Content Lab.app/Contents/MacOS/Zitter Content Lab'
"$app" --zitter-content-workspace \
  --zitter-rust-raster=compositor --zitter-content-workspace-theme=light
```

The lab supports HTTP and HTTPS address entry, new tabs, selecting and
closing tabs, back and forward, and reload. Each workspace keeps its own
tabs and history in memory while switching. The existing native
JavaScript dialogs are attached to each tab. Tab and window closing use
the shared beforeunload batch. The window checks its visible tab first,
then the remaining tabs in both workspaces. Closing commits after all
required prompts agree and the documents and tab topology still match.
Cancelling a prompt leaves the window and every tab open.

Run the interactive checks and review the window in light and dark
themes with both compositor and Metal. In each space, navigate two
disposable tabs, use back and forward, switch spaces and verify the
previous tab is restored.

Serve the local test fixtures from a separate terminal.

```sh
python3 -m http.server --bind 127.0.0.1 --directory testdata 8765
```

Open `http://127.0.0.1:8765/beforeunload_probe.html` in Work and Personal
and type a different draft in each tab. Verify that cancelling a tab
close keeps the draft. Close the window, accept the first prompt, and
cancel the next one. Both drafts must remain. Repeat with both prompts
accepted and verify that the app exits. The Chrome integration also has
a browser test for a navigation that invalidates an earlier approval
while a second prompt is open.

The linked GUI runs and visual review are pending. The default lab starts
fresh each run. Windows workspace hosting and remaining browser services
are still needed. Some shared shell controls are visible before their
corresponding lab services are available.

### Native macOS Workspace menu

The Content Workspace installs native application, File, Edit, View,
developer, and Window menus. Commands reuse the Workspace actions for navigation,
tabs, settings, downloads, history, Time Machine, and DevTools. AppKit
handles window management and application visibility. The Workspace
routes browser commands to the current key or main window, including
private windows. Workspace shortcut settings retain ownership of
browser shortcuts. About opens the shared Workspace dialog. The Dock
menu opens the new-tab address editor, and reopening the app from the
Dock restores the regular Workspace if no windows are visible. Import
is available in the File menu, matching the Chrome-backed app.

The relocated app's private-window verifier dispatches the native File
menu action to open the second window. It confirms that About, Dock New
Tab, and Settings open in the active private window, that History is
disabled there, and that Close Tab closes its last tab while leaving the
regular Workspace open. It also calls the AppKit reopen delegate and
checks that it shows the hidden regular window. This passed with both
themes on compositor and Metal. The regular Workspace navigation
verifier also passed.

The Edit menu exposes Undo, Redo, Cut, Copy, Paste, and Select All through
AppKit's first-responder chain. Chromium's native bridges deliver those
commands to the focused renderer or Workspace editor. The verifier
selects a textarea's contents on the page, then selects the address
field and checks Undo and Redo there. It does not use the system
clipboard. The test runs with both themes on compositor and Metal.

Browser import opens from the macOS File menu, Command Center, and
the shared settings form. The relocated app passed the native File menu
import probe.

### Browser import data transfer

The source preview, SQLite snapshot reader, and destination writer are
shared. The writer resolves each source mapping ID to the destination
history and cookie services. Chrome supplies its existing profile
keep-alive adapter. The standalone host can supply its own services
without adding a dependency on Chrome. Cookie persistence uses the
destination profile's normal encryption.

```sh
python3 tools/verify_content_workspace_session.py --browser-import \
  'path/to/Zitter Content Lab.app'
```

The relocated macOS app read two disposable Chromium source profiles
and mapped them to Work and Personal in reverse order. The probe
checked the history rows, encrypted cookies, and byte-for-byte
unchanged source files. It changed the source titles and cookie values
and retried after a restart. The existing destination rows stayed
intact. The probe uses plaintext source cookies and never accesses
source Keychain credentials. The GN graph has 13,720 labels and zero
under `//chrome`. The Chrome import and RSS adapters also compiled
as individual macOS translation units.

### Importing from the Workspace form

The form previews the selected browser without opening destination
profiles or reading cookies. On confirmation the embedder prepares
the new profiles and writes selected history and cookies through
their Chromium services. It closes the staging history backends and
waits for the cookie-store flush before atomically publishing the
profiles and spaces in the session catalogue. Bookmark folders and
pins are part of the same catalogue write.

The source identities are stored with imported entries. Repeating
an import preserves the previously imported spaces and does not
rewrite existing profiles. A source-read or directory preparation
failure leaves the catalogue unchanged. If a failure occurs after the
staging contexts have opened, their directories can remain on disk
without a catalogue entry.

```sh
python3 tools/verify_content_workspace_session.py --interactive-import \
  'path/to/Zitter Content Lab.app'
```

The macOS verifier selects Chrome and imports two disposable source
profiles through the form. It checks the bookmark targets, history,
encrypted cookies, and unchanged source files. It also rejects an
invalid source history database and verifies that a repeat after
restart preserves the imported profile and workspace IDs. This passed
in both themes on compositor and Metal. Import still uses the
standalone session's profile, workspace, pin, and file size limits.
Native Windows runtime and the Chrome-backed regression suite remain
open.

### Encrypted source cookies on macOS

The encrypted import probe uses two disposable version 24 cookie
databases with `v10` ciphertext and a private file Keychain containing
the source browser's synthetic Safe Storage password. The app uses
the regular source Keychain reader and the destination profiles use
their separate OSCrypt fixture key. The fixture permits only the
source password lookup and rejects password writes.

```sh
python3 tools/verify_content_workspace_session.py --encrypted-import \
  'path/to/Zitter Content Lab.app'
```

The probe checks that the Keychain lookup happens after confirmation,
that both imported cookie values are readable through the destination
cookie services after a restart, and that the source database bytes
stay unchanged. Refused Keychain access, a mismatched domain digest,
and an unsupported `v20` source each stop the import before any
profile is published. This passed with both themes on compositor and
Metal. The file Keychain password migration and rollback probe also
passed.

### Additional workspaces on existing profiles

The workspace settings form can create a named space on a registered
profile. A durable session writes the new workspace before making it
available in the window. The existing two-workspace session shape is
still accepted. An optional ordered catalogue stores the extra
workspace IDs and names, while each workspace session entry carries
its profile ID and its own tabs, split layout, and sleeping state.

Profile services are resolved from the workspace's profile ID.
Time Machine keeps the workspace and profile IDs separate and
checks each restored frame against the session's assignment.
Clearing a profile's Time Machine data removes the relevant records
from every workspace on that profile.

```sh
python3 tools/verify_content_workspace_session.py --extra-workspace \
  'path/to/Zitter Content Lab.app'
```

The probe creates Research through workspace settings, navigates to
a page that reads Work's cookies and local storage, and reopens the
session. It also checks that a session with an unknown profile and a
timeline with a different profile assignment are rejected. These
checks passed on macOS with both themes on compositor and Metal.
The native Windows app still needs verification.

### Link menus and workspace routing

The Content page delegate opens the shared link menu. It can open an
HTTP or HTTPS link in a new tab or Split View and assign its exact
hostname to a workspace. The catalogue saves rules by workspace ID.
Removing a workspace removes its rules. Undo restores them when a
newer rule does not already own the same host. The offline importer
also retains supported rules from the legacy workspace configuration.

The shared navigation throttle handles renderer-initiated top-level
link clicks with a user gesture. When the target is another space,
it cancels the source navigation before its first request and opens
the URL with the destination space's BrowserContext. The new request
has no source referrer or opener. Typed addresses, frame navigations,
redirects, POST requests, reloads, and history traversal do not
switch profiles through this throttle. Private windows do not load
the regular window's rules.

```sh
python3 tools/verify_content_workspace_session.py --link-routing \
  'path/to/Zitter Content Lab.app'
```

The probe creates a rule through the link menu, checks a typed
navigation and a renderer link on the same host, then restarts.
It opens the routed link in Split View, removes the rule through
the menu, and checks the saved session. The fixture records which
profile cookie and referrer arrived with each request. It received
only the Personal cookie and no Referer header on routed requests.
The probe passed on macOS in both themes with compositor and Metal.
The workspace-removal regression verified that only the restored
workspace's rule remains after Undo and restart. The offline import
unit test verified legacy rule conversion with no saved tabs.

The Workspace host and session store compiled to Windows x64 COFF.
Native Windows runtime remains a separate parity gate.

### Peek preview

The link menu and History can open an HTTP(S) page in Peek. The
preview uses the destination workspace's profile, including a
destination chosen by a hostname rule. The selected workspace and
its tab list stay in place while Peek is open. The toolbar navigates
the preview's own history. Promoting it to a tab or a split transfers
the existing WebContents and its history into the destination
workspace. Closed previews are omitted from the tab session.

Closing or replacing a preview, switching workspaces, and removing,
sleeping, or reassigning its owning workspace use the shared
beforeunload checks. A refusal keeps the preview open. A cross-workspace rule can
also route an eligible link clicked in the preview.

```sh
python3 tools/verify_content_workspace_session.py --peek \
  'path/to/Zitter Content Lab.app'
```

The probe opens Peek from the link menu in Work with a route to
Personal. The fixture confirms Personal cookies and local storage.
Hard Reload targets the visible Peek window and reloads the preview in
place while the Work tab stays on its existing page. The probe validates
the Peek window as a command target and then invokes the AppKit menu
item. This passed in light compositor and dark Metal on macOS using a
temporary bundle on a RAM volume.
The probe uses the toolbar to go back and forward, promotes the
same live page to a tab and then a different preview to a split,
and declines and accepts a beforeunload prompt. It checks that
closing Peek leaves no extra saved tab. The probe passed on macOS
in both themes with compositor and Metal. Link routing, popup,
private-window, history, workspace removal and reassignment probes
also passed. The Workspace host compiled to Windows x64 COFF.
The macOS dependency graph still has zero `//chrome` labels.

Shift-click opens ordinary HTTP(S) links and links with a new-window target
in Peek when the Workspace setting is enabled. The host matches the click to
the link URL and checks the renderer navigation before routing a new window
into Peek. Forms retain their original navigation. A window opened by a page
in Peek uses the preview's owning workspace and browser context. A foreground
child that switches workspaces follows the same beforeunload path as a manual
switch. The setting defaults to enabled, survives restart, and preserves an
explicitly disabled value during legacy Workspace import.

```sh
python3 tools/verify_content_workspace_session.py --shift-peek \
  'path/to/Zitter Content Lab.app'
```

On macOS, the relocated bundle passed this probe with compositor and Metal
in light and dark. It sends renderer mouse events for both link variants,
checks the destination profile's cookie on each request, opens a foreground
child from the preview, and confirms that Shift-click opens a regular tab
after disabling the setting and restarting. The updated Workspace host
compiled to Windows x64 COFF. Native Windows runtime remains open.

### Renderer-created tabs and popups

Renderer-created windows with a user gesture open as tabs in the
opener's workspace. The host installs page services and the navigation
throttle before resuming the first request. A same-profile popup can
retain its opener and can close itself. Background opens leave the
selected tab alone. Popups without a user gesture are blocked before
creation unless the source profile permits them.

A routed top-level link opened with `target="_blank"` discards the
temporary source tab before its first request and opens a fresh tab
in the target profile without an opener or referrer. GET and POST forms
keep the original profile, method, and submitted data.

```sh
python3 tools/verify_content_workspace_session.py --popup-commands \
  'path/to/Zitter Content Lab.app'
```

The probe checks a blocked `window.open`, a background open, a
same-profile popup with an opener, script-initiated popup close, GET
and POST forms, and a routed blank-target link. The local server
records profile cookies, referrers and the submitted payload. The
session retains the original tabs without empty popup placeholders.
It passed on macOS in light and dark with compositor and Metal. Link
routing, private windows, split commands and tab moves also passed
their regression probes. The macOS dependency graph still has 13,720
labels and zero `//chrome` labels.

The Windows launcher, Workspace host, and shared link throttle compiled
to x64 COFF. The cross-check of the browser client stopped on generated
Windows printing types and an incomplete V8 type in the MSVC headers.
Native Windows linking and runtime coverage remain open.

### Profile popup settings

The site panel can allow or block automatic popup creation for an
exact HTTP(S) origin. The profile's general setting supplies the
default. A site rule overrides that default, and forgetting the site
setting removes the exception. An unrelated embedded origin cannot
borrow the top-level page's permission. A popup allowed without a
gesture stays with the opener's profile and does not trigger link
routing. Existing behavior for user-initiated opens is unchanged.

Private sessions take a copy of the originating profile's popup
settings. Changes in that session are discarded with it. They do
not write to the source profile. The settings share the existing
site permissions file with Clipboard rules. Startup validates
canonical origins and decisions before using them.

The offline migration converts Chromium's default and durable
exact-origin popup rules. Managed, wildcard, temporary and unsupported
rules require a separate migration. The importer rejects those rules
before publishing the staged profile pair.

```sh
python3 tools/verify_content_workspace_session.py --popup-policy \
  'path/to/Zitter Content Lab.app'
```

The probe grants an exception in Work through the site panel, reopens
the app, and checks a popup in Work with the same hostname routed to
Personal for user clicks. Personal and a cross-origin iframe still
block the automatic popup. The probe resets the exception and checks
that a site block overrides an allowed profile default. It also
rejects a saved rule containing a path. The probe passed on macOS in
both themes with compositor and Metal. Private-window, popup, link
routing and Clipboard regressions passed. The private-window probe
verified inheritance and checked that private edits leave Work
unchanged. The offline importer passed its 36 tests, including
exact-origin conversion and wildcard rejection.

The Workspace host, Windows launcher and permission controller
compiled to Windows x64 COFF. Native Windows runtime and the browser
client build remain open. The macOS dependency graph contains zero
`//chrome` labels.

### Split View commands

Command Center can open and close Split View and focus either of its
first two panes. It pairs the current tab with another open tab when
available. In a one-tab workspace it opens a blank companion and its
address field. Closing Split View keeps both pages open.

A saved pin can open directly in Split View. The newly added pane gains
focus. Moving a pane carries its width or height with it. Taking a pane
out of the split keeps its page open and moves focus to a neighboring
pane if needed. Split edits are saved during the session.

Closing a split group checks beforeunload for all its pages before
closing any of them. A refusal leaves the layout and pages intact.
Closing an approved group retains the open pages outside the split,
keeps their saved pins, and records the closed pages for recovery.
If the group contained every tab in the space, the window opens an
empty tab and its address field.

```sh
python3 tools/verify_content_workspace_session.py --split-actions \
  'path/to/Zitter Content Lab.app'
```

The probe opens a split, switches pane focus, and closes it through
Command Center before it uses the sidebar menu to open a closed pin in the split,
reorder panes, add and remove a third pane, and close the group.
It first approves one page and rejects the second, then approves
both and checks that the survivor is the same WebContents. It
reopens the pin, closes a split that contains every tab, and checks
the blank tab, address field, persisted tab list, and retained pin.

The probe passed on macOS in both themes with compositor and Metal.
Tab moves, recently closed tabs, the developer project split, and
legacy pin and split migration with rollback also passed their probes.
The Workspace host compiled to Windows x64 COFF. Native Windows runtime coverage remains open.

### Duplicating and moving tabs

The sidebar tab menu can duplicate an open tab or a saved pin. The
keyboard duplicate command uses the same implementation. A duplicate
opens as an ordinary tab in the same profile. Open tabs copy Chromium's
navigation controller state, including the back and forward entries.
A closed pin opens its stored URL without another pin association.

The sidebar menu also moves a tab to another workspace. A move within
one profile transfers the existing WebContents and its navigation
state. A move to another profile opens the saved address in that
profile after any required beforeunload confirmation. It creates a
new WebContents on selection, so cookies and storage come from the
destination and the source page's navigation entries do not cross the
profile boundary. A moved tab is unpinned in the destination. The
source keeps its saved pin and gets a blank tab if the moved tab was
its last one. The destination keeps its selected tab. A tab in
an active capture session cannot be moved.

Command Center exposes a move for another space on the same profile.
For a space on a different profile, it opens a copy of the current HTTP
or HTTPS address in a fresh tab and selects it. The copy uses the
destination profile's storage and leaves the source tab open. The
command targets the selected tab, its committed navigation entry, and
the explicit destination space.

```sh
python3 tools/verify_content_workspace_session.py --tab-commands \
  'path/to/Zitter Content Lab.app'
```

On macOS, File exposes Duplicate Tab, and Window exposes Next Tab and
Previous Tab. The probe activates the native menu, checks that cycling
is disabled for a single tab, duplicates a Work tab with back history,
and switches to the original and back. The test passed in light mode
with the compositor and dark mode with Metal. It then rejects a stale
Command Center result after switching tabs, then moves the live duplicate
to another Work space through Command Center. The duplicate inherits the
Work tab's session storage. A separate move to Personal through the sidebar
honors refusal and acceptance of beforeunload. The probe then copies a
Work address to Personal through Command Center. Both destination pages
read Personal cookies and local storage and start without source history
or session storage. The probe checks the persisted layouts and verifies
that the copy leaves the Work tab open.

The probe passed on macOS in both themes with compositor and Metal.
The navigation, recently closed tab, shared favorite, and legacy
pin-edit and split migration with rollback probes also passed.
The Workspace host cross-compiled to Windows x64 COFF. Native
Windows runtime remains a separate check.

### Close other tabs

Command Center can close the other unpinned tabs in the current
workspace, including when the selected tab is blank. The active tab
and open pinned tabs retain their pages. The shared beforeunload batch
checks the closing pages before committing any closure. One refusal
leaves the entire group open. Successful closure records the group in
Time Machine and adds individual entries to recently closed tabs.
The toast offers grouped Undo in the originating workspace and profile.
It reopens the closed addresses in their original slots as deferred
tabs and removes the matching recently closed entries. The selected
and pinned pages retain their existing WebContents.

The `--split-actions` probe also checks this command. It approves the
first background page and refuses the second, then retries and
approves both. It checks that Command Center hides the action when
only the selected and pinned tabs remain. It then activates Undo
through the toast button and checks the restored deferred tabs, the
live active and pinned pages, and the recently closed list. It closes
the restored group again and checks the persisted two-tab layout.
The relocated macOS app passed in light and dark on compositor and
Metal. The tab transfer regression also passed. The modified Workspace
compiled to AMD64 COFF on macOS. Native Windows runtime remains open.

### Shared profile favorites

A profile favorite is available in every workspace on that profile.
Its URL and order are shared. Each workspace keeps its own open tab
association and navigation state. Unpinning removes the shared favorite
and converts its open instances to ordinary tabs. Workspace folders
and local pins remain scoped to their space.

The session catalogue saves favorites by profile ID. Removing the last
space on a profile leaves its favorites in the catalogue. Reassigning a
space shows the destination profile's favorites and leaves the source
profile's favorites available to its other spaces. Older sessions and
offline imports with favorites embedded in workspace entries are read
and normalized on the next session write. Tab associations are validated
against the workspace's local pins and the assigned profile's favorites.
Ambiguous legacy favorite IDs with different definitions are rejected.

```sh
python3 tools/verify_content_workspace_session.py --shared-favorites \
  'path/to/Zitter Content Lab.app'
```

The probe opens an older session, creates a second space on Work, and
checks that the favorite survives normalization and restart. The
closed favorite's context menu opens an unpinned copy in the second
space with the Work profile and fresh navigation history. The probe
then opens the same favorite in both Work spaces, edits the shared target, and
unpins it and checks both pages survive as normal tabs. A Personal tab
cannot restore an association to the Work favorite. The probe passed
on macOS in light mode with compositor and dark mode with Metal.
Earlier regressions covered workspace removal, reassignment, interactive import, and the
legacy pin-edit and split migration with rollback. The updated Workspace
and session store compiled to Windows x64 COFF. Native Windows runtime
coverage remains open.

### Renaming workspaces and profiles

The workspace settings and profile manager save names in the session
catalogue. The new name appears after the catalogue write succeeds.
Renaming keeps the workspace and profile IDs, browsing contexts,
tabs, and storage paths. Default Work and Personal names remain
implicit in older sessions. Optional name overrides are validated
against those built-in IDs. Extra and imported entries keep their names
in their existing catalogue records.

The `--extra-workspace` probe renames the additional workspace, the
built-in Work workspace, and its browser profile through the forms.
It checks that the live page survives the changes and the names and
profile assignment survive a restart. The probe also rejects a name
override for an unknown profile. The macOS trial passed in both
themes on compositor and Metal. The Windows launcher and shared
session and workspace files compiled to x64 COFF objects. The native
Windows runtime remains unverified.

### Reassigning a workspace to another profile

The workspace settings picker and workspace menu can change the profile
of a built-in or additional space. The current profile's pages can
request beforeunload confirmation. Reassignment waits for the catalogue
write before replacing the browser views. Like the Chrome host, it
opens a blank tab in the destination context and keeps the space's
identity and pins. Open and recently closed tabs, navigation state, and
the split layout are dropped. Cookies, site storage, and other services
remain with their owning profiles.

The catalogue assigns a fresh Time Machine epoch in the same atomic
write as the profile change. Timeline frames and marks from previous
assignments are discarded in memory and ignored when reopening an older
journal. This covers a process exit before the new journal is written
and prevents older records from returning if the space is later
assigned to its original profile. Spaces that have never been
reassigned keep the existing journal format and profile checks.

```sh
python3 tools/verify_content_workspace_session.py --reassign-workspace \
  'path/to/Zitter Content Lab.app'
```

The probe changes Research from Work to Personal using the workspace
settings form. It checks the blank tab, then navigates to a page on the
same origin to verify that the browser uses Personal's cookies and local
storage. It replaces the journal with older Work frames and a Work mark
before reopening. The current assignment's frames and mark remain
isolated, and the other workspaces keep their tabs and history. An invalid
epoch is rejected at startup. The reassignment probe passed on macOS in
both themes with compositor and Metal. The removal and two-profile
session probes also passed on the same build. The modified Workspace,
session, and timeline files compiled to Windows x64 COFF. Native Windows
runtime coverage, reassignment from the inactive workspace menu, and
cancellation through beforeunload remain open runtime checks.

### Removing and restoring workspaces

The Workspace confirmation can remove a built-in or additional space
when another space remains. If the removed space is active, the first
remaining space becomes active. Tabs that need a beforeunload prompt
are checked before removal. The catalogue is saved before changing
the displayed list. The existing Undo action restores the space to
the end of the list and reopens its tabs from their stored URLs, with
their selected tab, pins, split layout, and profile assignment.
Removing a space leaves its profile and browser data available.

The catalogue records each removed workspace ID and its profile in
the same atomic write. On restart, Time Machine validates and skips
older records for those IDs, so an exit before the journal is updated
does not make the remaining history unreadable. Undo within the same
session clears the removed ID and restores access to its timeline.

The `--extra-workspace` probe uses the confirmation and Undo action
for an inactive built-in space and an active additional space. It
then removes the additional space again and reopens with its old
timeline records still on disk. It checks that the remaining tabs and
names survive, and that the catalogue rejects a removed ID associated
with an unknown profile. The probe passed on macOS in light and dark
with both compositor and Metal. The Windows launcher, Workspace host,
session store, and timeline store cross-compiled to x64 COFF objects.
The native Windows runtime and a page rejecting removal through
beforeunload remain separate checks.

### Restoring an additional browser profile

The durable session accepts an ordered catalogue of additional
profiles. Each profile ID determines a direct child of the locked
workspace root. An existing profile and its network directory must
pass path, preference, and cookie-store checks before opening a
browser context. The two default profiles retain their existing paths
and session format. Each additional context uses the same OSCrypt
provider as its root and its own history, permissions, passwords,
network database and site storage.

On macOS each registered network directory is included in the
network process's sandbox configuration at launch. Windows checks
the same additional cookie databases with DPAPI and verifies the
empty network bootstrap directory before using Chromium's storage
grant. The catalogue can also restore the network layout used by
profiles created during the session.

```sh
python3 tools/verify_content_workspace_session.py --additional-profile \
  'path/to/Zitter Content Lab.app'
```

The disposable macOS trial bootstraps Work and Personal, then adds a
third directory and session entry offline. The verifier checks that a
same-origin page starts with empty cookies and local storage in the
new profile, then restores both across a restart. It checks that the
cookie on disk is encrypted and that startup rejects a missing
directory or invalid encrypted data. The trial passed in both themes
on compositor and Metal. A separate compositor run also rejected a
symbolic link in place of the profile directory. The shared context,
workspace, session and timeline files and the Windows launcher
compiled to Windows x64 COFF objects with local SDK headers. The
launcher check used Windows SDK headers in place of the non-Windows
Crashpad compatibility headers and set Chromium's
`WIN32_LEAN_AND_MEAN` define. Windows GN generation with the local
header-only toolchain stopped at the runtime-directory configuration.
Windows linking and runtime verification remain open.

### Creating a profile during a session

The profile manager creates a standalone profile that can be chosen
when creating a workspace. It prepares the directories and atomically
writes the session catalogue before adding the profile to the window.
The workspace creation form still uses existing profiles. Creating and
assigning a profile in one action remains open.

On macOS the network service receives access to a dedicated
`NetworkProfiles` directory before it starts. Each newly created
profile uses its own subdirectory there for network data. Its login
store, history and preferences stay in the profile directory. Existing
profiles keep their original paths. On Windows newly created profiles
use their own `Network` and `NetworkBootstrap` directories.

```sh
python3 tools/verify_content_workspace_session.py --create-profile \
  'path/to/Zitter Content Lab.app'
```

The probe uses the profile manager and workspace form after Work and
Personal have loaded. It opens the same origin in the new profile,
checks the encrypted cookie on disk, and reopens the workspace to
verify the profile assignment and site storage. This passed on macOS
with both themes on compositor and Metal. An additional run corrupted
the separate cookie database and verified that the next startup
rejected it before loading the workspace. Native Windows verification
remains open. The standalone macOS GN graph contains 13,720 labels
and zero under `//chrome`.

### Developer mode and local project comparison

The standalone Workspace uses the shared Developer mode menu from the
macOS menu bar, Command Center, and the Workspace header. Auto mode
shows the header control for localhost and loopback URLs. The shared
shell saves the selected mode in `ZitterTools.json` for regular
windows. Command Center also exposes DevTools, Console, Inspect, and
Hard Reload for the current page.

The Compare Project command opens the current local URL in a separate
tab in the same profile and displays both tabs in Split View. Each
pane has its own WebContents and navigation state.

The `--find-reload` verifier also opens the Developer menu, activates
Compare Project, and checks the two panes. It verifies that Auto hides
the control on a blank tab, opens the settings through Command Center,
switches to Off, and inspects the saved value. Native Windows runtime
coverage remains open. The relocated macOS bundle passed in both themes
on compositor and Metal. The navigation and private-window probes also
passed on the compositor.

### Tab favicons

The Content Workspace uses Chromium's Content favicon driver. Loaded
tabs publish their icons through the shared sidebar state. Closed pins
and deferred tabs read exact URL matches from the active profile's
favicon service, without creating a page or making a network request.
The lookup cache keeps only URLs for the displayed Workspace and
cancels outstanding lookups when the active profile changes. Private
contexts fetch icons for loaded pages in memory.

The offline importer verifies and copies `Favicons` and its SQLite
sidecars. It supports schema 9, checkpoints the staged copy, and
drops bitmap records with invalid dimensions or payload sizes.
The legacy source and rollback stay intact.

```sh
python3 tools/verify_content_workspace_session.py --favicons \
  'path/to/Zitter Content Lab.app'
```

The probe collects different tab icons for Work and Personal and uses
their databases as a migration fixture. It checks the sidebar pixels
over two launches while the icon endpoints are unavailable. Both
profiles have the same closed-pin URL with different icons. The probe
also checks an icon for each deferred tab, verifies that neither database
contains the other profile's icon, and requires no requests for the
closed pin or deferred pages. The relocated macOS bundle passed in both
themes with compositor and Metal, along with the shared favorites and
private window regressions.

The Command Center action **Refresh all favicons** refreshes loaded tabs,
deferred tabs, and closed pins in the active Workspace. It fetches
bounded HTML and images using the profile network partition with
credentials omitted. It uses the shared favicon link parser and sends
the fetched bytes to Chromium's renderer for decoding. It processes pages
sequentially and abandons the batch on a Workspace or profile switch.
A failed fetch keeps the old icon. Successful icons update open
navigation entries and the profile favicon database.

The verifier invokes the command with cached icons, changes the
server-side icon, and checks Work's open tab, deferred tab, and closed
pin. It verifies that discovery and image requests carry no profile
cookies and that image responses cannot set cookies. The refreshed
icons use Chromium's standard scale variants and reopen from disk
without another image request. The relocated macOS bundle passed in
both themes with compositor and Metal. Native Windows verification
remains open.

### Profile and site page zoom

The standalone Workspace uses the same default page zoom choices as the
Chrome-backed browser. Chromium's profile zoom map applies a change to
open pages in the selected profile. The chosen value persists in that
profile, and a new private session inherits its source profile's current
default with a read-only settings control.

The offline importer reads `partition.default_zoom_level.x` and
`partition.per_host_zoom_levels.x` from each legacy profile. Chromium
uses `x` for the default storage partition. The importer keeps exact
zoom levels, including valid values outside the Settings choices, and
preserves valid last-modified timestamps. It accepts both the older
numeric host entries and the current dictionary format. Invalid host
entries and entries matching the profile default are skipped. Invalid
default levels or malformed partition maps abort the import.

Content applies each host override through Chromium's zoom map. Changing
the profile default removes overrides that now match it, including from
the persisted profile. New private sessions copy the profile's current
zoom map into memory.

```sh
python3 tools/verify_content_workspace_session.py --page-zoom \
  'path/to/Zitter Content Lab.app'
```

The verifier seeds Work at 125% and Personal at 90% using the offline
conversion. It changes Work to 150% using Settings, checks the zoom
levels of open pages in both profiles, and relaunches to confirm the
values persist. It also checks distinct overrides for the same host, the
removal of an override that matches the new default, and private
inheritance. The private browser navigates between two loopback hosts
and checks the effective zoom on each page. The relocated macOS bundle
passed with both themes on compositor and Metal. Native Windows
verification remains open.

### Find in page and hard reload

The standalone host uses Chromium's `FindTabHelper` with Zitter's
existing find bar. Searches and renderer replies belong to the active
document. Changing the active tab or starting a cross-document
navigation in the main frame clears the bar. The hard-reload action uses Chromium's
cache-bypassing reload and retains the repost check.

```sh
python3 tools/verify_content_workspace_session.py --find-reload \
  'path/to/Zitter Content Lab.app'
```

On macOS the verifier opens Find through Command Center, reopens it
from the native menu, and uses the native Hard Reload item with both
themes on compositor and Metal. It finds two matches with different casing, advances with
Enter, switches workspaces, and checks that the query does not carry
over. The loopback server receives an ordinary request followed by a
request with `Cache-Control: no-cache` for the same cacheable document.
The second response supplies a different title, and the find bar is
closed after navigation. Native Windows execution remains open.

### Other Command Center actions

The standalone catalogue exposes New Tab, Settings, the sidebar toggle,
Downloads, History, Passwords, Reload, Close Tab, and Print in builds
with PDF printing enabled. History and Passwords are restricted to
eligible profiles. Page actions retain the navigation entry ID and
execute only while that entry is current in the originating workspace.
The shared print preview probe now opens the sheet from Command Center.

Command Center also opens the shared keyboard shortcut, search provider,
Workspace and profile settings. It opens the Assistant panel when a
configured provider is available, and offers Assistant settings when
the panel is unavailable in a persistent regular Workspace. Private
windows omit Workspace management, profile management and Assistant
commands. An empty query shows six available commands. A leading `>`
shows the full list, including Refresh all favicons.

The profile creation probe opens the shortcut and search provider panels
in succession, then opens Workspace and profile management and creates
a test profile. Switching panels exposed a dangling reference to a
shortcut button in the shared shell. The panel now clears references to
child controls before removing them. The Assistant configuration probe
opens settings through the disabled Assistant command and opens the panel
through the enabled command. The private-window probe checks the omitted
entries directly. The relocated macOS arm64 bundle passed all three probes
in light and dark with compositor and Metal. GN include checking passed,
and the graph contained 13,743 dependency labels with zero under
`//chrome`.

Both hosts use the same Command Center ranking and shortcut formatter.
The compact list applies its six-result limit after ranking, with
catalogue order preserved for equal scores. Usage is tracked by command
and target. Content checks that an action still appears in the current
catalogue with the same Workspace, tab and navigation entry before it
records the use. A regular Workspace saves its scores and shortcut
configuration in `ZitterTools.json`. Private windows keep tool changes
in memory.

```sh
python3 tools/verify_content_workspace_session.py --command-center \
  'path/to/Zitter Content Lab.app'
```

The probe changes the Downloads shortcut and submits the command through
Command Center. It checks that Downloads moves to the top of the compact
list, displays the new shortcut, and retains both after restart. A stale
Find result from a different tab is excluded from the saved usage history.
The relocated macOS arm64 bundle passed in both themes with compositor
and Metal. The tab move and copy and Find and Hard Reload regressions also
passed. The Chrome host's command adapter passed a Clang syntax check,
and the Content graph retained zero `//chrome` dependency labels.

### Copy the page address

The sidebar button and Command Center copy the active page address. The
macOS Edit menu exposes the same action. Copying from the address editor
honors the same setting. Zitter uses the existing tracker rules and
preserves the remaining query parameters and fragment. The setting
defaults to enabled, and migration preserves an explicit opt-out from
the legacy configuration. Private windows read the current setting
from the regular Workspace.

```sh
python3 tools/verify_content_workspace_session.py --copy-address \
  'path/to/Zitter Content Lab.app'
```

The macOS probe uses a process-local named pasteboard. It exercises the
sidebar and native menu with cleaning enabled, then checks the original
address through Command Center and the address editor after disabling
cleaning. It also verifies the saved setting. The relocated, ad hoc
signed app passed in both themes on compositor and Metal. An offline
migration test covers an opt-out with no legacy session file. The
modified Workspace and session store compiled to AMD64 COFF objects
on macOS. Native Windows clipboard behavior remains unverified.

### RSS reader

The native RSS reader opens from the macOS menu, Command Center, and
the shared keyboard binding. The existing internal tab address is
`zitter://rss/`. Chromium records that address in tab history using
an opaque document with no content beyond the reader title. The feed
and article text are rendered by the shared native panel.

Each browser context owns its subscriptions. The reader uses the
profile's network factory with credentials omitted and parses XML in
Chromium's sandboxed data decoder. Regular profiles write the existing
`ZitterRSS.json` format. Private contexts keep subscriptions in
memory and discard them with the context. The offline importer copies
verified RSS files into their corresponding profiles and preserves
the exact reader URL in tabs, pins, and Time Machine snapshots.

```sh
python3 tools/verify_content_workspace_session.py --rss \
  'path/to/Zitter Content Lab.app'
```

The relocated macOS app passed with both themes on compositor and
Metal. The verifier discovers an advertised RSS feed, fetches it
without Cookie or Authorization headers despite an existing profile
cookie, and checks that Personal has no subscriptions. After restart
it opens the restored Work tab through Command Center and checks the
saved and read flags. The migration tests also verify that a file
modified after trial preparation prevents publication. The standalone
GN graph contains 13,720 labels and zero under `//chrome`. The
Chrome-backed RSS browser tests, native Windows runtime, and a
dedicated private RSS runtime test remain open.

### Save a page as PDF

The standalone Workspace exposes Save as PDF for the active HTTP or
HTTPS page. It uses Chromium's renderer print helper and PDF job. The
tab delegate forwards cross-process subframe print requests to
Chromium's sandboxed print compositor. The browser passes the finished
PDF to the native save picker and writes it atomically to the selected
path. A document change or tab switch dismisses an outstanding export
before the user selects a destination.

```sh
python3 tools/verify_content_workspace_session.py --print-pdf \
  'path/to/Zitter Content Lab.app'
```

The relocated macOS verifier dispatches the native menu action with a
controlled save picker in both themes on compositor and Metal. It confirms that the iframe runs in a different
renderer process and uses PDFKit to verify that the saved document
contains text from both frames. The compositor checks its sandbox on
startup. The standalone GN graph has 13,708 dependency labels, with zero
under `//chrome`.

### Print sheet preview and ranges

The Print action uses Zitter's shared print sheet. Chromium produces the
preview PDF, then a sandboxed PDFium utility rasterizes its pages. The
browser receives bounded bitmaps. The preview accepts up to 64 pages at
480 pixels on the longest edge and coalesces layout changes while a
render is running. Changing tabs or navigating closes the sheet and
discards pending results. The separate Save as PDF action remains
available for larger documents.

```sh
python3 tools/verify_content_workspace_session.py --print-preview \
  'path/to/Zitter Content Lab.app'
```

The relocated macOS app opened Print through the native menu and
Command Center in separate runs. It rendered two preview pages,
selected page two, changed the orientation, and exported through a
controlled save picker. PDFKit verified one landscape page with only
the second page's content. This passed in light and dark on compositor
and Metal. The direct PDF export regression also passed.
The standalone GN graph has 13,719 dependency labels and zero under
`//chrome`.

### Page-initiated printing

`window.print()` in the primary main frame can open the shared sheet
after returning control to the page. The renderer keeps Chromium's
scripted print throttle. The browser binds the request to the document,
checks visibility before queuing and again before dispatch, and requires
the active tab and window. It ignores additional requests while the
sheet is open and spaces accepted requests at least two seconds apart.
The sheet uses the same sandboxed preview and PDF export path as the
menu command.

```sh
python3 tools/verify_content_workspace_session.py --scripted-print \
  'path/to/Zitter Content Lab.app'
```

The relocated macOS verifier calls `window.print()` in a hidden tab and
checks that no sheet appears. It then calls it in a fresh visible
document, waits for the sheet, and verifies that the selected landscape
page is exported. The verifier passed with both themes on compositor
and Metal. The manual-menu preview and direct PDF export probes passed
as regressions.

The shared sheet exposes PDF as its direct destination. Print requests
initiated inside iframes and interaction with the real save dialog remain
unverified.

### macOS system print dialog

The File menu, Shift+Command+P, and the link in the shared print sheet
open the AppKit print panel on the active Workspace window. After
confirmation, the embedder checks that the original document is still
active and uses the selected paper and orientation to generate a PDF
through Chromium's sandboxed printing pipeline. Core Printing receives
the PDF bytes, printer settings, and job title. A local PDF destination
is written atomically. Page ranges remain in the shared PDF sheet.

```sh
python3 tools/verify_content_workspace_session.py --system-print-cancel \
  'path/to/Zitter Content Lab.app'
python3 tools/verify_content_workspace_session.py --system-print-file \
  'path/to/Zitter Content Lab.app'
```

The relocated macOS app opened and cancelled the AppKit sheet in both
themes on compositor and Metal. A separate probe supplied a temporary
file destination directly through Core Printing settings. It verified a
landscape PDF containing text from the main document and its
cross-process frame. The shared preview and scripted-print probes passed
again. Submitting a job to a physical printer, confirming a destination
through the native panel, and the native Windows print dialog remain
open.

## Durable Content workspace trial

The macOS lab can reopen a disposable Workspace after a clean close. It
shares the URL, title, and profile association codec with the Chrome host.
It stores active workspace and tab selection in `ContentWorkspaceSession.json`
and loads inactive tabs on demand. The session store uses an atomic
replacement and rejects invalid data or profile associations before startup.
A successful close also flushes the two cookie stores. Work and Personal use
distinct profile directories and share a disposable OSCrypt key fixture.
An exclusive lock protects the root across both profiles.

Run from the migration worktree after building the lab.

```sh
root="$(mktemp -d "${TMPDIR%/}/zitter-content-probe-XXXXXX")"
root="$(cd "$root" && pwd -P)"
python3 -c 'import os, secrets, sys; fd = os.open(sys.argv[1], os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600); os.write(fd, secrets.token_bytes(32)); os.close(fd)' "$root/probe-cookie-key"
app='.chromium/src/out/ZitterContent/Zitter Content Lab.app/Contents/MacOS/Zitter Content Lab'
"$app" --zitter-content-workspace --zitter-content-workspace-root="$root"
```

Use distinct synthetic pages in Work and Personal, select a non-first tab,
switch to Personal, close the window, and rerun the last command with the
same root. Verify the selected workspace and both tab lists. With the
first app open, a second app targeting the same root must fail to lock it.
The existing profile storage probe checks a separate private context.

An automated loopback probe exercises the same open and close paths.

```sh
python3 tools/verify_content_workspace_session.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

It checks that a held lock and a mismatched profile reject startup. It
then reopens both workspaces twice with different cookies and
`localStorage` values on the same origin. Only selected tabs may reach
the server. The saved selection and all deferred tabs must survive, and
both cookie databases must store ciphertext. The linked macOS browser
passed the held-lock and mismatched-profile checks and both launches. The
loopback server received only the two selected tab requests on each run.

The session trial stores the last committed address and title per tab.
It can restore splits, pins, profile favorites and sleeping workspaces.
Back and forward entries remain a separate gate. This fixture uses a
root in the system temporary directory. Imported workspaces can use the
explicit prepared workspace mode described above.

To use the macOS Keychain with a disposable workspace, use a fresh temporary
root and pass `--zitter-content-workspace-keychain`. This mode uses
Chromium's OSCrypt Keychain provider and its own `Zitter Content Lab Safe
Storage` entry. It starts both persistent browser contexts only after the
provider makes an encryption key available. It uses separate profile
directories and session files from the fixture mode. The automated verifier
continues to use the fixture key.

```sh
"$app" --zitter-content-workspace --zitter-content-workspace-root="$root" \
  --zitter-content-workspace-keychain
```

A local C++ probe linked the Content cookie provider with Chromium's
Keychain provider and fake Keychain. It verified the lab service and account
names, retrieval of an existing key, creation when an item is missing, and
unavailability when access is denied. The file Keychain migration probe
below covers linked browser reopen with Security.framework. The login
Keychain prompt and migration of existing data encrypted under the
production `Zitter Safe Storage` item remain open.

### macOS file Keychain migration probe

Run the legacy storage and login probes with the installed macOS Zitter
app, the relocated ad hoc Content Lab app, and the local login verifier.

```sh
python3 tools/verify_content_storage_migration.py --file-keychain --split \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
python3 tools/verify_content_login_migration.py --file-keychain \
  /Applications/Zitter.app \
  '.chromium/src/out/ZitterContentLoginProbe/verify_login_database' \
  --content-app 'path/to/Zitter Content Lab.app'
```

The legacy browser writes synthetic stores using its fake Keychain and
disposable profiles. Content reads the copies without the mock flag.
The injected fixture scopes the Lab's generic password requests to a
private Keychain file through `kSecMatchSearchList` and `kSecUseKeychain`.
It seeds the fixture password into that file with Security.framework.
Chromium's normal Keychain provider retrieves it and derives the OSCrypt
key. The fixture does not install a user Keychain or set it as default.
See [Apple's macOS Keychain API technote](https://developer.apple.com/documentation/Technotes/tn3137-on-mac-keychains).

The storage probe requires two successful launches with distinct Work
and Personal cookies, localStorage, history and Clipboard decisions.
It checks that the Keychain password item is reused on the second
launch. A separate copy starts with an empty file Keychain, creates an
unrelated password item and rejects the encrypted store before exposing
either profile. It checks profile inventories after rejection and
verifies the legacy rollback against the offline baseline. The login
probe checks that both profiles autofill their imported synthetic
credentials after restart while retaining the original ciphertext.
Both probes passed with the relocated arm64 app. The storage trial also
passed with restored horizontal and vertical splits. The user's default
Keychain and search list matched before and after the trial. The fixture
covers a real file Keychain and the linked Content browser. The installed
application's Keychain access prompt, production signing identity, and
migration using a user's existing secrets remain unverified.


#### Legacy-generated key continuity

Pass `--shared-keychain` in place of `--file-keychain` to have both
browsers use the same private file. The verifier copies the installed
legacy app and ad hoc signs the copy so the test library can load. The
legacy browser runs without the mock flag and creates a random secret
under its `Zitter Safe Storage` identity. The fixture scopes both apps
to the private Keychain file and adds the copied legacy app, the
relocated Content app and, for the login probe, the offline database
verifier to the item's test access list.

The opt-in Content Workspace mode requires
`--zitter-content-workspace-keychain --zitter-content-workspace-legacy-keychain`.
It uses Chromium's OSCrypt provider with an existing-only lookup for
`Zitter Safe Storage` / `Zitter`. This lookup uses the legacy service
name directly. The provider never creates or replaces the item and
uses the same key derivation and v10 format as Chromium's usual provider.
The Lab's default Keychain identity remains separate.

Before opening either BrowserContext, the migration mode validates
both profiles' cookie and password databases on disposable copies.
A missing or denied key or an unreadable database aborts startup
before the profiles can open. Password validation checks decryption,
UTF-8 and exact re-encryption with the active key. Legacy password CBC
does not authenticate ciphertext, so this is a compatibility check.
Cookie validation also checks Chromium's host digest.

The shared probes compare cookie and login ciphertext before and after
migration. They check that the restored legacy copy still reads the
original stores. The login probe also checks refusal on missing and
denied lookups and malformed ciphertext in a profile with no cookie
database, with no Keychain write attempt or change to either profile.
The shared login and split storage trials passed on macOS arm64. The
isolated Lab Keychain path and relocated bundle also passed. The default
Keychain and search list matched before and after the trials. The
non-component GN graph contained 13,590 labels and zero `//chrome`
labels.

The tests use ad hoc copies and a private file Keychain. The installed
application's production signing identity, the login Keychain access
prompt, and migration with a user's existing secrets remain unverified.

A local C++ probe linked the session codec and store against the pinned
Chromium libraries. It round-tripped both workspace selections and a
Unicode title, rejected a profile mismatch and corrupt JSON, and checked
that writes through a symbolic link leave the original file intact.

## Windows bootstrap

The Windows lab target builds `zitter_content.exe` and starts browser and
child processes through ContentMain with Chromium's Windows sandbox setup.
It shares the resource client, temporary browser context and renderer
sandbox check with macOS. The initial window hosts one WebView. The
optional Workspace window uses the shared Views shell, Windows native frame,
and two separate browser contexts. It runs in the compositor. The default
Workspace uses temporary contexts. Passing an explicit probe root enables
two persistent contexts and DPAPI-backed cookie encryption.

From a developer shell with the toolchain described in
[Windows port](windows-port.md), stage into a dedicated pinned checkout.

```powershell
python tools/stage_content_embedder.py .chromium/src
New-Item -ItemType Directory -Force .chromium/src/out/ZitterContentWin | Out-Null
Copy-Item chromium/windows.args.gn .chromium/src/out/ZitterContentWin/args.gn
Set-Location .chromium/src
gn gen out/ZitterContentWin --root-target=//zitter_content:zitter_content
gn check out/ZitterContentWin "//zitter_content:*" --root-target=//zitter_content:zitter_content
gn desc out/ZitterContentWin //zitter_content:zitter_content deps --all --root-target=//zitter_content:zitter_content
autoninja -C out/ZitterContentWin zitter_content
```

Run `out/ZitterContentWin/zitter_content.exe --zitter-content-smoke-test` in
a GUI session with a timeout and captured stdout. The test should report
`Zitter Content Lab smoke passed` and exit. The marker requires JavaScript
execution in a separate renderer that passes the sandbox check. Inspect the
full Windows dependency graph for `//chrome` before considering this gate
passed.

Run the Workspace smoke in light and dark with the same timeout and captured
stdout. Each invocation should report `Zitter Content workspace smoke passed`
after selecting both profiles and returning to the original renderer.

```powershell
out/ZitterContentWin/zitter_content.exe --zitter-content-workspace-smoke-test --zitter-content-workspace-theme=light
out/ZitterContentWin/zitter_content.exe --zitter-content-workspace-smoke-test --zitter-content-workspace-theme=dark
```

Use `--zitter-content-workspace` for an interactive review of the window,
navigation, tab close, workspace switching, and native caption controls.

From the migration worktree, run the persistent Workspace verifier.

```powershell
python tools/verify_content_workspace_session.py .chromium/src/out/ZitterContentWin/zitter_content.exe
```

The verifier creates a disposable root directly below the Windows temporary
directory. It checks that a held lock or invalid session prevents startup,
then reopens Work and Personal on the same loopback origin with separate
cookies and DOM storage. The browser must store `v10` ciphertext under each
profile's `Network` directory. The verifier removes the key, corrupts it,
and substitutes a different key wrapped by DPAPI for the same Windows user.
It requires startup to refuse all three cases without altering the cookie
databases, their sidecars, or the saved session. It restores the original
key for one more reopen.

The Windows host uses Chromium's `DPAPIKeyProvider`. It creates a key only
when neither profile directory nor the lab Local State exists. It reads the
existing key without rewriting Local State on subsequent starts. The network
service grants sandbox access to each `Network` directory through Chromium's
existing path setup, with an empty bootstrap directory beside it.
Before opening the profiles, startup checks the cookie and login stores
against the active key on disposable copies, including SQLite sidecars.
The cookie check accepts schema 24 and verifies the host hash in every
decrypted row. The login check authenticates each nonempty password with
the Windows AES-GCM provider and validates the decrypted UTF-8. This also
protects profiles that have saved passwords and an empty cookie store.
The Windows encryption adapter, shared verifier, and entry point compiled
to x64 COFF. Native Windows must still exercise the new login rejection
probe and measure the cost of reading these stores at startup.

The offline importer can stage a DPAPI-only legacy profile pair for the
Windows host.

```powershell
python tools/profile_migration_trial.py import-workspace-profiles `
  "$trial" "$contentRoot" --layout windows-dpapi
```

It reads the snapshot-verified legacy `Local State` and writes only
`os_crypt.encrypted_key` into the staged `ContentWorkspaceLocalState.json`.
It checks the Base64 DPAPI envelope without unwrapping it. A legacy
App-Bound key or a `v20` cookie or login row aborts publication of both
profiles. Synthetic tests check key and ciphertext preservation and
rejection before publication. Native Windows must still verify reopening
with the original user's DPAPI key and the production profile format.

Run the portable DPAPI migration probe on native Windows with an unpacked
stable Chrome-backed Zitter build, the relocated Content executable, and
Node.js with WebSocket support. Use a disposable Windows account. The
verifier creates its own temporary browser roots and refuses a
`UserDataDir` policy that can override the legacy launch arguments.

```powershell
python tools/verify_content_windows_migration.py "path\to\zitter.exe" "path\to\zitter_content.exe"
```

The legacy browser seeds separate persistent HttpOnly cookies, localStorage
values, and saved logins for Work and Personal. The verifier imports both
profiles from the offline snapshot. It checks the original key envelope and
ciphertext, sees the original cookies on loopback requests, checks
localStorage in each renderer, and tests password autofill after restart.
In a second imported copy, the verifier removes the cookie stores and
tries a different DPAPI-wrapped key. Content must reject the login stores
before loading a tab and leave the key, login files, and saved session
untouched.
The verifier restores a byte-verified legacy snapshot and reopens both profiles in
the old browser to check the cookie, localStorage, and password-manager
entry. The browser fixture, two Content launches, and legacy reopen
passed with the mock Keychain on macOS. The native DPAPI run is pending.

At the pinned Chromium revision, App-Bound Encryption checks for a
system-level install and the default user-data directory before it can
encrypt new data. Its provider also needs the registered elevation service.
The Zitter installer offers per-user and system-wide installation, so a
portable executable launched with a disposable `--user-data-dir` can exercise
DPAPI but cannot establish what all shipped profiles contain. Sample both
installation modes in disposable Windows accounts. After closing Zitter,
prepare a trial from each offline copy and inspect it.

```powershell
python tools/profile_migration_trial.py prepare "$offlineCopy" "$trial"
python tools/profile_migration_trial.py inspect-windows-encryption "$trial"
```

The report shows the DPAPI envelope status, whether Local State contains
an App-Bound key, and counts for `v10`, `v20`, empty, and other ciphertext
in each profile's cookie and login stores. It reads verified scratch
copies of the databases and their sidecars. The report omits keys,
origins, account identifiers, and row contents. A synthetic test
confirmed that a `v20` login in a WAL is counted and a modified WAL is
rejected. If a shipped profile has App-Bound data, preserve the legacy
application's authenticated access to it and establish a verified
migration before cutover.

Windows build and runtime checks, including the visual review in both
themes, are pending. Cross-generation on the Mac stopped in Chromium's
Visual Studio toolchain setup because `WINDOWSSDKDIR` was unavailable. The
shared host and macOS entry point passed the macOS GN include checks and
Clang syntax checks. The DPAPI adapter also passed a syntax check against
pinned headers with the Mac compiler. The verifier's three rejection
paths passed with a stub process and synthetic ciphertext. The shared
cookie validator passed the macOS split migration, including its two
wrong-key trials, and the relocated non-component package checks. Native
Windows still needs to exercise the DPAPI runtime.

## Relocated Windows package

Use a dedicated Windows output for the non-component build. During
generation GN writes `zitter_content.runtime_deps` from the standalone
executable target. The package
verifier uses that list to select the executable, DLLs, and runtime
resources, then creates and extracts an unsigned ZIP in a temporary
directory. It checks x64 PE headers and regular and delayed imports.
It requires both the SwiftShader DLL and its ICD manifest and checks
that the manifest resolves to the adjacent packaged DLL. MSVC redistributable imports
must resolve in the package even if the test computer already has
those DLLs installed. The verifier starts the extracted app with a
system-only `PATH` and an unrelated working directory. It checks startup
sandbox overrides, renderer sandboxing, Workspace persistence and
private-window isolation. It also runs the fingerprint probe from the
extracted executable. That probe seeds a disposable DPAPI key
using the current Windows user and checks first reads across
renderers, the initial popup, live rotation, and persistence.
The malformed preference check runs on both platforms. The linked
preference fixture runs on macOS.

The extracted app also runs the private Workspace ad blocking probe
in both compositor themes and the download probe with quarantine
required. The latter needs a Windows temporary filesystem that supports
alternate data streams. Supply `--openssl` to additionally check the
untrusted-certificate rejection and download transport fixtures. The
transport probe uses loopback HTTP and TLS and checks quarantine for
accepted downloads.

```powershell
python tools/stage_content_embedder.py .chromium/src
New-Item -ItemType Directory -Force .chromium/src/out/ZitterContentWinBundle | Out-Null
Copy-Item chromium/windows-release.args.gn .chromium/src/out/ZitterContentWinBundle/args.gn
Set-Location .chromium/src
gn gen out/ZitterContentWinBundle --root-target=//zitter_content:zitter_content --fail-on-unused-args
gn check out/ZitterContentWinBundle //zitter_content:lab_app_win --root-target=//zitter_content:zitter_content
gn desc out/ZitterContentWinBundle //zitter_content:zitter_content deps --all --root-target=//zitter_content:zitter_content
autoninja -C out/ZitterContentWinBundle zitter_content
Set-Location ../..
python tools/verify_content_windows_bundle.py .chromium/src/out/ZitterContentWinBundle
```

Pass `--archive` with a new destination path to keep the verified ZIP.
Use `--runtime-deps` to supply a separately captured GN manifest.

### Verify an existing Windows ZIP

Pass the ZIP and the GN output that produced its runtime manifest. The
verifier opens the ZIP read-only. Before extraction it checks that the
archive contains the exact runtime filenames listed by the manifest,
under the `Zitter Content Lab/` directory. It accepts the known license
notices and rejects unexpected names, duplicates, and symlinks.

```powershell
python tools/verify_content_windows_bundle.py .chromium/src/out/ZitterContentWinBundle `
  --input-archive "path\to\Zitter-Content-Lab-win-x64.zip" `
  --signtool "path\to\signtool.exe"
```

With `--signtool`, every packaged EXE and DLL must pass Windows'
Authenticode verification policy for embedded signatures before the
browser starts. A warning exit code fails the check. Runtime tests use
the extracted files and the same restricted PATH as the generated
archive path. Signer identity pinning, native SignTool execution, and
installer verification remain release gates.
[Microsoft SignTool verification](https://learn.microsoft.com/windows/win32/seccrypto/using-signtool-to-verify-a-file-signature)

### Optional package probes

Pass `--legacy` to also run the portable DPAPI migration and rollback
probe against the executable extracted from that ZIP. Use a disposable
Windows account. The legacy executable and Node.js stay outside the
archive. The verifier resolves Node.js before restricting PATH, and
`--node` accepts an explicit executable path. The requested archive is
kept only after the migration probe succeeds.

```powershell
python tools/verify_content_windows_bundle.py .chromium/src/out/ZitterContentWinBundle `
  --legacy "path\to\portable\zitter.exe"
```

Pass `--openssl` to exercise the TLS, virtual WebAuthn, and HTTPS download
transport probes against the extracted executable. The package verifier's
Python tests cover dispatching these probes. The native Windows run
remains open.

To exercise signed extensions from the extracted executable, build the
host CRX signer in a separate GN output. OpenSSL must be available on the
invoking shell's PATH or supplied by absolute path with `--openssl`.

```powershell
Set-Location .chromium/src
gn gen out/ZitterContentSigner --root-target=//components/crx_file:crx3_build_action --args="is_debug=false is_component_build=false"
autoninja -C out/ZitterContentSigner crx3_build_action
Set-Location ../..
python tools/verify_content_windows_bundle.py .chromium/src/out/ZitterContentWinBundle `
  --crx-signer .chromium/src/out/ZitterContentSigner/crx3_build_action.exe
```

The verifier passes both signing tools by absolute path and keeps the
browser's system-only PATH. The tools generate disposable CRX fixtures
outside the ZIP. The extracted browser checks both profile owners,
sandboxed extraction, consent, module imports, failed worker activation,
and signed update and restore. The Settings fixture wraps a fresh key
with the current user's DPAPI before seeding approved extensions on disk.
Supplying the signer also enables the TLS and download transport probes.
The extension registry suite passed on macOS with a system-only PATH and
explicit paths to both signing tools. Native Windows execution remains
pending.

The Mac-side test created actual AMD64 PE fixtures with regular and
delay-loaded dependencies, packaged and extracted them, and verified
that removing their DLL caused both checks to fail. The unit tests cover
path selection, archive layout, signature command results, the app-local
CRT rule, and SwiftShader manifest containment. They also check that
the runtime probes use the extracted executable and restricted
environment, including when external signing or migration tools are
supplied.

The earlier macOS cross-checks compiled individual x64 COFF objects with
Windows SDK 10.0.26100.0 and MSVC 14.44 headers. This local SDK directory
contains only headers. The pinned Chromium setup uses SDK 10.0.28000.0.
A fresh non-component GN trial on macOS, with explicit Visual Studio
and SDK paths and the current `//zitter_content:zitter_content` root,
stopped in `win_toolchain_data.gni`. The explicit-path branch supplies
an empty list for `visual_studio_runtime_dirs`, which that GN file passes
to `exec_script` as a string argument.

A graph-only experiment temporarily replaced the Windows toolchain
discovery calls with placeholder metadata on macOS. GN generated the
x64 Content graph, passed the executable's include check, and found
15,848 dependency labels with zero under `//chrome`. The executable's
generated runtime manifest yielded eleven packaged paths, including ICU,
the external V8 snapshot, ANGLE, the SwiftShader DLL and ICD, and the
Vulkan loader. Chromium's toolchain file was restored after the check.
The graph used the cross-host snapshot configuration and did not compile
or link Windows code.

A later Windows x64 cross-toolchain trial on Apple Silicon used Visual Studio 2026 MSVC
14.50.35717, Windows SDK 10.0.28000.0 from Microsoft's 10.0.28000.2270
packages, and Microsoft's debugger redistributable. Chromium's pinned
Windows Clang runtime and the Windows-only dependencies from `DEPS` were
added to the isolated checkout. Local toolchain metadata pointed GN to
the installed files. Chromium's toolchain scripts were unchanged.
GN generation and the executable's include check passed. The full graph
contained 15,859 dependency labels, zero under `//chrome`. Ninja planned
50,470 actions for the non-component build, ending in the application
link. Ninja built the Windows system module and generated the build
flag headers needed by `download_file_actions_win.cc`. The compile
command from that graph produced an AMD64 COFF object for this source.
Chromium's `clang-cl` and `lld-link` also produced a standalone
x64 PE probe with a static C++ runtime and a `CryptProtectData` import
from `CRYPT32.dll`. The probe link excluded the legacy `oldnames.lib`
default library, which was absent from the assembled toolchain.

The application plan was a dry run and the SDK probe was inspected
without executing it. Native GN generation, application linking, packaged
runtime and sandbox checks, Authenticode signing, installer integration,
and updates remain pending.
[Chromium Windows cross-compilation setup](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/win_cross.md),
[Microsoft Windows SDK C++ package](https://www.nuget.org/packages/Microsoft.Windows.SDK.CPP/10.0.28000.2270),
[Microsoft debugger redistributable](https://www.nuget.org/packages/Microsoft.Debugging.Platform.DbgEng/20260319.1511.0)

## Navigation history

Each persistent Content profile lazily starts its own Chromium History
service and uses the standard `History` database in that profile. The
navigation observer records eligible committed HTTP and HTTPS visits in the
primary frame. It passes Chromium the transition, redirect chain, referrer
and response category, updates page titles, and ends the visit when the
document is replaced or the tab closes. Private contexts never attach the
history observer or open a history service.

The persistent Workspace verifier checks visit counts and titles after
each shutdown and restart, and requires the deferred tabs to remain absent
from history. The macOS profile verifier opens regular and private contexts
on the same loopback origin and requires the two durable databases to
contain only the visits made in each regular profile. Both verifiers passed
on macOS. With history linked, GN listed 12,678 transitive dependency labels
with zero `//chrome` targets.

The Content Workspace opens the shared Zitter history panel from the
History command. The panel queries the active profile, opens results in
new tabs, and uses Zitter's confirmation dialog for bulk deletion. Switching
profiles closes the old panel and cancels its pending queries. Private
profiles supply no history service. The panel hides actions whose host
services are still pending.

```sh
python3 tools/verify_content_workspace_session.py --history-panel \
  --theme light --renderer compositor \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The probe opens Work history, starts a query, switches to Personal, and
requires the new window to list only the Personal visit. It checks the
displayed row and its accessibility name as well as the inherited theme.
Both themes passed on macOS with compositor and Metal. The Metal check
also requires the visit title in the renderer scene.
Visited-link styling, production migration rollout, native Windows execution,
and end-to-end opening and deletion from the panel remain open. The Chrome
adapter was updated to supply the history
service and profile label. The Chrome shell include check in the isolated
checkout encountered Chrome-only dependency and staging errors, so the
Chrome host still needs its own build check.

### Back and forward across workspace switches

The header and shortcut navigate the active tab's Chromium controller.
Command Center offers Back and Forward when that controller supports
the requested direction. A submitted command checks the active workspace
and committed navigation entry before acting. The native macOS View menu
offers the same commands. It validates availability against the same
controller that the command will use, including a visible Peek tab.

```sh
python3 tools/verify_content_workspace_session.py --navigation \
  --theme dark --renderer metal 'path/to/Zitter Content Lab.app'
```

The relocated macOS bundle passed on compositor and Metal in both themes.
The probe builds separate two-page histories in Work and Personal and
switches between them several times. It checks the original WebContents,
the BrowserContext, both header buttons' enabled states, and the committed
URL after each header, shortcut, or palette action. The macOS run also
checks the enabled and disabled directions through AppKit and activates
both native menu commands. The server verifies profile cookies and the
saved session retains the expected tab layout.
The probe covers live tabs. After sleep or restart, Zitter follows the
legacy snapshot behavior and loads the last saved URL. Native Windows
execution remains open.

### Recently closed tabs

The Content host records the last committed URL and title when a tab
actually closes. It keeps up to 50 entries per workspace in memory and
excludes private contexts. The History panel's Recently Closed list
shows only the active workspace. The reopen shortcut, Command Center,
and native macOS File menu use the newest entry whose URL is not already
open. AppKit disables the command when the workspace has no eligible tab. Reopening adds a
regular tab in the same BrowserContext and saves the resulting workspace.
Clearing browsing history also removes recently closed entries from
the selected period in that profile.

Legacy Workspace snapshots, including sleeping workspaces, store each
tab's current URL and title. Import therefore restores those fields.
The browser's back and forward entries remain with the live WebContents.
All three entry points can fall back to the persisted Time Machine
timeline once the current run has no eligible closed tab.

```sh
python3 tools/verify_content_workspace_session.py --closed-tabs \
  --theme dark --renderer metal 'path/to/Zitter Content Lab.app'
```

The relocated macOS bundle passed in light and dark on compositor and
Metal. The probe reopened a Work tab from History, the shortcut action,
Command Center, and the native File menu. The local server observed
the Work cookie on every load. The Personal workspace showed no Work
entry, refused the shortcut, and disabled the menu item. After the final
close, the saved session contained the original open tabs and no closed-tab
metadata. GN checked 13,755 dependency labels with no `//chrome` entries.
Native Windows execution remains open.

### Time Machine storage and older closed tabs

Content uses the shared Rust timeline engine. The profile importer verifies
the legacy `ZitterTimeline.jsonl` against the snapshot and stages a separate
timeline next to the Content session. It remaps Work and Personal to their
Content profile IDs and skips frames and marks tied to earlier profile
assignments. It removes tab URLs that the Content host cannot restore and
remaps the selected index. The importer also stages the legacy Time Machine
enabled setting and retention period.

Content captures eligible navigation and both sides of a tab close. It
queues an atomic timeline rewrite on tab close and flushes pending frames
at orderly shutdown, using the same sequenced writer as the session.
Private contexts never record. Clearing browsing history removes timeline
frames in the selected profile and period. Reopen first checks this run's closed tabs
and then the active workspace's retained timeline, excluding already open
URLs in both cases.

```sh
python3 tools/verify_content_storage_migration.py --timeline --shared-keychain \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
```

The relocated macOS app passed the fixture with compositor and Metal. The
fixture imports different tabs closed 45 days earlier with 90-day retention,
reopens the older Work tab through Command Center, records a new Work
closure, and reopens it through the native macOS File menu after restart.
The restart checks the expected timeline entry and the absence of an
in-memory closed tab before using the menu. The loopback server checks
the Work cookie, and the verifier compares the legacy rollback against
the original snapshot. Native Windows execution remains open.

### Time Machine panel, snapshots, and layout undo

Content uses the shared Time Machine panel. Open it from the workspace
shortcut, Command Center, or History. The snapshot shortcut saves a mark
named Snapshot, and Command Center saves a mark named Command Center and
opens the panel. Both use the configured retention period.

Restoring a moment validates the selected workspace and profile, blocks
while one of its tabs is capturing, and checks its pages for beforeunload
handlers before replacing the live layout. The restored tabs use the active
profile's BrowserContext. Undo restores the previous tab order, selection,
open pin associations, and Split View. Like the legacy app, restore and
undo load tab URLs again from the saved layout.

```sh
python3 tools/verify_content_storage_migration.py --timeline-panel --shared-keychain \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
```

The relocated macOS app passed in light and dark with compositor and
Metal. The panel selected a migrated Work moment, restored its card,
undid to the original pinned split, and saved a Command Center mark.
The verifier checked that each run added one Work mark, Personal
retained its layout, and legacy rollback remained byte-identical. The
first trial found a stale card reference when reopening the panel. The
shared view now removes that reference before freeing the child.
GN still lists 13,590 dependencies with zero `//chrome` entries. The
Workspace host and timeline store cross-compiled to Windows x64 COFF.
The shared panel and Windows launcher still need the native toolchain
and generated headers. Native Windows runtime validation remains open.

### Clearing Time Machine

The workspace settings action clears Time Machine for both spaces. It
offers Undo after the atomic write succeeds. The advanced Clear Browsing
Data dialog can clear Time Machine for just the active profile and
selected time range. Its dedicated option preserves browsing visits,
cookies, and the open layout. Basic history clearing in Content also
clears Time Machine. A profile-scoped clear dismisses any pending global
Undo so it cannot bring back the erased frames or marks.

```sh
python3 tools/verify_content_storage_migration.py --timeline-clear --shared-keychain \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
```

The macOS fixture clears Work's recent frames and marks, preserves its
older entries and both Personal periods, then tests the global clear and
Undo across separate launches. It verifies the cleared timeline after
restart and checks legacy rollback. The shared Rust tests also cover
deleting a scoped mark after its frame has expired and the confirmation
copy for the four-week range.

### Command Center history suggestions

The Content Workspace feeds up to 12 history results from the active
profile to the existing address field and Command Center. It excludes
404 responses, non-web addresses, embedded credentials, and duplicates.
Private profiles and command-only input complete the search without a
history query. Changing profiles closes the palette and cancels pending
queries. The shell checks that the current input and profile still match
each result.

```sh
python3 tools/verify_content_workspace_session.py --address-history \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'

'./.chromium/src/out/ZitterContent/Zitter Content Lab.app/Contents/MacOS/Zitter Content Lab' \
  --zitter-content-workspace-smoke-test --zitter-content-address-history-smoke-test
```

The durable probe types the same title query in Work and Personal. It
checks the suggestion, starts another Work query before switching, and
requires the reopened palette to show only the Personal visit. The private
probe checks that the search completes with no history results. Both passed
on macOS in light and dark with compositor and Metal. Content integration
for tab, pin, and workspace suggestions, native Windows execution, and
selecting a result through the interface remain open.

### Clearing browsing data

The Content Workspace connects the shared clear-data dialog and the
Settings confirmation to the active durable profile. The dialog exposes
history, cookies and site storage, and cache. History removal uses
Chromium History, while site storage and cache use Content's
BrowsingDataRemover. The confirmation retains the identity of the profile
it names and becomes invalid if a different profile is active when the
user accepts it. The Workspace waits for the removal callbacks before
reporting completion.

```sh
python3 tools/verify_content_workspace_session.py --clear-data \
  --theme light --renderer compositor \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The probe seeds both profiles, cancels the Work dialog, then reopens and
accepts it. It checks that only Work's history and cookie are gone after
shutdown and that Work starts without its prior cookie or local storage
on restart. Personal retains both its browsing state and its history.
The dialog and restart probe passed on macOS in both themes with compositor
and Metal. The fixture uses uncached responses. A cache-specific runtime
test, form data, private-mode clearing UI, and native Windows verification
remain open.

The Advanced dialog clears the active profile's Clipboard and popup
exceptions and saved site zoom. It keeps the profile defaults. It uses
the timestamps retained from legacy Preferences and records a timestamp
when a rule changes in Content. Rules with no known timestamp are
eligible only for the all-time range. Clipboard permission subscriptions
are notified after a grant or block is removed.

```sh
python3 tools/verify_content_workspace_session.py --clear-site-settings \
  'path/to/Zitter Content Lab.app'
```

The probe uses the Advanced dialog to clear the last hour, then relaunches
and clears all time. It verifies the live rules and zoom map, the saved
preferences, and the unaffected Personal profile. The relocated macOS
app passed in both themes on compositor and Metal. Basic browsing data
removal and the Clipboard permission regression also passed.

The Advanced dialog also clears download history through Chromium's
`DATA_TYPE_DOWNLOADS` removal path. It removes entries in the selected
profile whose start times are in the selected period. Chromium skips
in-progress transfers, and the completed files remain on disk.

```sh
python3 tools/verify_content_downloads.py --clear-history --require-quarantine \
  'path/to/Zitter Content Lab.app'
```

The probe seeds completed transfers in both profiles, adds an older
Work entry, and clears Work's last hour and then all time using the
Advanced dialog. It checks the panel immediately, opens the history
again after restart, and verifies that Personal's records and all
completed files remain intact. The relocated macOS app passed with both
themes on compositor and Metal, including quarantine verification for
the original downloads. The relocated signed Content 0.1.4 DMG also
passed the period, profile, file, and quarantine checks in dark Metal.
The standard download panel and site settings removal regressions passed.

The Advanced dialog can remove saved passwords from the named profile.
It uses Chromium's profile password store and the credentials' creation
timestamps. Completion waits for the deletion and verifies that the
selected range is empty. The dialog uses the existing destructive-action
warning and defaults to Cancel. Passkeys remain a separate integration
gate.

```sh
python3 tools/verify_content_password_prompt.py --clear-passwords \
  'path/to/Zitter Content Lab.app'
```

The probe creates a real encrypted fixture login and prepares recent
and older entries in both profiles. It cancels once, clears Work's last
hour, clears all time, and repeats against the empty store. It verifies
both databases across launches and checks that Personal's ciphertext
stays unchanged. The relocated macOS app passed in both themes with
compositor and Metal. The password prompt, password manager, and site
settings clearing regressions also passed. Native Windows verification
remains open.

## Extension manifest and host permissions

The Content Lab has a manifest probe backed by Chromium's core extension
API provider, manifest handlers, and unpacked extension loader. The app
bundles the core extension resources and English strings. The probe reads
a disposable package before starting a browser profile and uses the
manifest's permissions to evaluate script access. It registers
`chrome-extension` as a standard URL scheme for the probe so Chromium
can resolve manifest resource paths. The lab client supplies
an empty unrestricted scripting allowlist and denies scripting on internal,
local-file, and Chrome Web Store pages.

```sh
python3 tools/verify_content_extension_manifest.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The positive fixture declares MV3 storage, a background service worker,
and a document-start content script. Its match patterns include loopback
and Chrome Web Store so the verifier can exercise the store restriction
even when the manifest requests access. The verifier also checks rejection
of a missing script, malformed JSON, a fixture that allows an unexpected
host, and a path outside the temporary fixture.

The manifest probe and the regular Workspace smoke passed on macOS.
GN checked the lab's includes and listed 12,688 transitive dependency
labels with zero `//chrome` targets. Native Windows execution is pending.

Runtime activation requires a profile-scoped ExtensionSystem and registry,
browser and renderer API providers, extension protocol and navigation
isolation, and worker and script dispatch. Installation, permission
prompts, update validation, private-mode policy, and representative
extension behavior still need coverage. The manifest probe exercises
validation and permission evaluation only.

### Supported signed manifests

Signed installation and restore use a capability policy separate from
the exact manifest probe. The host accepts MV3 extensions with a classic
service worker and optional static content scripts. The scripts must
run in isolated worlds and declare explicit HTTP or HTTPS hosts.
Manifest entries can opt into matching subframes with `all_frames`.
With `match_about_blank`, Chromium also evaluates eligible
`about:blank` and `about:srcdoc` documents against a related page's
full URL, including its path. The approval dialog discloses each option
for the script scope. `match_origin_as_fallback` uses Chromium's
initiator-origin matching for eligible documents such as `data:` frames.
Chromium requires an all-path match pattern in this mode and the
installer still requires explicit HTTP or HTTPS hosts. Approval discloses
that initiator paths are ignored. A signed update cannot add this mode
or switch to it from `match_about_blank`.
The supported API permissions are `alarms`,
`storage`, `unlimitedStorage`, and `tabs`. Required `host_permissions` can name an
named HTTP or HTTPS hostname, including a wildcard that covers a
registrable domain and its subdomains. Each grant applies to the
specified scheme on any port. Signed packages can declare the same
supported API permissions and host patterns as optional, subject to
Chromium's manifest rules. Content scripts can also name a domain and
its subdomains. The installer excludes wildcard public and private
registry suffixes and wildcard patterns that cover Chrome Web Store
hosts. The signed registry suite passed in light compositor and dark
Metal. A worker could read the domain and a subdomain after the user
approved the optional wildcard. The trial restored the grant through an
update and restart, then verified revocation after another restart. A
required exact host covered by a separate optional wildcard did not
appear in the saved consent record.

An optional `*://*/*` declaration is accepted and disclosed at install.
It lets the extension request a narrower HTTP or HTTPS host or named
subdomain pattern at runtime. Each requested pattern still needs the
existing user gesture and profile-scoped consent. The browser rejects
an all-sites request and filters restored grants through the same
host-pattern policy before it starts the extension. Developer ID trials
in light compositor and dark Metal exercised the rejected all-sites
request and the approved named subdomain request using only the broad
manifest declaration. The scoped grant and later revocation survived
browser restarts. GN header dependency checking passed.
The install review lists them as inactive and approval leaves them inactive.
The installer rejects unsupported optional APIs, broad host patterns,
main-world scripts, and explicit incognito modes. The browser still
isolates private contexts when the manifest omits its incognito field.

The install review lists explicit host grants, effective network read
origins, and content-script matches separately. It binds those disclosures
to the selected profile and signed package. Chromium computes network read
origins from both explicit grants and declarative script hosts, ignoring
the script path for network requests. It applies its extension CORS
blocklist and clears origin rules when an extension is disabled. The
extension worker inherits its secure creator policy and the embedder
classifies the extension scheme as loopback, matching Chromium's Chrome
worker setup. The shared extension URL loader factory setup applies only
to enabled extension origins in regular profiles. A signed update cannot
add host grants.

The embedder registers Chromium's read-only `chrome.permissions.getAll`
and `chrome.permissions.contains` handlers for privileged extension
contexts. Each reads the active grants of the calling extension in its
profile. `getAll` reports Chromium's effective host patterns, including
declarative script paths. The install review separately normalizes network
read access to origins. The host routes `chrome.permissions.request`
through Chromium's manifest parser and requires a user gesture. A new
grant requires confirmation in the foreground Workspace showing the
extension's profile. The confirmation checks the profile again, and the
API checks that the same extension is still enabled before applying the
grant with Chromium's PermissionsUpdater. `chrome.permissions.remove`
validates the request against the manifest and uses the same updater to
revoke optional access. The browser atomically saves the resulting
optional scope in the owning profile's extension installation before
applying either change. Managed startup verifies the package, checks the
saved signing key and bounds the restored scope to the current manifest
before loading the extension. Uninstall removes the record with the
extension directory. The profile-wide audit snapshot is informational.
The relocated macOS package passed the
permission query tests for Work in light compositor and Personal in dark
Metal. The worker reported its explicit and script grants, denied
ungranted permissions, and could not remove a required grant. Neither the page nor
its isolated content script could access the API. These assertions ran
after install, restart, and signed update. GN include checking passed
with 13,734 dependency labels and zero under `//chrome`.

The signed-package verifier installs an extension with an explicit host
grant and two declarative script scopes. A loopback server returns the
same response for three hostnames without a CORS grant. The worker must
read it on the explicitly granted host and the script host. The third
hostname is declared optional and reaches the same server, but the
browser denies the worker access to its response. The verifier checks that
the optional API and host are inactive, a request without a gesture is
denied, required permissions cannot be removed, and removing an inactive
optional permission succeeds without changing access.
It repeats the worker check during restart and signed update. It rejects
broad host patterns, unsupported optional APIs, Chrome Web Store host
grants, an unattended update adding a host, and an update that drops a
script glob without changing the aggregate host permissions. It also
checks document-start injection in the second script scope, including
a matching cross-origin iframe, and absence in a sibling iframe and
top-level page excluded by the glob. The frame fixture also checks that
the isolated world's extension API and globals are unavailable to page
scripts. A signed update that changes only a top-level scope to
`all_frames` is rejected. The managed multi-extension probe
exercises a package with just a manifest and a worker.

The Developer ID macOS candidate passed the signed registry suite in light
compositor and dark Metal with the optional `alarms` API and loopback host
declared. The JSON review disclosed both declarations. After install,
restart and signed update, the worker reported neither as active and
could not read the optional host response. The suite also rejected an
optional wildcard registry suffix and an unsupported optional API. The English
translation source compiled separately. This relink reused the previous
Rust library, so the signed run did not exercise the new English strings.

After wiring runtime requests and removal, the Developer ID registry
suite passed again in light compositor and dark Metal. It exercised the
no-gesture rejection, rejection of required-permission removal and no-op
removal of an inactive optional grant. GN header dependency checking
passed. A subsequent Developer ID run passed in light compositor and
dark Metal. The browser delivered Chromium's test user activation to
the signed content script, which sent a message to the worker. The test
dismissed the first Workspace dialog and accepted the second. The worker
then used the Alarms API and read the optional loopback host response.
After calling `permissions.remove`, it could no longer read that response.
The Developer ID persistence trial passed in light compositor and dark
Metal. The verifier grants the same optional scope again and checks its
signing key and owning profile on disk. After a same-key
signed update and restart, the worker uses the restored API and reads the
optional host response. It removes the scope and restarts once more to
check that neither permission returns and the host response is blocked.

```sh
python3 tools/verify_content_workspace_session.py --extension-registry \
  --crx-signer /path/to/crx3_build_action '/path/to/Zitter Content Lab.app'
```

The relocated macOS bundle passed the full signed registry suite for
Work and Personal, using light and dark themes on both compositor and
Metal. The suite exercised the visible Settings approval dialog and
checked its color mode and default Cancel action. The non-component
target passed GN include checking. Production publisher policy, broader
extension API coverage, the production installer, and native Windows
verification remain open.

The frame scope checks passed in the relocated macOS arm64 bundle with
Work in light compositor and Personal in dark Metal. They ran during
initial install, restart and signed update. The `match_about_blank`
fixture verified document-start injection into `about:srcdoc` under the
matching page. It also verified the script's effect in an explicit
`about:blank` iframe at the parent page's load event. Neither frame
received the script under a same-origin page excluded by its path. The
test checked isolation from page JavaScript and rejected an update whose
only scope change was enabling `match_about_blank`. The origin fallback
fixture verified document-start injection in `data:` and `blob:` iframes
created by an approved initiator and absence in the same documents under
an unapproved host. The fixtures checked isolated-world behavior on Work
with light compositor and Personal with dark Metal. The installer rejected a wildcard host and a signed
update that changed a scope from `match_about_blank` to
`match_origin_as_fallback`. A separate `filesystem:` trial hit
Chromium's default navigation block from both hosts before any script
could run. The standalone app leaves that policy in place.
GN include checking passed with 13,755 dependency labels and zero
under `//chrome`.

### Profile extension registry

The isolated Workspace probe links Chromium's browser extension module and
uses its keyed ExtensionRegistry factory. It opens the two durable fixture
profiles and a separate private context. The browser client maps each
durable context to itself and supplies no extension registry for private
contexts. The validated MV3 package enters Work's disabled set, is absent
from Personal, and can be removed from Work. The enabled and ready sets
remain empty during the disabled-registration check. Context shutdown
releases the keyed services.

See [Signed extension fixture install and restore](#signed-extension-fixture-install-and-restore) for the command and signing prerequisites.

The probe then enables the fixture in Work and navigates its primary
frame to the package's `probe.html` using Chromium's extension navigation
loader. The host throttle permits only that browser-initiated navigation
while the fixture is enabled. It denies extension redirects and
renderer-initiated navigations. The test verifies the extension origin,
a dedicated renderer distinct from the ordinary Work page, and enforcement
of the MV3 inline script policy. It requires a blocked navigation in
Personal, in the private context, and in Work after removing the fixture.
The scheme and loader are available only during the isolated probe.

The Work extension document also loads a packaged external script using
Chromium's extension subresource loader. The browser registers the
dedicated process in Chromium's ProcessMap and supplies the loader only
to the primary frame with the fixture extension's trusted origin. The
process map entries are removed when the fixture is disabled, the process
is destroyed, or the context shuts down. The script verifies that the
inline script was blocked by CSP. The ordinary Work document attempts to
load the same script and must receive an error. Both Workspace launches
passed these checks on macOS.

### MV3 background service worker

The Work fixture uses Chromium's dedicated extension partition under
the temporary Work profile. The browser's site routing and the worker
lookup both use Chromium's extension-ID partition resolver. The
association lasts until the fixture profile shuts down so the same
partition remains available for cleanup after disabling the package.
Content captures the worker script loader when the partition is created,
so the probe enables the fixture before opening that partition. The
regular Work page remains in Work's default partition. Chromium's
ServiceWorkerTaskQueue registers the manifest's background script via
Content's ServiceWorkerContext and the extension script loader. Work
provides in-memory extension preferences. RendererStartupHelper sends
the activation to the renderer, which reports the worker lifecycle via
ServiceWorkerHost. The browser exposes these worker interfaces only
for the enabled fixture, its first-party storage key, the declared
script, and the extension root scope.

The same Workspace command starts with an empty registration, waits for
the packaged worker to reply to the extension page over a MessageChannel,
and checks the worker's partition and process map entry. It then stops
the worker, waits until Chromium reports no running worker in the
partition, and sends a synthetic `runtime.onStartup` through Chromium's
EventRouter. The test requires a message from the worker's JavaScript
listener and confirms that the worker is running again. Deactivating the
fixture through the queue removes the worker registration. The probe
checks that the permission gate rejects a different script, a different
scope, Personal, and the private context.

The worker renderer startup channel remains available for the disposable
Work context until shutdown. Package removal revokes the activation and
process mapping and the navigation gate rejects subsequent requests.
The full probe passed both macOS launches with checks for renderer fatal
errors. The standalone manifest and ordinary Workspace with DevTools
also passed on the same build.

### Chromium extension renderer dependency

The Content target builds Chromium's extension renderer and packages its
generated JavaScript, static renderer resources, and Mojo JavaScript.
In the pinned Chromium revision, the renderer's GN target declares
`//chrome:resources` even though the renderer code builds with the resource
targets already supplied by `//extensions`, `//content`, and Mojo. That
edge introduced seven `//chrome` labels. The Content-only staging script
removes it after checking the pinned revision and the expected dependency
line. GN's existing `assert_no_deps` guards the full Content app target.
The isolated macOS build and header checks passed with 13,554 transitive
labels and zero `//chrome` labels.

### Declarative content scripts

The Work extension system publishes the fixture's load and unload through
Chromium's registry. Chromium's UserScriptManager loads the manifest
script and sends it through RendererStartupHelper to Work renderers. The
renderer uses Chromium's frame helper, URL permission checks, and
extension isolated world. Zitter's document-start adblock hook runs
before the extension dispatcher. The embedder checks that the frame and
document survived that hook before continuing.

The Workspace fixture allows a top-level `document_start` script on one
loopback path. Its first inline page script sees the extension's DOM
marker and cannot see its isolated JavaScript global. The same test
requires the marker to be absent at the unmatched path, in Personal and
private contexts visiting the allowed URL, and in the original Work
renderer after disabling the extension. The manifest test also rejects
a broadened URL pattern and main-world execution. The matching Chrome
Web Store pattern remains subject to the manifest probe's scripting
denial check. Both macOS Workspace launches passed, as did the manifest,
adblock scriptlet, network filtering, cosmetic filtering, and ordinary
Workspace with DevTools checks. At that stage the build graph contained
13,554 dependency labels and no `//chrome` targets.

### Extension local storage

The isolated browser registers Chromium's storage API functions and uses
the core ExtensionFunctionDispatcher and QuotaService. Work's
ExtensionSystem supplies Chromium's ValueStoreFactoryImpl, rooted at
the temporary profile. The storage frontend writes a per-extension
LevelDB under `Local Extension Settings`. The worker still has to pass
the existing scope, origin, process map, and extension permission checks.

In the Workspace fixture, the woken worker reads a counter using
`chrome.storage.local.get`, increments it using `set`, and reads back the
write. The verifier requires values one and two in consecutive macOS
launches with the same fixture ID. It checks for one LevelDB in Work and
no extension store in Personal or the private context. The loopback
page also checks that its main JavaScript world has no `chrome.storage`
binding. The test uses temporary profiles.

### Signed extension fixture install and restore

Build Chromium's local CRX signing helper alongside the app before
running the Workspace extension verifier. The verifier also requires
OpenSSL on PATH to generate a disposable RSA key.

```sh
(cd .chromium/src && third_party/ninja/ninja -C out/ZitterContent zitter_content crx3_build_action)
python3 tools/verify_content_workspace_session.py --extension-registry \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The probe receives a signed CRX3 and an independently supplied SHA-256
developer-key pin. While holding the validated temporary Workspace root
lock, it uses Chromium's CRX verifier before opening the ZIP. The
browser checks that the pinned proof identifies the extension, then
sends the verified archive bytes over Mojo to a utility process with
Chromium's `kService` sandbox. The utility gets no profile or file
handles. It accepts nested resources with portable UTF-8 paths, subject
to a 32 MiB CRX limit, 2,048 entries, 16 MiB per file, and 96 MiB of
total extracted data. A path may have up to 512 UTF-8 bytes and 16
components. It ignores directory metadata when materializing
the package. The browser rechecks returned paths and sizes and creates
files exclusively, so filesystem aliases cannot overwrite a file.
It validates the manifest key and ID against the signed identity. Chromium's install
helper stages the package in Work's `Extensions/<id>/<version>_0` layout.
The same path is available in Personal with `--extension-profile personal`
on the verifier. One profile owns the installed fixture for each run.
The host publishes the complete installation by renaming it within the
profile. A second install cannot replace it. An explicit update request
must use the same signed ID, a strictly newer version, and no additional
required or optional permissions.

The verifier removes the source directory and the disposable signing
key before restarting. The host revalidates the retained CRX3 against
the supplied pin and compares the installed files with its authenticated
payload before loading the package. The test checks the unchanged
extension ID and the next value of the `chrome.storage.local` counter.
It also tries an altered CRX3, a wrong pin, a signed archive with a
traversal entry, an oversized file, and a signed archive with a keyless
manifest. Rejected
packages leave no published installation. Mutating either the installed
worker script, a nested resource, or the retained archive prevents
restore before tabs load. Restore recursively checks the installed tree
against the authenticated files and their parent directories. On macOS,
it rejects a named pipe in place of a regular file and a symlink to a
directory containing the authentic files. The Windows installer code
rejects reparse points and uses Chromium's Windows file enumerator,
which reports them without following them during recursive enumeration.
Runtime verification of Windows reparse-point rejection remains open.

The relocated macOS bundle passed the four-launch install, interrupted
update, and restore sequence separately with Work and Personal as the
owner. Each run verified that the other profile and the private context
could not open the extension or acquire its storage. Changing the owner
for a restore request could not load the installation from the first
profile. The macOS non-component GN graph has 13,720 dependency labels
and zero `//chrome` labels.

### Concurrent extension contexts

The Content browser client resolves the installed fixture, registry, and
preferences from each browser context. The same signed extension ID can
run in Work and Personal with independent versions, extension processes,
service workers, and local storage. Private contexts have no extension
registry or storage for either installation.

```sh
python3 tools/verify_content_workspace_session.py --extension-pair \
  --crx-signer /path/to/crx3_build_action '/path/to/Zitter Content Lab.app'
```

The paired probe signs Work version 1.0 and Personal version 2.0 with
one key. It waits for both workers to handle startup and confirms both
are still running in their owning browser contexts before proceeding.
The host then disables the selected profile's installation. It retains
the package and data in that profile, moves the extension to the disabled
registry set, unloads renderer scripts, unregisters the worker, and
removes the extension from the profile's process map. The verifier checks
that the disabled profile can no longer open the extension page or inject
its script. It opens a fresh extension page in the other profile and
requires a reply from the same worker instance that was running before
the disable. The paired probe passed with Work and Personal disabled
first in separate runs. Use `--extension-profile personal` to disable
Personal first.

The expected Work counter advances from 1 to 3 and the Personal counter
from 41 to 43 across two restarts without the source packages. The probe
also checks document-start injection and private isolation for both
installations. Each profile has one signed installation in this trial.

#### Extension alarms

The Content host uses Chromium's `AlarmManager` and `StateStore`. It
initializes the manager before loading an extension that declares
`alarms`, so the manager can restore the profile's `Extension State`
database. Normal Workspace shutdown waits for queued extension file tasks
alongside profile cookie flushing. The paired probe creates persistent
and session-only alarms, receives an elapsed alarm event in each worker,
and checks named removal. On the first restart each worker sees only its
own persistent alarm. It clears that alarm and checks that the store
stays empty after another restart. The probe also verifies that private
contexts did not acquire an extension state database.

The relocated macOS package passed for Work in light compositor and
Personal in dark Metal. GN include checking passed with 13,734 dependency
labels and zero under `//chrome`. The updated manifest, registry, and
Windows entry point compiled to x64 COFF. The Windows package verifier
includes the paired probe when a CRX signer is supplied. This probe
covers alarms created by the Content app. Native Windows verification
remains open.

#### Importing extension alarms

The offline Workspace importer uses the verified legacy permission record
to select the extension IDs with an active `alarms` grant. It copies
the legacy profile's `Extension State` database to a disposable scratch
directory, then the helper reads only the selected `<id>.alarms` keys.
It validates the stored list and writes a fresh database in the staged
profile. The original and candidate snapshots remain byte-identical. A
missing helper or a changed source database aborts publication of both
profiles. An extension with an active legacy `alarms` grant can be
selected when the verified snapshot has no `Local Extension Settings`
directory for it. The importer writes its permission record under
`ContentLegacyExtensions/<id>.json` and leaves local storage absent.
The directory lets the signed installer reject a missing record even
before the extension has created its first local database. When the
snapshot records an existing store, it must still validate and copy
successfully.

Build the local helper against the pinned Chromium source and pass its path
to `import-workspace-profiles --alarm-importer`. The following reuses the
isolated Content bundle's GN output and restores the bundle root afterward.

```sh
cd .chromium/src
buildtools/mac/gn gen out/ZitterContentBundle \
  --root-target=//zitter_content:import_extension_alarms
third_party/ninja/ninja -C out/ZitterContentBundle import_extension_alarms
buildtools/mac/gn gen out/ZitterContentBundle \
  --root-target=//zitter_content:zitter_content
```

```sh
python3 tools/verify_content_extension_migration.py \
  --crx-signer .chromium/src/out/ZitterContent/crx3_build_action \
  --alarm-importer .chromium/src/out/ZitterContentBundle/import_extension_alarms \
  /Applications/Zitter.app '/path/to/Zitter Content Lab.app'
```

The verifier seeds a real Chromium alarm in a disposable legacy profile
and checks the exact name and time in the signed Content worker. It also
checks that the other profile has no imported alarm database and that
rollback recreates the byte-identical legacy snapshot. The rollback
reopen uses a command-line unpacked extension, which Chromium registers
again and may clear its registered state. The reopened legacy application
checks local storage. Reopening a production-signed legacy extension with
its alarm intact and running the migration on native Windows remain open.
The macOS trial passed for Work and Personal. In both runs the missing
helper and altered `CURRENT` cases left the destination unpublished.
The helper passed GN include checking and compiled to Windows x64 COFF.

Pass `--without-local-storage` to seed a legacy alarm without touching
`chrome.storage.local`. This variant checks the missing-record rejection
before installation, the exact alarm in the signed worker, and preservation
of the legacy disabled state. The macOS trial passed with Work and
Personal. The standard fixture with a pre-existing local store passed
again in Work, and all 48 offline profile migration tests passed.

### Persisted extension enablement

The signed fixture stores the user's enablement choice atomically beside
the active-version record in the owning profile's extension ID directory.
The choice applies across package versions. An absent record keeps the
existing default, and an invalid or linked record stops fixture startup.
An imported disable reason continues to block activation even when the
local record requests enablement. The legacy permission record and its
digest remain independent of the local choice.

The paired verifier also runs a disable, restart, and re-enable sequence
for the selected profile. It checks that the disabled extension cannot
navigate or inject a content script, its storage remains byte-identical,
and the other profile can still run the same ID. It reopens the enabled
extension and verifies its saved counter. It then terminates the browser
after the disable write and before runtime unload. The next launch keeps
the extension disabled. Re-enabling removes the stale service worker
registration before navigation and retains the extension's data.

The relocated macOS bundle passed for Work and Personal. The changed
extension code compiled to Windows x64 COFF.

### Extension enablement in Settings

The signed fixture uses the shared Settings toggle in Passwords and
profiles. The active profile supplies the value and receives changes
through the ordinary preference callback. The toggle is unavailable in
a profile without the installation, and the private window has no
extension settings row. The label and description use the shared Polish
and English translations.

The paired verifier opens Settings in both profiles, locates the row
through Settings search, and uses its accessible default action to
change the owning profile's choice. It checks the row state and the
panel's color mode. The regular disable, re-enable, and interrupted
disable tests all use this control. The fixture enables editing at the
points where it can verify the transition.

The relocated macOS bundle passed on compositor and Metal in light and
dark.

### Interactive extension management

The disposable Workspace can keep its signed extensions active after
startup. Each profile has its own controller for reading and changing
enablement. While a change is pending, its Settings toggle waits for
activation or worker removal. A new activation clears any stale worker
registration before the browser allows the extension to run. Closing
the Workspace uses the ordinary session save path.

```sh
python3 tools/verify_content_workspace_session.py --extension-manage \
  --interactive-extension-management --crx-signer /path/to/crx3_build_action \
  '/path/to/Zitter Content Lab.app'
```

Open Settings and search for **Zitter extension fixture**. Work and Personal
each have a signed package for the same extension ID. Close the window
to end the trial. The verifier creates disposable profiles and removes
them when it exits.

Omit `--interactive-extension-management` to run the automated
management check. It disables and re-enables the selected profile's
extension in one browser process and checks the surviving peer worker
by token. It closes with the selected profile disabled, removes the
source archives, and verifies that the next launch can re-enable that
installation through Settings. Use `--extension-profile personal` to
exercise Personal first.

The management check passed on macOS with compositor and Metal in light
and dark. The modified Workspace and extension modules compiled to
Windows x64 COFF. The macOS GN graph still has zero `//chrome` targets.

### Multiple extensions in one profile

The signed fixture installer can publish a second extension ID into the
same profile. Each package has its own key pin, active-version record,
enablement choice, and local storage. The browser uses the profile and
extension ID together for navigation, service-worker checks, storage
partitions, and process registration. Disabling one installation removes
its process registrations and leaves the sibling active. Settings shows
one named toggle per configured extension ID.

```sh
python3 tools/verify_content_workspace_session.py --extension-multi \
  --crx-signer /path/to/crx3_build_action '/path/to/Zitter Content Lab.app'
```

The verifier rejects a corrupt second archive, retains the first
installation, and retries the second. It checks two different worker
tokens and two local stores in the selected profile. It disables and
re-enables one extension through Settings while the sibling keeps the
same worker. The next launch restores both installed versions without
the source archives and preserves the selected extension's disabled
state. Use `--extension-profile personal` to exercise Personal, or
`--interactive-extension-management` to keep the disposable trial open.

The relocated macOS bundle passed with both profiles across compositor
and Metal in light and dark. The Workspace and extension modules
compiled to Windows x64 COFF. The macOS GN graph has zero `//chrome`
dependencies.

General installation and permission management, and native Windows
execution remain open.

### Restore approved extensions at startup

Ordinary macOS startup now enables the same approved-package restore
path and exposes the existing Settings install action. The host sets
the internal extension mode after accepting an ordinary launch and
before Content registers its URL schemes.

```sh
python3 tools/verify_content_ordinary_extensions.py \
  --crx-signer /path/to/crx3_build_action \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

The arm64 verifier used an outer-only ad hoc signature and a private
file Keychain to exercise argument-free launches of the GN bundle. It
restored a signed CRX approved in Work, confirmed content-script
messaging and worker storage across restart, observed no extension
storage in Personal at the same origin, and checked a saved disabled
state. A second approved CRX with a damaged archive was skipped on
ordinary startup, and the verifier checked that its archive was unchanged.
The approved fixtures were seeded offline. Settings install
and signed package verification have separate coverage.

The final 0.1.4 DMG passed signed extension installation, review,
restoration and profile-scoped management with APFS profile roots.
The ordinary-start verifier also restored Work's approved package,
preserved its disabled state and skipped a damaged package. That
ordinary-start run used the file Keychain and resealed the outer app.

The same signed registry test exposed an HFS+ restore failure. HFS+
enumerated an accented resource in decomposed form, while the signed
CRX stored the composed spelling. The restore validator compares macOS
tree paths in Unicode NFC and uses the actual filesystem paths for
canonical-path, file-type and byte checks. It rejects normalization
collisions. The signed registry suite passed on HFS+ and APFS with
the fix. The fix postdates the notarized 0.1.4 DMG.

Settings displays a longer permission review in its existing scrollable
confirmation dialog. It keeps the complete grant list and bounds the
rendered review at 16,384 UTF-16 code units. The signed registry fixture
added 36 distinct host grants and passed the cancellation, approval, and
restart checks on APFS and HFS+.

Settings can remove a signed extension from the active profile. The
confirmation names the profile and defaults to Cancel. The host disables
the extension and waits for its worker registration to disappear.
It terminates any remaining renderers in that profile's isolated
extension partition and waits for them to exit before clearing the
partition and local extension store. It then retires the
disabled installation directory. The registry then removes the extension
and notifies profile services. If retirement fails, the package remains
disabled.

The signed macOS management test removed Work's enabled installation
through the Settings button, checked the default Cancel action, and
verified that an open extension document lost its renderer and that
the package and local store were removed. Personal kept its
installation, local store, and running worker for the same extension ID.
The test passed in light mode with the compositor and dark mode with
Metal. This change postdates the notarized 0.1.4 DMG. Native Windows
runtime remains unverified.

The managed verifier repeats the removal with each profile as the
owner. After restarting, it reinstalls the same signed archive through
the fixture installer and checks that the selected profile recreates
its extension store and the peer keeps its own. Work passed with the
light compositor and Personal passed with dark Metal.

The Settings CRX picker also accepts updates to an installed extension.
The selected package must be newer, use the same trusted signing key,
and fit the host's supported manifest policy. The confirmation shows
both versions, the candidate's grants and script timing, and a warning
if its scope differs beyond the unattended update policy. On
approval, the host rechecks the active version and trusted key, stages
the verified files under the profile, then atomically switches the
active-version record. The running extension keeps its loaded version
until restart. The extension ID, user enablement and data directory
remain attached to the same profile. An installation interrupted before
the pointer write can be retried with the same signed package.

The Developer ID signed registry verifier exercised Settings cancellation,
approval from 1.0 to 2.0, the exact saved archive, restart and retained
extension data on temporary HFS+ profile roots. It also checked that the
peer profile did not receive the package or data. Both light compositor
and dark Metal passed. The build reused the existing Rust archive, so
the changed English strings were checked separately by the Chromium
Rust compiler. The update implementation postdates the 0.1.4 DMG.

The Settings path can approve expanded access for a Content-installed
package. Cancel preserves the active version. The offline update
fixture keeps the installed permission envelope, including script
timing. Legacy imports keep their original permission attestation.
Optional permission requests made by an already-running extension
remain unsupported.

The signed verifier selected a newer CRX with a new HTTPS host grant
and the same signing key. Settings disclosed expanded access. Cancel
left 1.0 active and published no files for 2.0. After approval and
restart, the 2.0 worker reported the new grant through
`chrome.permissions.contains` and retained its earlier profile data.
The peer profile received no package or data. The unattended update
tests still rejected expanded packages. Light compositor and dark
Metal passed. The new translations compiled separately from the
incremental link's existing Rust archive.

Before switching the active version, the host records the previous
version alongside the new version directory. The managed host dispatches
`runtime.onInstalled` with reason `update` and `previousVersion` after
the new worker becomes ready. A disabled extension retains the marker
until activation. The host removes the marker after dispatch, so a normal
restart does not dispatch the event again. The marker tracks dispatch and
does not acknowledge completion of the extension's JavaScript handler.
The verifier restarts the managed host, checks that the marker was cleared,
and reads the version pair written by the updated worker in its existing
profile store. The Developer ID run passed with both the light compositor
and dark Metal. The verifier stopped the disposable browser after reading
the worker's write. One local HTTP response was interrupted during teardown.

Managed extensions keep per-profile permission snapshots in
`Content Extension Preferences`. The host writes the permission
snapshot of each verified package atomically at normal profile
shutdown. An unreadable or invalid file is left in place. Chromium's
extension preferences remain in memory and the verified manifest
supplies the effective permissions at activation. The snapshot of an
uninstalled extension is removed at the next clean profile shutdown.

A trial that persisted the whole Chromium extension preference store
retained stale worker registration metadata after the embedder removed
the worker registration. This broke restart. The narrower snapshot
leaves worker and listener metadata in memory and the existing worker
startup flow still re-registers each worker. This is storage groundwork
for runtime optional grants. The approval and revocation path for those
grants still needs to be implemented.

The Developer ID light compositor registry trial passed and verified
that the Settings installation produced a snapshot only in the owning
profile while the peer kept its separate snapshot. Earlier management
runs stopped at the initial Back/Forward Cache checkpoint. A diagnostic
run also recorded `ENOSPC` on the APFS system volume. After removing
expanded copies of older release rehearsals and keeping their verified
ZIP and DMG archives, the unchanged signed code passed the full
management run with a temporary HFS+ profile root in light compositor.
The verifier exercised Back/Forward Cache eviction, uninstall and
reinstall. It confirmed that clean shutdown removed the owner's
permission snapshot and retained the peer's.

The signed installer writes the verified SHA-256 signing-key fingerprint
inside the extension ID directory before publishing the installation. An
explicit verification with the original key can backfill this record for
an older installation. Startup uses the validated Workspace profile
catalogue to enumerate approved installations, verifies each retained
archive and extracted tree, and
restores its saved enablement and legacy permissions. Directories without
an approval record are skipped. Ordinary macOS startup also skips invalid
installations and continues restoring verified packages. The explicit lab
restore fixture still stops before opening pages on a malformed record or
a changed approved package.

Launch a disposable Workspace root prepared by the signed installer.
The restore path takes its approval records from the profile.

```sh
'/path/to/Zitter Content Lab.app/Contents/MacOS/Zitter Content Lab' \
  --zitter-content-workspace --zitter-content-workspace-root="$root" \
  --zitter-content-restore-extensions
```

Add `--restore-installed-extensions` to the automated `--extension-manage`
or `--extension-multi` verifier. The second launch omits the package
paths, signing-key arguments, and the Workspace loopback probe switch.
It checks that a changed fingerprint, linked record, or corrupted archive
prevents startup, that a package directory without approval is skipped,
and that the original approved installations still restore.

The macOS bundle passed with the same ID in Work and Personal and with
two different IDs in one profile. Both trials checked Work and Personal.
The changed browser modules compiled to Windows x64 COFF. The macOS
graph still contains zero `//chrome` dependencies.

### Restore in an additional profile

Extension lookup uses the profile directory from the catalogue, including
on macOS when the profile's network data lives in a separate directory.
Settings resolves its switches through the active workspace's profile
assignment.

```sh
python3 tools/verify_content_workspace_session.py --extension-restore-extra-profile \
  --crx-signer /path/to/crx3_build_action '/path/to/Zitter Content Lab.app'
```

The verifier places an approved installation and its storage in a third
disposable profile while the browser is closed. It opens that profile's
workspace and toggles the extension there. It checks that Work and
Personal cannot toggle it, the sibling extension keeps its worker, and
a profile-like directory absent from the catalogue is skipped even when
it contains a malformed trust record. The verification uses the profile
ID and a workspace with a different name.

The macOS trial passed with compositor in light and Metal in dark.
The changed Workspace and extension modules compiled to Windows x64
COFF. The macOS GN graph still has zero `//chrome` dependencies.

The approval record belongs to the local Workspace profile. Production
trust and consent UI and native Windows execution remain open.

### Review a signed installation

The loopback lab can review a first install using an explicitly pinned
CRX3 signing key. Add `--zitter-content-extension-install-review=preview`
to a signed registry-probe launch with a disposable Workspace root and a
package path. The lab validates the archive and supported manifest,
prints the review as JSON, and exits without opening Workspace tabs or
publishing extension files. The review lists the package name, version,
ID, signing-key and archive fingerprints, target profile directory, API
grants, and declared script matches and exclusions. The host still applies
its own restrictions on script execution.

Replace `preview` with the JSON `approval` value to install. The host
verifies the candidate again and matches the review to the exact signed
bytes and profile before it writes the activation and trust records.
The review value binds the two launches. The lab computes it from public
package and profile details, so possession of the value alone is not
evidence of a human decision.

```sh
python3 tools/verify_content_workspace_session.py --extension-registry \
  --crx-signer /path/to/crx3_build_action '/path/to/Zitter Content Lab.app'
```

The verifier checks the reviewed grants, rejects a corrupt archive, and
tries the original approval against a different signed archive with the
same key and version and against the other profile. It checks that no
extension files were published and no tabs loaded, then installs the
reviewed bytes and exercises startup, storage, and signed update.
The relocated macOS bundle passed with Work on light compositor and
Personal on dark Metal. The managed restore regression also passed.
The review path is opt-in, uses the lab's disposable file-key profiles
on macOS, and leaves existing fixture workflows available. A production
Settings entry point, trusted source policy, and native Windows
execution remain open.

### Install a local CRX from Workspace Settings

A durable Content Lab Workspace launched with
`--zitter-content-restore-extensions` offers **Install extension** under
**Passwords and profiles**. The file chooser targets the active profile.
The browser verifies the selected CRX3 on a worker sequence, extracts it
in the sandboxed utility, and validates the supported manifest. The
review names the profile and extension, identifies the signing key and
lists the requested API and page-script grants. A valid CRX3 signature
establishes the package's signing key. The user's confirmation is the
source of trust for this local installation.

The browser retains the reviewed bytes in memory. Cancel discards them.
Accept publishes the approved version and key together in the chosen
profile. Publication refuses an existing ID, an orphan store with the
same ID, or a profile with legacy extension migration records.
After publication the browser registers the extension in the running
profile, starts its worker, dispatches `runtime.onInstalled` with reason
`install`, and adds its enablement switch to Settings. Existing tabs
receive the extension configuration for subsequent matching navigations.
Private windows omit the install action.

The registry verifier drives the Settings action, supplies the selected
file in place of the operating system picker, and exercises cancel and
accept on the actual consent dialog. It checks that cancel leaves no
extension directory and verifies the installed bytes and recorded key.
Before relaunch it checks the active worker, the enabled Settings switch,
profile isolation, the installation event written by the worker, and
document-start script injection on a new navigation. A separate launch
checks worker startup from the saved installation. The relocated macOS
bundle passed with Work and Personal in both themes across compositor
and Metal. The managed restore regression passed. The changed Content
modules also compiled to Windows x64 COFF objects.

The OS picker requires native interaction testing. Production source and
publisher policy, broader permission and API support, and native Windows
execution remain open.

#### Module service workers

Signed Manifest V3 extensions may declare a classic or module background
service worker. The standalone runtime uses Chromium's service worker
queue, which selects the script type from the manifest. The package
signature, sandboxed extraction, profile restrictions, and permission
review apply to both worker types.

Chromium requests the worker's imported scripts without a frame routing
ID. Content supplies the extension resource loader only when the
initiator matches an enabled extension in the worker's profile and
Chromium's process map assigns that process to the extension. The loader
checks the resource against that process.

```sh
python3 tools/verify_content_workspace_session.py --extension-registry \
  --extension-module-worker --crx-signer /path/to/crx3_build_action \
  '/path/to/Zitter Content Lab.app'
```

The module fixture imports an authenticated nested resource and checks that
the imported script ran before registering its listeners. It then runs the
same consent, profile isolation, worker startup, messaging, update, and
restore checks as the classic fixture. The relocated macOS bundle passed
for installations in both Work and Personal. The earlier macOS bundle
passed the classic fixture and rejected the module manifest. An
intermediate build that accepted the manifest without providing the
worker resource loader failed at script registration.

Managed activation has a 15-second deadline because Chromium can leave
worker tasks queued after a registration failure. On failure, the host
revokes the extension's active registration and persists its disabled
state. It retains the signed package and allows a manual retry when the
disabled state was saved successfully. The module registry probe installs a
separately signed archive with a missing import, checks the stored package
and disabled state, and reopens the Workspace with the disabled-state
probe. The relocated macOS app passed for installations in Work and
Personal, including the signed install and update checks for working
packages. Native Windows execution remains open.

### Storage from an isolated extension frame

The Content host attaches Chromium's extension WebContents observer to
regular profiles when the extension runtime is enabled. The observer
records script injection and the frame host forwards API calls through
Chromium's extension function dispatcher. The dispatcher validates the
renderer and extension identity before calling a registered function.
The frame event route uses Chromium's listener admission checks. The
existing worker API route remains separate. Private windows have no
frame bindings. Navigation tracking starts after the profile's
extension system initializes its
renderer startup helper. This preserves script delivery to new
renderers when the same signed extension runs in two profiles.

The signed registry trial installs an extension through Settings and
then navigates to a loopback page. Its document-start content script
writes and reads `chrome.storage.local` and observes its change event.
The page checks that its own world has no extension storage API. The
trial checks the saved value and the worker's installation event in the
selected profile, checks for the absence of an extension store in the
peer profile, and reopens the signed installation.

The relocated macOS bundle passed the Work compositor and Personal
Metal trials, the paired same-ID trial, the additional-profile restore,
and the managed and ordinary Workspace regressions. The extension
registry and Workspace modules cross-compiled to Windows x64.
Native Windows execution remains open.

### Runtime messages between an extension frame and its worker

The Content host uses Chromium's profile-keyed extension message
service. It validates senders and resolves enabled recipients in their
own browser context. Content scripts can call `runtime.sendMessage` and
exchange messages over a `runtime.connect` port. Native messaging and
private windows remain outside the extension runtime.

The Settings installation verifier sends a one-shot message and opens a
port from the isolated script after exercising local storage. It checks
the replies and verifies that the page itself has no messaging API.
After closing the port, the verifier stops the worker in its extension
storage partition and confirms it has stopped. The same document then
sends another message. A correct reply and a different running worker
token prove that the message started a new worker instance without
reopening the page or browser.

The paired verifier installs two signed versions with the same ID in
Work and Personal. Each script must receive its own version from its
worker. The trial also repeats the exchange after restart and verifies
that disabling one profile leaves the other worker available. The
relocated macOS bundle passed the Settings trials in Work and Personal,
the same-ID paired trial, and managed restore. The registry module
cross-compiled to Windows x64. The macOS dependency graph contains
13,720 labels and zero `//chrome` labels.

### Extension discovery and messaging for Workspace tabs

A regular Workspace tab owns a session-local ID independently of its
WebContents. The messaging delegate looks it up in the requesting
profile and supplies the ID as `sender.tab.id`. A Peek preview receives
an ID when promoted into a tab. The standalone host exposes `tabs.query`,
`tabs.sendMessage` and `tabs.connect` to privileged extension contexts.
Discovery covers loaded and deferred tabs in the regular Workspace
window within the calling profile. It returns the ID, order, selection,
pins and discard state. The current and last-focused window filters
refer to that single regular window. Private windows and unpromoted
Peek previews are excluded.

For ordinary web pages, the supported package policy requires a signed,
approved `tabs` permission to read the URL, pending URL and title through
discovery. URL and title filters apply the same access checks as the
returned fields. A signed update cannot add the permission to an
existing grant.

Local staging takes Chromium's tabs binding hooks and the selected API
schema from the pinned checkout. They compile in the Content target
without a Chrome GN target. The staged messaging service also verifies
that a document resolved by `documentId` belongs to the requested
WebContents. This keeps document targeting within the chosen tab even
when only a document ID is provided.

The Settings verifier opens two pages in one profile and keeps a tab
loaded in the peer profile. It checks discovery scope, tab order and
selection, and verifies that an extension without the grant can neither
read nor search by the page address or title. An approved receiver can
search by both fields. The updater rejects a same-key package that adds
the grant. The verifier sends a one-shot message and opens a port to the
chosen tab and document. It checks rejection for the same document paired
with the wrong tab ID and for the peer profile's tab ID. The content
script cannot call the privileged tabs API directly.

The relocated macOS bundle passed in Work on light compositor and
Personal on dark Metal. The paired extension and managed restore trials
passed. The changed extension and Workspace sources compiled for Windows
x64. Native Windows runtime verification remains open. The macOS bundle graph
contains 13,720 dependency labels and zero `//chrome` labels.

#### Tab lifecycle events

Privileged extensions can subscribe to `tabs.onCreated`, `tabs.onActivated`
and `tabs.onRemoved`. The event router dispatches within the tab's profile,
including to lazy service worker listeners. The creation payload applies the
same address and title access rules as `tabs.query`. Creation is published
when the tab first receives its Workspace metadata. Removal marks the
end of the logical tab. Sleeping a workspace reports `discarded: true`
and `status: "unloaded"` in an update. Restoring its page keeps the ID.
Messaging a discarded tab fails because there is no live document.
A move between workspaces in the same profile keeps the ID. A move to
another profile creates a new tab in the destination profile.

The signed Settings trial checks one creation event, activation in the
regular window, and redaction of unapproved fields. It also checks that
no creation or activation for the peer profile reaches the worker. It
then stops the service worker, closes the target tab, and requires a new
worker instance to write the removal payload to the extension's local
storage. The sender frame observes that write without messaging the
worker. The relocated macOS bundle passed in Work on light compositor
and Personal on dark Metal. The paired extension and managed restore
regressions passed. The changed modules cross-compiled for Windows x64.

The Settings verifier also opens a second workspace in the same profile
and sleeps the original workspace. The worker finds the same tab ID with
`tabs.query({discarded: true, status: "unloaded"})`, observes the discard
update, and receives a missing-recipient error when it sends a message.
The allowed receiver can still query the stored address and title. The
unapproved listener sees neither field and receives no removal for that
tab. The verifier then selects the original workspace and checks that
the new WebContents reports the original ID. This passed in Work on
light compositor and Personal on dark Metal. The paired and managed
extension trials, Peek, and the tab move and duplication trials also
passed. Native Windows execution remains open.

#### Tab update events

The host exposes `tabs.onUpdated` for primary-page navigation, loading
completion, committed document titles, pin state, and audibility. A
same-document address change reports the new address without inventing a
loading transition. The browser applies the same permissions to the
change information and the tab snapshot. An extension with no access
to the changed fields receives no event for that change alone.

The signed Settings verifier checks loading and completion for an
ordinary page, then compares the update payloads of two extensions in
the same profile. The extension with the `tabs` grant receives the
initial address, the same-document fragment change and the committed
title. The other extension sees neither the address nor the title in
the changes or snapshots. Both listeners exclude the peer profile. The
relocated macOS package passed the Work and Personal runs. The changed
modules cross-compiled for Windows x64. Pin and audibility delivery
still need dedicated runtime probes.

#### Tab creation, updates, and removal

Privileged extensions can open a foreground or background tab with
`tabs.create`, select or navigate a tab with `tabs.update`, and close one
or more tabs with `tabs.remove`. The browser resolves IDs in the caller's
profile. Creation uses the active workspace if it belongs to that profile
and the first workspace of that profile otherwise. Tab creation accepts
HTTP and HTTPS addresses without credentials and the calling extension's
own resource URLs. The browser resolves relative paths against the
calling extension and canonicalizes its own dynamic host. The new tab
authorizes that exact URL for a browser-initiated main-frame navigation
while the extension is enabled in the profile. Web-initiated requests
and redirects cannot use that authorization. `tabs.update` accepts
HTTP and HTTPS destinations without credentials. The `tabs` permission
controls sensitive response fields as it does for discovery.

Removal resolves the whole batch before changing Workspace state. It uses
the Workspace beforeunload check for every loaded target and commits after
approval. A denied confirmation leaves the batch intact. A background
page whose dialog cannot be presented fails the same check. Highlight,
pin and other mutation options remain open.

The signed Settings trial opens an extension-owned page in foreground
and background tabs, verifies its packaged script ran, and rejects the
same API request for another installed extension's page. It passed in
light compositor and dark Metal. The trial also opens a background web
page, navigates the same tab while activating it, and uses an implicit
active target. It checks redaction
on both returned tab objects, rejects internal and JavaScript URLs, and
confirms that a mixed-profile close changes neither target. It then
declines and approves a page's real beforeunload dialog. The first
response preserves both tabs, and the second closes both and delivers
their removal events. The relocated macOS trial passed for Work and
Personal. The Split View and tab move regressions passed. Native
Windows execution remains open.

#### Reordering and moving tabs

`tabs.move` uses the same profile-wide order as `tabs.query`. An index
inside another workspace of the same profile moves the tab to that
workspace. The operation keeps the tab ID and its live page or deferred
state. Moving a selected tab to another workspace keeps that tab active.
An in-workspace reorder preserves Split View pane membership. Moving a
pane to a different workspace removes it from the original split.

The host validates every requested ID and the planned layout before
applying a batch. It rejects foreign profiles, unsupported window IDs,
duplicate targets, and cross-workspace moves of pinned or captured tabs.
It leaves a blank tab if a source workspace becomes empty.
`tabs.onMoved` reports the old and new profile-wide index for explicitly
moved tabs. Index shifts for neighboring tabs do not produce move events.

The signed Settings trial moves a background tab to the first index,
appends a batch to the second workspace, and moves the batch back. It
checks the returned IDs and indices, the live page, move events, and
the original selection. It also moves a selected tab across workspaces
and rejects a mixed-profile batch without changing the order. The
relocated macOS trial passed in Work and Personal. Split View, the
native tab commands, and Peek passed their regression trials. The
changed modules cross-compiled for Windows x64.

#### Reloading tabs

`tabs.reload` accepts a tab ID from the requesting profile or defaults
to that profile's active tab. Loaded pages use the same Content reload
controller as the Workspace commands. `bypassCache` selects Chromium's
cache bypass mode. A deferred tab loads in place without changing its
ID or selecting it. RSS tabs keep the Workspace feed refresh behavior.

#### Form resubmission confirmation

Content's navigation controller holds a pending reload when a committed
page contains a POST result. The Workspace presents its native
page dialog on a visible tab, identifies the origin and profile, and
defaults to cancellation. Only the native confirmation can continue the
pending reload. A top-level navigation, hidden tab or destroyed page
invalidates the confirmation. Extension requests for background POST
results are rejected without selecting or reloading the tab.

The signed Settings probe checks `Cache-Control: max-age=0` on a normal
reload and `no-cache` on bypass reloads of an active page and a deferred
tab. It verifies that the deferred tab keeps its ID and stays in the
background. It also rejects a foreign tab ID. The form verifier submits
the original form, rejects a background reload, then declines and
approves two visible confirmations. The server expects exactly two
requests with the original form body. The verifier checks the origin,
the action label, the default cancel button, and rejection of the
JavaScript dialog handler for a repost.

The relocated macOS package passed in light and dark with compositor
and Metal. The Split View and native Hard Reload regressions passed.
The changed dialog and Workspace modules cross-compiled for Windows
x64. Native Windows modal behavior remains open.

#### Duplicating tabs

`tabs.duplicate` uses the Workspace duplication command and resolves the
source ID in the caller's profile. The copy has a new ID, is inserted next
to the source, and becomes active in the source workspace. A live source
contributes its committed navigation history and a separate copy of its
session storage. For an unloaded source, only the copy loads the stored
address. The original tab keeps its ID and discard state.

The operation rejects a current or initial pending entry that contains
POST data in the main frame or a child frame. Loading a cloned
navigation entry could replay the form. The returned tab uses the
same metadata permissions as `tabs.query`.
The copy is a regular tab and does not claim the source's pin association.

The signed Settings verifier navigates a source through two pages,
then navigates the copy back while leaving the source on the second
page. It checks that the copy initially reads the same session storage
and that a write in the copy leaves the source unchanged. It also
rejects a foreign tab and a committed POST result, and the server
observes one request when copying a deferred tab. The relocated macOS
package passed in Work and Personal. The native tab commands, Split
View and RSS regressions passed. The changed modules cross-compiled
for Windows x64.

#### Extension back and forward navigation

`tabs.goBack` and `tabs.goForward` accept an ID in the extension's profile
or use that profile's active tab. They leave deferred tabs unloaded.
The standalone staging hook asks Chromium's navigation controller
for the actual destination after its history skipping rules. It
rejects that entry if the main frame or any child frame used POST.
An earlier or later POST entry does not prevent a GET traversal.
The hook changes one Chromium implementation file and adds no
`//chrome` target or public Chromium header change.

The signed Settings verifier traverses the copied tab's history in
both directions and checks foreign IDs, an unloaded tab and the
forward boundary. It creates a top-level POST and an iframe POST,
navigates away, and verifies that each can be approached through a
normal GET entry without replaying the form. It also rejects duplicating
the iframe POST result. The relocated macOS package passed in Work
and Personal. The tab commands, Hard Reload and Peek regressions passed.
The extension and Workspace modules cross-compiled for Windows x64.
The standalone cross-check of the patched controller stopped at missing
Windows-generated Mojo headers. The native Windows build and runtime
trial remain open.

#### Tab lookup and audio muting

`tabs.get` resolves a single ID in the extension's profile and applies
the same address and title checks as `tabs.query`. Extensions can mute
and unmute an active or named tab through `tabs.update`. The returned
tab, the query filter, and the `tabs.onUpdated` payload use the same
mute state. Effective changes identify the extension that made them.

Mute state belongs to the logical tab for the running browser session.
Changing it on an unloaded tab leaves the page unloaded. When a page
attaches, Zitter applies the state to the new WebContents audio stream.

The Settings verifier checks single-tab metadata redaction and rejects
lookup or mute requests for a foreign tab. It mutes a deferred tab,
queries it without loading it, and checks Content's actual mute state
after loading and unmuting it. The worker receives one update per
effective change. A separate sleep and restore step checks that the
original tab's mute state reaches its replacement WebContents. The
relocated macOS package passed in Work and Personal. The paired
extension and native tab command regressions passed. The changed
modules cross-compiled for Windows x64. The audible tab indicator
probe below covers Web Audio separately from the mute control.

#### Native tab mute menu

The tab context menu offers **Mute tab** or **Unmute tab** based on the
target tab's current state. It resolves the target by its sidebar row
identity before applying the action. In an extension-enabled profile,
the menu and `tabs.update` share the tab's mute state. A menu change
reports `mutedInfo.reason: "user"` and clears the extension ID from
the previous change. Workspaces without the extension runtime use the
same Content audio control and retain the state on unloaded tabs.

The signed extension probe mutes a deferred tab, loads it, and uses
the menu to unmute the live page. It checks Content's mute state and
the updated extension event and lookup payload. The native tab command
probe also exercises both menu actions without the extension runtime.
The relocated macOS package passed Work and Personal, and the native
menu probe passed with light and dark compositor and Metal. The Split
View regression passed. The extension and Workspace modules
cross-compiled for Windows x64. The separate shell cross-check stopped
at missing Windows-generated Viz Mojo headers. The native Windows
build and menu trial remain open.

#### Audible tab indicator

Content forwards audio activity and mute changes from WebContents to
the Workspace tab rows. An audible, unmuted tab shows a speaker in
the trailing control slot. The close control takes precedence on
hover. Compositor and Metal use the same drawing recipe.

The extension probe starts Web Audio with Chromium's fake audio
output. It checks the live tab's audible state, query filter, update
event, and sidebar speaker recipe. Muting removes the speaker.
Stopping the stream clears the audible state and delivers the second
update. The fixture does not send audio to the host speakers.

The relocated macOS package passed all four combinations of light or
dark theme and compositor or Metal. The native tab commands and Split
View regressions passed. The Workspace modules cross-compiled for
Windows x64. Native Windows rendering and hardware audio output remain
open.

#### Discarding individual tabs

`tabs.discard` accepts a tab ID or selects the least recently active
eligible tab in the extension's profile. It excludes visible, active,
loading, audible, captured and picture-in-picture pages, and pages
that require a beforeunload check. Entries containing POST data also
remain loaded so a later restoration cannot replay a form.

The Workspace keeps the original tab ID and replaces the old page
with a renderer-free WebContents held off the view. Chromium copies
the navigation controller and session storage to the replacement.
The tab reports `discarded` and `unloaded`, and messages have no
recipient until the page is restored. Selecting it reloads the copied
entry. An explicit reload can also restore it in the background. The
restored document sees `document.wasDiscarded`.

The signed Settings verifier checks the original ID, history and
session storage after activation, and restores a second discard in
the background. It exercises explicit and automatic selection and
rejects foreign IDs, already unloaded and active tabs, a playing
audio stream, beforeunload, and POST results in the main or child frame.
The relocated macOS package passed Work and Personal. The native tab
commands and Split View regressions passed. The changed modules
cross-compiled for Windows x64.

### External extension sender policy

The standalone host accepts the `externally_connectable.ids` policy for
extension-to-extension messaging. It rejects website URL matches and
TLS channel ID requests. The signed installation review displays the
allowed IDs and identifies the default policy, which permits other
extensions in the same profile. An update can retain or narrow the
approved sender set. Adding IDs, switching to a wildcard, or removing a
restrictive policy requires a fresh approval path. The signed updater
rejects each change. The registry verifier then activates a version
that narrows its sender list from two IDs to one. The legacy migration
record version 1 cannot attest a declared policy. When a legacy
profile's cached manifest declares an extension-only sender list,
the migration tool exports a version 2 record with the normalized
sender set. Content requires an exact match with the signed package
and includes the record in its existing migration digest. Records
without a declared policy keep their version 1 format. Website
sender patterns and TLS channel ID requests remain unsupported.
Chromium omits the manifest cache for unpacked extensions, so
their explicit policies require a separate source of evidence.

The Settings verifier restores three signed receiver fixtures with
distinct IDs. The selected profile contains an allowed receiver and
a receiver whose policy excludes the sender. The other profile
contains a receiver that would accept the sender in its own context.
The sender receives a reply only from the allowed receiver. Both
rejections use Chromium's ordinary missing-recipient response. The
page's main world has no messaging API. Each receiver writes a startup
marker, and only the allowed receiver records the sender ID. The
relocated macOS bundle passed the Settings trials in both themes with
compositor and Metal, the paired and multi-extension restore trials,
and the two-extension legacy migration. Native Windows runtime
verification remains open.

The two-extension legacy verifier reads real Chromium storage and
permissions from disposable profiles. After Chromium closes, it adds
the packed manifest cache shape to one offline fixture because the
legacy verifier loads its source extensions unpacked. It checks
version 1 and 2 records together. It tries same-key signed packages
with a wildcard, different IDs, and an omitted policy. Each rejection
leaves the imported data and installation directory untouched. The
matching package installs and restores from its signed files and
migration record. The macOS trial passed in Work and Personal with a
synthesized packed manifest entry. The separate version 1 migration,
signed registry and managed restore trials also passed. The manifest
module cross-compiled to Windows x64.

### Signed update and interrupted activation

Work keeps each signed version in its own directory. The atomic
`active-version` record selects which package to load. Publishing the
candidate directory leaves the prior version active until the record is
committed. A retry can reuse the existing candidate after verifying its
signed bytes and installed files. The probe keeps the previous version
on disk and rejects an update request for the active or an older version.

The verifier stages a valid 2.0 directory while 1.0 remains active to
model an interruption before the record write. It restores and runs 1.0,
then retries the update and verifies that 2.0 runs and reads the same
local-storage data. A final restart restores 2.0 after removing the
source directory. The test also rejects a corrupt update, a signed
update that adds a permission, a downgrade, and an identical replay.
The four macOS launches passed. A separate local run also verified that
a fresh update publishes the candidate directory, activates 2.0, and
retains both the previous signed version and the local data. The
standalone manifest and ordinary Workspace with DevTools passed. GN
verified 13,554 dependency labels and zero `//chrome` labels.

### Nested extension archive resources

The package verifier includes a nested JavaScript resource with a Unicode
filename, an empty file, and a binary asset. The extension page must run
the nested script. The installer and restore checks verify the remaining
assets byte for byte. Signed negative cases cover traversal, a Windows
device name, case collisions, canonically equivalent Unicode names, and
a path used as both a file and a directory. Chromium's portable path
validator applies on both platform sources. Native execution has only
been verified on macOS. The four-launch verifier passed with deflated
archives, including the decompressed-size limit and the named-pipe
replacement. The standalone manifest and ordinary Workspace with
DevTools passed on the same build.

The bounded archive service serves the disposable fixture. It checks
that the utility is sandboxed, and the browser requires a PID distinct
from its own before accepting a result. Failure, disconnect, and timeout
abort before publishing a candidate. The four-launch macOS verifier
required a successful sandboxed extraction in every launch. A separate
oversized-file probe rejected a signed archive before publication. GN
checked 13,566 dependency labels and zero `//chrome` labels with the
archive service included. The version record controls activation in the
locked fixture profile. Production trust sources,
general manifest and package policy, durable install and
permission records, recovery from a broken active version, rollback
policy, and restore outside the probe remain open. Worker imports, dynamically registered content scripts,
broader extension API coverage, production install metadata migration,
automatic profile-start events, and native Windows execution also remain
open. Renderer-initiated and redirect denial still need their own runtime
fixtures.

## HTTP authentication

The Content browser client handles HTTP authentication through a native
Workspace dialog. It shows the network challenger's address separately from
the server-supplied realm, masks the password, and submits only on explicit
acceptance. Requests that Content marks as non-interactive and requests in
hidden tabs do not open a dialog. Hiding or destroying the requesting tab,
or starting a replacement navigation, cancels its pending prompt. The
embedder leaves HTTP authentication caching to each profile's network
context and does not save these credentials in Passwords.

`tools/verify_content_http_auth.py` serves real Basic and Digest challenges
on loopback. For each scheme it checks a successful Work sign-in, a separate
challenge from Personal at the same URL, explicit cancellation, replacement
navigation, and switching away while the prompt is open. The server checks
that only the accepted request carries credentials. All 32 cases passed on
macOS arm64 in light and dark themes with both compositor and Metal, and
the visible dialog was reviewed in all four configurations. The dark Metal
suite also passed in the sealed bundle produced by `./build.sh --content`
and on the relocated signed Content 0.1.4 DMG, with a disposable profile
root for each of the eight cases.
GN header checking passed and the target's 13,755 dependency labels include
no `//chrome` targets. Proxy and integrated enterprise authentication were
not exercised by this fixture.

## TLS certificate errors

The Content host uses Chromium's default denial for certificate errors.
Its profile supplies no SSL exception store. Startup rejects
`--ignore-certificate-errors`, `--allow-insecure-localhost`, and
`--ignore-certificate-errors-spki-list` before creating a profile. The
Workspace TLS probe accepts only an HTTPS loopback URL in smoke mode.

When a main-frame navigation fails certificate validation, the renderer
shows a local error document with the Chromium error code. The document
uses a neutral light or dark surface, a fixed English message matching
the lab locale, and no links, scripts, or override controls. Its content
security policy permits only the embedded stylesheet.

```sh
python3 tools/verify_content_tls.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The verifier generates a disposable self-signed certificate with OpenSSL
and confirms the fixture is reachable from a client configured to trust
it. The isolated Workspace must commit a certificate error page with
`ERR_CERT_AUTHORITY_INVALID`, show a visible heading within the viewport,
and offer no override control. The server must receive no HTTP request
from the browser. The verifier also checks startup rejection for each
of the three bypass switches and for using the TLS probe outside smoke
mode. The relocated macOS app passed in light and dark Workspace
appearances with compositor and Metal. The embedded HTML was also
rendered separately for visual review in both color schemes.

Pass `--trusted-url` to additionally test an HTTPS URL reachable from
the host with its ordinary trust settings. Use a direct URL without
redirects. The browser must commit the requested page and report a
certificate with no certificate-status errors. The offline part also
sends the self-signed loopback fixture through this mode and requires
it to fail. The two modes require an ephemeral Workspace smoke run
and cannot be combined in one browser invocation.

```sh
python3 tools/verify_content_tls.py --trusted-url https://www.chromium.org/ \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

The relocated macOS app passed the trusted Chromium navigation and
rejected the self-signed fixture in both probe modes. The offline
regression also passed in dark Metal. Native Windows execution remains
open.

### WebAuthn transport and tab visibility

The shared Content browser client applies a WebAuthn transport policy to
the active frame. It requires a committed top-level page. For HTTPS it
uses Chromium's shared security-state classification and requires a
secure page. HTTP is permitted only when both the calling origin and the
top-level URL are loopback. The check rejects error pages and requires
the visible navigation entry to refer to the committed page. Chromium
Content still validates the caller, relying-party ID, and permissions
policy.

The TLS smoke modes invoke the browser client's WebAuthn policy as part of
their assertions. The self-signed fixture must fail the policy. The
optional trusted HTTPS page must pass it. The relocated macOS bundle
passed the self-signed fixture in both themes with compositor and Metal,
and the trusted Chromium URL in light compositor. GN include checking
passed, and the rebuilt graph contained 13,734 labels with zero
`//chrome` dependencies. These checks exercise the transport policy
without creating or using credentials.

The shared WebAuthn delegate uses the same visibility criterion as
Chromium's Chrome delegate for credential creation. The Workspace smoke
test checks the delegate before and after switching between Personal
and Work. The hidden page must fail the visibility check, and the
visible page must pass it. The relocated macOS bundle passed the
light compositor and dark Metal Workspace smoke runs, including the
DevTools frontend in the Metal run. The same package run passed
sandbox override rejection and reopening the two encrypted profiles
with a fresh private context.

The optional virtual WebAuthn smoke test uses an ephemeral Workspace and
an exact `http://localhost:<port>/` fixture. A restricted, in-process
DevTools session configures a CTAP2 platform authenticator and executes a
WebAuthn registration and assertion in the renderer. Registration
requires a resident credential and user verification. The assertion
omits an allow list and must return the same credential ID and user
handle with the presence and verification flags set. The fixture checks
the RP ID hash, client data origin, operation, and fresh challenge. It
verifies the ES256 signature with the public key returned at registration
and requires an IP address submitted as the RP ID to fail with
`SecurityError`. The verifier also checks rejection outside smoke mode,
with a persistent Workspace root, and with a different hostname. The
browser's transport policy is
checked directly before attachment because Chromium skips its own TLS
check for virtual authenticators. The relocated macOS bundle passed
the probe in light compositor and dark Metal, and passed the separate
package, sandbox, DevTools, and persistent profile regressions. The
relocated signed Content 0.1.4 DMG also passed the full virtual probe
in light compositor and dark Metal. The native Windows and system
authenticator runs remain open.

```sh
python3 tools/verify_content_tls.py --virtual-webauthn \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

### Native macOS passkey discovery

The macOS request delegate supplies the Workspace window to Chromium's
iCloud Keychain discovery for modal WebAuthn requests from visible,
active frames in regular profiles. Resident keys for real requests
require that window and Chromium's iCloud Keychain support check.
Chromium checks the
`com.apple.developer.web-browser.public-key-credential` entitlement and
requires macOS 13.5 or later for this discovery path. The delegate
rechecks the window before authenticator dispatch. The native picker
selects the iCloud account. If an authenticator returns accounts that
require a browser picker, the delegate cancels that request. Private
profiles, conditional requests and virtual tests do not start this
native discovery path.

The relocated development bundle passed TLS, virtual registration and
assertion, and the Workspace visibility smoke test in light compositor
and dark Metal. The visibility test verifies that the hidden Workspace
frame cannot advertise resident keys. GN include checking passed with
13,755 dependency labels and zero under `//chrome`.

Check an already-signed candidate without resealing it:

```sh
python3 tools/verify_content_bundle.py --preserve-signature \
  --require-native-passkeys '/path/to/Zitter Content Lab.app'
```

This checks the signed browser entitlement and requires resident-key
discovery to follow the visible Workspace in an ephemeral session.
It exercises readiness without creating a credential or opening the
system account picker. The entitlement check and runtime probe both
rejected the ad hoc signed bundle. The probe also rejected invocation
outside smoke mode and with a persistent profile root or a virtual
authenticator. Ordinary package verification with the existing
signature passed on the same bundle, including sandbox, DevTools, and
persistent and private storage checks.

The development bundle and the inspected signed legacy Zitter sample
lack the browser credential entitlement. The sample also lacks the
keychain access group required by Chromium's separate Touch ID
authenticator. Production remains gated on an entitled package and a
native run covering registration, assertion, cancellation and profile
behavior. Browser account selection, conditional mediation and separate
Touch ID credential storage remain open. Windows Hello needs native
execution and browser handling of discoverable credentials.

The 0.1.1 Developer ID rehearsal also lacks the managed browser
credential entitlement. Apple requires the organization Account Holder
to request it. The signed candidate declares both HTTP and HTTPS
handlers, which the production bundle verifier now checks. A disposable
copy under the URL probe identity received a cold and a warm HTTP
LaunchServices event and saved the two tabs. This verifies the URL
handoff with an isolated identity. The actual system passkey trial
requires a bundle signed with Apple's granted capability.
[Apple browser credential entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.web-browser.public-key-credential)

The passkey entitlement preflight also rejected the notarized 0.1.4
Content release DMG. Its production identity and notarization do not
provide the managed capability. Registration and assertion using the
system authenticator still require an entitled candidate.
The ordinary verifier passed on the same DMG with the original signatures,
including Workspace startup, DevTools, two encrypted profiles after
restart, private session disposal, and the bundled Assistant checks.

The unchanged 0.1.1 signed app passed the TLS policy and virtual
WebAuthn registration and assertion probes in dark Metal.
The current local build repeated the TLS and virtual WebAuthn cases
in dark Metal. The signed 0.1.6 candidate still lacks
`com.apple.developer.web-browser.public-key-credential`. The
virtual result covers registration and assertion through Content.
The native trial requires the managed entitlement and a system
authenticator.

## Download probe

Content Lab profiles block ordinary downloads by default. The probe opens
one browser-initiated GET to an exact loopback URL in a temporary private
profile. It allows a single-hop response declared as plain text with a
positive Content-Length of at most 64 KiB. The browser reserves a fixed
filename with Chromium's collision handling, ignores the server's filename,
and supplies Chromium's quarantine implementation. On Windows the browser
launches Chromium's dedicated quarantine utility service and supplies a
stable Zitter client ID for Windows Attachment Services. The size and MIME
checks use response metadata and are specific to the fixture.

After staging and building, run this in a macOS GUI session from the
migration worktree.

```sh
python3 tools/verify_content_downloads.py --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

On Windows run the same verifier with the native executable on a filesystem
that supports alternate data streams.

```powershell
python tools/verify_content_downloads.py --require-quarantine .chromium/src/out/ZitterContentWin/zitter_content.exe
```

The fixture checks the new file's bytes, preservation of an existing file,
rejection of binary and oversized responses, and cleanup of temporary
profiles and partial files. The linked macOS app passed all three cases.
The accepted file must have macOS quarantine
without a Spotlight origin attribute, or a Windows `Zone.Identifier` stream
with `ZoneId=3` and no private source or referrer. Native Windows execution
of the quarantine utility remains pending.

### Profile-scoped Downloads panel

The Chrome and Content hosts now share the Downloads panel. The Content host
binds it to the active profile's DownloadManager and reuses the footer
progress indicator, completion notice, filtering and list actions.
Switching profiles closes the old panel, clears its filters and transient
download UI, and invalidates outstanding panel confirmations. An approved
file deletion stays bound to the originating browser context if it
finishes after the switch.

```sh
python3 tools/verify_content_downloads.py --workspace --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The verifier accepts `zitter_content.exe` on Windows and supports
`--theme dark` and, on macOS, `--renderer metal`. It uses a disposable
durable workspace and two exact loopback URLs. Work and Personal receive
different payloads and quarantine tags in separate fixture folders. The
panel opens for each profile, shows only its own completed item, and
recovers Work's item after switching back. The test also checks that the
old filter, completion notice and progress cue are gone in Personal. The
linked macOS app passed all four theme and renderer combinations. The
empty and in-progress panel states were visually checked in each
combination. The download panel and Windows workspace passed a Clang x64
syntax check with Windows SDK headers. The Chrome adapter's binding code
passed a separate Clang syntax check. The macOS dependency graph still has
zero `//chrome` labels.

### Importing download history

The offline importer retains completed, cancelled, and interrupted
downloads with a safe Chromium verdict, a local target path, and an
HTTP or HTTPS URL chain. It drops active transfers and rows with
unsupported metadata. Retained entries preserve the profile, GUID,
name, URL chain, times, size, and original failure reason in SQLite.
The importer clears the content hash, resume validators, and slice
records. It also clears the partial-file path for unfinished entries.

The Downloads panel shows these entries and allows copying their link
or removing the record. An imported interrupted item is constructed
with a non-resumable reason in memory. The browser does not open its
old partial file. File and transfer actions are unavailable for all
imported entries, including pause, resume, retry, and cancel. The usual
Chromium expiration rule applies to cancelled and interrupted rows.
Import leaves the actual files, their quarantine attributes, and the
source and rollback databases intact.

```sh
python3 tools/verify_content_downloads.py --import-history --require-quarantine \
  'path/to/Zitter Content Lab.app'
```

The probe uses the pinned History schema with real completed downloads
and adds cancelled, interrupted, active, and dangerous source rows.
It checks both profile lists across two launches, rejects file and
transfer actions, and confirms that no download request was replayed.
It compares the original files, the legacy partial file, and the
source and rollback inventories. The relocated macOS bundle passed in
both themes on compositor and Metal. The standard Workspace download
history and Content-owned recovery after restart and process crash
regressions passed. All 48 offline profile migration tests passed. The
history adapter, Workspace host, and shared panel compiled to Windows x64
COFF. Native Windows runtime verification and trials with representative
shipped profiles remain open.

### Restoring completed downloads

Each durable Content profile records completed, non-temporary downloads
that passed its safety checks through Chromium's `HistoryService` and its
existing `downloads` and URL-chain tables. It
allocates IDs from that database and restores records through
`DownloadManager::CreateDownloadItem`. Private contexts do not attach a
download history observer. Restoration accepts completed entries with a
valid ID, URL and local absolute target path. It leaves other records in the
database. The panel can remove restored entries from the list. Content file
open, reveal and disk deletion remain disabled for these entries.

The workspace verifier above checks both SQLite databases after shutdown.
It reopens the profiles, restores both items without network requests,
removes the Work item from the panel, and reopens again. Personal's entry
and both downloaded files must survive. The verifier works from an
independent copy of the Work database before modifying the candidate.
Durable startup also rejects symlinks or directories at the History and
Favicons database paths and their SQLite sidecars. The linked macOS app
passed the restart and removal checks in both themes and renderers. The
updated verifier also passed with the independent database copy and
symlinks to History, its journal and Favicons on macOS. The Windows entry
point, download history, delegate and panel passed a Clang x64 syntax check
with Windows SDK headers. The macOS graph has 13,603 labels and zero
`//chrome` labels.

### Download destination picker

The Content download delegate can prompt with Chromium's native Save As
dialog, parented to the initiating tab's top-level window. The picker
validates the selected local path, reserves it through Chromium's download
path tracker, and forwards the exact destination with a unique intermediate
name. Cancellation, a hidden or replaced document, and an invalid window
detach the outstanding reply. The policy supplied by the disposable probe
accepts only new targets in its own downloads directory and preserves the
validated filename extension.

```sh
python3 tools/verify_content_downloads.py --save-picker --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The automated verifier drives the dialog interface and checks accepted
selection, explicit cancellation, an existing file, an outside destination,
a mismatched extension, and a reply after hiding the source tab. On macOS
it also rejects a dangling target symlink and accepts a valid parent-directory
alias after resolving it. The accepted file must contain the fixture bytes
and carry the platform quarantine tag. Passing
`--zitter-content-download-save-probe=native` alongside the single-download
probe switches to the actual platform dialog. Use a fresh fixture folder
and choose a new `.txt` destination inside it. The linked macOS app passed
both Save and Cancel in the native dialog. Saving preserved the bytes and
quarantine tag, and cancelling left the second fixture directory empty.
The Windows picker and browser integration passed a Clang x64 syntax
check with Windows SDK headers. The macOS graph still contains no
`//chrome` targets.

### Final download publication without replacement

The standalone host opts into a same-directory move without replacement at
the point where Chromium gives a completed download its final name.
Chromium's download pipeline reserves the destination and writes the
intermediate file. macOS uses `renamex_np` with `RENAME_EXCL`. Windows uses
`MoveFileExW` with neither replacement nor cross-volume copy enabled. A
failed move interrupts the download and preserves the existing target.
Chromium applies quarantine after a successful move.
[Darwin rename reference](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/man/man2/rename.2)
[Windows MoveFileExW reference](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)

```sh
python3 tools/verify_content_downloads.py --publish-collision \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The verifier holds the loopback response after its first bytes. Once the
save picker has accepted the path and the `.crdownload` exists, the verifier
creates the final target and releases the remaining response. On macOS the
linked app interrupted both a late file collision and a late symlink
collision, preserved the target and its contents, and removed the partial
file. The save picker, quarantine, and download history regressions passed.
The warm macOS component build executed 41 Ninja actions after
staging the Chromium hook and the probe. Repeating patch preparation
produced identical staged bytes. Both changed Chromium translation units
cross-compiled to Windows x64 COFF objects. The macOS dependency graph has
13,603 labels and zero `//chrome` labels.

### Download transport

The delegate requires an HTTPS response URL, HTTPS for every hop in the
download URL chain, and a non-opaque HTTPS origin when the request supplies
an initiator. Browser-initiated navigation can omit the initiator. The
target check runs before path reservation, so an HTTP redirect to an HTTPS
file still fails even when the initial delegate check sees an HTTPS response.
The older loopback fixtures retain an exception for their exact single-hop
HTTP URL. This policy is stricter than Chrome's policy for mixed downloads,
which takes the request context and file type into account.
[Chromium mixed-download policy](https://chromium.googlesource.com/chromium/src/+/main/chrome/browser/download/insecure_download_blocking.cc)

```sh
python3 tools/verify_content_downloads.py --transport --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The verifier generates a fresh TLS leaf. The lab trusts that exact
certificate for IPv4 loopback only in the standalone probe's disposable
private default network context. The verifier checks that direct HTTPS,
HTTPS redirects, and navigation initiated by an HTTPS page save the
expected bytes with quarantine. It also checks that direct HTTP, HTTPS-to-HTTP redirects, and
navigation initiated by an HTTP page drop the download request. An
HTTP-to-HTTPS redirect reaches target determination and leaves no file.
All seven cases passed in the linked macOS app, along with the untrusted
TLS, original download, save picker, publication collision, and Workspace
download history regressions. The warm macOS build ran 13 Ninja actions
after regeneration. The macOS dependency graph still contains 13,603
labels and zero `//chrome` labels. The download delegate, probe, profile
context, shared browser client, and Windows launcher cross-compiled to
Windows x64 COFF objects using Microsoft SDK headers and locally generated
Chromium headers. This check covers compilation of those translation units.

### Download file types

The lab packs Chromium's platform-specific Safe Browsing file type table
in `zitter_content.pak`. The delegate uses Chromium's filename generator
with the Content-Disposition header, suggested name and URL. It leaves
MIME-based extension inference off for the current probe policy, so a
response labeled `text/plain` cannot turn a declared executable into a
text file. Before it opens a dialog or reserves a destination, the
delegate requires a portable basename that Chromium classifies as
non-dangerous, with no archive or full reputation check required. Unknown
and extensionless names use Chromium's full-check default. A missing table
also fails the gate because Chromium's fallback requires a user gesture.
The save dialog enforces the same policy on the selected basename and
keeps the validated extension.

```sh
python3 tools/verify_content_downloads.py --file-types --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

On macOS, the verifier saved the expected bytes under names taken from
Content-Disposition, an encoded UTF-8 filename, and a URL ending in
`.csv`, each with quarantine. It also checked that a double-extension
executable, a macOS command script, a ZIP archive, an unknown extension,
a name without an extension, and a dangerous `filename*` paired with a
benign `filename` leave no file. All responses use the same MIME type and
payload. The resource index confirms that the packaged table is present.
The transport, save picker, publication collision, and Workspace history
regressions passed. The Workspace check used dark mode with Metal. The
warm macOS build ran six Ninja actions after regenerating the graph.
The updated delegate cross-compiled to a Windows x64 COFF object. The
macOS dependency graph has 13,603 labels and zero `//chrome` labels.

### Open and reveal downloaded files

The shared Workspace panel routes explicit Open and Reveal actions through
the active profile's download manager. Content accepts only completed
items that have a known target and carry neither a dangerous nor an
insecure status. Opening also applies the local file type policy. A
background check requires a local regular file at the recorded path and
rejects symlinks, directory replacements, and paths that resolve elsewhere.
For Open it reads an open file handle and compares the length and SHA-256
with the completed record. Reveal checks the path so a user can still
locate a file they edited or replaced. The validated path goes to AppKit
on macOS or the Windows shell on a COM worker. Those APIs resolve the path
again when handling the request, so a replacement after validation falls
outside the guarantee.
[AppKit file reveal](https://developer.apple.com/documentation/appkit/nsworkspace/activatefileviewerselecting(_:))
[Windows file selection](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shopenfolderandselectitems)

```sh
python3 tools/verify_content_downloads.py --workspace --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The verifier restores downloads from two independent histories. It checks
that each panel can reach only its own items and that the correct Open or
Reveal request reaches the shell boundary. It rejects a directory in place
of the downloaded file, a symlink in place of the file, and a symlink in
place of its parent directory. It also verifies that replacing the file
with different bytes of the same length blocks Open and still permits
Reveal. The probe captures the request before
calling the platform API, so it can run without launching an editor or
leaving Finder or Explorer windows open. The linked macOS app passed the
Workspace checks in light and dark modes with both compositor and Metal.
The relocated signed Content 0.1.4 DMG also passed the history and
shell-boundary checks in dark Metal with quarantine required.
The relocated non-component app passed the replacement test in light
compositor and dark Metal. It also passed the destination collision,
publication binding, URL and artifact reputation, transport, restart
and crash recovery, and confirmed deletion probes with quarantine
required where supported by each verifier.
Before dispatching a validated path, the delegate re-resolves the
download in its owning manager and checks that the row and destination
are still valid. Open also requires the original size and digest. The
regression removes the completed history row while an Open check is
pending and verifies that the file stays on disk and no path reaches the
shell boundary. It passed with quarantine required in light compositor
and dark Metal. The artifact-reputation regression also passed.
The Windows shell implementation, delegate, Workspace integration, and
shared panel cross-compiled to Windows x64 COFF objects. The macOS
dependency graph still has 13,603 labels and zero `//chrome` labels.

### Closing with ongoing downloads

The Workspace close flow completes its beforeunload checks, then asks
Chromium's download managers how many transfers should block shutdown
across both profiles. The count includes paused transfers and excludes
transient, dangerous, and insecure items. The existing Zitter prompt
defaults to waiting. Repeated close requests reuse the pending decision.
Waiting or dismissing the prompt keeps the profiles and their downloads
open. Confirming saves the session and then lets Chromium cancel the
in-progress downloads and remove their intermediate files. On macOS,
AppKit's application quit selector also enters this flow.
[AppKit application termination](https://developer.apple.com/documentation/appkit/nsapplication/terminate(_:))

```sh
python3 tools/verify_content_downloads.py --close-with-downloads --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The verifier holds a Work download after its first bytes and selects
Personal before requesting closure. It checks that a repeated close
leaves the same prompt and that dismissing it leaves the window open.
The Wait case lets the download complete and checks the file, quarantine,
SQLite history, and saved session. The Quit case pauses the transfer,
requests app termination on macOS or window closure on Windows, and
checks that the session was saved and no partial file or completed
download entry remains. Both cases passed in the linked macOS app with
each combination of theme and renderer. Both also passed on the relocated
signed Content 0.1.4 DMG in dark Metal with quarantine required. The
download-history and two-profile session restart regressions passed
during integration.
The final incremental build ran five Ninja actions. The dependency graph
contains 13,603 labels and zero `//chrome` labels.

The Workspace host, profile context, and Windows entry point cross-compiled
to x64 COFF objects. The shared dialog translation unit needs Windows
Mojo outputs for a Windows build. Native Windows runtime and a combined
run with an actual beforeunload prompt remain pending.

### Interrupted download resumption

Chromium resumes eligible HTTP downloads with the stored offset and a
validator such as an ETag. The Content delegate rechecks the response and
reserves the same destination after an interruption. A partial response
can omit the name or MIME type, in which case the delegate keeps the
already validated values. A full replacement response goes through the
filename and MIME checks. A collision at the reserved destination
interrupts publication. The delegate also checks the actual received
length before publishing, including for a response without Content-Length.
[HTTP range and If-Range semantics](https://www.rfc-editor.org/rfc/rfc9110.html#name-range-requests)

On reopening an intermediate file, the Content host opens an existing
regular file through a no-follow handle and rejects a leaf with multiple
hard links. For a resumed prefix, it recomputes the SHA-256 digest from the
file before writing further bytes. Chromium can restart from byte zero
when the prefix differs. The checks are scoped to the Content host's
protected download mode.

```sh
python3 tools/verify_content_downloads.py --resume --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The verifier checks the requested offset and If-Range header, the final
bytes and quarantine, the destination chosen in the save dialog, and a
full replacement when the server changes its ETag. It also checks that
incompatible MIME or filename metadata, oversized responses, and a late
destination collision produce no replacement file. The Workspace case
fails the automatic range attempt with a 503 and uses the existing Retry
action. It checks that the same item completes and that its profile's
history contains one completed row. Changing bytes in the on-disk prefix
forces a fresh request without Range. Replacing the intermediate file
with a hard link or symlink leaves the separate sentinel unchanged.

All twelve cases passed in the linked macOS app. The final app also
passed the publication collision and download shutdown probes. The
transport, file type, save picker, and Workspace history regressions
passed during integration. Fresh and repeated staging produced identical
Chromium source. The resumption protection adds no header changes to the
existing publication patch. The modified Chromium and embedder
translation units cross-compiled to Windows x64 COFF objects. Native
Windows runtime coverage remains pending. The macOS dependency graph
contains 13,603 labels and zero `//chrome` labels.

### Interrupted downloads after restart

A persistent profile saves an eligible interrupted download in its existing
History database. The row holds the original GUID, target and partial paths,
received byte count, strong ETag, and SHA-256 prefix digest. Recovery admits
only an authorized URL, validated file type, bounded size, usable offset,
and filenames under the fixture's configured directory. Startup checks the
partial file's recorded SHA-256 prefix off the UI thread for every
interrupted checkpoint. Retrying opens the file under the protected
resumption rules above and continues with the restored profile's default
storage partition. The restored item waits for the user's Retry action.

A successful retry updates the original row. Confirming app shutdown while
that retry is active removes its obsolete checkpoint as Chromium discards
the partial file. A private profile keeps interrupted downloads in memory
during its lifetime and deletes their partial files when it closes.

```sh
python3 tools/verify_content_downloads.py --resume-after-restart --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The verifier interrupts a Work download, closes the app, and checks its
SQLite checkpoint and partial bytes. It reopens the profile and checks that
startup makes no download request. It rejects a row that points to a
Personal file, a partial with different bytes at the same size, a hard
link to an otherwise byte-identical file, and, on macOS, a partial path
replaced by a symlink. The
verifier then retries the original item, checks the exact requested range,
file bytes and quarantine, and verifies that the same GUID now represents a
completed download. Separate cases exercise shutdown after starting a
restored transfer and cleanup when a private profile closes. All cases
passed in the linked macOS app. The modified embedder translation units
cross-compiled to Windows x64 COFF objects. Native Windows runtime
verification remains pending. The macOS graph has 13,603 labels and zero
`//chrome` labels.

Ordinary downloads currently bind the save-picker approval to the live
item. Recovery after restart will also need a durable approval bound to
the profile, GUID, URL, selected target and staging file before it can
accept paths outside the fixture directory.

### Active downloads after a process crash

For an eligible persistent download, the Content file sequence takes a
SHA-256 snapshot of the bytes written from the sequential stream and
passes the digest with that progress update. The profile history commits
an active checkpoint with the GUID, destination and staging paths,
byte offset, digest, and strong ETag. A normal shutdown removes
checkpoints for downloads it cancels. The private browser context has
no download History database.

Chromium marks surviving active rows as interrupted by a crash. The
Content host checks each eligible prefix on disk before exposing the
interrupted item. Startup leaves the file untouched and sends no
request. Retry reuses the recorded offset and ETag and verifies the
prefix again before appending. Extra bytes beyond the recorded offset
are discarded. Successful completion updates the original history row.

```sh
python3 tools/verify_content_downloads.py --resume-after-crash --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The macOS verifier holds a response open and observes checkpoints at
1,024 and 4,096 bytes under the same GUID before killing the browser
process. It checks the original database after exit, rejects a
modified prefix without a request, and resumes the verified prefix with
the exact `Range` and `If-Range` headers. It also checks the resulting
bytes, quarantine, GUID, and Personal download folder. The linked app
passed on the light compositor and dark Metal renderers. Resumption,
normal shutdown, Workspace history, and publication regressions passed.
The changed Chromium and embedder translation units cross-compiled to
Windows x64 COFF objects. Native Windows runtime verification remains
pending. The GN graph still has 13,603 labels and zero `//chrome` labels.

This covers process exit after a checkpoint is committed. Recovery
before the first checkpoint, power loss, and reconciliation after final
rename but before history commit remain separate cases. Sparse or
parallel transfers and ordinary site downloads are outside the current
fixture policy.

### Confirmed deletion of downloaded files

The shared download panel uses its existing confirmation dialog for a
single item or a selection. The Content policy admits completed downloads
from the active profile's configured source and destination. The history
row stores the SHA-256 digest of the completed download. The deletion
worker checks the recorded length and digest through an opened file
handle and requires a local regular file with a single link. If the file
changes or cannot be verified, the row stays in history.

On Windows the worker deletes through the verified handle while excluding
writers and renames. On macOS it anchors the operation to an open directory
descriptor and checks the leaf's identity immediately before calling
`unlinkat`. A concurrent same-user replacement in the interval between
that final check and the unlink is outside this guarantee. After deletion
succeeds, the panel removes the record from its owning profile even if
the user has switched workspaces. Normal window closure waits for pending
deletion replies.
[Windows handle-based deletion](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)
[Darwin unlink reference](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/man/man2/unlink.2)

```sh
python3 tools/verify_content_downloads.py --delete --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The verifier downloads one file per profile and reopens the Workspace.
It exercises cancelling the dialog, replacing the bytes without changing
the file size, and substituting links. A confirmed bulk deletion removes
the Work file and its SQLite row while the panel shows Personal. The
Personal file and history remain intact. All deletion cases passed on
macOS in light and dark modes with both compositor and Metal. The
download history and restart resumption regressions also passed. The
modified translation units cross-compiled to Windows x64 COFF objects.
Native Windows runtime verification remains pending. The macOS dependency
graph still has 13,603 labels and zero `//chrome` labels.

### Download checks on the notarized app

In 0.1.3, ordinary startup blocked downloads. The signed Workspace tests
explicitly configure disposable targets and exercise the pipeline,
history and quarantine behavior in the fixture.

Current ordinary macOS startup configures the save picker for persistent
profiles. It requires an HTTPS GET navigation with a user gesture or a
browser-initiated typed address and a fully HTTPS redirect chain. The
declared MIME type must match the suffix for text, CSV, PDF, PNG, JPEG,
GIF, WebP, MP3, MP4, M4A, MOV, Ogg audio, WAV, or WebM. Chromium's
file type table must also classify the name as
low risk. The final file must contain between one byte and 16 MiB.
Responses without a declared size can reach the save picker. The download
is cancelled when reported progress exceeds 16 MiB, and the publication
check independently enforces that limit on the complete file. The user
selects a new local file in the save picker. A worker verifies the staged bytes against the
download digest before Chromium publishes them with native quarantine.
An explicit same-origin HTTPS link with the HTML `download` attribute
also reaches the save picker. The browser consumes transient activation
from the visible active document before admitting that request. The
macOS fixture saved the clicked link and rejected the same scripted
click without activation before requesting the file.
The page context menu also offers **Save Link As…** for HTTPS links
from a visible HTTPS document. The callback is tied to that document and
rechecks it when dispatched. It starts a browser-initiated request in
the same profile and uses the same picker and publication policy. The
local HTTPS test invoked the download action and verified the saved
bytes and native quarantine.
Private windows use the same picker and publication checks. They share
the session's off-the-record download manager and have no HistoryService.
The selected file remains after closing the session. Quarantine does
not include its source URL or referrer.

The `--user-save --require-quarantine` probe passed with this delegate
policy, a disposable TLS fixture and a test save picker. It covered
acceptance, selection of another local directory, cancellation, a changed
extension, an executable filename, and an unsupported MIME type. The
macOS run saved CSV, PDF, PNG and a valid PCM WAV with their expected
bytes and quarantine. It rejected PDF and WAV filenames delivered as
`text/plain`. The
off-the-record fixture checks that the saved file carries quarantine
without its source URL and that the download context has no HistoryService.
Run the same verifier with `--ffmpeg ffmpeg` to generate additional
audio and video fixtures locally. The macOS run saved AAC in M4A,
MPEG-4 video in MOV and MP4, MP3, Opus in Ogg, and VP8 in WebM.
Each file matched the generated bytes and carried native quarantine.
The same fixture saved a short stream without `Content-Length` and
cancelled an oversized stream while the server was still holding the
response open. No partial or final file remained in the oversized case.
The Downloads panel can request confirmed deletion for a completed
download verified in the current session. It checks the selected path,
size and SHA-256 again before removing the file. The probe deleted an
unchanged download and preserved a file edited after publication. The
separate Workspace confirmation and profile isolation suite also passed
on the relocated signed Content 0.1.4 DMG in dark Metal, with quarantine
required and no further network requests during deletion.
An ordinary download can resume in the same session if the user approved
its destination, the original response supplied a strong ETag, and the
same HTTPS URL returns the remaining byte range with the original ETag
and total size. The app retains the selected path and checks the complete
file again before publication. The TLS probe completed a valid range
response and rejected a replacement response without leaving a file.
The user download delegate checks HTTPS redirect-chain reputation when a
checker is configured. It requires a clean verdict before opening the save
dialog or accepting a resumed target and rejects threats and unavailable
verdicts. The macOS reputation verifier covered the user save dialog, a
match at either redirect hop, service failure, and a second lookup on
resume when the initial verdict was not cached. The user-save suite also
passed with native quarantine. A regular profile records a resumable
checkpoint only after the user chooses a destination. The checkpoint
binds the selected path, the partial file, the original HTTPS URL,
the size and a strong ETag in the profile history. Startup checks the
partial file against its stored SHA-256 prefix digest. A restored
download waits for the user to resume it. Resumption checks the range
response and reserves the same destination without overwriting a file.
Private contexts keep no download history.

The local macOS verifier ran the user save flow in a disposable persistent
profile over HTTPS. It waited for the first committed checkpoint, killed
the browser, and confirmed that a corrupted partial restored no item and
made no new request. With the committed prefix restored and unverified
bytes appended beyond the expected file size, the browser discarded the
tail, issued a range request with the saved ETag, and published the exact
payload at the selected path with native quarantine. The user-save and
Workspace crash recovery suites also passed.

```sh
python3 tools/verify_content_downloads.py --user-resume-after-crash --require-quarantine \
  'path/to/Zitter Content Lab.app'
```

The Developer ID rehearsal passed the HTTPS user save, media, quarantine,
crash recovery, and reputation suites with disposable profiles. The
production identity admits this standalone probe only with an isolated
temporary root and loopback HTTPS fixture. Negative checks rejected a
non-loopback URL, an invalid root, and a certificate override. The
browser's signature stayed valid throughout the tests.

The separate `--user-save --native-picker --require-quarantine` trial
used the macOS save sheet in the Developer ID app. It verified the
suggested directory and filename, saved under the newly entered
`native-save.txt` name with matching bytes and quarantine, then
cancelled a separate save without leaving a file. Both transfers used
the isolated HTTPS fixture.

The interactive verifier requires the Content Lab bundle identity and
rejects bundles configured for ordinary startup before launching them.
The browser applies the same identity restriction to the interactive
probe. Prepare the signed lab with `notarize_app.py --content-version
<version> --prepare-only` and the audited license path, omitting the
production identity options. The Developer ID lab repeated both
save-panel cases with this restriction in place.

Signed argument-free startup in a disposable account and live provider
verification remain cutover checks. The signed download probe and
native save test postdate the notarized 0.1.4 DMG.
The transport probe retains its exact-URL and temporary-directory restrictions.
A relocated snapshot of the retained non-component lab bundle passed
all seven transport cases with quarantine required. Its separate
`--user-save` run exited before issuing the first HTTP or HTTPS request.
The retained framework contains the generic save-probe switch but lacks
the current user-save action strings and native save-panel marker.
The staged sources for the ongoing build match the migration worktree.
The completed integrated arm64 build passed the `--user-save
--require-quarantine` suite with the simulated save dialog. A relocated
copy of the integrated Content Lab bundle then passed native AppKit
Save and Cancel. Save produced the selected fixture filename with the
expected contents and quarantine. Cancel left no file.
The verifier reports the exit status and observed requests on failure.

The unchanged 0.1.1 Developer ID arm64 candidate passed separate Work
and Personal download history checks with native quarantine. Dark Metal
checks covered resumption after a normal restart and a forced process
exit, discarding a saved checkpoint, both choices in the quit dialog,
confirmed file deletion, and clearing history by profile and period.
The notarized 0.1.3 app repeated the Work and Personal history, native
quarantine, restart resumption, and checkpoint discard fixtures in dark
Metal. The verifier copied the app from the final image and retained
its Developer ID signature.

Use `--workspace-restart` to run restart and discard through the
Workspace entry point on a production-identity candidate. The combined
`--resume-after-restart` suite also exercises the standalone private
download probe, which the production identity refuses. The ad hoc lab
bundle separately passed all three publication binding cases: rewrite,
replacement, and symbolic link substitution.

### Download URL reputation

The disposable download probe can require a Safe Browsing V5 URL
verdict before selecting or reserving the destination. It uses Chromium's
canonicalization and SHA-256 URL expressions for every hop, deduplicates
the four-byte hash prefixes, and checks returned matches against the full
hashes locally. It sends the prefixes to an explicitly configured HTTPS
loopback endpoint with credentials and redirects disabled. The response
cache stays in memory in the private browser context.

Exact matches for malware, malicious binaries, phishing, unwanted
software, and potentially harmful applications block the download. A
prefix collision alone allows it to proceed through the existing type,
path, size, and quarantine checks. An unclassifiable match, malformed
response, invalid cache duration, timeout, or unavailable service blocks
target selection. Chromium can stage bytes before the target verdict.
The probe checks that the destination is unpublished during lookup and
that rejected downloads leave no file behind.

```sh
python3 tools/verify_content_downloads.py --reputation --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The linked macOS app passed all 15 lookup cases, including malicious
matches on both redirect hops, a same-prefix collision, transport and
protocol failures, and resumed transfers with and without a cached
verdict. The fixture checked the prefixes for both hops and confirmed
that a cookie present on the download origin, the referrer, the raw
URLs, and API credentials were absent from the lookup. Allowed files
received native quarantine. Transport, restart recovery, and Workspace
download history regressions also passed. The modified embedder
translation units cross-compiled to Windows x64 COFF objects. Native
Windows runtime coverage remains pending. The macOS graph still has
13,603 labels and zero `//chrome` labels.

Ordinary macOS startup reads the existing Zitter key file on a worker
sequence. Configure it at `~/.config/zitter/safe-browsing-api-key` or
set `ZITTER_SAFE_BROWSING_API_KEY_FILE` to an absolute path. Use a
private file with mode `600`. Each persistent profile uses a separate
memory cache. A valid key file enables lookups to the fixed Google Safe
Browsing V5 endpoint. The request contains the hash prefixes and the
API key, and omits cookies and referrers. Treat raw NetLog captures as
private because the request URL includes the key. Private windows and
disposable install fixtures skip the provider.

Without a key, regular downloads use the existing local checks. A
configured provider failure blocks the download. Live verification and
provider eligibility for the intended distribution remain release
checks. Google limits its public Safe Browsing API to non-commercial use.
After wiring the provider, the locally relinked macOS bundle passed the
loopback reputation and user-save quarantine suites. The dependency audit
reported 13,755 labels with no `//chrome` labels. Ninja declared only the
Chromium version and branding metadata as inputs under `chrome/`.
[Safe Browsing V5 hash lookup](https://developers.google.com/safe-browsing/reference/rest/v5/hashes/search)
[Safe Browsing API usage terms](https://developers.google.com/safe-browsing/reference)

### Download artifact reputation

The disposable Content host can require a separate file verdict after
Chromium saves all bytes and before it releases its final publication
callback. The local catalog contains `allow` or `block` entries keyed by
lowercase SHA-256 digest. A blocking worker reads the bounded catalog.
For an allow entry it opens the staged file and checks its exact size
and SHA-256 against Chromium's final digest. This check also covers a
transfer completed through a range request. Unknown digests, ambiguous
or unreadable catalogs, and mismatched staged bytes interrupt the item,
discard its partial state, and prevent resumption. A known blocked
digest is reported separately as dangerous content. The check sends no
data over the network.

```sh
python3 tools/verify_content_downloads.py --file-reputation --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
python3 tools/verify_content_downloads.py --reputation --require-quarantine \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The macOS app passed ten artifact cases. A relocated snapshot of the
retained non-component lab bundle also passed all ten with quarantine
required for the allowed downloads. The verifier changed a staged
file during the transfer and independently served a different file
under the same URL, filename, and size. Both attempts left the
destination empty. Three combined HTTPS cases verified that either
verdict can block publication independently and that a file with both
verdicts can complete with native quarantine. The confirmed-deletion
regression passed on the macOS compositor. The changed translation
units also compiled to Windows x64 COFF objects. Native
Windows runtime coverage remains pending. The GN graph has 13,603
labels and zero `//chrome` labels.

The local catalog verifies the completion gate. Production still needs
an authorized, maintained source of artifact verdicts and a policy for
ordinary destinations. Native Open and Reveal runtime coverage also
remains pending. New transfers remain restricted to disposable fixtures.

### Revalidation at final publication

On the download file sequence, the Content-only publication patch
records Chromium's final byte count and SHA-256, then reopens and
rehashes the completed file immediately before the no-replacement
move. Invalid file type, unexpected hard links, or changed bytes
interrupt publication. The same check runs for a resumed transfer.

On Windows the destination directory and source file are opened as
handles. The source handle excludes writers and renames, and Windows
renames that same handle with replacement disabled. On macOS the
worker opens the source relative to the destination directory
descriptor, checks its device, inode, and change time again, and
calls `renameatx_np` relative to that descriptor with `RENAME_EXCL`.
A same-user process that replaces the source between the final
metadata check and the path-based rename remains outside the macOS
guarantee.
[Windows handle-based rename](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info)
[Darwin rename reference](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/man/man2/rename.2)

```sh
python3 tools/verify_content_downloads.py --publication-binding \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The disposable verifier changes the file after the catalog returns an
allow verdict and before Chromium starts final publication. It tests a
same-length rewrite and leaf replacement. On macOS it also replaces
the leaf with a symlink and checks that the link target stays intact.
All three cases blocked publication and left the destination empty in
the linked macOS app. Destination collision, artifact reputation,
restart recovery, and save picker regressions also passed. The changed
Chromium and embedder units compiled to Windows x64 COFF objects.
Native Windows runtime verification remains pending. The GN dependency
graph contains 13,603 labels and zero `//chrome` labels.

## File chooser

The macOS lab window and the Windows and macOS Views hosts now use
Chromium's native file dialog for read-only file selection. The handler
supports single files, multiple files, and folder upload. Folder enumeration
runs off the UI thread and excludes symbolic links and Windows reparse
points. Content remains responsible for granting read access to the
initiating renderer. The host drops results if the original document
navigates away or its tab becomes hidden.

Launch the rebuilt app with `--zitter-content-file-chooser-smoke-test` in a
GUI session on each platform. Use a disposable folder containing two small
text files with distinct contents, a nested directory, and a symbolic link
or junction pointing to a separate disposable folder.

- Select one text file and confirm the page reports its name and contents.
- Select both files at once and confirm both appear. Open a fresh picker,
  cancel it, and confirm it reports cancellation without a new selection.
- Select the folder and check the relative paths of real files, including
  nested files. No contents or paths from the link target should appear.
  Drag the same folder onto the folder input and repeat the check.
- Click **Replace document**, then open a picker within three seconds. If
  the dialog is still visible after the reload, select a disposable file.
  The fresh page must receive no result.

The linked macOS lab passed native single and multiple selection using
disposable text files. The page reported each expected name and content.
Canceling a new dialog reported cancellation and preserved the previous
selection. Selecting the test directory returned its three real files
with relative paths, including the nested file. The renderer received
neither the directory symlink nor the file in its external target.
Opening the dialog just before a timed reload dismissed it without
delivering a new selection. The probe reloads the same URL and may
restore form values, so this check does not independently verify the
new document's identity.

### Profile-scoped starting directory

The native dialog starts in the current profile's most recent successful
selection directory. A file selection remembers the parent of the first
file and a folder selection remembers the folder itself. The host checks
the directory on a blocking worker before opening the dialog. A new profile
or a missing directory starts in the user's home directory. Cancellation,
navigation and drag-and-drop enumeration do not change the preference.

Persistent Work and Personal store their paths in separate `Content File
Chooser Preferences` files. Private contexts keep the directory in memory.
Invalid preference files are left untouched and the context can still
remember a selection for that session. Network-style and relative paths
are excluded from remembered state.

In the macOS Workspace trial, Work and Personal selected files from two
disposable directories and the loopback page displayed the expected file
contents in each space. Personal's first dialog started at Home after Work
had selected a file. Both profiles reopened their own folder after a process
restart, and canceling the reopened dialogs preserved both preference files.
The selection ran in the light compositor and the reopen in dark Metal.
The Workspace session and encrypted profile regression passed on the
same component framework.

Drag-and-drop folder enumeration, native Windows dialogs, and private
picker restart behavior still need runtime coverage. Production picker
policy, the File System Access API, and the remaining capture capabilities are separate gates.

## Fingerprint protection

Both browser hosts use the same in-memory fingerprint session implementation
and Blink readback hooks. A session belongs to one BrowserContext, including
private contexts. Its random key is never written to disk. The Chrome
host carries the key in RendererPreferences. The Content host carries
it in a dedicated Mojo message. An empty key disables protection. The Content workspace uses the
existing switch in Settings → Privacy for the active profile. New profiles
default to off. Private contexts can toggle protection in memory. Durable
profiles keep only the boolean in their own `Content Fingerprint Preferences`
file. Before creating any pages, the host loads this setting or uses the
boolean from the copied legacy `Preferences` when there is no explicit
Content setting. The legacy file is left intact. Malformed or linked
fingerprint preferences prevent durable workspace startup.

The standalone staging command applies the guarded Blink changes without
reading or modifying Chrome's preference registration. Stage them after
any active Chromium build exits. Upgrading an older Content checkout
removes the exact legacy fingerprint additions from the renderer
preferences headers and Mojo definitions. This schema change triggers a
one-time rebuild. Staging the same bytes again preserves their mtimes.

On macOS, run the shared readback fixture after staging and building.

```sh
python3 tools/verify_content_fingerprint.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The probe uses one private context and one loopback origin. It checks
canvas serialization and pixels, WebGL readback and offline audio with
protection off, enabled, rotated and then disabled again. Two enabled
navigations must agree before rotation, and the final sample must match
the initial unprotected sample. It also checks private profile cleanup.
The linked macOS runtime probe passed.

```sh
python3 tools/verify_content_fingerprint.py --workspace \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

The workspace probe starts from a synthetic legacy Work preference file
and opens Settings → Privacy in both profiles. It compares canvas, pixel,
WebGL and audio results across Work and Personal on the same site and across
two sites in Personal. It enables protection in Personal, disables it in
Work, closes the workspace, and verifies that the switch values survive
while the protected readback changes with the new session key. The verifier
also checks that the legacy file is unchanged and that malformed or linked
Content preferences are rejected. Use `--theme dark` and `--renderer metal`
to cover the other shell configurations. The linked macOS app passed in
light and dark with both the compositor and Metal. The macOS dependency
graph still contains 13,602 labels with zero `//chrome` labels.

### Fingerprint reads in a separate renderer

```sh
python3 tools/verify_content_fingerprint.py --workspace --cross-process-frame \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

This mode forces site isolation and embeds a loopback frame on a
different site. The browser checks that the frame committed in a
different renderer process. The frame samples canvas, WebGL and
offline audio in its first readback script. A same-origin iframe
also runs the first read in its initial `about:blank` document.
Both frames and the parent must produce the same result for each
state because noise is partitioned by the top-level site. The
fixture resamples those same documents after the toolbar rotates
or disables the key. The verifier rejects a run if the server
receives an additional top-level request during either update.
The workspace probe still checks different profiles, different
top-level sites, and a new session after restart.
The relocated macOS app passed in both themes on compositor and Metal.
The single-page regressions also passed. Negative controls rejected
a modified canvas sample and a frame that kept reporting its
pre-rotation fingerprint.

The first protected Work sample also opens a popup through the
normal user-gesture path. Its first readback runs in the initial
`about:blank` document and must match the opener. The browser checks
that the popup has separate WebContents on the same profile and
renderer process, then closes it and returns to the opener before
continuing the workspace samples. This passed in both themes on
compositor and Metal. A modified popup sample failed the test,
and the Workspace popup routing regression passed.

The browser-to-renderer channel must order the initial key before
document execution, including a cross-site commit in a fresh process.
It must also handle renderer-created initial documents and apply
rotation and disablement to already-open documents. At the pinned
revision, `RenderFrameImpl::CreateNewWindow` copies the opener's
RendererPreferences into the popup before returning it to script.
A replacement transport needs to preserve that synchronous behavior.

### Associated Mojo fingerprint transport

The Content lab uses a dedicated fingerprint interface. The browser sends the profile key
through the frame-associated channel at frame creation and before a
navigation commits. It also updates the live frames when the profile
rotates its key or changes the setting. Blink keeps the latest key in
renderer process memory and ignores older sequence numbers. The
initial popup can read the existing key in its opener's renderer
synchronously. Chromium ties a renderer process to one BrowserContext
and the standalone host rejects single-process mode.

```sh
python3 tools/verify_content_fingerprint.py --workspace --cross-process-frame \
  '.chromium/src/out/ZitterContentBundle/Zitter Content Lab.app'
```

The dedicated interface is the default in Content staging. Content
keeps the adblock scriptlet preference and removes the old fingerprint
field and traits. Chrome staging keeps its existing fingerprint transport.
The rebuilt default configuration passed GN include checking. Its
standalone graph contained 13,731 dependency labels and zero under
`//chrome`. The relocated macOS app passed the cross-process
fingerprint probe in both themes and both renderers, and also passed
the single-page fingerprint, scriptlet and full bundle verifiers.
The browser-side profile adapter cross-compiled to AMD64 COFF during
the transport trial. The renderer client cross-check reached the
existing Windows printing Mojo header gap. Native Windows verification
remains open.

The relocated, signed macOS Content 0.1.4 DMG also passed the cross-process
Workspace probe with a disposable root in light/compositor and dark/Metal.
It exercised the initial popup, site and profile isolation, live key
rotation without a reload, and the Work and Personal settings after restart.

### Fingerprint toolbar and manual rotation

The Chrome and Content hosts share the Views fingerprint button and popover.
The Content popover shows the active page's site code and can rotate the
active profile's session key, disable protection, or open Privacy settings.
Switching profiles closes the old popover. The durable profile button is
visible while protection is enabled. Off-the-record contexts keep the button
available and can enable protection for that session.

The same workspace verifier presses the toolbar button and its menu actions.
It checks that the displayed code changes on rotation, that the next readback
on the same site uses different values, that switching profiles closes the
popover, and that turning protection off removes the durable profile button.
The macOS screenshots were reviewed in light and dark with the compositor and
Metal. The shared toolbar and Windows workspace code passed a Clang x64
syntax check against the Windows SDK. The Chrome adapter passed a Clang
syntax check with the overlay header and generated API headers. A full
Windows build and runtime checks and the Chrome host's native browser tests
are pending.

The relocated non-component app passed the canvas, WebGL and audio probe.
Its Workspace probe exposed stale view references when closing the toolbar
popover. The panel now stops its animation timer and releases the view
references before destroying the widget. The Workspace probe passed
after the fix in both themes with the compositor and Metal.

The notarized 0.1.1 arm64 app passed the separate-renderer Workspace
probe in dark Metal with its Developer ID signature intact. Canvas,
WebGL, and offline audio agreed across the parent and child renderers
for each protection state. The profile setting survived restart and
the protected readback changed with the session key.

## Adblock scriptlets, request filtering, and cosmetics

The standalone renderer runs Zitter's existing scriptlet executor from
Content's document-start callback. It consumes the same per-tab script
list as the Chrome renderer and runs matched scripts in the page's main
world. Both hosts use the same vendored adblock-rust target and Chromium
registrable-domain resolver. Standalone staging retains the adblock
scriptlet field in RendererPreferences.

On macOS, run the loopback scriptlet fixture after staging and building.

```sh
python3 tools/verify_content_scriptlets.py \
  '.chromium/src/out/ZitterContent/Zitter Content Lab.app'
```

A private profile receives a small synthetic filter list parsed by adblock-rust.
The first inline script in the main page and in an inherited-origin
`srcdoc` iframe must see the value set by the scriptlet. A sandboxed
iframe with an opaque origin must see no value. The same profile wraps
Chromium URLLoaderFactory instances with the filter shared by both hosts. It tests a blocked fetch, an exception and a local
script stand-in. The loopback server must see the document and the allowed
fetch, with no request for the blocked URL or the stand-in. The fixture
checks temporary profile cleanup. The probe uses fixed rules and never
downloads lists or changes production filter settings.

The Content host runs the shared cosmetic script in Zitter's isolated
world. Site-specific selectors and procedural actions are applied after
commit. Generic selectors are looked up from classes and IDs reported on
load and during a delayed rescan. Results are bound to the initiating
document. The fixture checks hidden and removed elements, an exception,
a delayed element, and the absence of isolated-world state in the page's
main world.

The linked macOS Content browser passed the document-start, inherited
frame, opaque frame, network, and cosmetic checks. The fixture anchors its
network rules to the full loopback URLs, including the ephemeral port, and
checks that the engine recognizes them before creating a page. Windows
validation remains open. The durable profile settings, rule editor, and
filter list lifecycle are described below.

The isolated macOS build passed all 15 Rust engine tests, including
loopback scriptlets, cosmetic rules and exceptions. The combined staging tests passed.
GN checked the standalone graph with 12,598 dependency labels and no
`//chrome` labels. Clang syntax checks passed for the renderer hook,
browser support and macOS probe.

A local Mojo harness exercised the shared factory against a fake downstream
network factory. It confirmed client-side blocking, a local response body,
a blocked completion when the replacement is invalid, forwarding of an
allowed request, and bypass of main-document requests. The linked browser
probe confirmed that the loopback server received the document and allowed
fetch without receiving the blocked fetch or the replaced script.

A headless Chromium run with a disposable profile executed the actual
cosmetic script in an isolated world. It checked site and generic hiding,
procedural text matching, element removal, delayed scanning and isolation
from the main world. The linked macOS probe subsequently passed the
cosmetic checks through the standalone browser's navigation and scan
scheduling. The relocated non-component package also passed the scriptlet
timing, request filtering and cosmetic probes.

The local arm64 lab app passed the scriptlet timing and private
Workspace suites with the current Content code. The unchanged 0.1.1
Developer ID app separately passed the back/forward cache probe in
light and dark with compositor and Metal. The probe verified that
restored parent and cross-site child documents use current cosmetic
rules without fetching their HTML again. The relocated signed
Content 0.1.4 DMG passed the same check in all four combinations of
theme and renderer, using a separate disposable profile for each run.

### Durable profile rules

The signed macOS Content 0.1.4 app passed the storage verifier's
`--adblock --replace-app` fixture using a copy of the installed Chrome
app and disposable profiles with a mock Keychain. LaunchServices opened
the signed replacement from the same temporary installation path. The
test covered document-start scriptlets, cosmetics, request blocking,
profile exceptions, and settings across restarts. It also verified that
the signed Chrome app and its test data were readable after rollback.

Work and Personal each load their own ad blocking engine before creating
pages. The existing settings switch, Zitter stand-ins, and shared rule
editor update that profile only. The request factory reads the current
switch and exception list. Existing tabs receive updated scriptlet
preferences. Cosmetic filtering evaluates the current policy at navigation
and updates loaded documents when the engine or profile policy changes.
A site exception bypasses blocking for that top-level page,
including its child-frame scriptlets.

The offline profile importer copies the master switch, site exceptions,
custom rules, and list switches into the matching Content profile. It
validates the stored shape and bounds before publishing either profile.
Content applies the built-in and optional list switches from the same
profile preferences.

```sh
python3 tools/verify_content_storage_migration.py --adblock --shared-keychain \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
```

The relocated macOS app passed the fixture in light and dark with
compositor and Metal. The fixture checks filtering before the first inline
script, cosmetic hiding, two blocked fetches, and a Personal site exception
on the same origin. It adds a Work rule in the shared editor, changes the
Work switch through Settings, and checks both profiles after restart and
legacy rollback. It also checks that a committed HTTP 404 document appears
only as a hidden history row. The private scriptlet probe and the workspace
navigation regression passed. GN still lists 13,590 dependencies with no
`//chrome` entries.

Native Windows runtime coverage remains open.

### Site shield

The Workspace header shows the shield when the active profile has ad
blocking enabled. The panel uses the shared toolbar menu and opens the
existing ad blocking settings. Its site action updates the current
profile's exception and reloads the current tab. The action is bound to
the initiating document, and the panel closes when the tab or document
changes. When a broader domain exception already covers the site, the
panel directs the user to Settings to edit that exception at its source.

The relocated macOS app passed the shield test in light and dark with
compositor and Metal. The verifier activates the button and site action
through Views accessibility. Each Work run checks blocking, adds the
exception, checks the bypass after reload, removes the exception, and
checks filtering again. The Personal exception on the same origin
survives each run. The verifier checks the persisted preferences on disk
and reopens the disposable legacy profiles for rollback.

Both browser hosts share the shield button and summary card. In Content
the count combines blocked network requests attributed to the primary
page and hidden elements reported by the main document's cosmetic scan.
The network factory carries Chromium's page source ID from creation.
A block only increments the count while that ID matches the active
document. Requests without an attributable page are filtered according
to the same policy and omitted from the count. A new document starts at
zero, and a restored back-forward cache page keeps its count.

In all four appearance combinations the relocated macOS app reported
exactly three in Work, from two blocked fetches and one hidden element.
The shield displayed the disabled state for the Work exception and for
the separate Personal exception. Removing the Work exception produced
three again after reload. The scriptlet, network, and cosmetic regression
probe also passed. The Chrome toolbar adapter passed a Clang syntax
check with the shared controls. A linked Chrome-host regression remains
open.

#### Recent blocked requests

The Content shield has a disclosure for the last five blocked network
requests attributed to the current page. The page owns this list in
memory, using the same Chromium page source ID as the request counter.
The addresses omit query parameters, fragments, and credentials, and
each displayed value is bounded. Cosmetic matches contribute to the
shield count without adding a request row. Changing the document
closes the shield. A profile exception shows an empty request list.

The Work and Personal migration probe opens the disclosure through Views
accessibility. It checks that the labels fit in the panel, that Work
shows both blocked requests without the fixture's query value, and that
Work's temporary exception and Personal's separate exception both show
an empty list. The `--initial-appearance` option on the `--adblock`
migration command selects the theme and renderer used for the interactive
part of the trial. All four combinations passed in the relocated macOS
package, along with the independent scriptlet, network filtering, and
cosmetic regression. GN include checking passed. The profile and
Workspace modules compiled to Windows x64 COFF. Native Windows execution
remains open.

#### Temporary rule trials

The profile rules editor offers **Try on tab** for one network or cosmetic
rule with an explicit site scope. It builds a temporary engine from the
current profile's lists and custom rules and reloads the selected tab.
Other tabs continue to use their profile engine. The shield offers
**End rule trial**, which restores regular filtering and reloads the tab.
The trial also expires when the tab closes, the filtering settings change,
or the main frame commits to another origin. Trial documents are excluded
from the back/forward cache so a return through history loads a fresh page.
The trial has no preference or session record.

Document-start scriptlets remain available in the ordinary profile rules.
Their renderer preferences can be inherited by new windows, so scriptlet
rules are excluded from this trial path. Assistant-driven rule repair and
its preview and acceptance flow remain separate parity work.

The Work and Personal migration fixture activates **Try on tab** through
Views accessibility for a network rule and a cosmetic rule. It observes
whether the request reaches the loopback server and checks the sponsored
element's computed visibility. For each rule, it verifies the effect after
reload, ends the trial through the shield, then repeats the trial and
returns through back history after leaving the origin. The normal request
and element visibility return, and both trial rules stay out of the
copied preference files. Use `--adblock --shared-keychain --initial-appearance`
with `tools/verify_content_storage_migration.py` to select the theme and
renderer. All four combinations passed in the relocated macOS app.
The independent profile filtering regression covers document-start
scriptlets. GN include checking passed. The profile adapter, rules panel
and Workspace module compiled to Windows x64 COFF. Native Windows
execution remains open.

### Temporary Workspace filtering

The Content Workspace without a persistent root creates an in-memory ad
blocking preference store for each private context. It uses the shared
engine, site shield, rules editor, and list service. The list service
starts fetching missing public lists when the Workspace opens. The cache
belongs to the temporary Workspace directory, and the preference store
has no backing file. A fresh session starts with default preferences.

```sh
python3 tools/verify_content_scriptlets.py --private-workspace \
  'path/to/Zitter Content Lab.app'
```

The relocated macOS verifier launches four independent sessions on the
same loopback origin, covering both themes with both renderers. Each run
uses the normal list service with loopback publishers, confirms a
scriptlet before the first inline script, hides one element and blocks a
request. It creates an exception through the shield, verifies the reload
bypass and confirms that the other private context still filters. The
temporary directory is empty after each exit, and the same origin filters
again on the next launch.

The signed Content 0.1.4 DMG rejected both the standalone scriptlet probe
and the private Workspace ad-block probe at startup. Its production
identity guard requires an isolated Workspace trial. The durable-profile
fixture above verified document-start timing on the signed bundle.
A relocated snapshot of the retained non-component lab bundle passed both
the standalone scriptlet fixture and the private Workspace fixture in all
four theme and renderer combinations. The completed integrated arm64
build also passed both fixtures with the same coverage.

### Private windows from a regular Workspace

The private-window shortcut and the macOS File menu open the shared
Workspace shell in private mode. The native item has no key equivalent
of its own, so Workspace shortcut settings continue to determine the
binding. The first window selects the regular profile that supplies the
session's defaults. Further private windows share the same off-the-record
browser context and retain separate tab layouts. Closing the last tab of
a private window closes that window. Closing the last private window
destroys the context and schedules its temporary directory for removal.
Workspace quit includes open private pages in its before-unload check
and counts ongoing private downloads once for the session.

Private ad blocking preferences use Chromium's overlay store with
in-memory writes. Both ad blocking and fingerprint protection follow the
opening profile until changed in the private session. Private overrides
and the fingerprint session key stay in the private context. The ad
blocking engine uses the regular Workspace's public filter list cache.
Clipboard prompts are routed to the private window displaying the
requesting document.

```sh
python3 tools/verify_content_workspace_session.py --private-windows \
  'path/to/Zitter Content Lab.app'
```

The relocated macOS app passed with the compositor and Metal in both
Workspace themes. The verifier resolves the configured shortcut for the
first window and dispatches the native File menu action for the second.
It checks cookie and localStorage sharing with a loopback origin. It verifies inherited ad blocking, document-start scriptlets,
and fingerprint switches, then changes the regular settings and checks
that private overrides leave regular preferences intact. The second
window retains its state after closing the first. After closing the
last tab and opening from Personal, the new session starts without the
old data and uses Personal's ad blocking setting. No private cookie
appears in the regular databases, and the temporary root is empty after
exit.

Native Windows execution and broader private window interactions with
site permissions and downloads remain unverified. The wider macOS menu
migration remains open.

### Downloaded lists and cache

Both browser hosts use the shared filter-list service. Content opens a
separate in-memory network context with Chromium certificate verification.
Requests omit credentials and reject redirects. The service fetches
required lists and optional lists selected by an enabled profile. It
checks for missing or stale lists after startup and then daily. Settings
can request an immediate refresh.

The profile importer verifies and copies the known cache files and keeps
their modification times. Startup reads the cache and compiles each
profile's engine on background sequences before loading restored tabs.
A refresh bounds each complete list to 16 MiB, verifies its header and
minimum rule count, and resolves only local sibling include names.
The service publishes a complete file atomically and rebuilds both profile
engines using their respective list switches. Failed downloads and
includes leave the previous list active.

```sh
python3 tools/verify_content_storage_migration.py --filter-lists --shared-keychain \
  /Applications/Zitter.app 'path/to/Zitter Content Lab.app'
```

The relocated macOS app used the imported cache, fetched required lists
and an optional list with a sibling include, and applied the optional
rules only to Work. The fixture included a response larger than 5 MiB.
The loopback server received no Cookie, Authorization,
or Referer header from the list requests. A restart retained the refreshed
rules after an invalid list, a redirect, and a missing include. The
verifier compared the cached bytes and checked legacy rollback. Publisher
roots were checked separately over HTTPS. Native Windows runtime and a
native Chrome-host regression build remain open.

### macOS parity with Zitter 0.4.3–0.4.10

The Chrome-backed macOS build opened the browser import panel on a fresh
profile. The full welcome wizard and its password CSV option were Windows
only. The Content macOS app now shares the wizard, including assistant setup,
browser import and the starter space's name and color. On macOS the default
browser step directs the user to System Settings. Imported browser spaces are
added alongside the existing spaces. A completed wizard does not appear on
subsequent launches. The wizard and the password manager both offer CSV
import. The parser runs in a sandboxed utility process, and the active
profile stores the new logins without replacing existing entries.

The seven browser sources (Arc, Chrome, Edge, Brave, Firefox, Opera and
Vivaldi), clean address copying, workspace favicon refresh, page zoom and
Night Shift settings are wired into Content. The macOS updater continues to
use a signed DMG; the Windows installer, taskbar integration and installer
update flow remain platform specific.

The startup import regression check uses an isolated profile and synthetic
browser data. It imports with Night Shift enabled, when publishing a profile
writes to the new profile's extension storage, and checks that the imported
profiles and spaces survive restart. It also checks that CSV import skips a
duplicate and a non-web URL and stores the accepted login encrypted. The
production bundle verifier checks that the app declares and contains the
Zitter icon.
