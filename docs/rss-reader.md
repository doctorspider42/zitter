# RSS reader

Open **RSS reader** from Command Center (`RSS`, `Atom`, or `reader`), or use
Command+Option+Shift+R on macOS / Ctrl+Alt+Shift+R elsewhere. This command belongs
to the shared shortcut catalogue and can be reassigned in input settings.

Choose **Add feed**, paste an HTTP(S) RSS 2.0 or Atom feed URL, and subscribe.
For an empty reader, the URL field is already visible. When the current page
advertises RSS or Atom with `link rel="alternate"`, the reader offers those
feeds. Select a source or browse all sources, unread articles, or saved articles.
Entries are sorted by parsed publication date, with stable ordering for missing
or equal dates. Select an entry to read its text preview; use **Open article**
to navigate to the original in a normal tab. Selecting a preview marks it read.
The preview also supports saving and marking unread. **Manage feeds** reveals
compact source controls, including removal.

## State and isolation

Subscriptions, cached previews, and read/saved flags belong to the browsing
profile, shared by its spaces and windows. `ZitterRSS.json` lives in that
profile's directory. A private browsing profile starts with an empty reader,
keeps its state only in memory, and discards it with that private profile.
Explicit article navigation remains bound to the profile that opened the reader.

The profile owns the reader service, so closing the panel does not interrupt a
refresh. Profile destruction cancels network work, invalidates pending parser
callbacks, and closes the panel. Discovery is scoped to both the panel generation
and the source document; results from replaced documents or closed panels are
ignored. The window tears down its panel before releasing profile leases.

## Fetching and untrusted content

Refresh runs manually, at reader startup, and every 30 minutes while the profile
service is running. Sources refresh sequentially. Each request uses that profile's
Chromium network stack, omits credentials and cookies, sends no referrer, bypasses
the cache, and has a 20-second timeout and 2 MiB response limit. Normal TLS and
Chromium redirect policies remain enabled. Redirect aliases are deduplicated.
An individual source failure leaves its previous articles available.

XML is parsed through Chromium's isolated Data Decoder service. Tree conversion
runs on a worker, with ceilings of 12,000 nodes, 64 levels, and 128 namespace
declarations per element. DTD/entity
declarations and NUL-containing encodings are rejected before parsing. Atom
namespace prefixes and inherited `xml:base` are resolved. Only HTTP(S) article
links without embedded credentials are accepted. Previews are inert text:
HTML tags and script/style bodies are discarded; numeric entities, XML entities,
and common named typography entities are decoded, and
paragraphs remain readable. There is no WebContents, script execution, image
loading, iframe, or automatic tracker request in the preview.

## Bounds and retention

A profile supports 50 feeds, 100 recent articles per feed, and 500 saved articles. Saved entries
can extend a feed beyond the recent-article limit.
Previews are limited to 4,000 UTF-8 bytes at character boundaries. Each feed has
a 2,048-byte URL limit and bounded title, identifier, and date fields. Saved
articles survive a full batch of new articles. When the global 8 MiB string
payload budget is reached, the oldest unsaved entries are evicted first.
The worst-case escaped JSON fits within the matching 64 MiB read/write ceiling.
Read and saved flags are merged by stable feed identifier or article URL.

The list initially builds 100 rows, with explicit pagination. Excerpts collapse
paragraphs into compact snippets; the article preview preserves paragraphs.
Dates use an unambiguous local `YYYY-MM-DD HH:mm` representation. UI refreshes are
coalesced and deferred beyond the originating control callback. Address drafts
remain intact. Writes coalesce for 500 ms; serialization and atomic disk writes
run on a sequenced worker. Pending writes flush on profile teardown.

OPML import/export, full-page extraction, authenticated feeds, and background
refresh while the application is closed are outside this version's scope.

## Local verification

`RssReaderTextBudgetAndSavedRetention` covers text/entity/namespace conversion,
Unicode boundaries, budget eviction, saved retention, and a real disk reload.
`RssReaderFetchParseIsolationAndPersistence` covers local RSS and Atom requests,
relative links, duplicate entries/redirect aliases, malformed XML and entities,
credential omission, profile separation, saved/read persistence, and private
profile destruction during a fetch. `RssReaderLifecycleDiscoveryAndVisuals`
covers Command Center/shortcut entry, live feed discovery, panel close and profile
switch during a delayed response, English empty state, and captures of list,
article, narrow, focused, and scrolled layouts in light/dark themes. Pass
`--zitter-test-rss-render-dir=/absolute/output/directory` for screenshots; repeat
with `--zitter-rust-raster=metal` to inspect the Metal renderer.
