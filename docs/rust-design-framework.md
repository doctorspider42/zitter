# Design system w Rust UI

Stan na 2026-10-28. Moduł `design` w crate `sidebar_scene` przenosi
[design system](design-system/README.md) do Rusta. Rust staje się jedynym źródłem
wyglądu; C++ będzie tylko pytać o gotowe kolory i wymiary.

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

Osoba wybiera kolor dowolnie: barwę, nasycenie i jasność. `WorkspaceColor` zaokrągla
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

**Zapis w profilu:** `WorkspaceColor::index()` (0..600) zamiast hex.
**Stare profile:** `WorkspaceColor::from_argb` mapuje zapisany `#RRGGBB` na najbliższy
punkt (barwa i chroma z koloru, jasność środkowa). Dotychczasowe 4 kolory trafiają w
siatkę, a nazwane kolory design systemu w swoje presety.

Presety (`WorkspaceColor::PRESETS`): fiolet, koral, niebo, bursztyn, róż, mięta, każdy
z nasyceniem 4 i jasnością 2. `WorkspaceColor::GRAPHITE` to brak koloru.

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

## Testy

```bash
out/ZitterOptimized/sidebar_scene_unittests design::
python3 -m unittest tests.test_design_tokens
```

Moduł nie zależy od cxx, więc da się go sprawdzić bez budowania Chromium, samym
`rustc` z toolchaina Chromium (`third_party/rust-toolchain/bin/rustc --test`).

## Następny krok: most do C++

1. `ffi::DesignPalette` + `design_palette(dark, workspace_index)` w `sidebar_scene.rs`.
2. `WorkspaceShell::ApplyTheme()` i miejsca używające `Tone`, `FrameTint`, `Hairline`,
   `HoverWash`, `kInk`… oraz `kWorkspaceColors` przechodzą na paletę z Rusta;
   stałe w `workspace_shell.cc` znikają.
3. Style przekazywane do sceny (`ToggleStyle`, `SidebarRowStyle`, `EditorStyle`)
   powstają z `recipe::*`.
4. Picker koloru obszaru: trzy suwaki + presety, zapis indeksu, migracja hex.
5. Fonty Instrument Serif, Schibsted Grotesk i JetBrains Mono (OFL) w pakiecie
   aplikacji i w audycie licencji.
