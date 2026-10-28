# NewTab

Ekran nowej karty: data, wielkie kursywne powitanie, pole szukania i kafelki przypiętych stron.

**Daje konsument:** datę i nazwę obszaru, tekst powitania (domyślnie „Gdzie dziś”), kafelki (inicjał lub favicon, nazwa, opcjonalnie `data-accent`).

- Karta strony `bg-raised` + `shadow-card`, padding `space-12`, odstępy `space-6`.
- Nad powitaniem `eyebrow`: „Środa, 28 października · Praca”.
- Powitanie `display-xl` w `ink`. Karta strony jest neutralna (`bg-raised`), a wokół niej rama w kolorze obszaru — powitanie zawsze stoi na spokojnym tle.
- Pole szukania: pigułka 48 px na `bg-base` z obrysem `line`; ⌘L przenosi fokus do `AddressPill`.
- Kafelki 48 px, `radius-md`, inicjał w `--accent-ink`; `data-accent` na kafelku nadaje mu odcień innego obszaru; maks. 8 w rzędzie, ostatni „Dodaj”.
- Bez tapet, newsów i statystyk.
