# AddressPill

The address pill at the top of the sidebar shows the host in bold `ink` and the remaining path in `ink-muted`, both in the `url` typeface.

**Consumer provides:** the URL split into host and path, the security state (a lock in `ok`, `warn` for mixed content, or `danger` for a certificate error, always with an icon), and the `editing` state.

- Resting: `bg-raised` + `shadow-card`, 36 px high, `radius-pill`.
- Cmd+L / Ctrl+L switches to `editing`: a 2 px `--accent` outline, an `--accent` caret, the full URL in `ink`, and `↵` on the right.
- A 28 px link button beside the pill copies the current page URL without opening the address palette. Privacy preferences control whether copies from the button and the full address field remove known tracking parameters. This is enabled by default. Its icon is 16 px, with a tooltip and an accessible name. It is disabled for pages without an HTTP(S) URL.
- Copying the address makes the pill tremble once (`zt-tremor`, `dur-tremor`), with no toast.
- Hide `https://` and `www.` in the resting address.
