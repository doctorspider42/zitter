# Rust + Metal — stan i pozostałe prace

Stan na 2026-10-27. **Cel wstrzymany na prośbę użytkownika.** Nie kontynuować
migracji ani nie uruchamiać testów UI bez wznowienia. Kod ostatniego etapu:
`733f04f`, wypushowany na `origin/main`. Wstrzymanie nie oznacza ukończenia celu.

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
2. **Domknąć graf sidebara.** Odwzorować parę kart splitu, jej zakotwiczenie wśród
   przypięć, rzeczywistych rodziców i widoczność w grafie Rust. Obecne logiczne
   kolekcje kart i folderów nie opisują jeszcze całego fizycznego drzewa.
   Niedokończony eksperyment jest wyłącznie lokalnie w
   `build/sidebar-pair-deferred-2026-10-27.patch`; nie jest częścią commita,
   nie jest zweryfikowany i nie należy go stosować bez przeglądu względem HEAD.
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

Po wznowieniu najpierw odczytać stan repo, celu i limitu. Warunek zatrzymania przy
spadku pozostałego limitu tygodniowego poniżej 50% pozostaje zapisany w celu;
aktualna pauza wynika z bezpośredniej prośby użytkownika.
