# Apache 2.0, otwarty backend RAR i pomiar zoptymalizowanego UI

Własny kod Zitter jest teraz objęty **Apache-2.0** ([LICENSE](../LICENSE),
[NOTICE](../NOTICE)). Kod i zasoby upstream zachowują swoje licencje. Zwykła
aplikacja `build/Zitter.app` ma nowy backend RAR; warianty optymalizacji UI są
w osobnym prototypie `build/ZitterUiOptimized.app`. Nie zastępujemy funkcjonalnego
interfejsu przeglądarki demonstracyjnym sidebarem z prostokątów.

## UnRAR → libarchive

Zastąpiono zależność UnRAR w trzech grupach targetów Chromium: common Safe
Browsing, utility Safe Browsing i file-util service. Reader/writer delegaty
pozostają w istniejącym sandboxie, w tym delegaty obfuskowanych plików oraz
rekurencyjna analiza archiwów. Parser otrzymuje już otwarte deskryptory i zapisuje
wyłącznie do przekazanego pliku tymczasowego; nie wypakowuje ścieżek z archiwum
na dysk. Odrzuca ścieżki absolutne, `..`, dowiązania i nieobsługiwane typy.

Nowy backend to przypięte **libarchive 3.8.9**, ze źródłem i sumą SHA-256 w repo.
Rejestrujemy wyłącznie RAR/RAR5 i filtr none. Opcjonalne biblioteki zewnętrzne,
programy CLI i filtry uruchamiające inne programy nie są używane. Iconv pochodzi
z macOS SDK. Biblioteka budowana jest statycznie dla macOS ARM64; inne platformy
nie zostały w tej integracji zweryfikowane.

Limity adaptera: 64 MiB na plik, 1 GiB łącznie i 10 000 wpisów. Przekroczenie,
uszkodzenie danych, nieobsługiwane szyfrowanie lub błąd zapisu kończy analizę
jako nieudaną. Nie zamieniamy takiego wyniku na informację „zawartość jest czysta”.

**Ograniczenie:** libarchive nie odszyfrowuje RAR. Dotyczy to także przypadku,
gdy Chromium ma hasło. Zaszyfrowane archiwum pozostaje nieprzeanalizowane;
nie zachowujemy pełnej funkcjonalności odszyfrowywania UnRAR. Wielowoluminowe
archiwa wymagające kolejnych plików również nie są obsługiwane przez ten adapter.

Test uszkodzonego stored RAR3 ujawnił, że skrót EOF w libarchive może ominąć
sprawdzenie CRC. Build nakłada małą, idempotentną poprawkę walidującą CRC przed
tym skrótem; upstream archive pozostaje niezmieniony. Kod i uzasadnienie poprawki
są w [build_archive.py](../third_party/libarchive/build_archive.py).

Przeszło 11 natywnych przypadków: poprawny stored RAR3, RAR5 z kontrolą treści,
RAR5 solid z czterema plikami, zmienione CRC, ucięcie danych, traversal, RAR4/RAR5
z szyfrowaniem, szyfrowane nagłówki, dowiązanie i błąd zapisu. Przeszły także testy
Python oraz GN header dependency checks dla adaptera i UI.

Legacy nazwy `safe_browsing_use_unrar` i `USE_UNRAR` pozostają nazwami upstream
przełącznika możliwości analizy RAR; nie oznaczają linkowania UnRAR.
W przypiętym checkoutcie nadal mogą istnieć nieużywane źródła upstream UnRAR.
Nie są zależnością żadnego z dystrybuowanych targetów.

[Nowy audyt paczki](licenses/open-source-audit-2026-10-21.json) obejmuje **20/20
unikalnych Mach-O**, dopasowanych do linkowania po UUID, i 325 wpisów paczki.
UnRAR nie występuje w grafach tych targetów, a libarchive występuje.
[Pełny manifest](licenses/open-source-manifest.json.gz) zachowuje wejścia linkera,
SHA-256 i podział na komponenty z bezpośrednim dowodem linkowania oraz szerszy
graf metadanych. [Kolekcja licencji](licenses/open-source-THIRD_PARTY_LICENSES.txt.gz)
została dołączona do aplikacji przed podpisaniem, razem z LICENSE i NOTICE.

Apache nie zmienia obowiązków wobec pozostających komponentów LGPL/MPL.
Przy publicznej dystrybucji nadal potrzebne są odpowiadające źródła, poprawki
oraz możliwość przebudowy/relinkowania wymaganych części. To lokalny podpisany
build deweloperski, a nie opublikowany/notaryzowany instalator ani gotowy pakiet
corresponding source. Szczegóły tych obowiązków opisuje
[pierwotny audyt](ui-profile-and-licenses.md).

## Wdrożone warianty UI

Jedna binarka zawiera `--zitter-ui-variant=baseline|optimized`:

- **Rust/Skia:** bazowy wariant odmalowuje cały sidebar. Zoptymalizowany porównuje
  starą i nową scenę, grupuje invalidacje do najwyżej dwóch pasów (albo jednego przy pełnej zmianie geometrii),
  pozostawia niezmienione raster tiles i pomija prymitywy poza clipem przy paint.
- **Rust/Metal:** bazowy wariant alokuje MTLBuffer na klatkę i używa
  `waitUntilScheduled` + prezentacji w transakcji. Zoptymalizowany ma trzy
  bufory współdzielone z kontrolą ukończenia przez GPU, tworzy bezpieczny spill
  przy zajętym slocie oraz używa `presentDrawable` na kolejce bez synchronicznego
  oczekiwania wątku UI. Zmiana geometrii warstwy nadal jest zatwierdzana przed draw.
- **Views:** wspólny wariant kontrolny; optymalizacja Rust nie zmienia jego kodu
  rysowania. Różnice między jego przebiegami pokazują m.in. szum pomiarowy.

Zmiany dotyczą prototypu, nie kompletnego Rust UI. Tekst, IME, accessibility i
pełne zachowanie produkcyjnego sidebara nadal wymagają osobnego wdrożenia.

## Poprawiony pomiar prezentacji

Nie opieramy głównego wyniku na przewidywanym terminie Chromium. Obserwator
[ScreenCaptureKit](../tools/ui_capture.swift) jest ograniczony do okna procesu
benchmarku na jego wyświetlaczu. Odczytuje mały znacznik numeru klatki rysowany
przez ten sam backend co sidebar i kontroluje aktualny oraz poprzedni wiersz
zaznaczenia. Strona WWW ma drugi znacznik, kodujący jej viewport width; test
resize czeka również na poprawny rozmiar i paint po stronie renderera.

Dopiero zgodny obraz otrzymuje timestamp `SCStreamFrameInfo.displayTime`
WindowServera. Ten pomiar jest niezależny od estymacji Chromium, ale nadal nie
jest pomiarem fizycznych fotonów. ScreenCaptureKit może dostarczyć klatkę jeszcze
przed przypisanym jej czasem wyświetlenia; ujemne `capture_delivery_ms` jest
zachowane, a nie zerowane ani używane jako powód usunięcia próbki.

Nie zapisujemy obrazów ani treści innych okien — tylko ID klatki, timestampy
i wynik kontroli sceny. Odczyt wymaga istniejącego uprawnienia Screen Recording;
narzędzie nie przyznaje sobie uprawnienia ani nie wysyła automatycznie prośby.

Każda próba musi mieć potwierdzenie swojej klatki. Brak, niepoprawna scena lub
niezgodny zegar przerywa przebieg; nie zastępujemy brakujących próbek późniejszą
klatką. Nadal zapisujemy osobno timestamp API Chromium/Metal i czas callbacku.

Aktualizacje mają wspólny rytm początku: 40 ms + deterministyczne 3–15 ms,
z zabezpieczeniem przed kolejną aktualizacją przed feedbackiem/raportowanym
terminem poprzedniej. Koszt szybszego submit nie zwiększa częstotliwości pracy
jednego wariantu względem drugiego. Aktualizator komponentów jest wyłączony
wyłącznie w labie. Ten test mierzy opóźnienie pojedynczych aktualizacji, nie
maksymalny FPS w ciągłej animacji.

Znaczniki i przechwytywanie dodają koszt. Oba warianty mierzymy z identyczną
instrumentacją. Ich wyników nie należy bezpośrednio odejmować od wcześniejszego
benchmarku bez obserwatora i z inną semantyką prezentacji.

## Wyniki końcowe

Build UI: `4f1debe1a99604e9be6d81185e669c98b5bce5e2`, macOS 27.0.1, M3 Pro, 36 GiB, Release ThinLTO/PGO.
36 procesów, 4320 zweryfikowanych aktualizacji; 3 osobne procesy na każdy backend/wariant/scenariusz.
Kolejność wariantów jest odwracana między powtórzeniami. Wszystkie mierzone próbki zachowano.
12 odrzuconych drawable dotyczyło wyłącznie rozgrzewki Metal, przed pomiarem.
[Pełne dane](benchmarks/ui-optimized-2026-10-21.json) zawierają SHA binarki, źródeł UI, obserwatora i strony, GN args oraz surowe próbki.
[Zdarzenia obserwatora](benchmarks/ui-capture-2026-10-21.jsonl.gz) pozwalają ponownie skorelować ID i timestampy.

W tabeli wartości bazowa → zoptymalizowana. Submit oznacza synchroniczny czas ścienny na wątku UI, **łącznie z oczekiwaniem**, a nie wyłącznie zużyty czas CPU.

| Backend / scenariusz | Submit, mediana ms | WindowServer, mediana ms | WindowServer, p95 ms |
|---|---:|---:|---:|
| views / selection | 0.227 → 0.222 | 16.573 → 16.604 | 25.288 → 25.932 |
| rust-skia / selection | 0.193 → 0.204 | 16.970 → 16.563 | 25.943 → 25.309 |
| rust-metal / selection | 1.082 → 0.399 | 14.410 → 13.694 | 24.854 → 22.771 |
| views / resize | 0.916 → 0.935 | 21.134 → 21.178 | 28.535 → 29.596 |
| rust-skia / resize | 0.553 → 0.558 | 21.148 → 21.156 | 29.538 → 29.572 |
| rust-metal / resize | 3.688 → 2.255 | 21.134 → 19.954 | 28.390 → 27.295 |

**Metal:** submit skrócił się o około **63% przy selection** i **39% przy resize**. To przede wszystkim odblokowanie wątku UI. Całkowity czas CPU procesu przeglądarki na aktualizację zmienił się z 4,116 do 3,737 ms przy selection (około −9%), a przy resize pozostał praktycznie równy: 7,426 → 7,433 ms. Samo wykonanie GPU pozostało około 0,07 ms. Nie nazywamy więc spadku submit o 63% spadkiem całkowitego zużycia CPU o 63%.

**Prezentacja:** zbiorcze mediany Metal poprawiły się o około 0,72 ms przy selection i 1,18 ms przy resize. Wynik nie jest jednak równomierny: zmiany median trzech procesów to odpowiednio −1,215 / −1,303 / +0,462 ms oraz −0,074 / +0,076 / −1,593 ms. Przy trzech powtórzeniach i podobnym szumie kontroli Views nie traktujemy tego jako dowodu stabilnego przyspieszenia prezentacji o konkretną wartość. Próbki w jednym procesie są skorelowane; 360 klatek nie oznacza 360 niezależnych eksperymentów.

**Skia:** browser submit i czas resize pozostały bliskie wersji bazowej. Korzyść widać w pracy GPU. Osobna para sprzętowych śladów Metal System Trace, po 120 aktualizacji selection, bez znaczników i bez obserwatora ScreenCaptureKit, wykazała:

| Rust/Skia, selection | Bazowy | Zoptymalizowany |
|---|---:|---:|
| Zajętość GPU: unia przedziałów / aktualizację | 0,392 ms | 0,137 ms |
| Pakiety GPU w oknie pomiarowym | 794 | 287 |
| Łączna unia przedziałów GPU | 47,073 ms | 16,494 ms |

To około **65% mniej pracy GPU** w tej parze śladów. Unia usuwa nakładanie Vertex/Fragment; obejmuje pakiety procesów naszej paczki, nie WindowServera. Nie jest to mediana kosztu klatki ani pomiar energii. Przedział został dopasowany do fazy aktualizacji z logów, a granice są przybliżone. Jedna para służy lokalizacji oszczędności, nie ocenie rozkładu opóźnień. [Dane i przefiltrowane sprzętowe przedziały](benchmarks/ui-gpu-optimized-2026-10-21.json) zachowują również długości nagrań i granice analizowanych okien.

**Wniosek:** optymalizacje oszczędzają pracę GPU w Skia oraz synchroniczny czas wątku UI w Metal. Nie uzyskaliśmy dowodu równie dużego skrócenia opóźnienia całego okna — pozostały koszt obejmuje kompozycję, renderer strony i rytm prezentacji. Nie należy porównywać tych liczb bezpośrednio ze starymi „18 ms” z innego API.

W tym prostym UI asynchroniczny Metal ma najniższą medianę czasu WindowServera, szczególnie dla selection, przy koszcie około **183 MiB** footprintu procesu przeglądarki wobec około **66 MiB** dla Skia/Views. Przy Twoim priorytecie responsywności warto rozwijać tę ścieżkę jako następny prototyp. Nie przesądza to przewagi kompletnego UI z tekstem, IME i accessibility; nie przenosimy jeszcze produkcyjnej przeglądarki na demonstracyjny renderer.

Wcześniejsze próby diagnostyczne nie są mieszane z końcową serią: `build/ui-performance/20261021T171456190064Z/` przerwano po wykryciu kosztu wielu invalidacji, przed poprawką grupowania. Pierwszą parę GPU `build/ui-gpu-optimized/20261021T172913833336Z/` wykluczono, ponieważ stały czas nagrywania nie objął końca bazowego procesu. Nowy recorder zatrzymuje się po zakończeniu aplikacji, a analyzer odrzuca ślad niepokrywający całej fazy. Końcowa seria to `20261021T171922957672Z`; poprawne ślady GPU to `20261021T173357009039Z`.

Walidacja końcowa: **56 testów Python**, **11 przypadków natywnych RAR**, kompilacja Release, GN header checks i weryfikacja podpisów obu paczek. Odczyt obrazu sprawdzał też poprzedni wiersz zaznaczenia i zgodność viewportu strony podczas resize.

## Odtworzenie

```sh
python3 tools/install_archive_backend.py .chromium/src
python3 tools/stage_performance_candidate.py --variant zitter_ui_lab
# Zbuduj zitter_ui_lab, zitter_optimized i chrome w out/ZitterOptimized.
# Do testów natywnych dodaj //third_party/zitter_libarchive:rar_reader_tests
# do root_extra_deps i zbuduj rar_reader_tests.
python3 tools/test_archive_backend.py --binary .chromium/src/out/ZitterOptimized/rar_reader_tests
swiftc -parse-as-library -O tools/ui_capture.swift -o build/ui_capture
python3 tools/benchmark_ui.py --app build/ZitterUiOptimized.app \
  --iterations 120 --rounds 3 --variants baseline optimized --capture build/ui_capture
# Uruchomić osobno, po benchmarkach, bez równoległego builda:
python3 tools/profile_ui_gpu.py --app build/ZitterUiOptimized.app
```

Pakowanie z notices: najpierw wygenerować targetowy audyt, a przy końcowym
pakowaniu podać `--third-party-licenses PATH/THIRD_PARTY_LICENSES.txt`; potem
ponownie zinwentaryzować podpisaną paczkę. Nie wnioskujemy o licencjach wyłącznie
z Cargo.toml ani z listy bibliotek załadowanych w jednej próbie.

Źródła: [libarchive i obsługiwane formaty](https://github.com/libarchive/libarchive),
[libarchive 3.8.9](https://github.com/libarchive/libarchive/releases/tag/v3.8.9),
[Apple: displayTime](https://developer.apple.com/documentation/screencapturekit/scstreamframeinfo/displaytime),
[Apple: presentsWithTransaction](https://developer.apple.com/documentation/quartzcore/cametallayer/presentswithtransaction).
