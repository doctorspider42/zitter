# Developer tools

Zitter uses Chromium's bundled DevTools frontend: Elements, Console, Sources,
Network, Application and the other upstream panels. Open it from the Developer
menu on macOS, the application menu on Windows, Command Center, or a configured
shortcut. F12 toggles DevTools. The platform panel shortcut is Cmd+Option+I on
macOS and Ctrl+Shift+I elsewhere; J opens Console and C starts element selection
with the same modifiers. Cmd/Ctrl+Shift+R reloads without the HTTP cache.
All bindings belong to the existing shortcut editor and can be changed or disabled.
Older saved assignments win over newly introduced defaults.

## Discovery mode

The developer menu has three persistent discovery settings:

- **Off:** hide the contextual header control.
- **Auto** (default): show it while a local project is active.
- **On:** show it on all pages.

These settings control discovery, not access to DevTools. Auto never opens
DevTools by itself. Only visited HTTP(S) URLs are considered: localhost,
its dot-delimited subdomains, IPv4 loopback addresses, and IPv6 loopback. A final
DNS root dot is accepted. LAN addresses and names such as localhost.evil are not
local projects. Detection does not resolve DNS, scan ports, or make requests.

The compact header control identifies the active host and explicit port. Long
hosts are elided, with the full address in its tooltip and developer menu.
The menu offers DevTools, Console, element selection, and reload without cache.
For a local project, **Compare project in Split View** creates an independent
copy next to the focused page using the existing split lifecycle. The two pages
share the workspace profile and storage. This action is unavailable in Peek;
Peek's existing Split action can first promote it into the workspace.

## Target and lifetime

Keyboard actions target Peek when it is open, otherwise the selected tab or
focused split pane. Inspect from the page menu retains the initiating document,
frame, and coordinates, and becomes inert after that document is replaced.
Editable fields keep Chromium's native context menu, including editing,
spellchecking and upstream Inspect Element. Chromium retains its own policy
checks for opening DevTools.

DevTools opens embedded in the inspected page card by default, including Console,
Split View panes, and Peek. Use the standard **Dock side** control in the DevTools
menu to dock right, left, or bottom, or open a separate window. Drag the frontend
separator to resize the inspected page. Chromium remembers the selected dock
position per profile. The first launch after upgrading resets the formerly
forced separate-window state to docked right; later choices are preserved.

Zitter pages live in WebViews rather than Browser tab strips. A page-scoped
docking host mounts Chromium's borrowed frontend and applies its inspected-page
bounds. Chromium retains frontend ownership, docking transitions, and target
teardown. Local tools do not weaken TLS, CORS, or sandbox restrictions, and do
not enable a remote debugging endpoint.

On macOS, Zitter's app shell also handles main-window notifications from these
upstream windows. Chromium's AppController must not attach its tab, bookmark or
history menu bridges to Zitter's application menu, which has its own commands
and no Chromium bookmarks submenu. The native app-shell regression test opens,
evaluates JavaScript in, closes and reopens DevTools with the actual Zitter menu.

Settings are merged into `ZitterTools.json` with an independent startup dirty
guard. Private windows keep mode changes in memory without writing them.

## Assistant debugging

In a foreground conversation, the assistant can read recent console messages
and exceptions, inspect elements with CSS selectors, change an element's text,
classes, visibility or selected styles, and reload a tab without its cache.
Enable tab and page access, and enable browser actions for edits or reloads.
The assistant asks before each debugging action. Approval applies to the current
document and expires when that document is replaced. DOM edits are temporary and do not change
project files. Console messages and DOM details can be sent to the selected
assistant model after approval.
