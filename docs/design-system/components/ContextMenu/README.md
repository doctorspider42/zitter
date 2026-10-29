# ContextMenu

Menu pod prawym przyciskiem: kart, linków i przestrzeni; krótkie, z akcją destrukcyjną zawsze na końcu.

**Daje konsument:** wiersze (ikona, etykieta, opcjonalny skrót), separatory `<hr>`, wiersz `danger`.

- 248 px, `bg-raised`, `radius-md`, `shadow-float`, padding `space-1`; wiersz 30 px, hover `bg-hover`.
- Etykiety to czasowniki („Przypnij kartę”), bez wielokropków, chyba że otwiera się okno.
- Akcja niszcząca w `danger` po separatorze, ostatnia. Maks. 3 grupy.
