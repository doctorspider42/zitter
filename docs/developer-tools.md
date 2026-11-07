# Developer tools

Zitter uses Chromium's bundled DevTools frontend: Elements, Console, Sources,
Network, Application and the other upstream panels. Open it from the Developer
menu on macOS, the application menu on Windows, Command Center, or a configured
shortcut. F12 toggles the window. The platform panel shortcut is Cmd+Option+I on
macOS and Ctrl+Shift+I elsewhere; J opens Console and C starts element selection
with the same modifiers. Cmd/Ctrl+Shift+R reloads without the HTTP cache.
All bindings belong to the existing shortcut editor and can be changed or disabled.
Older saved assignments win over newly introduced defaults.

## Discovery mode

The developer menu has three persistent discovery settings:

- **Off:** hide the contextual header control.
- **Auto** (default): show it while a local project is active.
- **On:** show it on all pages.

These settings control discovery, not access to DevTools. Auto never opens a
DevTools window by itself. Only visited HTTP(S) URLs are considered: localhost,
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

Zitter pages live in WebViews rather than Browser tab strips. The pinned
Chromium `DevToolsWindow::Create` checks for a compatible Browser dock and
selects an undocked TYPE_DEVTOOLS window for these targets. Chromium owns this
window and its target lifecycle; Zitter neither embeds the frontend nor changes
the docking preferences globally. Consequently docking into Zitter's page card
is not supported. Local tools do not weaken TLS, CORS, or sandbox restrictions,
and do not enable a remote debugging endpoint.

On macOS, Zitter's app shell also handles main-window notifications from these
upstream windows. Chromium's AppController must not attach its tab, bookmark or
history menu bridges to Zitter's application menu, which has its own commands
and no Chromium bookmarks submenu. The native app-shell regression test opens,
evaluates JavaScript in, closes and reopens DevTools with the actual Zitter menu.

Settings are merged into `ZitterTools.json` with an independent startup dirty
guard. Private windows keep mode changes in memory without writing them.
