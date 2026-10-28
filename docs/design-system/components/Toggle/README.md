# Toggle

Przełącznik 34×20 do ustawień, które działają natychmiast (bez „Zapisz”).

**Daje konsument:** `aria-checked`, `aria-label` (lub etykietę obok), handler. Klasa `zt-toggle`, w wierszu `zt-field` z etykietą i podpowiedzią.

- Wyłączony: obrys 1.5 px `line-strong`, gałka `ink-muted`. Włączony: tło `--accent`, gałka `--on-accent`.
- Etykieta opisuje stan włączony („Blokuj elementy śledzące”), podpowiedź w `label`/`ink-muted` mówi o skutku.
- Nie używaj do akcji jednorazowych — to `Button`.
