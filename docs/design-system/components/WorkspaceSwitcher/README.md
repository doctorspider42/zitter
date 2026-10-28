# WorkspaceSwitcher

Wgłębiona szyna kropek na dole paska bocznego; aktywny obszar wysuwa się z niej jako pigułka z nazwą, a jego kolor przemalowuje całe okno.

**Daje konsument:** listę obszarów (nazwa, akcent jako `data-accent` = `violet | coral | sky | amber | rose | mint`, opcjonalnie emoji), aktywny indeks, akcję „+”. Po przełączeniu ustaw `data-accent` aktywnego obszaru na oknie (`<html>` lub korzeniu chrome).

- Szyna: `bg-void` z wewnętrznym obrysem `line`, `radius-pill`.
- Każdy przycisk niesie własny `data-accent`, więc kropka zawsze ma kolor swojego obszaru, niezależnie od aktywnego.
- Nieaktywne: sama kropka 8 px (lub emoji zamiast kropki). Aktywny: `bg-raised` + `shadow-card`, kropka 10 px z pierścieniem w swoim akcencie, nazwa w `label` 600.
- Przejście szerokości i koloru `dur-move`. ⌘⌥1…9 przełącza; każdy przycisk ma `aria-label` z nazwą.
- Po przełączeniu rama, pasek boczny, hover i linie przyjmują odcień obszaru (`WorkspaceAccent`); strona i aktywna karta zostają neutralne.
