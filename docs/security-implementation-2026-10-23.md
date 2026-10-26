# Safe Browsing i ograniczenie usług Google — wdrożenie

23 października 2026. Uzupełnienie audytu `network-audit-2026-10-23.md`.
Zmiany są w źródłach. Osobny pakiet testowy: `build/ZitterSecurity.app`;
głównego pakietu `build/Zitter.app` nie zastąpiono podczas równoległych prac UI.

## Stan ochrony

Standardowy mechanizm Chromium pokazuje czerwone ostrzeżenie phishingu i malware,
blokuje dokument i obsługuje „Back to safety”. Potwierdzono to w działającym
pakiecie z kontrolowanymi werdyktami Chromium (`--mark_as_phishing` oraz
`--mark_as_malware`) i powrotem do poprzedniej lokalnej strony. Nie dodano
specjalnych wyjątków dla adresów testowych do kodu produktu.

To **nie potwierdza dostępu do prawdziwych list Google**. Dotychczasowy pusty
klucz powodował HTTP 400 (`API_KEY_INVALID`). Projekt nie ma jeszcze własnego
klucza; rzeczywista ochrona pozostaje niezweryfikowana i blokuje publiczne wydanie.
Osobny test wygasłego certyfikatu potwierdził stronę „Privacy error” i
`NET::ERR_CERT_DATE_INVALID` po zastosowaniu polityki.

`tools/verify_safe_browsing.mjs` rozróżnia dwa tryby:

```sh
# Diagnostyka samego mechanizmu blokowania; nie certyfikuje usługi Google.
node tools/verify_safe_browsing.mjs --app build/ZitterSecurity.app

# Bramka wydania: bez sztucznych werdyktów, świeże profile, dwie strony testowe,
# prawdziwa aktualizacja list HTTP 200, ostrzeżenie i powrót na bezpieczną stronę.
node tools/verify_safe_browsing.mjs --live --app build/ZitterSecurity.app
```

Tryb live czeka 310 s przed nawigacją: domyślny harmonogram pierwszej aktualizacji
list w przypiętej rewizji losuje 60–300 s. Każda próba trwa 350 s. Nie przyspiesza
harmonogramu ani nie omija certyfikatów. Test obejmuje dwa rodzaje zagrożeń,
nie całą reputację pobrań ani wszystkie tryby ochrony Chromium.

## Klucz developerski i wydania publiczne

`chromium/app/network_policy.cc` ładuje lokalny klucz przed inicjalizacją usług
Chromium. Domyślny plik to `~/.config/zitter/safe-browsing-api-key`, tryb `600`.
Alternatywę wskazuje `ZITTER_SAFE_BROWSING_API_KEY_FILE` (ścieżka absolutna).
Istniejący `GOOGLE_API_KEY` ma pierwszeństwo. Klucz nie trafia do repozytorium
ani argumentów procesu. Surowe NetLogi mogą zawierać klucz w query URL;
narzędzie tworzy prywatny katalog wyników. Nie publikować surowych logów.

Aby uzyskać klucz developerski:

1. Utworzyć projekt w [Google Cloud Console](https://console.cloud.google.com/).
2. W API Library włączyć **Safe Browsing API**.
3. W Credentials utworzyć API key i ograniczyć go do tego API.
4. Zapisać klucz lokalnie we wskazanym pliku i nadać `chmod 600`.
5. Uruchomić test `--live`; brak czerwonych ostrzeżeń jest błędem, nawet jeśli
   samo pobranie list odpowie HTTP 200.

To konfiguracja autora, nie onboarding użytkownika. Oficjalne wydania powinny
mieć własny klucz projektu przez wspieraną konfigurację buildu Chromium
`google_api_key` lub równoważną konfigurację procesu wydawniczego. Klucza w aplikacji
desktopowej nie można traktować jak nieodczytywalnego sekretu. Ograniczenie do API,
monitorowanie wykorzystania, rotacja i zapas limitu są konieczne.

Firefox pobiera listy lokalne i dodatkowo pyta o potencjalne dopasowania;
nie wykonuje niezależnego pełnego lookupu każdej otwieranej strony. Zen wstrzykuje
projektowy `ZEN_SAFEBROWSING_API_KEY` z sekretu procesu wydawniczego. Nie potwierdzono
ich prywatnych limitów ani umów. Nie używać cudzych kluczy lub identyfikatorów klienta.

Google podaje, że Safe Browsing jest bezpłatny do zastosowań niekomercyjnych,
z limitem projektu i możliwością wnioskowania o zwiększenie. Darmowy i otwarty
produkt nie jest automatycznie niekomercyjny, jeśli generuje przychody.
Przed publikacją: potwierdzić warunki dla modelu projektu, zmierzyć koszt zapytań
na aktywną instalację i uzyskać limit z zapasem na wzrost oraz świeże instalacje.
Proxy nie usuwa limitów ani warunków usługi. Własne serwowanie list wymagałoby
osobnego uzgodnienia praw dystrybucji i projektu bezpiecznych aktualizacji.

Źródła:

- [Google: warunki, cena, limity](https://developers.google.com/safe-browsing/reference/Appropriate.Usage)
- [Google: uzyskanie klucza](https://developers.google.com/safe-browsing/v4/get-started)
- [Mozilla: mechanizm ochrony Firefox](https://support.mozilla.org/en-US/kb/how-does-phishing-and-malware-protection-work)
- [Zen: klucz w procesie wydawniczym](https://github.com/zen-browser/desktop/blob/dev/.github/workflows/macos-release-build.yml)
- [Zen: konfiguracja klucza](https://github.com/zen-browser/desktop/blob/dev/configs/common/mozconfig)

## Polityka sieciowa

Konfiguracja jest stosowana w `main.cc` przed FeatureList i tworzeniem profili.
Wyłączone funkcje: AI Mode eligibility, zapytania czasu sieciowego, serwerowe
Autofill, ranking tłumaczeń, Optimization Guide i wykonywanie modeli, raportowanie
metryk oraz prewencyjne pobieranie modelu rozpoznawania mowy. Zachowano możliwość
użycia funkcji dostępności na żądanie. Wyłączono Chrome Sync, Domain Reliability,
pobieranie Variations i zestaw eksperymentów testowych Chromium. Nie wyłączono
całego ruchu w tle ani updatera komponentów.

Włączone dwie funkcje Chromium ograniczają automatyczne `ListAccounts`: bez
cookies Google lista nie jest pobierana; nieaktualny cache nie wywołuje samoczynnych
ponowień. Logowanie do zwykłych witryn Google nadal korzysta z ich cookies.
Nie jest to obietnica braku zapytań kont po świadomym korzystaniu z tych witryn.

Przed rejestracją updater odrzuca siedem znanych, nieużywanych komponentów:
manifest modeli Optimization Guide, Optimization Hints, sugestie wyszukiwania,
listy bezpieczeństwa aktora AI (Zitter nie udostępnia aktora), ekstrakcję cen,
dostawców CAPTCHA Chrome oraz wstępne oceny zaangażowania mediów.
Identyfikatory i uzasadnienia są w `network_policy_components.inc`.

Zachowano m.in. CRLSet, PKI Metadata, FileTypePolicies, SafetyTips,
SubresourceFilter, CrowdDeny i SSL Error Assistant. Nie zmieniono weryfikacji
podpisów CRX, CUP, certyfikatów TLS, sandboxa ani kodu klasyfikacji Safe Browsing.
Nowy nieznany komponent odziedziczy zachowanie upstreamu; audyt przed wydaniem
musi sklasyfikować nowe połączenia po aktualizacji Chromium.

Patch updatera jest mały, warunkowy i instalowany idempotentnie przez
`tools/install_chromium_overlay.py`. Bez przełącznika zachowuje upstream.
Zmiana układu kodu przy aktualizacji kończy instalację błędem zamiast dopasowywać
patch w przybliżeniu. `tools/check_network_policy.py` sprawdza rewizję i definicje
funkcji, aby zmiana nazwy flagi nie wyłączyła polityki po cichu.

## Weryfikacja i dalsze wydania

Kompilacja `zitter_security` i `zitter_security_network_policy_tests` przeszła.
Trzy testy C++ polityki/klucza przeszły, podobnie 8 testów instalatora,
7 testów stagingu oraz 7 testów analizatora NetLog. Nowy wariant zbudowano
w `out/Zitter`, oddzielnie od aktywnego buildu UI `out/ZitterOptimized`.
Dodatkowe 5 testów bramki wydania potwierdza m.in., że sztuczny werdykt,
HTTP 400, brak ostrzeżenia lub niekompletny log nie zaliczają ochrony live.

W pomiarach pierwszego wariantu polityki aktualizacje komponentów nadal
otrzymywały HTTP 200; pobrano m.in. CRLSet, PKI Metadata, FileTypePolicies,
SafetyTips i SSL Error Assistant. Nie wystąpiły wcześniejsze endpointy czasu,
AI Mode, rankingu tłumaczeń i serwerowego Autofill. Wykryte przy okazji
`accounts.google.com/ListAccounts` spowodowało dodanie powyższych dwóch funkcji
ograniczających konta. Szczegółowe końcowe wyniki są w osobnym pliku dowodowym.

Końcowy wariant: 340 s bezczynności na świeżym profilu oraz 120 s lokalnego
formularza/fetch/pobrania. Nie odnotowano kont Google, czasu, AI Mode, rankingu
tłumaczeń ani serwerowego Autofill. W bezczynności pozostały updater i CDN
komponentów (HTTP 200) oraz Safe Browsing V4 (HTTP 400 z nieprawidłowym kluczem).
Po wyłączeniu testowych eksperymentów Chromium używa domyślnego protokołu V4;
nie wymuszono eksperymentalnego V5. Krótka próba lokalna zawierała tylko localhost
i updater/CDN. Oba końcowe testy kontrolowanych ostrzeżeń i powrotu przeszły.

Dowody: [zestawienie JSON](audits/security-implementation-2026-10-23.json).
Plik rozróżnia SHA-256 pierwszego wariantu i końcowego wariantu z ograniczeniem
kont. W pierwszym potwierdzono zainstalowane wersje CRLSet `10854`, PKI `1815`,
FileTypePolicies `145.0.7584.0`, SafetyTips `3092`, SSL Error Assistant `7`.
Końcowy pomiar potwierdza działanie updatera, nie ukończenie instalacji każdego
komponentu w jego krótkim oknie czasowym.

Przed publicznym wydaniem wymagane są: klucz i przyznany limit projektu,
udany test `--live`, powtórka audytu po aktualizacji Chromium i połączeniu
z finalną wersją UI. Krótkie lokalne próby nie dowodzą braku wszystkich
możliwych połączeń (np. po zmianie ustawień, upływie godzin lub zgodzie na geolokację).

### Przy każdej aktualizacji Chromium

1. Zaktualizować przypiętą rewizję zgodnie z procedurą projektu.
2. Zainstalować nakładkę; ręcznie rozpatrzyć każdy błąd dopasowania patcha.
3. Uruchomić `python3 tools/check_network_policy.py` i testy polityki C++.
4. Zbudować i spakować finalną aplikację. Sam test starej binarki nie sprawdza
   nowej rewizji ani zmian UI.
5. Uruchomić oba tryby `tools/verify_safe_browsing.mjs`, w tym obowiązkowo `--live`.
6. Powtórzyć audyt pustego profilu, restartu, formularza i pobierania; objąć
   dłuższy okres bezczynności i funkcje uruchamiane za zgodą użytkownika.
7. Porównać nowe endpointy i komponenty. Potwierdzić aktualne wersje komponentów
   bezpieczeństwa w profilu, nie tylko odpowiedź HTTP updatera.

Nie zamrażać wersji Chromium, aby utrzymać listę flag. Błędy zgodności mają
wymuszać przegląd małej nakładki przy aktualizacji, a nie opóźnianie łatek silnika.
