# Profil UI i audyt dystrybucji — 2026-10-21

**18 ms to przede wszystkim droga do raportowanej prezentacji, nie czas pracy GPU.**
Dla naszego kodu rekomenduję **Apache-2.0**. Obecna paczka Chromium zawiera jednak
LGPL, MPL oraz UnRAR z ograniczeniem użycia, więc nie da się nadać wszystkim jej
składnikom jednej licencji Apache. Ten audyt nie zmienia licencji istniejących plików.

## Co zmierzyliśmy

MacBook Pro M3 Pro, 36 GiB, macOS 27.0.1; Release, ARM64, ThinLTO/PGO,
Chromium `1a732fcc23b20dd0bcf38f35d72c1fe1ec799d35`. Osobna podpisana
`build/ZitterUiProfile.app`, izolowane profile, lokalna strona testowa. Normalna
`build/Zitter.app` pozostała niezmieniona.

Sześć procesów ze śladem Perfetto: trzy backendy × selection/resize,
120 aktualizacji po 10 rozgrzewkach. Dodatkowo dwa osobne zapisy Instruments:
Time Profiler i Metal System Trace, po 240 aktualizacji resize w Rust/Skia.
Ślady obejmują proces przeglądarki, renderery i proces GPU.

[Surowe próbki i etapy kompozytora](benchmarks/ui-profile-2026-10-21.json),
[próbkowany CPU](benchmarks/ui-cpu-2026-10-21.json),
[sprzętowe przedziały GPU](benchmarks/ui-gpu-2026-10-21.json).
SHA binarki i źródeł prototypu znajdują się w pierwszym pliku.
Pełne ślady lokalnie: `build/ui-profile/20261021T161843344535Z/` i
`build/ui-profile/instruments/`. Zapisów całego systemu nie dodajemy do repo;
analizy Instruments filtrują procesy należące do badanej paczki.

### Skąd bierze się czas

Mediany etapów, ms; są to różne rozkłady, więc suma median nie musi być medianą sumy.

| Etap | Views selection | Rust/Skia selection | Views resize | Rust/Skia resize |
|---|---:|---:|---:|---:|
| Budowa sceny | 0,002 | 0,005 | 0,002 | 0,003 |
| Cała synchroniczna aktualizacja UI, ze sceną | 0,117 | 0,137 | 0,643 | 0,315 |
| Od początku aktualizacji do odbioru klatki przez Viz | 2,221 | 2,189 | 2,323 | 1,684 |
| Odbiór Viz → rozpoczęcie rysowania | 0,161 | 0,190 | 0,158 | 0,142 |
| Rysowanie → początek swap | 0,299 | 0,903 | 0,778 | 0,629 |
| Początek → koniec swap | 0,067 | 0,075 | 0,087 | 0,089 |
| Koniec swap → raportowana prezentacja | 14,306 | 15,159 | 14,974 | 15,046 |
| Łącznie aktualizacja → raportowana prezentacja | 16,583 | 18,946 | 19,787 | 18,923 |

Wiersze sceny i synchronicznej aktualizacji zawierają się w pierwszym etapie
potoku; nie należy ich ponownie dodawać. Żaden etap swap/CPU nie jest pomiarem
czasu wykonywania shaderów.

Raportowany interwał wyświetlacza wynosi **8,333 ms**. Callback dostajemy zwykle
około **11,2 ms przed** raportowanym czasem prezentacji. W tym konkretnym Chromium
`gpu/ipc/service/image_transport_surface_overlay_mac.mm:GetDisplaytime()`
wyznacza przyszły termin z CVDisplayLink, z marginesem latch 1,5 ms i dodatkowym
interwałem. `CALayerTreeCoordinator::CommitPresentedFrameToCA()` przekazuje ten
czas do `PresentationFeedback` i ustawia flagi 5 (`kVSync | kHWCompletion`).
Dlatego nawet obecność `kHWCompletion` w tych danych nie jest niezależnym dowodem
fizycznego wyświetlenia nowej zawartości. Ujemny czas callback–prezentacja jest
oczekiwany dla tej implementacji.

To doprecyzowuje [poprzedni benchmark](rust-ui-prototype.md): jego liczby pozostają
zapisem tego, co raportuje API, ale nie dowodzą 18 ms pracy GPU ani rzeczywistego
input-to-photon. Metal używa `MTLDrawable.presentedTime`, więc porównanie jego
latencji z przewidywanym terminem Chromium nie rozstrzyga, który backend szybciej
pokazuje obraz. Kolejna aktualizacja jest planowana po callbacku; dla Chromium
może to nastąpić jeszcze przed terminem prezentacji poprzedniej aktualizacji.
Do pomiaru rzeczywistej latencji trzeba osobnego eksperymentu z identyfikatorem
klatki i wiarygodnym potwierdzeniem wyświetlenia lub pomiarem zewnętrznym.

### CPU i rzeczywiste GPU

Perfetto potwierdziło ścieżkę **GPU raster**: `GpuRasterBuffer::Playback`,
`RasterImplementation::RasterCHROMIUM`, `RasterDecoderImpl::DoRasterCHROMIUM`
i `SubmitToGPU` w procesie GPU. Rust przygotowuje scenę na CPU; `OnPaint`
zapisuje operacje. Raster i kompozycja trafiają do GPU. Nie oznacza to, że cała
praca UI odbywa się na GPU.

Metal System Trace, Rust/Skia resize, 240 aktualizacji:

- 3708 sprzętowych przedziałów Vertex/Fragment przypisanych do procesu naszej paczki;
- mediana pojedynczego pakietu GPU **0,0545 ms**, p95 **0,1290 ms**;
- mediana opóźnienia CPU→GPU **0,3902 ms**, p95 **0,7793 ms**;
- unia przedziałów zajętości GPU **185,9 ms** w oknie **4,376 s**, około **4,25%**;
- średnio **0,775 ms zajętości GPU na aktualizację**, razem ze stroną i UI.

Unia usuwa nakładanie faz Vertex/Fragment. To średnia w całym oknie, nie mediana
kosztu pojedynczej klatki ani koszt samego sidebara. Nie obejmuje pracy
WindowServer jako kosztu przypisanego do naszego procesu. Profilowanie zmienia
czasy; te dane służą lokalizacji kosztu, a nie nowemu rankingowi backendów.
Osobny adapter Metal nadal raportuje medianę wykonania command buffer około
**0,070 ms** przy selection i **0,074 ms** przy resize.

Time Profiler w oknie około 4,425 s zebrał **3298 ms próbek CPU** z procesów
paczki, około **13,74 ms CPU/aktualizację** sumowane po procesach i rdzeniach.
Sam proces przeglądarki miał 906 ms próbek, jego główny wątek 411 ms.
Wynik obejmuje również renderery, proces GPU, sieć i pracę w tle. Próbki mają
wagę około 1 ms; to estymacja statystyczna, nie suma czasu ściennego klatki.

Wśród liści stosów znalazły się `Cr_z_inflate_fast_chunk_` (158 ms), `write`
(137 ms), alokacje i kopiowanie pamięci. W tle trwały instalacje komponentów
Chromium (w izolowanych profilach pojawiły się SODA i modele Optimization Guide).
Samo `--disable-background-networking` ich nie wyeliminowało. W harnessie dodałem
`--disable-component-update` dla następnych prób. Zmiana dotyczy testów;
nie wyłącza aktualizacji w normalnej aplikacji. Te profile wykonano przed tą
zmianą, nie przedstawiam więc jej jako zmierzonego przyspieszenia produktu.

### Kolejność optymalizacji

1. **Najpierw poprawna miara latencji i pacing.** Oddzielić docelowy termin
   DisplayLink, callback, zakończenie GPU i faktyczne wyświetlenie. Dla testu
   sekwencyjnego nie zaczynać następnej próby przed raportowanym terminem.
   Sprawdzić liczbę klatek w kolejce oraz commit względem latch deadline.
   Nie usuwać marginesu Chromium na podstawie samych przewidywanych timestampów.
2. **Ograniczyć zakres invalidacji Rust/Skia.** `Sidebar::SetScene()` obecnie
   odmalowuje cały sidebar przy zmianie zaznaczenia. Porównać poprzednie i nowe
   prostokąty, odmalować stare/nowe zaznaczenie i zachować resztę zawartości.
   Duża liczba raster tasks w śladzie potwierdza sens eksperymentu; nadal trzeba
   zmierzyć efekt. Przy resize zmienia się także rozmiar strony i jej raster.
3. **Metal: usunąć synchroniczne oczekiwanie z wątku UI w osobnym wariancie.**
   Aktualny adapter wywołuje `waitUntilScheduled`, `present` i flush transakcji,
   alokuje nowy MTLBuffer na aktualizację. Porównać asynchroniczny submit z ringiem
   buforów i poprawną synchronizacją CAMetalLayer/CoreAnimation. Obecna
   synchroniczna aktualizacja kosztuje około 1,1 ms selection / 2,3 ms resize,
   przy około 0,07 ms wykonania GPU. Nie obiecywać identycznej semantyki
   prezentacji po mechanicznym usunięciu oczekiwania.
4. **Oddzielić start i komponenty w tle od testu ustalonego UI.** Dłuższy warmup,
   wyłączony updater tylko w labie, rejestrowanie wszystkich procesów i stanu
   termicznego. Dodatkową serię kontrolną z tego dnia wykluczono z rankingu,
   ponieważ częściowo nakładała się na audyt licencji i próbę Instruments.

Nadal rekomenduję rozwijanie Rust-scene + Chromium/Skia: obecny profil nie
pokazuje wąskiego gardła w obliczaniu sceny ani przeciążenia GPU uzasadniającego
porzucenie kompozytora. To nie jest dowód przewagi kompletnego UI nad Views;
prototyp nie zawiera prawdziwego tekstu, IME ani accessibility.

## Audyt faktycznie zbudowanej paczki

Badana jest **normalna `build/Zitter.app` z `out/ZitterOptimized`**, nie cały
checkout ani sam Cargo.toml prototypu. Sprawdziłem:

- 322 wpisy paczki, SHA-256 plików i symlinki;
- 37 fizycznych ścieżek Mach-O, **20 unikalnych binariów po UUID**;
- dopasowanie wszystkich 20 do reguł linkowania Ninja po LC_UUID;
- obiekty, archiwa i rlib na poleceniach linkowania oraz `LC_LOAD_DYLIB`;
- graf GN głównego programu, frameworka, helperów i wszystkich dołączonych dylibów;
- 318 rekordów upstream `Shipped: yes` oraz 8 ręcznych uzupełnień metadanych;
- 247 rekordów z bezpośrednim dowodem wejścia do linkera; pozostałe obejmują też
  generatory, zasoby, schematy i zależności pośrednie.

[Zestawienie binariów i licencji](licenses/audit-2026-10-21.json),
[pełny manifest i wejścia linkera, gzip](licenses/manifest.json.gz),
[zebrane licencje i notices, gzip](licenses/THIRD_PARTY_LICENSES.txt.gz).
Rozpakowane kopie są w `build/license-audit/current/`.

GN reachability i obecność archiwum na wejściu linkera nie dowodzą, że każda
sekcja archiwum przetrwała ThinLTO/dead stripping. Raport zachowuje ten podział,
zamiast nazywać wszystkie 326 rekordów kodem wykonywanym w produkcji. Zasoby i
biblioteki dystrybuowane, lecz nieładowane w próbie, nadal podlegają warunkom
redystrybucji. Apple system frameworks są importami systemowymi, nie kopiami
bibliotek Apple dołączonymi przez ten packager.

| Potwierdzony składnik / warstwa | Warunki w tym buildzie | Konsekwencja |
|---|---|---|
| Chromium i większość Blink/V8/Skia/ANGLE | głównie BSD; licencje podskładników osobno | Zachować copyright, licencje i disclaimers; bez sugerowania poparcia Google |
| Nowy Rust scene/Metal adapter | kod projektu; GPUI nie jest używane | Możemy wybrać Apache-2.0 dla własnego oryginalnego kodu |
| CXX / foldhash | Apache-2.0 / Zlib | Permisywne; zachować wymagane notices |
| Rust std/compiler-builtins | m.in. MIT/Apache-2.0, wyjątki LLVM; pełny copyright library w kolekcji | Licencja kompilatora sama nie pokrywa wszystkich fragmentów runtime |
| FFmpeg | LGPL-2.1-or-later + podskładniki BSD/ISC/MIT | Źródła, zachowanie warunków LGPL, możliwość przebudowy/relinkowania |
| libusb/libusbx | LGPL-2.1 | Jak wyżej; upstream README wymienia lokalne poprawki Chromium |
| historyczne fragmenty Blink/WebKit | BSD oraz LGPL-2.0/LGPL-2.1 | Zachować odpowiednie licencje i udostępnić odpowiadające źródła |
| Hunspell | wybrana w Chromium MPL-1.1 | Źródła objętych plików i zmian, wymagane notices |
| 7 crate'ów Symphonia | MPL-2.0 | Copyleft na poziomie plików; źródła objętych plików i sposób ich uzyskania |
| UnRAR | własna licencja UnRAR | Zachować tekst; zakaz wykorzystania do odtworzenia kompresora RAR/kompatybilnego archiwizera |

Konfiguracja faktycznie linkowanego FFmpeg ARM64/Chromium deklaruje
`LGPL version 2.1 or later`; `CONFIG_GPL`, `CONFIG_GPLV3`, `CONFIG_NONFREE`
i `CONFIG_VERSION3` są równe 0. To jest sprawdzenie konkretnej konfiguracji,
a nie obietnica, że każda możliwa konfiguracja FFmpeg jest permisywna.

Dodatkowo obecna paczka zawiera `libvulkan`, `libvk_swiftshader`,
`libVkLayer_khronos_validation`, `libVkICD_mock_icd`, `libaperitif` oraz stuby
`libEGL`/`libGLESv2`. Wszystkie objęto skanem. Packager kopiuje `out/*.dylib`,
więc przed publiczną dystrybucją warto zbudować minimalny zestaw runtime i nie
pakować niepotrzebnej walidacji/mock ICD. Nie usuwałem tych plików z działającej
aplikacji w ramach audytu.

### Braki metadanych upstream

Oficjalny `licenses.py` raportował 8 problemów, a mimo nich zwracał sukces:
Catapult, wrapper zlib Crashpada, dwa katalogi schematów Perfetto, agregator Rust,
Crubit, LLVM 10 SwiftShader i Marl. Nie zostały wyciszone.
[Uzupełnienia z uzasadnieniami](licenses/supplements.json) dodają teksty licencji;
wrapper Crashpada odwołuje się do już ujętego Chromium zlib.
Brakujący pełny LICENSE LLVM 10 pobrano z oficjalnego taga `llvmorg-10.0.0`;
zachowano też podkatalogowy notice Support. Rust std ma dodatkowo kompletny
`COPYRIGHT-library.html` z użytego toolchaina. Oryginalne diagnostyki są w manifeście.
Generator zbiera też pliki NOTICE obok wskazanych licencji.

## Czy możemy wydać na Apache 2.0?

**Tak: własny oryginalny kod Zitter na Apache-2.0, a dołączone komponenty na ich
licencjach.** Apache daje wyraźną licencję patentową i pozwala na dalsze użycie
komercyjne. MIT jest krótsza, ale nie daje tak wyraźnego grantu patentowego;
przy Twojej preferencji wybrałbym Apache. Nie oznacza to przepisania nagłówków
cudzego kodu ani nadania Apache plikom LGPL/MPL.

Samo BSD Chromium nie wymusza otwarcia całego naszego UI. Obowiązki otwarcia
odpowiednich źródeł i możliwość relinkowania wynikają z dołączonych LGPL/MPL.
Otwarte repo Zitter pomaga, ale **sam link do upstream Chromium i numer DEPS nie
zastępują dostarczenia kompletnego, odpowiadającego binarce źródła i poprawek**.
Przy obecnym statycznym linkowaniu i ThinLTO najczyściej dostarczyć odpowiadające
źródła całego builda z poprawkami oraz instrukcję, konfigurację i narzędzia
pozwalające przebudować go ze zmienioną biblioteką LGPL. Alternatywa z plikami
obiektowymi musi rzeczywiście umożliwiać relinkowanie; nie wystarczy dowolny
zbiór obiektów sprzed optymalizacji LTO.

Do publicznego wydania należy dołączyć licencje/notices, wskazanie użycia LGPL,
źródła objęte LGPL/MPL wraz z lokalnymi zmianami i używalną ścieżkę przebudowy.
Nie wolno narzucać zakazu reverse engineering potrzebnego do debugowania zmian
biblioteki LGPL. Zmodyfikowane pliki Apache wymagają oznaczenia zmian, a istniejące
NOTICE i atrybucje trzeba zachować. Nie dostajemy praw do znaków Google/Chrome.

**UnRAR jest wyjątkiem od celu „cały zestaw w pełni open source”.** Jego ograniczenie
celu użycia sprawia, że nie traktujemy tej licencji jak zwykłej licencji otwartej.
Można rozprowadzać przeglądarkę zawierającą UnRAR zgodnie z jego warunkami i nadal
licencjonować własny kod Apache, ale nie oznaczać całej paczki jako wyłącznie
Apache-2.0 ani w pełni FOSS. Dla w pełni otwartej dystrybucji trzeba usunąć lub
zastąpić tę implementację analizy RAR, zachowując świadomie funkcję kontroli
pobranych plików Safe Browsing. Samo usunięcie notice nie rozwiązuje problemu.

Ten audyt przygotowuje inventory i kolekcję notices. Nie powstał jeszcze
publiczny pakiet corresponding source/relink ani publiczny instalator; nie
ogłaszam obecnego deweloperskiego bundla gotowym wydaniem dystrybucyjnym.

Podstawy: [Chromium BSD](https://chromium.googlesource.com/chromium/src/+/1a732fcc23b20dd0bcf38f35d72c1fe1ec799d35/LICENSE),
[Apache-2.0 §§3–4](https://www.apache.org/licenses/LICENSE-2.0),
[LGPL i linkowanie statyczne — GNU FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#LGPLStaticVsDynamic),
[MPL 2.0 §§3.1–3.3](https://www.mozilla.org/en-US/MPL/2.0/),
[MPL 1.1](https://www.mozilla.org/en-US/MPL/1.1/),
[polityka licencyjna LLVM 10](https://releases.llvm.org/10.0.0/docs/DeveloperPolicy.html).
Wersje/licencje poszczególnych komponentów pochodzą z przypiętego lokalnego źródła,
a nie z obecnych gałęzi main tych projektów.

## Odtworzenie

```sh
python3 tools/stage_performance_candidate.py --variant zitter_ui_lab
# Build zitter_ui_lab w out/ZitterOptimized i spakuj osobny bundle jak w raporcie prototypu.
python3 tools/profile_ui.py --app build/ZitterUiProfile.app --trace
python3 tools/audit_licenses.py --app build/Zitter.app --out .chromium/src/out/ZitterOptimized
python3 -m unittest discover -s tests -p 'test_*.py'
```

Instruments: uruchomić osobno `Time Profiler` i `Metal System Trace` na 18 s,
wykonać w środku jeden przebieg Rust/Skia resize z 240 aktualizacjami. Eksport
`xctrace export --toc` zapisać jako `cpu-toc.xml` / `gpu-toc.xml`; tabele
`time-profile` / `metal-gpu-intervals` eksportować przez XPath do
`cpu-samples.xml` / `gpu-intervals.xml`. Logi nazwać `cpu-skia.log` / `gpu-skia.log`.

```sh
python3 tools/analyze_ui_instruments.py build/ui-profile/instruments --kind cpu
python3 tools/analyze_ui_instruments.py build/ui-profile/instruments --kind gpu
```

Perfetto trace processor zgłasza ostrzeżenia konfiguracji zapisu (bufor DISCARD,
brak flush_period_ms) oraz brak obsługi syscall traces arm64. W badanej serii
znaczniki i komplet 120 próbek/etapów są obecne; nie używamy tych śladów do
wnioskowania o syscallach ani jako dowodu braku wszelkich możliwych strat zdarzeń.
CPU scopes w JSON są zagnieżdżonymi czasami ściennymi na wątkach, nie należy ich
sumować ani utożsamiać z próbkowanym czasem CPU Instruments. Granice okna analizy
Instruments są przybliżone na podstawie timestampów logu.
