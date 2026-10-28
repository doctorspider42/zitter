# Rust + Metal — stan i pozostałe prace

Stan na 2026-10-28. Migracja wznowiona 2026-10-27 na prośbę użytkownika (cel: pełne
przeniesienie implementacji albo 25% tygodniowego zużycia; testy UI po większych
etapach). Implementacja jest przeniesiona do granic platformy; otwarta pozostaje
akceptacja (ręczna, wydajność, wydanie) — patrz lista niżej.

## Gdzie jesteśmy

Mamy działający, opcjonalny backend Rust + Metal na obecnym forku Chromium.
Duża część rysowania, układu, obsługi wejścia i logiki formularzy jest już w Ruście.
Pozostają natywne drzewa Views i adaptery; pełna zgodność funkcjonalna oraz przewaga
wydajnościowa najnowszego kodu **nie są jeszcze potwierdzone**. Nie podajemy procentu
ukończenia: sam rozmiar kodu lub liczba testów nie mierzą pozostałego ryzyka.

Ostatnie etapy obejmują:

- Rustowe modele checkboxów, przełączników i selektorów oraz ich stan wejścia.
- Tożsamość, rodziców i ponowne użycie adapterów przestrzeni, historii, wyników
  palety, reguł uprawnień stron oraz kart i przypiętych elementów sidebara.
- Rustową hierarchię folderów przypięć, widoczność i anulowanie wejścia potomków;
  zachowanie niezmienionych kontenerów przy odświeżaniu samych metadanych.
- Semantyczne komendy preferencji, przestrzeni, profili i importu, kategorie,
  zamykanie paneli oraz przypisanie komend do właściwego panelu w Ruście.
- Jednorazowe bilety odroczonych komend z dzierżawą i epoką anulowania; używa ich
  paginacja wyjątków stron. Bilety nie utrzymują węzłów ani adapterów przy życiu.
- Komendy wszystkich czterech presetów koloru i sześciu presetów emoji przestrzeni,
  z walidacją indeksu i rozwiązywaniem wartości przez katalog Rust.
- **Para splitu w grafie Rust** (punkt 2 poniżej): węzeł `sidebar.pair`, jego
  kotwica wśród przypięć (rzeczywisty rodzic: grupa folderu lub korzeń kart),
  członkowie jako dzieci pary, widoczność zależna od zwinięcia folderu oraz ukrycie
  przypięcia reprezentowanego przez kartę. Natywny `RebuildTabs` w trybie Rust bierze
  sloty i widoczność przypięć z grafu zamiast z `SidebarTree::Tabs`. Zmiana układu
  anuluje naciśnięcia; identyczny układ zachowuje wejście i ID.
- **Statusy formularzy w Ruście**: wyniki zapisu preferencji (tekst, wyjątki stron,
  usuwanie wyjątku, wartości, przełączniki) są typowanym `PreferenceOutcome` z
  tekstem/tonem w `PreferencesForm`; przechwytywanie skrótu (Escape/niepoprawny/
  poprawny) jest przejściem stanu `ToolsForm::tools_capture`; komunikaty wczytywania
  historii i błędu zapisu pochodzą z `tools_status`. Views zachowuje dotychczasowe teksty.

Natywne usługi okien, IME, schowka, dostępności i menu oraz autoryzacja operacji
Chromium/profili/magazynów pozostają potrzebnymi granicami integracji. Celem nie jest
zastąpienie usług systemowych własnymi implementacjami.

## Konkretna lista pozostałych prac

**Końcowy checkpoint sesji (`160960c`):** 275/275 Metal, 226/226 Views, 503 testy Rust,
68 Python, granice zależności OK. [Raport](benchmarks/rust-ui-checkpoint-2026-10-28-final.json).

Stan po sesji 2026-10-27/28. Implementacja logiki UI jest w Ruście: modele kontrolek,
graf węzłów sidebara (karty, para splitu, przypięcia, foldery), uzgadnianie z
zachowaniem tożsamości adapterów i kontenerów, komendy formularzy, wszystkie menu
kontekstowe, paleta i katalog Command Center, teksty statusów i komunikatów usług,
układ kolumn/paneli. W trybie Rust natywne kontrolki Views powstają tylko w gałęziach
Views. Natywne pozostają udokumentowane granice platformy: okna/widgety, runnery menu,
ramki dialogów, fokus/AX/IME, dostarczanie zdarzeń oraz usługi przeglądarki.

Wszystkie pozostałe punkty wymagają użytkownika lub jego decyzji: akceptacja ręczna,
notaryzacja (poświadczenia Apple) oraz wybór domyślnego backendu.

0. **Automatyczna akceptacja skali i RTL (wykonana).** Skala 1× — 276/276 Metal.
   RTL: znalezione i naprawione 4 realne błędy w trybie Rust (kierunek przewijania
   poziomego, projekcja sceny kropek stopki, lustrzenie elementów scen, przeciąganie
   poziomego suwaka); testy RTL są teraz niezależne od kierunku. Pełne macierze na
   `4bbee47`: Metal LTR 276/276, Metal RTL 276/276, Views LTR 227/227, Views RTL
   227/227. [Raport](benchmarks/rust-ui-rtl-scale-acceptance-2026-10-28.json).
1. **Akceptacja ręczna (wymaga użytkownika):** IME i okna kandydatów, bidi/RTL,
   VoiceOver, trackpad/gesty, skala Retina, motywy, utrata drawable/GPU, fallback.
2. **Porównanie wydajności — wykonane, otwarte optymalizacje.** Test
   `PerformanceUiInteraction` + `tools/benchmark_ui_interaction.py` (prawdziwa aplikacja,
   naprzemienne rundy, [raport](benchmarks/rust-ui-interaction-performance-2026-10-28.json)):
   mediany opóźnień identyczne we wszystkich operacjach (p95 otwarcia palety +1 klatka);
   CPU procesu przeglądarki +~26%; footprint przeglądarki ~262 vs ~112 MB, z czego
   ~106 MB to stały koszt sterownika przy pierwszym przebiegu renderowania Metal w
   procesie (odtworzony sondą); całe drzewo procesów +12%. Przewagi wydajnościowej
   Rust/Metal **nie** ma. Eksperymentalny tryb `--zitter-rust-raster=compositor`
   (cała logika Rust, malowanie przepisów sceny przez kompozytor Chromium) ma opóźnienia
   i pamięć jak Views, a CPU jak Rust/Metal — więc narzut CPU (+~26%) jest w warstwie
   adapterów/projekcji Rust, a narzut pamięci wyłącznie w Metalu w procesie
   ([raport](benchmarks/rust-ui-interaction-compositor-2026-10-28.json)). W tym trybie
   pełna macierz przechodzi 202/276 (większość niepowodzeń czeka na widoczność warstw
   Metal z założenia). Następne kroki: profil CPU warstwy adapterów; decyzja, czy
   wspierać tryb kompozytora jako backend (wymaga dostosowania ~70 testów).
   Pierwsza optymalizacja z profilu: `PositionPanels` pomija `Widget::SetBounds` przy
   niezmienionych granicach okna — wywołanie otwarcia palety 9,3→6,9 ms (Metal) i
   6,8→5,6 ms (Views), CPU/iterację 30,0→27,7 ms (Metal), 23,6→22,6 ms (Views).
   Pełna macierz po zmianie: 276/276 Metal, 227/227 Views. Pozostała różnica CPU (~5 ms/iterację) jest
   rozproszona (planowanie odrysowań, skracanie tekstu, malowanie pól) bez jednego
   gorącego punktu w profilu.
2a. **Tryb kompozytora — wspierany (opt-in).** `--zitter-sidebar=rust-metal
   --zitter-rust-raster=compositor`: cała logika Rust, malowanie przepisów scen przez
   kompozytor Chromium, bez `MTLDevice` w procesie przeglądarki (pamięć jak Views).
   Testy sprawdzają wyrocznie prezentacji Metal tylko gdy Metal rysuje
   (`MetalPresents()`); zestaw `ZitterMetalSidebarBrowserTest` jest pomijany jak w Views.
   Pełne macierze: Metal 276/276, Views 227/227, kompozytor 227/227.
3. **Kandydat wydania — zbudowany i sprawdzony lokalnie.** `build/ZitterRustMetalRelease.app`
   (package_app.py, podpis lokalną tożsamością, `codesign --verify --deep --strict` OK),
   świeży audyt licencji: 326 komponentów, 0 niedopasowanych binariów, dołączone
   `THIRD_PARTY_LICENSES.txt` identyczne z aktualnym składem. Test dymny na izolowanym
   profilu: start Rust/Metal, zapis sesji, zamknięcie i odtworzenie (24 + 1 karty),
   wycofanie do Views na tym samym profilu. [Raport](benchmarks/rust-metal-release-candidate-2026-10-28.json).
   Otwarte: notaryzacja/przenośny instalator (bundle jest deweloperski, zależny od
   checkoutu) oraz decyzja o domyślnym włączeniu po akceptacji ręcznej.
4. **Opcjonalnie dalsza redukcja natywnych kontenerów** (zwykłe `views::View` z rustowym
   układem pełnią dziś rolę adapterów zdarzeń/fokusu/AX) — tylko jeśli akceptacja
   wykaże taką potrzebę.

## Co jest sprawdzone, a co odłożone

**Zachowanie kontenerów sidebara:** w trybie Rust kolumna kart i kontener `SplitPair`
przeżywają przebudowę struktury, gdy rustowy węzeł `sidebar.pair` zachowuje ID
(`SidebarTabColumnAndPairContainersSurviveStructuralRefresh`; podzbiór sidebar/split/
pin/folder: 91 Metal, 42 Views zaliczone).

**Menu linków w Ruście:** kompozycja menu linku i podmenu tras, stan enabled/checked
oraz rozwiązywanie komendy i docelowej przestrzeni są w Ruście; natywnie zostaje
`SimpleMenuModel`/runner i usługi routingu. Testy Rust 489; podzbiory UI linków/
menu/Peek i `DomainRoutingAndPersistence` zaliczone w obu trybach.

**Menu przestrzeni w Ruście:** podmenu profilu/koloru/ikonki, stan zaznaczenia i
dostępności oraz typowane akcje (profil, kolor, emoji, ustawienia, usunięcie) z
celem przechwyconym w Ruście. Testy Rust 491; podzbiór UI menu przestrzeni zaliczony
w obu trybach.

**Menu folderów w Ruście:** kompozycja (folder/podfolder, zmiana nazwy, usunięcie),
dostępność i typowane akcje z przechwyconą przestrzenią/folderem; natywnie zostają
okna dialogowe folderów i zapis. Testy Rust 492; podzbiór UI folderów/przypięć
22 Metal / 19 Views zaliczony.

**Teksty stanu zarządzanego w Ruście:** dostępność i podpowiedzi kontrolek preferencji
(niedostępne/zarządzane/edytowalne), baner profilu, opis reguł wyjątków (polityka,
osadzenie, „Zarządzane”) i tekst pustej listy. Testy Rust 494; podzbiór UI preferencji
i wyjątków 38 Metal / 32 Views zaliczony.

**Plan palety w Ruście:** sugestia główna (tytuł/szczegół/ikona), wykrywanie prefiksu
słowa kluczowego, tryb samych komend oraz kolejność głównej/wyników/komend (rotacja,
wstawienie komend) są w `palette_plan`; natywnie zostaje rozwiązywanie adresu,
wyszukiwanie i komendy aplikacji. Testy Rust 495; podzbiór UI palety/adresu 33 Metal /
30 Views zaliczony.

**Statusy usług profili jako zdarzenia:** warstwa aplikacji zgłasza typowane
`ProfileStatusEvent` (tworzenie, zmiana nazwy, usuwanie, błędy, wczytanie), a tekst
pochodzi z Rusta (`profile_status_copy`); Views używa natywnej tabeli. Testy Rust 496;
cała klasa `ZitterProfilesBrowserTest` przeszła: 216 Metal / 208 Views.

**Statusy importu jako raporty:** warstwa aplikacji przekazuje `ImportStatusReport`
(etap, liczniki, błąd usługi, wynik zapisu historii/ciasteczek); tekst formatuje Rust
(`import_status_copy`), Views — natywny formatter. Testy Rust 497, Python 68;
podzbiór UI importu 17 Metal / 14 Views zaliczony.

**Katalog Command Center w Ruście:** lista komend warunkowa od strony/splitu/historii/
przestrzeni, ich teksty, składanie diakrytyków, dopasowanie słów i skrócenie widoku
startowego są w `command_catalogue`; natywnie zostaje wiązanie z kartą/stroną,
skróty i wykonanie. Testy Rust 499; podzbiór UI 33 Metal / 29 Views zaliczony.

**Status pobierania w Ruście:** decyzje i sformułowania (zablokowane, anulowane,
ukończone/usunięte, przerwane, wstrzymane, prędkość, pozostały czas) w `download_status`;
formatowanie bajtów zostaje w natywnej usłudze lokalizacji. Testy Rust 500; testy UI
pobierania zaliczone w obu trybach.

**Czyszczenie danych w Ruście:** treść potwierdzenia (profil, zakres, typy danych) i
statusy operacji (zajęte, brak wyboru, zakaz zasad, w toku, gotowe, błąd) jako typowane
zdarzenia z tekstem/tonem z Rusta. Testy Rust 501; testy UI czyszczenia/historii
zaliczone w obu trybach.

**Statusy operacji i wiersze Time Machine w Ruście:** zdarzenia cyklu życia stron
(sprawdzanie, anulowano, gotowe, blokada przy nagrywaniu) oraz tytuł/szczegóły
snapshotów, zamkniętych kart i uśpionych przestrzeni. Testy Rust 502; podzbiór UI
33 Metal / 26 Views zaliczony.

**Pochodzenie wyników palety i monit hasła w Ruście:** prefiksy szczegółów wyników
(karta, przypięta, przestrzeń, historia) oraz tytuł/wiersze/przyciski monitu zapisu
hasła w ścieżce sceny. Testy Rust 503; podzbiór UI 33 Metal / 30 Views zaliczony.

**Układ kolumny sidebara i nagłówków w Ruście:** główna kolumna sidebara oraz nagłówki
ustawień przestrzeni i importu używają `RustPanelLayout` (fallback Views: ten sam
`BoxLayout`). W trybie Rust natywne kontrolki Views (Label/MdTextButton/Combobox/
Checkbox/Toggle/ProgressBar) powstają już tylko w gałęziach Views. Pełna macierz po
zmianie: 274/274 Metal, 225/225 Views.

**Zachowanie grup folderów przy zmianie topologii:** przy przebudowie drzewa przypięć
w trybie Rust kontener listy, kontenery wcięć wierszy i `ScenePinGroup` są zachowywane (według klucza przypięcia i rustowego ID grupy)
(nowy test `PinTreeTopologyChangeRetainsFolderGroupsAndListContainer`); podzbiór UI
sidebar/split/pin/folder 92 Metal / 43 Views zaliczony.

**Pełny checkpoint UI 2026-10-28 (`90c7e8b`):** 274/274 Metal i 225/225 Views.
[Raport](benchmarks/rust-ui-checkpoint-2026-10-28.json).

**Pełny checkpoint UI 2026-10-27:** po poprawkach **273/273 Metal i 224/224 Views**
(pierwszy przebieg: 6 niepowodzeń Metal, 1 Views — trzy crashe testów na odłączonych
widokach, brakująca komenda Rust `HistoryMore`, tooltip tytułu Peek, wyścig tytułu
strony w Peek, synchronizacja układu w teście wyjątków, FocusRing w teście menu).
[Raport](benchmarks/rust-ui-checkpoint-2026-10-27.json). To zastępuje checkpoint 10-26.

Dla etapów pary splitu i statusów: **487 testów Rust**, **68 testów Python**,
kontrola granic zależności; `sidebar_scene_unittests`, `zitter_ui_lab` i
`zitter_prototype_ui_lab` skompilowane. Testy UI nie były uruchamiane.

Dla kodu `733f04f`: **481 testów Rust**, **68 testów Python w drzewie roboczym
oraz 68 z czystego archiwum commita**, **180 przypadków CoreText plus rasteryzacja**,
kontrola granic zależności i nagłówków GN. Najnowsze przypadki UI są tylko kompilowane.
Szczegóły końcowego buildu: [raport presetów](benchmarks/rust-appearance-command-headless-2026-10-27.json).

Ostatnia ukończona pełna macierz UI pozostaje historycznym checkpointem tekstu/
akcji z 2026-10-26 (`dadea9b`): 210 Metal i 162 Views zaliczone, po sześć pominięć
przypadków opcjonalnych. Późniejszy pełny przebieg selektorów został przerwany;
nie stanowi akceptacji obecnego HEAD. Nowsze etapy mają walidację bez okien.

Uruchomiony wcześniej podgląd nie został podczas tych etapów przebudowany ani
zrestartowany; nie należy traktować go jako prezentacji najnowszego HEAD.
Buildy UI-lab używają udokumentowanej zamrożonej bazy/override'ów, więc nie dowodzą
akceptacji wszystkich niezależnych zmian bieżącego Chromium/network bootstrapu.

## Materiały do wznowienia

- [Bieżąca kolejka i historia etapów](rust-metal-next-steps-2026-10-23.md).
- [Szczegółowa lista zgodności i granic migracji](rust-metal-parity.md).
- [Raport biletów komend](benchmarks/rust-form-ticket-headless-2026-10-27.json).
- [Raport hierarchii przypięć](benchmarks/rust-pin-tree-headless-2026-10-27.json).
- [Ostatni pełny checkpoint UI](benchmarks/rust-text-action-checkpoint-2026-10-26.json).

Po wznowieniu najpierw odczytać stan repo, celu i limitu. Budować wyłącznie
`.chromium/src/third_party/ninja/ninja` (1.12); ninja z Homebrew (1.13) odrzuca
log v6 i wymusza pełną przebudowę Chromium.
