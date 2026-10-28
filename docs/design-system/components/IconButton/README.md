# IconButton

Kwadrat 28 px z jedną ikoną 16 px: nawigacja w nagłówku (wstecz, dalej, odśwież) i przełączniki paneli.

**Daje konsument:** ikonę SVG (`zt-icon`, stroke 1.6, `currentColor`), wymagany `aria-label` po polsku, opcjonalnie `aria-pressed` + klasa `on`.

- Spoczynek `ink-muted`, hover `ink` na `bg-hover`, wciśnięty (`on`) `--accent-ink` na `--accent-soft`.
- `disabled` przechodzi na `ink-faint` i traci tło. Używaj tylko tam, gdzie brak akcji jest informacją (np. „Dalej” bez historii).
- Nagłówek okna ma tylko wstecz/dalej/odśwież — nie dokładaj tam kolejnych ikon.
