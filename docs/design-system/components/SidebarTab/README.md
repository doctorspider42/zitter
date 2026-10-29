# SidebarTab

Wiersz karty w pionowym pasku bocznym; aktywna karta unosi się nad zabarwionym paskiem jako mała neutralna karta.

**Daje konsument:** favicon 16 px (lub kafelek z inicjałem: `--accent` z `--on-accent`, kolor wybrany przez `data-accent` na kafelku), tytuł, stany `active`, `split` (karta w grupie podziału), odtwarzanie dźwięku, akcję zamknięcia.

- Wiersz 34 px, `radius-sm`, odstęp `space-1`, padding `space-3`.
- Aktywna: `bg-raised` + `shadow-card`, tytuł w `body-strong`. Pasek boczny ma kolor przestrzeni (`bg-base` z odcienia), a aktywna karta zostaje neutralna — tak jak strona obok niej. Bez dodatkowych znaczników.
- Nieaktywne w `ink-muted`; przycisk zamknięcia pojawia się dopiero na hover.
- Karty w podziale (dwie i więcej) stoją obok siebie w jednym wierszu, w kolejności paneli: każda jako kafelek z ikoną i tytułem, aktywna uniesiona (`bg-raised`). Wiersz przesuwa się w całości. Grająca karta pokazuje głośnik w `--accent-ink`.
- Nie: kolorowe tło całego wiersza, lewa kolorowa belka, licznik kart.
