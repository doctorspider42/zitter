# Dialog

Okno decyzji na zasłonie `scrim`: tytuł-pytanie, jedno zdanie skutku, maks. dwa przyciski.

**Daje konsument:** tytuł (pytanie: „Zamknąć 12 kart?”), opis skutku i drogi powrotu, opcjonalnie `Toggle` „Nie pytaj ponownie”, akcje.

- 400 px, `bg-raised`, `radius-lg`, `shadow-float`, padding `space-6`; tytuł `title`, opis `body` w `ink-muted`.
- Przyciski do prawej: `zt-btn-quiet` „Anuluj”, potem akcja. Destrukcja: `zt-btn-danger strong`; zwykła: `zt-btn-primary`.
- Etykieta akcji powtarza czasownik z pytania („Zamknij karty”, nie „OK”).
- `role="alertdialog"`, fokus startuje na „Anuluj”, `esc` zamyka, ↵ potwierdza.
- Jeśli da się cofnąć — zrób akcję od razu i pokaż `Toast` z „Cofnij” zamiast pytać.
