# CommandPalette

Command Center (Cmd+K / Ctrl+K): one input and one list of actions, tabs, pins, workspaces, and history.

**Consumer provides:** the query, grouped results (`zt-eyebrow` above each group), the selected index, and the loading state.

- A 560 px panel with `bg-raised`, `radius-lg`, and `shadow-float`, over a dimmed `bg-void`.
- The input starts with an italic ket bar `|` in `--accent-ink`, with 17 px `ink` text and an `ink-muted` placeholder.
- A 36 px row contains an `ink-muted` icon, an `ink` name, a URL in `url`/`ink-muted`, and a shortcut in `Kbd`. The selected row uses `--accent-soft`, with its icon and name in `--accent-ink`.
- Loading uses three trembling `zt-loader` dots (zing, accent, zing).
- The footer uses `bg-base` and four shortcuts: select with the arrow keys, open with Enter, complete the selected URL with Tab, and close with Esc.
- Tab leaves the palette open and the caret at the end of the completed URL. It does not navigate or switch to the suggested tab. Platform clipboard shortcuts support copy, cut, paste, and select all.
- Group order: Actions, Tabs, Pins, Workspaces, History. Show at most eight result rows at once.
