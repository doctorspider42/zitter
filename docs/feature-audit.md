# Przegląd funkcji Zitter — 2026-10-17

Przegląd lokalnego kodu, integracji i testów. To rozróżnienie między działającym
interfejsem Zitter a usługami dostępnymi w samym Chromium. Obecność preferencji
profilu nie dowodzi, że odpowiadający jej przepływ działa w naszym WebView.
Nie wykonano w tym audycie testów wszystkich publicznych serwisów ani audytu bezpieczeństwa.

## Najpierw domknąć codzienne korzystanie

| Priorytet | Brak / ograniczenie | Dowód w kodzie | Konkretny zakres następnego kroku |
| --- | --- | --- | --- |
| Wdrożone | Wysyłanie plików ze stron | Natywne pickery pliku/wielu plików/folderu, Chromium FileSelectHelper, anulowanie i powiązanie z dokumentem/profilami. | Weryfikacja w testach uploadu; foldery mają względne ścieżki i rekurencję. |
| Wdrożone | Kamera, mikrofon i udostępnianie | Dispatcher Chromium, natywna zgoda strony/profilu, TCC macOS, picker ekranu/okna/kart tego samego profilu, wskaźnik i Stop. | Karty w pickerze mają nazwy bez miniaturek. Zgody rzeczywistego OS wymagają testu na urządzeniu; automaty używają urządzeń testowych. |
| Wdrożone | Dialogi JavaScript i ochrona niezapisanej pracy | Natywne arkusze alert/confirm/prompt i wspólny, sekwencyjny beforeunload przed operacjami niszczącymi strony. | Ochrona dotyczy stron implementujących beforeunload; snapshot nadal nie przechowuje dowolnych formularzy. |
| Wdrożone | Panel pobrań | Transfery Chromium, postęp/bajty/prędkość/ETA, pauza i wznowienie, anulowanie, Finder; lista odseparowana według profilu. Pasek, menu, Command Center i konfigurowalne ⌘J. | Zachowane blokady niebezpiecznych plików. Brak własnego przepływu odblokowania ostrzeżeń i ponawiania transferów, których Chromium nie potrafi wznowić. |
| P1 | Podstawowe narzędzia strony | Brak integracji Find in Page / Print w naszych delegate’ach, skrótach i menu. Menu strony zawiera głównie Peek i reguły domen, brakuje standardowych akcji zaznaczenia/linku/obrazu. | Cmd+F z licznikiem i następnym wynikiem; druk / PDF; kopiowanie adresu i tekstu, zapisywanie obrazów. Osobne testy focusu w Peek/Split. |

## Następnie kompletność i niezawodność

| Priorytet | Obszar | Aktualny stan i zakres |
| --- | --- | --- |
| P1 przed wydaniem | Aktualizacje i dystrybucja | `tools/package_app.py` tworzy lokalny component build zależny od checkoutu. Brak samodzielnego podpisanego/notaryzowanego instalatora i kanału aktualizacji. Potrzebny regularny proces aktualizacji przypiętej rewizji Chromium oraz testy migracji profili; nie tylko przycisk aktualizacji UI. |
| P1 do weryfikacji | Bezpieczeństwo i tożsamość strony | Bootstrap/sandbox i część usług pochodzą z Chromium; nie oznacza to zweryfikowania wszystkich ścieżek bez `BrowserWindow`. Potrzebne testy złego certyfikatu/HTTPS-only, ostrzeżeń pobierania/Safe Browsing oraz czytelny panel połączenia i uprawnień witryny. Ten przegląd nie dowodzi braku ochrony silnika. |
| P2 | Rozszerzenia | Brak własnego przepływu instalacji/zarządzania, toolbaru, popupów i adapterów dla APIs zależnych od Chrome tabs/windows. Zacząć od konkretnego rozszerzenia i testów jego content scripts, popupu, uprawnień i profili. Samo dołożenie przycisku instalacji nie wystarczy. |
| P2 | Wideo i aplikacje webowe | Brak własnych delegate’ów HTML fullscreen / screen capture oraz testów E2E PiP, WebRTC i DRM. Bazowy WebView obserwuje część zmian fullscreen; wymaga to testów, nie deklaracji pełnego wsparcia. Zwykłe powiększenie okna macOS jest inną funkcją. |
| P2 | Hasła i uwierzytelnianie | Zapis/aktualizacja za zgodą, autofill i lista metadanych działają. Brak ujawniania/edycji/usuwania haseł; adresy i płatności Autofill celowo wyłączone dla osadzonego hosta. Passkeys/WebAuthn wymagają osobnego sprawdzenia. |
| P2 | Trwałość sesji i awarie | Zapisujemy adresy, Split, piny i snapshoty, lecz nie back/forward, sessionStorage czy formularze. Brak własnego UI awarii renderera. Potrzebne testy przerwania zapisu, odzyskania po crashu, uszkodzonego JSON i migracji formatów. |
| P2 | Prywatne okno i wiele okien | Mamy jeden shell i trwałe profile, bez okna off-the-record. Profil „osobisty” jest normalnym trwałym profilem. Wymaga osobnego modelu życia, usług i izolacji trybu prywatnego. |
| P2 | Zarządzanie kartami i pinami | Import i odtwarzanie hierarchii są rozbudowane, lecz edycja własnych folderów/pinów i pełne porządkowanie drag-and-drop nie dorównują zakresowi importera. Command Center dodaje przenoszenie żywej karty w obrębie profilu i wyraźną kopię między profilami. |
| P3 | Sync / eksport | Brak pełnego eksportu/importu konfiguracji Zitter i synchronizacji między urządzeniami. Najpierw wersjonowany eksport z podglądem konfliktów; nie przenosić cookies/haseł w zwykłym JSON. |

## Co już stanowi mocny fundament

Dynamiczne profile i przestrzenie z izolacją Chromium; import Arc; zachowywany,
cache’owany panel i animacje; motywy; Peek i Split; reguły domen; historia i zapis
haseł; bogate preferencje z politykami i wyjątkami witryn; konfigurowalne skróty
oraz wyszukiwarki; Time Machine, odtwarzanie kart i usypianie przestrzeni.
Command Center scala te funkcje pod jednym poleceniem.

Po wdrożeniu plików, dialogów i mediów rekomendowana kolejność to
**Find/druk → kompletność bezpieczeństwa i dystrybucji → rozszerzenia**.
Dokładny zakres i wyniki weryfikacji nowych integracji: [verification.md](verification.md).
