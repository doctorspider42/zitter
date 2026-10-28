# CommandPalette

Command Center (⌘K): jedno pole i jedna lista akcji, kart, przypiętych, obszarów i historii.

**Daje konsument:** zapytanie, wyniki pogrupowane (`zt-eyebrow` nad grupą), zaznaczony indeks, stan ładowania.

- Panel 560 px, `bg-raised`, `radius-lg`, `shadow-float`, na przyciemnionym `bg-void`.
- Pole zaczyna się kursywną kreską ket `|` w `--accent-ink`, tekst 17 px `ink`, podpowiedź w `ink-muted`.
- Wiersz 36 px: ikona `ink-muted`, nazwa `ink`, URL w `url`/`ink-muted`, skrót w `Kbd`. Zaznaczony: `--accent-soft`, ikona i nazwa w `--accent-ink`.
- Ładowanie: trzy drżące kropki `zt-loader` (zing, akcent, zing) zamiast spinnera.
- Stopka na `bg-base` z trzema skrótami: wybierz, otwórz, zamknij.
- Kolejność grup: Akcje, Karty, Przypięte, Obszary, Historia. Maks. 8 widocznych wierszy.
