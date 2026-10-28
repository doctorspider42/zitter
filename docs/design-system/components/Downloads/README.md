# Downloads

Strona pobranych: tytuł `display-l` i lista wierszy pliku z postępem, stanem i akcjami.

**Daje konsument:** dla każdego pliku: rozszerzenie (kafelek), nazwę, rozmiary, prędkość i czas, stan (`w toku`, `wstrzymano`, `gotowe`, `przerwano` + powód), akcje.

- Nagłówek `zt-pagetitle`: kursywne „Pobrane” i licznik w `url`/`ink-muted`.
- Wiersz: kafelek 32 px z rozszerzeniem w `mono`, nazwa `body` w `ink`, meta w `url`/`ink-muted`, pasek postępu 3 px (`--accent`; wstrzymany `warn`).
- Stan zawsze ikoną i słowem: `ok` ✓ Gotowe, `warn` ‖ Wstrzymano, `danger` ⚠ Przerwano: powód.
- Akcje to `IconButton`: wstrzymaj/wznów, anuluj, pokaż w Finderze, ponów. Najnowsze na górze.
- Zakończenie pobierania: `Toast` z „Pokaż” + jedno drgnięcie ikony Pobrane w pasku bocznym.
