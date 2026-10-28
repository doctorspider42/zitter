# Button

Pigułka z czasownikiem: jedna akcja, jedno słowo lub krótkie zdanie, zawsze w trybie rozkazującym („Otwórz w podziale”, nie „Podział”).

**Daje konsument:** etykietę (tekst, opcjonalnie ikona 16 px przed nim), `disabled`, handler kliknięcia. Klasy: `zt-btn` + jeden wariant.

| Wariant | Kiedy |
| --- | --- |
| `zt-btn-primary` | Główna akcja widoku na `--accent` obszaru roboczego (tekst `--on-accent`). Najwyżej jedna na ekran. |
| `zt-btn-zing` | Akcja „wow”: utworzenie czegoś nowego (obszar roboczy, profil). Limonka `zing` z tekstem `on-zing`. Najwyżej jedna na **całą aplikację w danym momencie**, nigdy obok `primary`. |
| `zt-btn-outline` | Akcja drugorzędna obok primary. Obrys `line-strong`. |
| `zt-btn-quiet` | Anuluj, akcje w paskach narzędzi. Kształt `radius-sm`, nie pigułka. |
| `zt-btn-danger` | Destrukcja: tekst `danger` z ikoną, nigdy pełne wypełnienie. |
| `zt-btn-danger strong` | Potwierdzenie destrukcji w `Dialog`: pigułka z obrysem 1.5 px `danger`. Tylko w dialogu. |

- Wysokość 32 px, poziomy padding `space-4` (quiet i danger: `space-3`).
- Nie: dwa przyciski `zing`, emoji w etykiecie, WERSALIKI.
