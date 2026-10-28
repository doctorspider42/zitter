# WorkspaceAccent

Każdy obszar roboczy ma własny kolor, który barwi **całe okno** — ramę, pasek boczny, hover i linie — oraz akcent kontrolek. Sześć kolorów: `violet`, `coral`, `sky`, `amber`, `rose`, `mint`.

**Daje konsument:** `data-accent="<n>"` na oknie (`<html>` lub korzeniu chrome) dla aktywnego obszaru; na pojedynczym elemencie (kropka obszaru, kafelek) — żeby pokazał kolor innego obszaru. Bez `data-accent` okno jest niezabarwione (neutralne `bg-*`), a akcent fioletowy.

`bundle.css` podmienia pod `[data-accent]`:

| Zmienna | Z tokenu | Co barwi |
| --- | --- | --- |
| `--bg-void` | `accent-<n>-void` | Rama okna wokół paska i strony |
| `--bg-base` | `accent-<n>-base` | Pasek boczny, nagłówek, strony wewnętrzne |
| `--bg-hover` | `accent-<n>-hover` | Hover i wciśnięcie na zabarwionym tle |
| `--line` | `accent-<n>-line` | Linie podziału na zabarwionym tle |
| `--accent` | `accent-<n>` | Wypełnienia: primary, przełącznik, kropka obszaru, pasek ładowania, ring edycji adresu |
| `--on-accent` | `on-accent-<n>` | Tekst i ikony na `--accent` |
| `--accent-soft` | `accent-<n>-soft` | Zaznaczony wiersz, wciśnięta ikona |
| `--accent-ink` | `accent-<n>-ink` | Akcent jako tekst lub cienki znak |

- **Nie barwi się:** `bg-raised` (strona, aktywna karta, Command Center, menu, pigułka adresu) — to ona odcina się od kolorowego okna. Tekst (`ink`, `ink-muted`), statusy, `focus` i `zing` też zostają.
- Odcienie są wyliczone w OKLCH z barwy akcentu: Noc L 0.18/0.23/0.30, Papier L 0.885/0.945/0.90 (void/base/hover), przy umiarkowanym nasyceniu. `ink-muted` trzyma ≥4.5:1 na każdym z nich, `line-strong` i `focus` ≥3:1.
- Przełączenie obszaru zmienia kolory w `dur-move`.
- Koral i mięta są blisko `danger` i `ok`: statusy zawsze mają ikonę lub słowo.
- Własny kolor użytkownika: aplikacja liczy osiem wartości tą samą metodą (ten sam L i C, barwa z wybranego koloru) i ustawia zmienne inline na oknie.
