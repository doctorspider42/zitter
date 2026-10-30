# Extensions

Chrome extensions at the right end of the top band: pinned actions, the puzzle button with its menu, and extension popups.

**The consumer provides:** for each extension of the active profile: an icon (16 px bitmap), a name, a badge (text of up to 4 characters), whether it is pinned, its access to the current site (`ma dostęp`, `po kliknięciu`, `chce dostępu`, `brak`), and optionally a popup.

- Placement: the right end of the 44 px header band, on `bg-void`, sharing its vertical axis with the traffic lights and navigation, `space-2` from the window edge. Left to right: pinned icons, a 16 px `line` stroke, the puzzle. With no extensions, only the puzzle remains.
- Each icon is a 28 px `IconButton` with a 16 px bitmap in `radius-xs`; hover `bg-hover`, open popup `on`. The icon is always the one the extension set for this tab (`action.setIcon`): a password manager that signs out swaps it to grey itself, and the bar shows that at once. An extension that disabled its action on this page (`action.disable`) gets a greyed icon (full color on hover); the menu states site access in words.
- Badge: a 12 px pill in the lower right corner, `mono` 9 px 600, `ink` with `bg-raised` text and a 1.5 px `bg-void` ring. Always neutral: we ignore the extension's badge color so it never competes with the space accent. The badge text goes into `aria-label`.
- At most 6 pinned. As the window narrows they move into the menu from the left; the puzzle always stays.
- Popup: `bg-raised`, `radius-md`, `shadow-float`, 4 px below the icon, its right edge at the icon's right edge (never outside the window). The extension provides its content and size (up to 800×600). Escape and a click outside close it.
- Menu (`⌘⇧E` or the puzzle): a 296 px `ContextMenu`. An `eyebrow` header „Rozszerzenia”, with no host: each row states its site access. Rows are 44 px: a 20 px icon, the name in `body`, its access below in `label`/`ink-muted`; pin (`on` when pinned, then always visible) and „Więcej” appear on hover. An access request is `warn` with an icon and a word, plus a quiet „Zezwól” button. After a separator: „Zarządzaj rozszerzeniami” and „Chrome Web Store”.
- Right click on an icon: a `ContextMenu` with „Odepnij”, „Opcje”, „Zarządzaj”, and after a separator „Usuń z Zittera…” in `danger`.
- Extensions belong to a Chromium profile, not to a space: spaces that share a profile share the set, and switching to another profile swaps the icons without animation. A newly installed extension: one tremor (`zt-tremor`) of the puzzle, no toast.
