# Extensions

Rozszerzenia Chrome w prawym końcu paska górnego: przypięte akcje, przycisk z puzzlem i jego menu, popup rozszerzenia.

**Daje konsument:** dla każdego rozszerzenia aktywnego profilu: ikonę (bitmapa 16 px), nazwę, odznakę (tekst do 4 znaków), czy jest przypięte, dostęp do bieżącej witryny (`ma dostęp`, `po kliknięciu`, `chce dostępu`, `brak`), opcjonalnie popup.

- Miejsce: prawy koniec 44‑px pasa nagłówka, na `bg-void`, oś pionowa wspólna ze światłami i nawigacją, `space-2` od krawędzi okna. Kolejność od lewej: przypięte ikony, kreska `line` 16 px, puzzle. Bez rozszerzeń zostaje sam puzzle.
- Każda ikona to `IconButton` 28 px z bitmapą 16 px w `radius-xs`; hover `bg-hover`, otwarty popup `on`. Ikona zawsze jest ta, którą rozszerzenie ustawiło dla tej karty (`action.setIcon`): menedżer haseł po wylogowaniu sam podmienia ją na szarą i pasek pokazuje to od razu. Rozszerzenie, które wyłączyło akcję na tej stronie (`action.disable`), dostaje ikonę w szarości (pełny kolor na hover); dostęp do witryny menu mówi słowami.
- Odznaka: pigułka 12 px w prawym dolnym rogu, `mono` 9 px 600, `ink` z tekstem `bg-raised` i obrysem 1.5 px `bg-void`. Zawsze neutralna — kolor odznaki z rozszerzenia ignorujemy, żeby nie walczył z akcentem przestrzeni. Treść odznaki trafia do `aria-label`.
- Przypiętych najwyżej 6. Gdy okno się zwęża, chowają się od lewej do menu; puzzle zostaje zawsze.
- Popup: `bg-raised`, `radius-md`, `shadow-float`, 4 px pod ikoną, prawą krawędzią przy prawej krawędzi ikony (nie wychodzi poza okno). Treść i rozmiar daje rozszerzenie (do 800×600). Escape i klik poza zamykają.
- Menu (`⌘⇧E` lub puzzle): `ContextMenu` 296 px. Nagłówek `eyebrow` „Rozszerzenia” z hostem w `url`. Wiersz 44 px: ikona 20 px, nazwa `body`, pod nią dostęp w `label`/`ink-muted`; przypnij (`on`, gdy przypięte — widoczne zawsze) i „Więcej” pojawiają się na hover. Prośba o dostęp to `warn` z ikoną i słowem oraz cichy przycisk „Zezwól”. Po separatorze: „Zarządzaj rozszerzeniami” i „Chrome Web Store”.
- Prawy klik na ikonie: `ContextMenu` — „Odepnij”, „Opcje”, „Zarządzaj”, po separatorze „Usuń z Zittera…” w `danger`.
- Rozszerzenia należą do profilu Chromium, nie do przestrzeni: przestrzenie dzielące profil mają ten sam zestaw, przełączenie na inny profil podmienia ikony bez animacji. Nowo zainstalowane rozszerzenie: jedno drgnięcie (`zt-tremor`) puzzla, bez tostu.
