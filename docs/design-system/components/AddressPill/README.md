# AddressPill

Pigułka adresu na górze paska bocznego: pokazuje host pogrubiony w `ink`, resztę ścieżki w `ink-muted`, cała w kroju `url`.

**Daje konsument:** URL rozbity na host i ścieżkę, stan bezpieczeństwa (ikona kłódki w `ok`, `warn` przy mieszanej treści, `danger` przy błędzie certyfikatu — zawsze z ikoną), stan `editing`.

- Spoczynek: `bg-raised` + `shadow-card`, wysokość 36 px, `radius-pill`.
- ⌘L przełącza w `editing`: obrys 2 px `--accent`, kursor `--accent`, pełny URL w `ink`, po prawej `↵`.
- Po skopiowaniu adresu pigułka drży raz (`zt-tremor`, `dur-tremor`) — to jedyne potwierdzenie, bez tostu.
- Nie pokazuj `https://` ani `www.`.
