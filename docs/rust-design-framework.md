# Design system w Rust UI

Stan na 2026-10-28. Moduł `design` w crate `sidebar_scene` przenosi
[design system](design-system/README.md) do Rusta. Rust jest jedynym źródłem kolorów
okna: `WorkspaceShell` pyta most cxx o paletę i nie ma już własnych wartości.

## Warstwy

| Plik | Co robi |
| --- | --- |
| [`design_tokens.rs`](../chromium/prototype/design_tokens.rs) | **Generowany** z [`tokens.json`](design-system/tokens.json): kolory neutralne (Noc/Papier), style tekstu, odstępy, promienie, cienie, czasy. |
| [`design.rs`](../chromium/prototype/design.rs) | Matematyka koloru (OKLCH, kontrast WCAG), `WorkspaceColor`, `Palette`, `motion`, `recipe`. |
| [`gen_design_tokens.py`](../tools/gen_design_tokens.py) | Generator; `--check` zwraca błąd, gdy plik Rust jest nieaktualny. |

Po zmianie `tokens.json`:

```bash
python3 tools/gen_design_tokens.py
```

## Kolor obszaru roboczego

> **Zmiana (2026-10-28, wieczór):** siatkę porzuciliśmy — kolor jest **ciągły**
> (barwa OKLCH w stopniach, nasycenie i jasność 0..1). Zapis: jasność jako L 0,60..0,76,
> nasycenie jako chroma 0..0,10. `same()` porównuje z tolerancją rozdzielczości hexa
> (2°, 3%). Test kontrastu sprawdza gęstą próbkę: co 10° × 9 × 9 = 5832 palety.
> Koło barw nie przyciąga już do siatki; opis poniżej o siatce jest historyczny.

Osoba wybiera kolor dowolnie: barwę, nasycenie i jasność. `WorkspaceColor` zaokrąglał
wybór do siatki **24 barw (co 15°, barwa OKLCH) × 5 nasyceń × 5 jasności = 600 punktów**.

- **Barwa** barwi wszystko: ramę, pasek boczny, hover, linie i kontrolki.
- **Nasycenie** mówi, jak mocno. Poziom 0 to grafit (okno i przyciski bez koloru),
  poziom 4 to chroma 0,14, czyli tyle, ile mają nazwane kolory design systemu.
- **Jasność** przesuwa tylko okno w bezpiecznym paśmie motywu. Wypełnienie przycisku
  ma stałą jasność dla motywu, więc tekst na nim zawsze ma kontrast.

`Palette::for_workspace(scheme, color)` liczy wszystkie role. Gdy kolor tekstu lub
stanu spadłby na zabarwionym tle poniżej progu, przesuwa się go po jasności (barwa
zostaje). Test `every_workspace_color_keeps_every_contrast_floor` sprawdza **wszystkie
1200 palet** (600 punktów × 2 motywy) i każdą parę tekst/tło, którą malują komponenty:

| Para | Próg |
| --- | --- |
| `ink` na każdym tle | 7:1 |
| `ink_muted`, `accent_ink`, `ok`, `warn`, `danger`, `zing_ink` | 4.5:1 |
| `on_accent` na `accent`, `accent_ink` na `accent_soft` | 4.5:1 |
| `line_strong`, `focus`, kropka obszaru na pasku | 3:1 |

Korekty w praktyce: w Nocy `line_strong` na 388 z 600 mocniej zabarwionych teł
(i `danger` w 2 przypadkach); w Papierze `ok`, `warn`, `danger` i `line_strong`
ciemnieją o odrobinę zawsze, bo ich wartości z `tokens.json` nie trzymały progu na
`bg-hover` już w neutralnym oknie, a `zing_ink` w 260 przypadkach.

**Zapis w profilu bez zmian:** profil trzyma dowolny `#RRGGBB`, a siatka działa przy
liczeniu palety (`WorkspaceColor::from_argb`: barwa i chroma z koloru, jasność z
lightness). Nie ma migracji: stare kolory i wpisany ręcznie hex trafiają w najbliższy
punkt. `WorkspaceColor::to_argb` daje kanoniczny hex punktu (jasność 0..4 jako OKLCH
L 0,60..0,76, nasycenie jako chroma 0..0,10, które każda barwa mieści w sRGB), który
czyta się z powrotem dokładnie; test sprawdza to dla całej siatki.

Presety (`WorkspaceColor::PRESETS`): fiolet, koral, niebo, bursztyn, róż, mięta, zieleń
(nasycenie 4, jasność 2) i grafit (`WorkspaceColor::GRAPHITE`, nasycenie 0), zapisywane
jako `#928FD4`, `#CC8463`, `#5AA0D0`, `#BC8F4D`, `#CB7E96`, `#55AC89`, `#7DA667`,
`#989898`.

Rodziny `accent-*` w `tokens.json` są teraz **renderingami poglądowymi**: generator je
pomija, a prawdziwe wartości liczy `design.rs`.

## Przepisy komponentów

`recipe::*` zwraca rozmiary, promienie i kolory komponentów z
[docs/design-system/components](design-system/components): `window`, `button`
(primary, zing, outline, quiet, danger, danger strong × spoczynek/hover/wciśnięty/
wyłączony), `icon_button`, `sidebar_tab` (aktywna karta to neutralna karta z cieniem),
`address_pill`, `toggle`, `workspace_dot`, `command_center` + `command_row`, `menu` +
`menu_row`, `toast`, `dialog`, `progress`, `focus_ring`, `shadow`.

`Palette::mix` interpoluje dwie palety w OKLab: przełączenie obszaru animuje się przez
`duration::MOVE_MS` (`motion::progress`). `motion::tremor_offset` to drżenie.
Oba respektują ograniczenie ruchu.

## Most do C++

`sidebar_scene.rs` wystawia `ffi::DesignPalette` (wszystkie role), `design_palette(dark,
workspace_color)` (paleta z pamięci podręcznej, raz na punkt siatki i motyw) oraz
`design_same_workspace_color(a, b)` (czy dwa zapisane kolory to ten sam punkt).

W [`workspace_shell.cc`](../chromium/prototype/workspace_shell.cc):

- `kInk`, `kMuted`, `kCard`… to już tylko **nazwy ról**. `Tone(view, rola)` bierze
  paletę dla trybu widoku (Noc/Papier) i koloru obszaru jego okna: `kInk` → `ink`,
  `kSubtle`/`kMuted` → `ink_muted`, `kDisabled` → `ink_faint`, `kCard` → `bg_raised`,
  `kAccentWash` → `accent_soft`, `kChip` → `bg_hover`, `kFrameBase` → `bg_base`,
  `kError` → `danger`. `Hairline` to `line`, `HoverWash` to `bg_hover`, `FrameTint` to
  `bg_base` palety danego koloru. Ręczna mapa jasny→ciemny i mieszanie akcentu
  zniknęły.
- Kolor obszaru jest przypisany do okna najwyższego poziomu (`widget_`,
  `sidebar_widget_`, `address_widget_`). Okna, których powłoka nie rejestruje (dymki,
  menu), biorą ostatnio użyty kolor.
- Gdy kolor obszaru okna się zmienia (przełączenie obszaru, nowy kolor),
  `ApplyAppearance` wykonuje też `ApplyTheme()`, bo zmieniają się wszystkie role, nie
  tylko rama.
- Przełącznik: tor `accent`, gałka `on_accent`. Pasek przy krawędzi: `accent`.
- 8 presetów w menu kontekstowym obszaru („Fiolet”, „Koral”, „Niebo”, „Bursztyn”,
  „Róż”, „Mięta”, „Zieleń”, „Grafit”) i w panelu wyglądu; zaznaczenie presetu porównuje punkty
  siatki, więc wpisany ręcznie hex bliski presetowi też go zaznacza. Domyślne kolory:
  praca = fiolet, prywatne = mięta.

## Testy

```bash
out/ZitterOptimized/sidebar_scene_unittests design::
python3 -m unittest tests.test_design_tokens
```

Stan na `sidebar_scene` z mostem: 517/517 testów Rust (flagi GN: edycja 2024,
`-Dwarnings`). `workspace_shell.cc` i `profile_workspace_browsertest.cc` kompilują się z
flagami GN i pluginem stylu Chromium. **Testy przeglądarkowe nie były uruchamiane**;
oczekiwania kolorów presetów w nich są zaktualizowane do nowych wartości.

Moduł nie zależy od cxx, więc da się go sprawdzić bez budowania Chromium, samym
`rustc` z toolchaina Chromium (`third_party/rust-toolchain/bin/rustc --test`).

## Dalej

1. Przepisy komponentów, etap 1 (zrobione): `layout()` bierze promienie z tokenów
   (wiersze `radius-sm`, tła `radius-md`, Command Center `radius-lg`, pigułka adresu
   = pełna pigułka); aktywna karta to neutralna karta z 1 px obrysem `line`
   (`Item::edge`, `SidebarRowStyle::edge`); przyciski biorą kolory z palety (primary:
   `accent`/`on_accent`, outline: `line_strong`, wyłączony: `ink_faint` + `line`).
   Etap 2 (zrobione): fokus to pierścień 2 px w kolorze `focus` palety wewnątrz kształtu
   kontrolki (`Item::focus`; `layout()` zamienia nim dawny pasek, kontrolki z własnym
   obrysem fokusu — checkbox, selektor, preferencje — go zachowują); pole adresu ma 36 px,
   nieprzezroczyste `bg_raised` i obrys `line`; wiersze miały już 34 px.
   Zostaje: odstęp między wierszami 2 px zamiast `space-1` (4 px; dotyka geometrii
   przeciągania), natywne menu i ramki dialogów (granica platformy, `recipe::menu` i
   `recipe::dialog` nie mają czego malować), cienie poza 1 px obrysem (scena nie ma
   rozmycia).
2. Osobne `bg_void` dla ramy okna (dziś rama i pasek boczny mają wspólne `bg_base`).
3. Picker koloru obszaru (zrobione): pod presetami trzy rzędy próbek — barwa (24),
   nasycenie (5) i jasność (5). Każdy pokazuje aktualny kolor przesunięty wzdłuż jednej
   osi siatki (`WorkspaceColor::with_axis`), klik zapisuje kanoniczny hex punktu;
   barwa wybrana na grafit przywraca pełne nasycenie.
   Potem (projekt: https://claude.ai/artifact/FrS3Q8MLtrxVtwmTWhP23j) rzędy zastąpiło
   **koło barw** — pierścień (rodzaj sceny 28) i pole nasycenie × jasność (29) — w
   `ColorWheelControl`: ciągłe kolory, uchwyt przyciągany do siatki
   (`design::wheel`: `ring_hit`, `pad_hit`, `target`, `step`), klik, przeciąganie,
   strzałki, AX jako suwak z opisem wartości. Pierścień to 96 nachodzących kół, pole
   to siatka 24 × 24 komórek z zaokrąglonymi rogami (scena nie ma gradientów).
   Panel według projektu: presety to pigułki z nazwą w siatce 4 × 2
   (`appearance_pill_layout`, pigułka = próbka z nazwą szersza niż wysoka), pola hex
   nie ma (dowolny kolor daje koło), rząd ikon kończy kafelek „Więcej emoji…” otwierający
   systemowy picker; pole emoji zostało tylko jako 1 px odbiorca, bo picker macOS wpisuje
   znak do pola z fokusem. Kolejność: kolor i ikona, potem nazwa i profil.
4. Kafelki monogramów (`kMonogramColors`) z presetów i `on_accent` zamiast białego
   tekstu w scenie.
5. Fonty Instrument Serif, Schibsted Grotesk i JetBrains Mono (OFL) w pakiecie
   aplikacji i w audycie licencji.
