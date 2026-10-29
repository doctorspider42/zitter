Zitter to przeglądarka, która **schodzi z drogi**: rama w kolorze przestrzeni wokół neutralnej strony, gęsty pionowy pasek kart i jeden Command Center. Ekstrawagancja jest dozowana jak przyprawa: okno, które zmienia kolor razem z przestrzenią, kursywny szeryf w wielkich momentach, limonkowy `zing` w jednym miejscu naraz i drżenie, od którego wzięła się nazwa (*Zitterbewegung*, „ruch drżący” elektronu). Znak to ket z mechaniki kwantowej, `|z⟩`.

## Zasady

1. **Okno ma kolor przestrzeni, strona jest neutralna.** Rama (`bg-void`) i pasek boczny (`bg-base`) przyjmują odcień aktywnej przestrzeni; strona, aktywna karta i wszystko, co pływa, to neutralne `bg-raised`, które odcina się od kolorowego okna. Bez gradientów i ozdobnych teł.
2. **Jedna nuta naraz.** `zing` pojawia się w jednym miejscu widoku: przycisk `zt-btn-zing`, loader albo uchwyt podziału w ruchu. Jeśli na ekranie są dwa — usuń jeden.
3. **Aktywne się unosi.** To, co jest „teraz” (aktywna karta, aktywna przestrzeń), dostaje neutralne `bg-raised` i `shadow-card` na zabarwionym tle — bez strzałek, belek i znaczników.
4. **Klawiatura pierwsza.** Każda akcja ma skrót i pokazuje go w `Kbd`. ⌘L adres, ⌘K Command Center, ⌘T nowa karta (otwiera Command Center; osobnej strony nowej karty nie ma), ⌘⌥1/2 przestrzeń.
5. **Drżenie zamiast tostów.** Potwierdzenia drobnych akcji (skopiowano, przypięto) to jedno drgnięcie elementu (`zt-tremor`, `dur-tremor`), nie wyskakujące okienko. `Toast` tylko dla zdarzeń do cofnięcia lub z tła.
6. **Przestrzeń ma swój kolor.** Przełączenie przestrzeni przemalowuje całe okno w `dur-move`: ramę, pasek boczny, hover i linie, a do tego akcent kontrolek. Tekst, statusy i strona się nie zmieniają.

## Treść i ton

- Interfejs po polsku i angielsku (język wybierany w Preferencjach). Nazwy funkcji — Peek, Split View, Time Machine, snapshot — zostają po angielsku w obu wersjach.
- Polski: druga osoba, krótko: „Otwórz w podziale”, „Zamknij 12 kart”, „Cofnij”. Bez „Proszę”, bez wykrzykników.
- Wielka litera tylko na początku etykiety. WERSALIKI wyłącznie w stylu `eyebrow` (etykiety grup: „PRZYPIĘTE”, „PRZESTRZENIE”).
- Przyciski i pozycje menu to czasowniki. Liczby cyframi („3 nieprzeczytane”).
- Emoji tylko jako wybrany przez użytkownika znak przestrzeni — nigdy w tekstach interfejsu.
- Adresy bez `https://` i `www.`; host pogrubiony, ścieżka wyciszona.

## Kolor

Dwa motywy: **Noc** (`dark`, domyślny) i **Papier** (`light`, ciepła kość słoniowa zamiast bieli).

- Warstwy od najgłębszej: `bg-void` (rama okna) → `bg-base` (pasek boczny, ustawienia) → `bg-raised` (karta strony, aktywna karta, Command Center, menu, pigułka adresu). Hover: `bg-hover`. Pierwsze trzy (i `line`) są zabarwione kolorem przestrzeni; `bg-raised` nigdy.
- Tekst: `ink` dla treści, `ink-muted` dla URL-i, podpowiedzi i ikon w spoczynku (≥4.5:1 na każdym `bg-*` w obu motywach). `ink-faint` tylko dla stanów wyłączonych — podpowiedzi w polach (placeholder) i znaki, które trzeba zobaczyć, są w `ink-muted`.
- **Kolor przestrzeni.** Każda przestrzeń ma własny kolor, który barwi okno i kontrolki: `violet` (domyślny, = `ket`), `coral`, `sky`, `amber`, `rose`, `mint`. Ustaw `data-accent` aktywnej przestrzeni na oknie. Wtedy `bg-void`, `bg-base`, `bg-hover` i `line` biorą odcień z `accent-<n>-void|base|hover|line`, a kontrolki malują `--accent` (primary, przełącznik, kropka przestrzeni, pasek ładowania), `--on-accent`, `--accent-soft` (zaznaczony wiersz) i `--accent-ink` (akcent jako tekst). Niezabarwione `bg-*` to okno bez przestrzeni (świeży profil). Szczegóły w komponencie `WorkspaceAccent`.
- `ket` (fiolet ze znaku) to logo i okładka; w interfejsie pojawia się tylko jako akcent `violet`.
- Ekstrawagancja: `zing` (#d7ff3f) tylko jako wypełnienie, zawsze z `on-zing`. Jako tekst (np. etykieta „nowe”) — `zing-ink`. Żadnych limonkowych strzałek ani znaczników.
- Stany: `ok` (morski, nie zielony), `warn`, `danger` (pomarańczowy). Zawsze z ikoną lub słowem — nigdy sam kolor.
- Kolor przestrzeni nie zmienia tekstu (`ink`, `ink-muted` trzymają 4.5:1 na każdym odcieniu), statusów, fokusu, `bg-raised` ani `zing`. Limonka nie jest kolorem przestrzeni: zostaje taka sama wszędzie.
- Linie: `line` tylko rozdziela; obrys kontrolek, które trzeba znaleźć, to `line-strong` (≥3:1).

## Typografia

Trzy kroje z Google Fonts, każdy z jednym zadaniem:

- **Instrument Serif** (`display`), zawsze kursywą: `display-xl` na powitanie nowej karty i puste stany, `display-l` na tytuły stron wewnętrznych. Jeden na ekran. To główny akcent „luksusu” — kontrast z gęstym chrome.
- **Schibsted Grotesk** (`sans`): cały interfejs. `body` (13/18) to domyślny tekst chrome, `body-strong` dla aktywnej karty i przycisków, `label` dla wierszy drugorzędnych, `title` dla nagłówków sekcji, `eyebrow` dla etykiet grup.
- **JetBrains Mono** (`mono`): styl `url` dla adresów, klawiszy i rozmiarów plików.

Chrome jest gęsty: nie podnoś tekstu interfejsu powyżej 13 px poza `title` i polem Command Center (17 px).

## Przestrzeń, kształt, głębia

- Siatka 4 px: `space-1` między kartami, `space-2` ikona–etykieta, `space-3` padding wierszy i przycisków, `space-4` wnętrze paska bocznego, `space-6` między grupami, `space-12` marginesy nowej karty.
- Promienie rosną z rozmiarem: `radius-xs` klawisze i kafelki, `radius-sm` karty i wiersze, `radius-md` karta strony i menu, `radius-lg` Command Center i dialogi, `radius-pill` adres, primary, przełączniki, przestrzenie.
- Głębia przez warstwy, nie cienie: `shadow-card` dla strony i pigułki adresu, `shadow-float` dla wszystkiego, co pływa. Każdy cień zaczyna się obrysem 1 px w kolorze `line`.

## Ruch

- `dur-quick` dla hoverów i wciśnięć, `dur-move` dla paneli, paska bocznego, podziału.
- **Tremor** (`zt-tremor`): cztery kroki ±1 px w `dur-tremor` — gdy coś „ląduje” (nowa karta, skopiowany adres, przypięcie).
- **Loader** (`zt-loader`): trzy kropki drgające jak cząstka (zing, akcent, zing) zamiast spinnera; pasek ładowania strony to 2 px `--accent` u góry karty.
- Pod `prefers-reduced-motion` oba efekty znikają; zostają zmiany koloru.

## Fokus i dostępność

- Pierścień fokusu: 2 px solid `focus`, odstęp 2 px (`space-0-5`). W Nocy to limonka, w Papierze `ink` — dzięki temu nie zlewa się z żadnym akcentem; ≥3:1 na każdej powierzchni.
- Każdy przycisk z samą ikoną ma `aria-label` po polsku.
- Przełączniki to `role="switch"` z `aria-checked`; przestrzenie to `role="tab"`.

## Ikonografia

- Ikony liniowe 16 px na siatce 16, stroke 1.6, zaokrąglone końce i łączenia, `currentColor` (klasa `zt-icon`). Rysowane w podglądach inline jako SVG — zestaw własny, nie z biblioteki; przy rozbudowie dobieraj ikony o tej samej grubości (najbliższy odpowiednik: Lucide ze stroke 1.6).
- Kolor ikony = kolor tekstu wiersza (`ink-muted` w spoczynku, `ink` na hover, `--accent-ink` gdy wybrana). Ikony stanów w `ok`/`warn`/`danger`.
- Znak `|z⟩` (grupa Logos, `zitter.svg`) to ikona aplikacji; nie przerysowuj go i nie zmieniaj kolorów.

## Czego nie robimy

Limonkowych strzałek i znaczników aktywności, gradientów fiolet–niebieski, kolorowych lewych belek na kartach, emoji jako dekoracji, cieni bez obrysu, dwóch limonkowych akcentów naraz, Inter/Roboto.
