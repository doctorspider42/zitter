# Zitter: audyt ruchu w tle i plan uniezależnienia od usług Google

Data: 23 października 2026, Europe/Warsaw. Audyt lokalnego macOS ARM64.
Status: pomiary istniejącej aplikacji i eksperymenty z argumentami uruchomienia;
nie wdrożono jeszcze polityki sieciowej w aplikacji.

Późniejsze wdrożenie i oddzielną weryfikację opisuje
[raport zmian bezpieczeństwa](security-implementation-2026-10-23.md).
Poniższe pomiary zachowują stan pierwotnej aplikacji.

## Najważniejsze ustalenia

Zitter wykonuje automatyczne połączenia z Google również bez otwierania stron.
Własny interfejs Rust/Metal nie wyłącza usług uruchamianych przez zaplecze Chromium.
Potwierdzono odpowiedzi serwerów, a nie tylko obecność adresów w kodzie.

Szczególnie istotne są dwa ustalenia:

1. Interfejs Zittera wybiera domyślnie DuckDuckGo, ale odziedziczona usługa
   AI Mode nadal kontaktuje się z Google. Własny wybór wyszukiwarki nie jest
   połączony z `TemplateURLService` profilu Chromium.
2. Aktualizacja list Safe Browsing otrzymuje HTTP 400. Oficjalna, nieszkodliwa
   strona testowa phishingu została załadowana. Nie można traktować obecnej
   kompilacji Safe Browsing jako potwierdzenia działającej ochrony.

Eksperymentalne wyłączenie pięciu funkcji usuwa zaobserwowane połączenia AI Mode,
czasu, sieciowego Autofill i modelu rankingu tłumaczeń. Aktualizacje komponentów
pozostają aktywne. Jest to dowód możliwości ograniczenia ruchu małą zmianą
konfiguracji, nie certyfikat bezpieczeństwa ani gotowe ustawienie produkcyjne.

## Badana aplikacja i granice wnioskowania

- Bundle: `build/Zitter.app`; plik wykonywalny `Contents/MacOS/Chromium`.
- SHA-256: `9b7a3e217c3c31fb64b17f87c13ed83f8d548377e6ff40bd7aa974b7fdc3b630`.
- Czas modyfikacji pliku: 2026-10-21 17:09:51.968 UTC.
- Wersja zgłoszona przez aplikację: Chromium `158.0.8106.0`, V8 `15.8.6`.
- Rewizja Chromium w checkoutcie i metadanych nakładki:
  `1a732fcc23b20dd0bcf38f35d72c1fe1ec799d35`.
- HEAD repozytorium podczas pomiarów:
  `324658cbeb88b5372fd3eca2ff13eb2bac74164e`.
- Runtime zgłasza zerowy identyfikator rewizji. Powiązanie z upstreamem opiera się
  na lokalnych metadanych buildu, nie na samodzielnej atestacji binarki.

Nie przebudowano aplikacji. Nakładka `zitter_optimized` ma zgodne z aktualnymi
źródłami `main.cc`, `browser_client.cc` i `workspace_preconnect.cc`; część innych
plików, np. `runtime.cc`, różni się. W czasie audytu pojawiały się również inne
zmiany robocze w repozytorium. Wyniki runtime dotyczą powyższego SHA-256, a nie
wszystkich nieprzebudowanych zmian interfejsu.

Wygenerowane flagi badanego katalogu buildu: `CHROMIUM_BRANDING=1`,
`GOOGLE_CHROME_BRANDING=0`, `FULL_SAFE_BROWSING=1`,
`SAFE_BROWSING_DB_LOCAL=1`, `SAFE_BROWSING_DOWNLOAD_PROTECTION=1`.
GN pokazuje puste własne klucze API i identyfikatory OAuth; w środowisku procesu
audytu nie podano zmiennych `GOOGLE_API_KEY`, `GOOGLE_DEFAULT_CLIENT_ID`,
`GOOGLE_DEFAULT_CLIENT_SECRET`. Brak kluczy nie uniemożliwia wysłania żądania.

## Metoda i materiał dowodowy

Narzędzie: [audit_background_network.mjs](../tools/audit_background_network.mjs).
Node 24, standardowe biblioteki i wbudowany WebSocket. Każdy scenariusz dostaje
syntetyczny, oddzielny `--user-data-dir`; restart używa tego samego profilu
testowego. Nie odczytywano profili użytkownika ani cookies jego stron.

Pomiar używa Chromium NetLog w trybie `Default`, od startu procesu do zwykłego
zamknięcia przez CDP. CDP jest dostępny na lokalnym porcie i służy do kontroli
scenariusza. Nie użyto `--no-sandbox`, ignorowania certyfikatów ani wyłączenia
Safe Browsing w próbach bazowych. Użyto sztucznego Keychain, aby oddzielić test
od systemowego magazynu haseł; nie testuje to zachowania produkcyjnego Keychain.
Pobieranie pliku tekstowego przekierowano do katalogu audytu.

Dodatkowo próbkowano gniazda procesu i jego potomków przez `lsof` co około
5 sekund. Potwierdza to ruch poza localhost, w tym UDP/443 i TCP/80. Nie jest
to pełny przechwyt pakietów: bardzo krótkie połączenia i ruch zlecony usługom
systemowym mogą nie być widoczne w takim próbkowaniu.

Zestaw scenariuszy:

| Próba | Czas obserwacji | Działanie |
|---|---:|---|
| fresh-onboarding | 120 s | Całkowicie nowy katalog, brak otwierania witryn |
| fresh-ready-idle | 120 s | Syntetycznie ukończony onboarding, puste karty |
| warm-ready-idle | 120 s | Zwykły restart tego samego profilu |
| local-features | 120 s | Strona wyłącznie na 127.0.0.1, formularz, fetch i pobranie TXT |
| external-navigation | 120 s | Przywrócenie kart z https://example.com |
| suppressed-control | 120 s | Puste karty; jawnie wyłączone background networking i component update |
| targeted-idle | 90 s | Puste karty; pięć wyłączeń konkretnych funkcji |
| targeted-local | 90 s | Lokalny formularz/fetch/download; te same pięć wyłączeń |
| safebrowsing-check | 90 s | Oficjalna strona testowa phishingu, bez wpisywania danych |
| long-idle | 420 s | Nowy profil gotowy do pracy, bezczynność |
| dns-failure | 90 s | Nawigacja do zarezerwowanej domeny `.invalid` |

W pomiarze nawigacji przywracane są dwie syntetyczne przestrzenie, dlatego
liczba żądań do strony może być większa niż przy pojedynczej karcie.
Lokalny formularz sprawdza automatyczną klasyfikację pól i wpisanie wartości;
nie jest testem zapisu adresu, karty płatniczej ani przesyłania formularza.

Surowe logi, profile, polecenia, wyniki CDP i manifesty zachowano w ignorowanym
`build/network-audit/2026-10-23-*`. Przenośny zestaw wyników i hashe logów:
[network-audit-2026-10-23.json](audits/network-audit-2026-10-23.json).
Surowy NetLog nie jest materiałem do publikacji bez ponownej oceny prywatności;
tryb Default nie usuwa wszystkich adresów URL i metadanych urządzenia.

Liczby w podsumowaniu oznaczają zdarzenia `URL_REQUEST_START_JOB`, z rozdzieleniem
hostów po przekierowaniu. Nie oznaczają liczby pakietów, różnych połączeń TCP ani
udanych operacji usługi. Osobno zapisano HTTP 200, HTTP 400, błędy transportowe,
próby DNS, TCP i TLS. Brak błędu transportu nie zmienia HTTP 400 w sukces API.

Wszystkie 11 scenariuszy zakończyło się kodem 0 i kompletnym NetLogiem.
Obserwator gniazd zapisał 126 próbek, w 122 widząc połączenia poza localhost.
Porównanie wybranych prób (liczniki żądań URL):

| Próba | Czas Google | AI Mode | Updater | CDN | Model tłumaczeń | Safe Browsing | Autofill |
|---|---:|---:|---:|---:|---:|---:|---:|
| fresh-ready-idle, 120 s | 1 | 4 | 16 | 13 | 2 | 1 / HTTP 400 | 0 |
| warm-ready-idle, 120 s | 0 | 4 | 14 | 10 | 0 | 1 / HTTP 400 | 0 |
| local-features, 120 s | 1 | 4 | 26 | 22 | 2 | 0 | 2 / HTTP 400 |
| suppressed-control, 120 s | 1 | 4 | 0 | 0 | 2 | 0 | 0 |
| targeted-idle, 90 s | 0 | 0 | 5 | 3 | 0 | 0 | 0 |
| targeted-local, 90 s | 0 | 0 | 23 | 20 | 0 | 0 | 0 |
| long-idle, 420 s | 1 | 4 | 26 | 22 | 2 | 1 / HTTP 400 | 0 |

Zera są wynikiem danego okna obserwacji, nie zapewnieniem trwałego wyłączenia.
Szczególnie brak Safe Browsing w krótszej próbie nie potwierdza działania ani
wyłączenia tej usługi. Próba zbiorczego wyciszenia pozostawia czas, AI Mode
i model tłumaczeń; ten zestaw argumentów nie zapewnia braku ruchu Google.

W próbie błędu DNS odnotowano `ERR_NAME_NOT_RESOLVED` (-105) dla domeny
`.invalid`. Nie zaobserwowano w tym NetLogu zdarzeń zawierających 8.8.8.8 /
8.8.4.4 ani oddzielnego resolver request do `google.com`. Diagnostyka publicznym
DNS Google pozostaje więc kandydatem wynikającym ze źródeł, a nie potwierdzoną
aktywną integracją tej próby.

## Potwierdzone usługi

| Host / ścieżka | Obserwacja | Funkcja i decyzja |
|---|---|---|
| `clients2.google.com/time/1/current` | HTTP, odpowiedź 200, także na pustym profilu | Podpisany protokół czasu Chromium. Zastąpić świadomie czasem systemowym / własną usługą; zachować walidację czasu certyfikatów. |
| `www.google.com/async/folae` | HTTPS 200, przy starcie i po restarcie | Sprawdzanie uprawnień do AI Mode. Wyłączyć usługę/funkcję; jest niezależne od rysowania naszego paska. |
| `update.googleapis.com/service/update2/json` | HTTPS 200, wiele żądań | Updater komponentów, nie aktualizator całej aplikacji Zitter. Rozdzielić składniki konieczne i zbędne. |
| `edgedl.me.gvt1.com/edgedl/...` | HTTP 200, pobieranie paczek i różnic komponentów | CDN dystrybucji komponentów Google. Docelowo kontrolowana dystrybucja potrzebnych komponentów. |
| `www.gstatic.com/chrome/intelligence/assist/ranker/models/translate/...` | HTTPS 200 | Pobranie modelu decydującego o proponowaniu tłumaczenia; nie dowód przesłania tekstu strony do tłumaczenia. Wyłączyć ranker lub dostarczyć model lokalnie. |
| `safebrowsing.googleapis.com/v5/hashLists:batchGet` | HTTPS 400 w próbach bazowych | Próba aktualizacji list zagrożeń. Pilnie wyjaśnić brak skutecznej aktualizacji i zweryfikować ochronę end-to-end. |
| `content-autofill.googleapis.com/v1/pages/...` | HTTPS 400 dla syntetycznego lokalnego formularza | Sieciowe rozpoznawanie pól formularza. Wyłączyć komunikację z serwerem, osobno zachować i testować lokalne hasła/Autofill. |

HTTP w protokole czasu i pobieraniu komponentów nie jest samo w sobie dowodem,
że zaakceptowano niepodpisany kod. Chromium ma oddzielną kryptograficzną
weryfikację odpowiedzi/paczek. W nowej dystrybucji należy stosować HTTPS oraz
zachować weryfikację podpisów i ochronę przed cofnięciem wersji.

Ścieżka zapytania Autofill zawiera zakodowane metadane formularza. Pomiar nie
udowadnia przesłania wpisanej wartości „Audit Person”; nie przechwytywano treści
TLS poza tym, co zapisuje NetLog. Źródła wskazują na mechanizm crowdsourcingu
klasyfikacji formularzy. Powinien być wyłączony niezależnie od tego, że API
odrzuca te żądania.

### Dlaczego Google mimo DuckDuckGo

`WorkspaceShell` i `BrowserTools` obsługują własny wybór wyszukiwarki. Usługa
`AimEligibilityService` odczytuje natomiast `TemplateURLService` Chromium.
W źródle `GetRequestUrl()` odrzuca żądanie, jeśli domyślny dostawca tego serwisu
nie jest Google; żądania z pomiaru oraz brak powiązania obu konfiguracji wskazują
na rozjazd ustawień. Wyłączenie samego przycisku AI lub `AIModeSettings=1` nie
jest wystarczające: adnotacja sieciowa upstreamu mówi, że ta polityka może
zatrzymać użycie odpowiedzi, pozostawiając samo pobieranie.

### Co naprawdę pobrał updater

Na świeżym profilu zapisano i odczytano manifesty m.in.:

- `CertificateRevocation` / CRLSet, wersja `10854`;
- `PKIMetadata`, wersja `1815`;
- `FileTypePolicies`, wersja `145.0.7584.0`;
- `SafetyTips`, wersja `3092`;
- `SSLErrorAssistant`, wersja `7`;
- `OptimizationGuideModelsManifest`, `OptimizationHints`, `OnDeviceHeadSuggestModel`;
- SODA i pakiet językowy en-US, modele mowy / dyktowania;
- `WasmTtsEngine`, `DictationConnector`, `ZxcvbnData`;
- `FirstPartySetsPreloaded`, `TrustTokenKeyCommitments`, `Crowd Deny`;
- `ActorSafetyLists`, `CaptchaProviders`, `AmountExtractionHeuristicRegexes`, `MEIPreload`.

Pobranie modelu mowy nie jest dowodem nagrywania mikrofonu ani przesłania audio.
Lista pokazuje natomiast, że domyślna rejestracja komponentów jest znacznie
szersza niż aktualnie potrzebna funkcjonalność Zittera. Aktualizator otrzymuje
metadane wersji produktu/komponentów; nie wykazano tym pomiarem przesyłania listy
przypiętych kart, katalogu przestrzeni ani historii odwiedzin przez updater.

## Ochrona Safe Browsing: ustalenie wymagające naprawy

Oficjalny [test Google](https://testsafebrowsing.appspot.com/) wskazuje
`https://testsafebrowsing.appspot.com/s/phishing.html` jako stronę, która powinna
pokazać ostrzeżenie na macOS. Badany Zitter załadował jej dokument i zasoby.
CDP zwróciło tekst „Phishing Page Example.” oraz „This is an example of a
phishing page.”; historia nawigacji zawierała ten dokument. Nie wpisano loginu
ani hasła i nie pobierano testowych plików wykonywalnych.

Ten wynik, razem z HTTP 400 aktualizacji list w innych próbach, nie pozwala
uznać ochrony antyphishingowej za działającą. Sam pomiar nie rozstrzyga pełnej
przyczyny: trzeba osobno sprawdzić autoryzację i konfigurację API, pobranie list,
integrację sprawdzania nawigacji oraz prezentację interstitiali w naszych
`WebContents`. Nie zakładać, że naprawienie wyłącznie klucza API wystarczy.
Nie wyciągać też wniosku, że wszystkie mechanizmy bezpieczeństwa są wyłączone:
sandbox, TLS i aktualizowane dane PKI są odrębnymi mechanizmami.

## Obecne w źródłach, lecz niepotwierdzone jako aktywny ruch w tym teście

| Mechanizm | Ustalenie / dalszy test |
|---|---|
| Variations | `IsFetchingEnabled()` domyślnie nie pobiera seedów w niebrandowanym buildzie bez nadpisania URL. Nie obserwowano pobierania. |
| Crash reporting / UMA / UKM | Nie obserwowano uploadów. Crash reporter zwraca brak zgody dla buildów bez Google branding. Brak uploadu w krótkim teście nie obejmuje wszystkich polityk i scenariuszy awarii. |
| Google Sign-in / Sync | Nie obserwowano synchronizacji ani logowania przeglądarki. Wygenerowany build nadal zawiera obsługę DICE, więc brak UI nie jest równoważny usunięciu kodu integracji. |
| Geolokalizacja | macOS domyślnie używa trybu HybridPlatform z możliwością fallbacku do sieciowego dostawcy Google. Nie udzielano zgody ani nie pobierano rzeczywistej lokalizacji w audycie. Docelowo PlatformOnly. |
| Diagnostyka błędów DNS | Upstream zawiera próby `google.com` z publicznymi resolverami 8.8.8.8 / 8.8.4.4. Osobna próba `.invalid` rozdziela błąd strony od zwykłego tła; kod tej funkcji nie dowodzi jej uruchomienia w naszym hoście. |
| Rozszerzone sprawdzanie pisowni, zdalne tłumaczenie, Cast, zdalne Optimization Guide | Brak dowodu aktywnych żądań do tych konkretnych API w zbadanych scenariuszach. Nie utożsamiać pobrania modelu/komponentu z użyciem zdalnej usługi. |
| WebRTC, osadzone fonty/Analytics/YouTube, service workers stron | Ruch zależny od odwiedzanej strony i jej uprawnień. Wymaga oddzielnej polityki prywatności witryn, nie usuwania usług przeglądarki. |

Pierwsze obserwowane pobrania oraz interwały aktualizacji mają losowe opóźnienia.
420 sekund bezczynności nie obejmuje cyklu updatera wynoszącego około 5 godzin,
wszystkich retry Safe Browsing, wznowienia systemu, zmian sieci, wszystkich
uprawnień, rozszerzeń ani istniejącego profilu z rzeczywistymi danymi. NetLog
nie obejmuje automatycznie każdego natywnego procesu i usług macOS.
Nie jest to deklaracja „kompletna lista wszystkich przyszłych połączeń”.

## Plan wdrożenia z zachowaniem bezpieczeństwa i aktualizowalności

### Etap 0 — bramka bezpieczeństwa i punkt odniesienia

1. Naprawić i zweryfikować aktualizację list oraz faktyczne ostrzeganie przed
   testową stroną phishingu/malware. Dodać test na zwykłym hoście Zittera, a nie
   tylko na standardowym oknie Chrome lub mocku bazy Safe Browsing.
2. Potwierdzić blokowanie nieprawidłowych certyfikatów, izolację profili,
   sandbox rendererów i obsługę niebezpiecznego pobrania na bezpiecznym fixture.
3. Zachować manifest binarki, NetLog i listę wersji komponentów jako bazę
   porównania. Awarie i brak aktualizacji ochrony muszą być widoczne diagnostycznie.

Kryterium przejścia: dowód działania ochrony, nie tylko brak błędów kompilacji.

### Etap 1 — wyłączyć zbędne wywołania bez odcinania ochrony

Wprowadzić pojedynczy moduł polityki w nakładce Zittera, np.
`chromium/app/network_policy.{h,cc}`. Ustawiać politykę funkcji przed
inicjalizacją usług i profili; preferencje lokalne/profilowe przed użyciem
konkretnej usługi. Uwzględnić profil startowy `Default`, wszystkie przestrzenie,
nowe/importowane profile i istniejące ustawienia. Nie opierać się na ręcznych
argumentach uruchomienia ani ustawieniu jednej kontrolki w UI.

Zmiany pierwszej kolejności:

- Wyłączyć `AimEnabled`; samo `AIModeSettings` nie zatrzymuje wszystkich żądań.
- Wyłączyć `AutofillServerCommunication`, zachowując lokalne hasła i heurystyki.
- Wyłączyć `TranslateRankerQuery` i `TranslateRankerEnforcement` albo dostarczać
  zatwierdzony model z aplikacją. Sam ranker nie wykonuje tłumaczenia.
- Dla czasu wybrać zegar macOS jako źródło z jasno określonym zachowaniem przy
  błędnej dacie. Wyłączenie `NetworkTimeServiceQuerying` usuwa pomocniczą
  diagnostykę poprawności czasu; nie może prowadzić do omijania błędów TLS.
- Uzgodnić własną wyszukiwarkę z `TemplateURLService`; niezależnie wyłączyć
  nieużywane usługi AI i zdalnej optymalizacji.
- Jawnie wyłączyć browser sign-in / Sync, zdalne raportowanie i rozszerzone
  sprawdzanie pisowni. Zwykłe logowanie do witryny Google ma pozostać możliwe.
- Dla geolokalizacji wybrać natywny provider macOS bez fallbacku Google.
  Usługi systemowe Apple pozostają oddzielną zależnością.
- Sprawdzić diagnostykę DNS, DoH i preconnect. Nasz preconnect kontaktuje się
  z wpisanym originem przed Enter i respektuje preferencję predykcji; nie jest
  stałym połączeniem z Google. W wariancie maksymalnej prywatności wyłączyć go
  domyślnie lub zapewnić czytelny wybór.

Potwierdzony zestaw eksperymentalny:

```text
--disable-features=AimEnabled,NetworkTimeServiceQuerying,AutofillServerCommunication,TranslateRankerQuery,TranslateRankerEnforcement
```

W obu próbach z tym zestawem zniknęły cztery odpowiadające mu grupy usług.
Pozostał updater i CDN komponentów. Nie udowadnia to sprawności lokalnego
Autofill/całej przeglądarki po wdrożeniu; wymagane są odrębne testy funkcjonalne.

### Etap 2 — kontrolowana dystrybucja komponentów

Nie stosować zbiorczego `--disable-component-update` jako końcowego rozwiązania.
Przestaje wtedy działać również dostarczanie danych bezpieczeństwa.
CRLSets służą m.in. awaryjnemu blokowaniu certyfikatów; PKI Metadata aktualizuje
m.in. trust store i dane CT. Aktualizacja samej aplikacji raz na jakiś czas nie
jest automatycznie równoważna szybkiemu kanałowi danych bezpieczeństwa.
[CRLSets](https://www.chromium.org/Home/chromium-security/crlsets/),
[aktualizacja Chrome Root Store](https://chromium.googlesource.com/chromium/src/+/main/net/data/ssl/chrome_root_store/faq.md).

Zdefiniować rejestr potrzebnych komponentów: identyfikator, cel, źródło, podpis,
zgodność z rewizją Chromium, maksymalny wiek, sposób aktualizacji i test odbioru.
CRLSet, PKI Metadata, polityki typów plików i wybrane listy ochronne mają osobne
uzasadnienie bezpieczeństwa. Modele AI, mowy, podpowiedzi, heurystyki zakupowe
i dane mediów nie powinny być pobierane automatycznie bez używanej funkcji.
Składniki dostępności wymagają świadomego zachowania funkcji lub pobrania na
żądanie; ich usunięcie nie może być niewidoczną regresją dostępności.

Docelowo klient pobiera zatwierdzone dane z infrastruktury Zittera, a proces
budowania/dystrybucji nadal może pobierać upstream Google. To usuwa bezpośrednie
połączenia użytkownika, nie zależność od pochodzenia tych danych. Pełna
niezależność danych to dodatkowy projekt, zwłaszcza dla revocation i threat feeds.

Własny endpoint musi obsłużyć protokół i weryfikację odpowiedzi, oryginalne
podpisy CRX oraz własne podpisane metadane, kontrolę wieku, zgodności wersji
i ochronę przed rollbackiem. Samo podmienienie adresu aktualizacji nie wystarczy.
Zachować ostatni poprawny zestaw; stan przeterminowania ochrony nie może być
raportowany jako „wszystko aktualne”. Ustalić osobny szybki kanał awaryjny.

### Etap 3 — działająca ochrona antyphishingowa z docelowym dostawcą

Opcja przejściowa: jawnie dopuścić tylko działające usługi bezpieczeństwa Google,
po wyłączeniu funkcji zbędnych. Jest to ograniczenie zależności, nie „zero Google”.

Opcja docelowa: adapter dostawcy list/reputacji z lokalnymi listami/prefiksami
i możliwie oszczędnym zapytaniem uzupełniającym, z zachowaniem ostrzeżeń,
anulowania nawigacji i ochrony pobrań Chromium. Zmierzyć pokrycie, opóźnienia,
fałszywe alarmy i dostępność dostawcy; nie zakładać równoważności przypadkowej
listy blokad z Safe Browsing. Pełne URL-e nie powinny być wysyłane domyślnie.

Publiczne API Safe Browsing i prywatne API Chrome nie są tym samym produktem.
Google wskazuje Safe Browsing API do użytku niekomercyjnego, a Web Risk do
zastosowań komercyjnych. Wybór dostawcy i uprawnień trzeba ustalić przed
integracją produkcyjną. [Dokumentacja Google](https://developers.google.com/safe-browsing).

Nie wyłączać sandboxa, izolacji witryn, weryfikacji TLS, CT ani ostrzeżeń pobrań
w celu uzyskania wyniku „zero połączeń”. Firewall blokujący wszystkie domeny
Google uszkodzi również witryny i nie zastąpi selektywnej polityki usług.

### Etap 4 — utrzymywać politykę podczas aktualizacji Chromium

- Moduł polityki i adaptery w źródłach Zittera; ustawienia GN tylko tam, gdzie
  istnieją wspierane flagi. Nie masowo usuwać katalogów upstreamu i nie
  przepisywać ciągów `google.com` w całym drzewie.
- Niezerowe patche upstreamu trzymać małe, osobne i z kontrolą oczekiwanego
  kształtu kodu w instalatorze nakładki.
- Przy każdym podniesieniu `chromium/revision.json`: synchronizacja DEPS,
  nałożenie patchy, build, testy ochrony i ruchu. Sprawdzać istnienie symboli
  wyłączanych funkcji; nieznana nazwa `--disable-features` może być zignorowana.
- Automatyczna dzienna detekcja wydań Stable i sprawdzanie kolejnej wersji na
  oddzielnym buildzie. Bump dopiero po testach; aktualizacja silnika nie czeka
  na nowe funkcje interfejsu.
- Test odbioru: świeży profil, restart, profil migrowany, formularz/pobranie,
  wyszukiwanie, błąd DNS, lokalizacja odmówiona/przyznana, brak sieci, wznowienie,
  minimum sześciogodzinna bezczynność. Porównać też natywne gniazda i UDP/QUIC.
- Lista dozwolonych usług zależy od scenariusza. Nowy endpoint usługowy powinien
  zatrzymywać publikację do wyjaśnienia; nie klasyfikować wyłącznie po końcówce
  domeny, bo dostawcy używają CDN i mogą zmieniać hosty.
- Osobne profile: tryb bez synchronizacji, świadomie włączona synchronizacja
  Google Drive i ruch stron użytkownika. Synchronizacja opt-in jest jawnym
  wyjątkiem, nie uzasadnieniem dowolnego ruchu Google w tle.

## Synchronizacja pinów, przestrzeni i ikonek przez Google

### Zalecenie: Google Drive appDataFolder + własny moduł synchronizacji

Można użyć Google do przechowywania danych bez budowania własnego serwera.
Drive ma ukryty folder danych aplikacji i ograniczony zakres OAuth
`https://www.googleapis.com/auth/drive.appdata`. Folder nie jest widoczny w
zwykłym UI Drive i jest dostępny dla tej aplikacji. To izolacja dostępu API,
nie szyfrowanie end-to-end. Użytkownik może usunąć te dane.
[Dokumentacja appDataFolder](https://developers.google.com/workspace/drive/api/guides/appdata).

Google zapewnia autoryzację i magazyn. Zitter nadal implementuje schemat danych,
łączenie zmian z urządzeń, usuwanie, kolejność, pracę offline, migracje, retry,
integralność i szyfrowanie. Drive API udostępnia dziennik zmian również dla
`appDataFolder`; nie rozwiązuje on konfliktów modelu aplikacji.
[changes.list](https://developers.google.com/workspace/drive/api/reference/rest/v3/changes/list).

Nie używać prywatnego backendu Chrome Sync jako podstawy produktu. Google
ograniczył dostęp zewnętrznych przeglądarek. Dodatkowo własne piny/przestrzenie
Zittera nie stają się automatycznie danymi znanymi standardowemu Chrome Sync.
[Ogłoszenie Chromium](https://blog.chromium.org/2021/01/limiting-private-api-availability-in.html).

### Granice danych

| Synchronizować jako własne obiekty | Pozostawić lokalnie w pierwszej wersji |
|---|---|
| Piny, foldery, tytuły, URL-e, kolejność i stabilne ID | Cookies, tokeny sesji, hasła, Keychain |
| Przestrzenie, nazwy, kolory, emoji | Cache, bazy Chromium i stan procesów |
| Nazwy/ID profili i powiązania przestrzeń–profil | Ścieżki katalogów profili i identyfikatory importu z innego komputera |
| Wygląd, wyszukiwarki i świadomie wybrane ustawienia | Uprawnienia do mikrofonu/kamery/lokalizacji i wyjątki bezpieczeństwa |
| Ikonki: identyfikator wbudowanej grafiki lub mały zasób pod hashem | Tożsamość ostatnio aktywnego okna i rozmiary okien, chyba że użytkownik wybierze inaczej |

Obecnie `ZitterWorkspaces.json` i `ZitterAppearance.json` zawierają dużą część
metadanych przestrzeni/pinów/emoji, a `ZitterSession.json` sesję kart.
To źródła do eksportu obiektów synchronizacji, nie format plików do bezpośredniego
nadpisywania z chmury. `ProfileInfo` ma ID, nazwę, katalog i `source_id`; nie ma
jeszcze osobnego pola awatara profilu. Ikonki profili trzeba jawnie dodać do modelu.
Na nowym urządzeniu profil otrzymuje lokalny katalog i lokalne sesje logowania.
Synchronizacja nazwy/ikonki nie oznacza przeniesienia zalogowania do stron.

### Proponowana implementacja

1. `SyncModel` i `SyncEngine` poza zależnościami Chromium; `SyncTransport` z
   implementacją Drive REST. Na początku można dostarczyć tylko Drive, ale
   pozostawić możliwość innego transportu. Adapter Chromium odtwarza metadane
   profilu i uruchamia zwykłe mechanizmy aplikacji.
2. Stabilne UUID obiektów i urządzeń, wersja schematu, ID operacji, monotoniczny
   licznik urządzenia, tombstones usunięć. Zmiany offline jako dziennik lokalny.
   Równoległe edycje i przesunięcia pinów mają deterministyczne scalanie;
   zabezpieczyć drzewo folderów przed cyklami i osieroceniem.
3. W Drive nie nadpisywać jednego wspólnego pliku stanu metodą „ostatni zapis
   wygrywa”. Preferować niezmienne paczki operacji per urządzenie plus snapshoty
   i kursory. Retry deduplikuje operacje po ID. Kompaktowanie usuwa tombstones
   dopiero po zaprojektowanym protokole potwierdzeń/wycofywania starych urządzeń.
4. Logowanie w zewnętrznej przeglądarce systemowej, OAuth Desktop App,
   Authorization Code + PKCE, losowy `state`, loopback na 127.0.0.1. Token
   odświeżania w Keychain. Nie opierać OAuth o browser sign-in Chromium ani
   nie traktować sekretu zaszytego w aplikacji desktopowej jako poufnego.
   [OAuth dla aplikacji desktopowych](https://developers.google.com/identity/protocols/oauth2/native-app).
5. Szyfrowanie po stronie klienta przed wysłaniem, z uwierzytelnianiem danych,
   wersją klucza, poprawnym nonce i sprawdzaniem schematu po odszyfrowaniu.
   Klucz nie może leżeć w Drive w postaci jawnej obok danych. Zaprojektować
   parowanie urządzeń i kod odzyskiwania; samo logowanie Google nie daje
   automatycznie dostępu do klucza E2EE. Google nadal widzi m.in. rozmiar i czas
   operacji. Minimalizować jawne metadane i nazwy obiektów.
6. Sync domyślnie wyłączony; działa tylko dla wybranego konta i zakresu danych.
   Odłączenie konta zatrzymuje ruch, zachowuje dane lokalne i umożliwia osobne
   usunięcie kopii w chmurze. Wdrożenie wymaga własnego projektu OAuth/Drive,
   konfiguracji zgody aplikacji i przetestowania limitów/backoffu.

Firebase/Firestore to druga opcja, jeśli potrzebna będzie interaktywna
synchronizacja i własne konto/usługi Zittera. Oznacza jednak bazę w naszym
projekcie, reguły dostępu, rozliczenia i utrzymanie. Nie osadzać klucza konta
serwisowego ani uprzywilejowanego SDK serwerowego w kliencie. Dokumentacja
Firebase zaznacza, że desktopowe wsparcie C++ SDK jest przeznaczone do workflow
deweloperskiego, nie do publicznej dystrybucji. Dla tej natywnej aplikacji Drive
REST jest prostszą podstawą do synchronizacji niewielkiego stanu użytkownika.
[Firebase C++ desktop](https://firebase.google.com/docs/cpp/setup),
[biblioteki Firestore](https://firebase.google.com/docs/firestore/client/libraries).

### Testy synchronizacji przed wydaniem

Dwa urządzenia offline, równoległe dodanie/edycja/usunięcie/przesunięcie pinów,
konflikt folderów i kolejności, powtórzone pobranie operacji, wycofany token,
limit API, przerwany upload, uszkodzony szyfrogram, utrata i rotacja klucza,
starszy schemat klienta, usunięcie danych Drive, ponowne dołączenie starego
urządzenia i brak resurrectowania usuniętych obiektów. Sukces synchronizacji
nie może być raportowany, zanim dane zostały trwale zapisane lokalnie.

## Powtarzanie audytu

```sh
node --test tests/test_network_audit.mjs
node tools/audit_background_network.mjs \
  --output build/network-audit/new-baseline \
  --seconds 120 --external-url https://example.com --suppression-control
node tools/audit_background_network.mjs \
  --output build/network-audit/new-targeted \
  --seconds 90 --targeted-probe --only targeted-idle,targeted-local
node tools/audit_background_network.mjs \
  --output build/network-audit/new-soak --seconds 3600 --only fresh-ready-idle
```

Każde wywołanie wymaga nowego katalogu. Ostatnia komenda jest godzinnym testem;
docelową sześciogodzinną bramkę CI trzeba jeszcze dodać jako odrębny scenariusz.
Analizator ma testy rozdzielania DNS/TCP od HTTP, interpretacji HTTP 400,
identyfikacji źródeł i przypisywania statusów po przekierowaniach.

## Miejsca w źródłach do dalszej pracy

Kod Zittera: `chromium/app/main.cc`, `browser_client.cc`, `runtime.cc`,
`chromium/prototype/page_services.cc`, `workspace_preconnect.cc`,
`workspace_shell.cc`, `browser_tools.cc`, `browser_data.h`,
`tools/install_chromium_overlay.py`, `chromium/revision.json`.

Upstream, przy rewizji wskazanej powyżej:

- `components/omnibox/browser/aim_eligibility_service.cc` i `_features.cc`;
- `components/network_time/network_time_tracker.cc`;
- `components/autofill/core/browser/crowdsourcing/autofill_crowdsourcing_manager.cc`;
- `components/translate/core/browser/translate_ranker_impl.cc`;
- `components/component_updater/configurator_impl.cc` i `component_updater_url_constants.cc`;
- `chrome/browser/component_updater/registration.cc`, `pki_metadata_component_installer.cc`;
- `components/safe_browsing/core/browser/db/v5_update_protocol_manager.cc`;
- `components/variations/service/variations_service.cc`;
- `chrome/app/chrome_crash_reporter_client.cc`;
- `services/device/public/cpp/device_features.cc`, `services/device/geolocation/location_provider_manager.cc`;
- `chrome/browser/net/dns_probe_service_factory.cc`, `dns_probe_runner.cc`.

Przy kolejnej aktualizacji te warunki i nazwy wymagają ponownego sprawdzenia;
opis nie zakłada, że ustawienia obecnej rewizji są trwałym API Chromium.
