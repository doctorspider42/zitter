# Toast

Krótki komunikat na dole okna — tylko dla zdarzeń, które można cofnąć albo które przyszły z tła.

**Daje konsument:** tekst (rzeczownik pogrubiony: „Zamknięto **12 kart**”), opcjonalnie ikonę stanu, jedną akcję (`zt-btn-quiet` w `--accent-ink`), zamknięcie. „Cofnij” nie ma skrótu: ⌘Z należy do edycji tekstu na stronie.

- `bg-raised`, `shadow-float`, `radius-md`, min. 44 px, maks. 420 px, tekst `label`.
- Pojawia się z jednym drgnięciem (`zt-tremor`), znika po 6 s; najechanie zatrzymuje licznik. Maks. jeden naraz.
- Nie dla potwierdzeń drobnych akcji (skopiowano, przypięto) — tam wystarczy samo drgnięcie elementu.
- `role="status"`; błędy wymagające decyzji to `Dialog`, nie tost.
