# Private browsing

Open **New Private Window** from the File menu on macOS or the Zitter menu on
Windows. The default shortcut is **Command+Shift+N** on macOS and **Ctrl+Shift+N**
on Windows. A compact mask icon and dark violet appearance identify private
windows, including when the sidebar is hidden. Hover over the icon for a brief
explanation.

All private windows in the running app share one temporary Chromium
off-the-record profile. Its cookies, HTTP cache, local storage, IndexedDB,
service workers and other site data are separate from the regular profiles.
Closing a private window leaves this session available to the other private
windows. Closing the last private window releases the profile and its temporary
data; a subsequent private window starts a fresh session. Chromium may finish
renderer teardown asynchronously, but a new session never reuses that context.

Private URLs and titles are excluded from browsing history, history suggestions,
Time Machine, recently closed tabs, saved workspace layouts and session restore.
Links, new tabs and allowed script popups retain their private browser context.
Private browsing uses a single temporary workspace. Profile creation and import
are unavailable there. Downloaded files remain on disk when the session ends.
Pins created in a private window are temporary too.

The fingerprint icon remains available in private windows even when protection
is disabled in the regular profile. Its popover and **Settings → Privacy** let
you turn protection on or off for the shared private session. This setting is
temporary and does not change the regular profile.

The AI assistant, its background pages and scheduled tasks are disabled in
private windows. Extensions are also disabled. Site permission choices and ad
blocking exceptions made in private browsing use the temporary profile; they do
not update the regular profile. Appearance and command usage changes in private
windows are not saved to the regular Zitter configuration.

Private browsing does not provide network anonymity. Websites, network operators,
employers and internet providers may still observe activity. Signing in on a
website identifies you to that site. Downloaded files and data deliberately
shared with websites remain outside the temporary browsing session.

## Verification

`ZitterProfilesBrowserTest.PrivateWindowsIsolateShareAndForgetSiteData` checks
cookies, localStorage and IndexedDB isolation, sharing across private windows,
popup inheritance, survival after one window closes and an empty replacement
session after profile destruction.

`ZitterProfilesBrowserTest.PrivateWindowsNeverPersistOrRestore` compares the
regular Zitter files before and after private navigation, tab closure, command
usage, appearance changes and attempted assistant writes. It also checks that
the assistant cannot be enabled, ad blocking exceptions stay isolated, private
engines receive refreshed filter lists, and a recreated regular window does not
restore the private workspace or its URL.

`ZitterFingerprintProtectionBrowserTest.PrivateControlWorksWhenRegularProtectionIsOff`
checks that the private control remains available while regular protection is
off, enables actual canvas/audio/WebGL noise in the private session, updates
both private windows, and leaves the regular profile unchanged.
