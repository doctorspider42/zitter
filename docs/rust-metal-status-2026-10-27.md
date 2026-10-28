# Rust + Metal — stan i pozostałe prace

Stan na 2026-10-27 (wieczór). **Migracja wznowiona** na prośbę użytkownika: cel
kończy się przy pełnym przeniesieniu implementacji albo gdy tygodniowe zużycie
przekroczy 25%. Testy UI uruchamiamy tylko po większych etapach.

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

Kolejność jest propozycją na wznowienie; testy z oknami pozostają odłożone.

1. **Dokończyć własność formularzy w Ruście.** Zinwentaryzować pozostałe akcje
   pomocnicze, popupy i callbacki edytorów. Przenieść pozostałe właściwości kontrolek,
   komunikaty statusu/błędów/postępu, stan busy/managed i koordynację paneli do
   wspólnych modeli. Nie przepisywać ponownie już działających kontrolerów walidacji.
   Zachować walidację IME, wybór profilu, stan importu i autoryzację zapisów.
2. **Graf sidebara — zrobione headless.** Para splitu, kotwica i widoczność są w
   Ruście (patrz wyżej). Pozostaje akceptacja GUI (`SidebarPairRustNodeOwnsMembersAnchorAndFolderVisibility`
   jest tylko kompilowany) oraz dalsze ograniczenie przebudowy kontenerów.
3. **Poszerzyć przyrostowe uzgadnianie drzew.** Wykorzystać istniejące zachowywanie
   wierszy kart/przypięć, zamiast je pisać od nowa. Ograniczyć przebudowę kontenerów
   przy zmianie topologii i rozszerzyć ponowne użycie na pozostałe drzewa formularzy.
   Jawnie zachować focus, kolejność tabulacji, scroll/reveal i tożsamość AX;
   anulować naciśnięcia, drag/capture i popupy przy zmianie celu lub widoczności.
4. **Dokończyć modele tekstu, właściwości i układ sceny.** Włączyć pozostałe
   modele/projekcje edytorów i tekstu do wspólnej własności oraz cyklu życia grafu.
   Dokończyć koordynację zewnętrznych paneli, dialogów i palety. Ograniczyć kopie
   właściwości w C++ tam, gdzie nie są potrzebne adapterowi. Uporządkować własność
   prymitywów fallbacku CPU, zachowując poprawne działanie bez drawable/GPU.
5. **Przeprowadzić zbiorczy checkpoint UI po osobnej zgodzie.** Najpierw zebrać
   aktualny wykaz przypadków, włącznie ze wszystkimi przypadkami kompilowanymi po
   ostatniej pełnej macierzy. Uruchomić odpowiednie macierze Rust/Metal i Views
   na izolowanych profilach oraz regresje formularzy, splitu, drag/drop, sesji,
   profili, przechwycenia wejścia, focusu i AX. Szczególnie sprawdzić obserwatorów
   aktywacji oraz sekwencje hide/reopen i zmiana/powrót profilu. Kompilacja tych
   przypadków nie jest wynikiem ich wykonania.
6. **Wykonać akceptację ręczną.** IME i okna kandydatów, bidi/RTL, VoiceOver,
   trackpad/gesty, skala Retina, motywy, clipping, przywracanie focusu i dostępności,
   ukryte okna, utrata drawable i odzyskiwanie GPU. Zweryfikować także natywny fallback.
7. **Powtórzyć porównanie wydajności Views ↔ Rust/Metal.** Użyć tego samego
   rzeczywistego obciążenia i warunków. Zmierzyć osobno CPU, GPU, rzeczywistą
   prezentację klatki oraz pamięć; uwzględnić layout, shaping/tekst, synchronizację,
   przebudowy i koszty adapterów. Wyniki szybkich testów jednostkowych nie dowodzą
   przyspieszenia interfejsu. Wyjaśnić pozostałe diagnostyki compositor/mailbox.
8. **Przygotować wydanie.** Zbudować i podpisać bundle odpowiadający zweryfikowanemu
   commitowi; sprawdzić profile, sesje, zapis/odtworzenie i rollback. Odświeżyć manifest
   faktycznie dołączonych zależności/licencji oraz Apache-2.0 i wymagane informacje
   upstream. Zweryfikować skład końcowej binarki. Utrzymać Rust/Metal jako opt-in
   do czasu zamknięcia akceptacji, bez deklaracji pełnej zgodności na podstawie buildu.

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
