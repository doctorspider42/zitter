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

Stan na `sidebar_scene` z mostem: 528/528 testów Rust (flagi GN: edycja 2024,
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
6. Typografia w scenie (zrobione): `Item::text_style` (`design::TextStyleId`: DisplayXl,
   DisplayLarge, Title, BodyStrong, Body, Label, Eyebrow, Url) — `layout()` nadaje
   tekstom elementu rodzinę, rozmiar, grubość, kursywę i rozstrzelenie z tokenów
   (Eyebrow wersalikami). `Primitive` niesie `family` (0 systemowa, 1 Instrument Serif,
   2 Schibsted Grotesk, 3 JetBrains Mono), `italic` i `tracking` (em). Metal składa
   `NSFont` z rodziny i stosuje `NSKernAttributeName`; raster (`DesignFont`) ma rodzinę,
   ale bez rozstrzelenia. Bez zainstalowanych fontów (testy, binaria spoza pakietu)
   zostaje font systemowy. Most: `design_text_style`, `design_font_family`.
7. Zestaw panelu (zrobione): `design_metrics()` daje C++ tokeny odstępów i promieni;
   w `workspace_shell.cc` `PanelText` (tekst w stylu z tokenów), `PanelStack`,
   `PanelSection` (Eyebrow nad treścią, `space-2`), `PanelColumns` (równe kolumny,
   `space-2`) i `SpaceIconTile` (ikona przestrzeni na `accent_soft`, 48 px).
   Panel przestrzeni na nich: nagłówek z kafelkiem, nazwą w DisplayLarge i profilem w
   Label; sekcje „Kolor przestrzeni”, „Ikona”, „Nazwa i profil” (nazwa | profil w dwóch
   kolumnach); marginesy `space-6`, odstęp sekcji 20 px.

## Zgodność z design systemem (2026-10-28, noc)

Raster kompozytora (domyślny) maluje teraz wiersze paska, pigułkę adresu, wiersz
„Nowa karta”, kropki obszarów i wiersze Command Center tym samym przepisem Rust co
Metal (`PaintSceneRecipe`), zamiast starego malowania Views.

- Karty: nieaktywne w `ink_muted`, tytuł `body`/`body-strong`, zamknięcie tylko na
  hover, monogramy z presetów obszarów (`accent` + `on_accent`), foldery z ikoną.
- „PRZYPIĘTE” w `eyebrow`; adres w `url` (host 600), kłódka w `ok`.
- Przełącznik obszarów: szyna `bg_void` z obrysem `line`, kropki w kolorze swojego
  obszaru, aktywny jako pigułka `bg_raised` z kropką 10 px w pierścieniu i nazwą
  w `label` 600.
- Rama okna i nagłówek w `bg_void`; przyciski-ikony: `ink` na hover, `accent_ink` na
  `accent_soft` gdy włączone.
- Kbd (rodzaj sceny 30); skróty wyświetlane symbolami macOS (`DisplayShortcut`).
- Command Center: 560 px, kreska ket `|` w `accent_ink`, pole 17 px, wiersze 36 px
  bez kafelka, zaznaczony w `accent_ink`, stopka na `bg_base` z „↑↓ wybierz, ↵
  otwórz, esc zamknij”.
- `--zitter-open-panel` przyjmuje też `sidebar`, `palette`, `settings`, `profiles`.

Druga runda: przyciski z `recipe::button` (most `design_button`; 32 px, `body-strong`,
warianty primary/outline/quiet/danger/danger strong; w dialogach „Anuluj” quiet, akcja
primary albo danger strong, gdy dialog domyślnie wybiera Anuluj), przełącznik z
`recipe::toggle` (`design_toggle`; 34×20, obrys 1.5 px), uchwyt podziału 3 px
`line_strong` → `zing` na hover, pasek postępu 3 px `accent`, tytuł „Preferencje” w
`display-l`.

Trzecia runda (decyzje produktowe): nazwa aktywnej przestrzeni w szynie opcjonalna
(Preferencje → Wygląd), „przestrzeń” wszędzie, ⌘T otwiera Command Center (bez strony
nowej karty), foldery przypiętych zwinięte z podglądem jednej karty, tost z „Cofnij”
zamiast dialogów dla usuwania folderu i przestrzeni.

Czwarta runda: grupy w Command Center (nagłówek `eyebrow` w pierwszym wierszu grupy,
klawiatura go omija) na przyciemnionym oknie, `zt-loader` zamiast kreski ket podczas
ładowania, strona Pobrane z designu (kafelek rozszerzenia, stan ikoną i słowem, akcje-ikony,
tost „Pobrano X · Pokaż”), drgnięcie tostu, pigułka adresu w trybie edycji i drgnięcie po
„Kopiuj adres strony”, głośnik grającej karty, dialogi 400 px, odstęp wierszy `space-1`,
przyciski stopki 28 px, przycisk `zing` przy tworzeniu przestrzeni, Split View (host w
`url`, pasek 28 px, obrys aktywnego panelu).

Menu kontekstowe kart, linków i przestrzeni są menu Views w kolorach i kroju designu,
z akcjami niszczącymi na końcu po separatorze; menu aplikacji macOS mówi językiem
interfejsu; „+” kończy szynę przestrzeni, dopóki się mieści.

Panel menu ma `radius-md` (przekazywany do `RunMenuAt`), a akcje niszczące są w
`danger` (`DesignMenuModel`).

Ograniczenie platformy: dialogi modalne okna na macOS są arkuszami, którym Chromium
narzuca promień 2 px (pozostałe dialogi dostają `radius-lg`). Pasek boczny w designie
nie ma ikony Pobrane, więc koniec pobierania sygnalizuje tost.

## Języki interfejsu

Polski jest językiem źródłowym: tekst UI pisze się po polsku w miejscu użycia i owija w
`t("…")`/`tf("… {} …", &[&x])` (Rust, [`i18n.rs`](../chromium/prototype/i18n.rs)) albo
`T(u"…")`/`TF(u"… {} …", {x})` (C++, [`i18n.h`](../chromium/prototype/i18n.h)).
Angielski tekst jest w katalogach `chromium/prototype/i18n_en_{scene,shell,native}.rs`
(klucz to polski literał dokładnie jak w kodzie). Brak wpisu = tekst po polsku;
`python3 tools/i18n_check.py --missing` pokazuje braki, a `tests/test_i18n.py` pilnuje
pokrycia, duplikatów, liczby `{}` i spójności między katalogami.

Język ustala się raz przy starcie (`ApplyUiLanguage`): wybór z Preferencji („Język
interfejsu”: Systemowy / Polski / English, zapisany w domyślnych aplikacji jako
`ZitterLanguage` razem z `AppleLanguages`) albo język aplikacji. Zmiana działa po
ponownym uruchomieniu. Testy przeglądarkowe przypinają polski
(`SetUiLanguageForTesting("pl")`); angielski UI do podglądu:
`defaults write dev.zitter.browser ZitterLanguage en`.
