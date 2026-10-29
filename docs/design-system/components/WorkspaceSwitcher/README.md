# WorkspaceSwitcher

Wgłębiona szyna kropek na dole paska bocznego; aktywna przestrzeń wysuwa się z niej jako pigułka z nazwą, a jej kolor przemalowuje całe okno.

**Daje konsument:** listę przestrzeni (nazwa, akcent jako `data-accent` = `violet | coral | sky | amber | rose | mint`, opcjonalnie emoji), aktywny indeks, akcję „+”. Po przełączeniu ustaw `data-accent` aktywnej przestrzeni na oknie (`<html>` lub korzeniu chrome).

- Szyna: `bg-void` z wewnętrznym obrysem `line`, `radius-pill`.
- Każdy przycisk niesie własny `data-accent`, więc kropka zawsze ma kolor swojej przestrzeni, niezależnie od aktywnego.
- Nieaktywne: sama kropka 8 px (lub emoji zamiast kropki). Aktywny: `bg-raised` + `shadow-card`, kropka 10 px z pierścieniem w swoim akcencie, nazwa w `label` 600.
- Nazwę można wyłączyć (Preferencje → Wygląd → „Nazwa aktywnej przestrzeni”); wtedy aktywna przestrzeń to krążek 22 px `bg-raised` wokół kropki z pierścieniem.
- W aplikacji szyna nie ma „+”: przy nazwanej pigułce i kilku przestrzeniach nie mieści się w pasku bocznym bez przewijania. Nową przestrzeń tworzy się z panelu przestrzeni albo z Command Center.
- Przejście szerokości i koloru `dur-move`. ⌘⌥1…9 przełącza; każdy przycisk ma `aria-label` z nazwą.
- Po przełączeniu rama, pasek boczny, hover i linie przyjmują odcień przestrzeni (`WorkspaceAccent`); strona i aktywna karta zostają neutralne.
