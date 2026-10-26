# Rust + Metal — przerwa przy limicie tygodniowym

Cel ma status **paused**: pozostało **69%** limitu tygodniowego. Praca została
zatrzymana zgodnie z warunkiem użytkownika; polecenie obejmuje też wyłączenie Maca.
Pełna zgodność UI nie została jeszcze osiągnięta.

Ostatni sprawdzony kod: `c7d0eab` (raport wypushowany w `b189ea5`).
Przełączniki, checkboxy i selektory korzystają z hostów Rust bez ButtonController.
Selektory otwierają popup na naciśnięciu myszy, z natywną blokadą ponownego
kliknięcia przez 100 ms. Klawiatura/AX omijają tę blokadę. Model odrzuca stare
callbacki, zamyka popup po wycięciu kotwicy i odsłania ją przed otwarciem przez AX.

Przeszły **293 testy Rust**, **11 Metal / 7 Views celowanych** i **61 Metal /
42 Views regresyjnych**, 180 przypadków font/kierunek plus raster, 68 testów
Pythona lokalnie i z czystego archiwum, GN/boundaries oraz końcowy Ninja no-op.
[Raport etapu](benchmarks/rust-select-pointer-targeted-2026-10-26.json).

Pełny checkpoint dla połączonych zmian został **przerwany**, po pozycji **130/218
Metal**; Views jeszcze nie ruszył. Zatrzymano wyłącznie procesy tej macierzy.
Nie jest to pełna akceptacja ani awaria produktu. Manifest/logi pozostają w
`build/ui-checkpoint-20261026T224725Z`.
[Zapis przerwania](benchmarks/rust-select-pointer-interrupted-2026-10-26.json).
Ostatnia ukończona pełna macierz dotyczy wcześniejszego kodu `dadea9b`:
210 Metal / 162 Views plus po sześć oczekiwanych pominięć.

Po wznowieniu:

1. Powtórzyć przerwany pełny checkpoint na spójnym źródle/binarkach; oczekiwany
   inwentarz to 218 przypadków, w tym 48 wyłącznie Metal. Skrypty lokalne:
   `build/run_full_select_pointer_checkpoint.py` i
   `build/report_full_select_pointer.py`; jawny UI source commit `c7d0eab`.
2. Domknąć pozostałe dekoracyjne/glyph Buttony i sprawdzić drzewo rzeczywistych
   kontrolek. Następnie kontynuować stabilne węzły Rust i ograniczanie natywnej
   tożsamości View, kontenerów, koordynacji i własności menu/popupów Views.
3. Wykonać ręczne testy trackpada, IME, VoiceOver, RTL, skali/motywu, fokusu i
   odzyskiwania GPU oraz kontrolowane pomiary CPU/GPU, prezentacji, opóźnienia
   wejścia i pamięci. Niefatalne diagnostyki kompozytora nadal są otwarte.
4. Zweryfikować docelową aplikację z aktualną polityką sieciową, izolację/zapis/
   rollback i finalne licencje/notices. Zamrożony UI lab tego nie zastępuje.
