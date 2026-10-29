# SplitView

Dwie lub więcej stron w jednym widoku: obok siebie albo jedna pod drugą, z uchwytami do zmiany proporcji.

**Daje konsument:** panele (host, zawartość), aktywny panel, orientację (`stacked`), proporcję.

- Na `bg-void` z paddingiem `space-2`; panele to karty `bg-raised`, `radius-md`, `shadow-card`.
- Aktywny panel: obrys 1.5 px `--accent`; host w `ink`, nieaktywny w `ink-muted`.
- Pasek hosta 28 px w `url` nad każdym panelem — adres pokazuje też pigułka, ale tylko dla aktywnego.
- Uchwyt (`zt-gutter`, `role="separator"`): kreska 3 px `line-strong`, na hover rośnie i zmienia się w `zing`. Strzałki przesuwają o 5%.
- W pasku bocznym karty podziału stoją obok siebie w jednym wierszu `SidebarTab`, w kolejności paneli.
- Kliknięcie paska panelu daje mu fokus. Karta przeciągnięta z paska bocznego na stronę dołącza do podziału przy najbliższej krawędzi: podgląd przyciemnia stronę, pokazuje przyszłe panele jako karty, a nowy w `accent` z nazwą karty.
