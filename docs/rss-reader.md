# RSS reader

Open **RSS reader** from Command Center (`RSS`, `Atom`, or `reader`), or use
Command+Option+Shift+R on macOS / Ctrl+Alt+Shift+R elsewhere. This command belongs
to the shared shortcut catalogue and can be reassigned in input settings.
**RSS reader** also appears beside History in the browser menu (Windows) and
Zitter's native application menu (macOS), with its current shortcut.

The command opens or activates a real internal tab at `zitter://rss/` in the
current space. It reuses an open reader, including one in Split View, or reopens
its existing pin. Pin and unpin it with the normal tab controls. Closing the tab
keeps subscriptions; reopening a pin, restoring a session, or reopening a closed
tab restores the reader. Manually duplicating a reader in Split View creates an
independent view of the same profile's sources. Filters, selection, drafts and
scroll position belong to each open view and are not session data. Reload
refreshes the profile's feeds; Copy address copies the canonical internal URL.

Use the **RSS settings** gear in the tab header to open a separate settings
screen in the same tab. It lists feed names and URLs with removal controls.
Choose **Add feed**, paste an HTTP(S) RSS 2.0 or Atom feed URL, and subscribe.
For an empty profile the settings screen already shows the URL field. **Back to
reader** returns to the previous list or article preview. The reading screen
keeps its filters and source picker, with source editing confined to settings.

When the page from which you invoke the command
advertises RSS or Atom with `link rel="alternate"`, the reader offers those
feeds. Discovery starts before switching tabs; invoking the command from the
reader itself preserves the last discovered sources. Choose a discovered feed
to review its URL in settings before subscribing. Select a source or browse all
sources, unread articles, or saved articles.
Entries are sorted by parsed publication date, with stable ordering for missing
or equal dates. Article titles highlight on hover and use a pointer cursor.
Click a title (or focus it and press Enter/Space) to read its text preview; use
**Open article**
to navigate to the original in a normal tab. Selecting a preview marks it read.
The preview also supports saving and marking unread.

### Layout

Reader updates batch layout invalidations while rebuilding the entry list.
Chromium's WebView forwards descendant additions to its bounds handler even
when the host size is unchanged; the RSS host only sets reader bounds when they
actually change. This avoids synchronously laying out the growing list after
every added row and text node, including when RSS is restored at startup.
The regression test checks that descendant additions keep layout queued and
that a real window resize still resizes the reader.

The layout follows the "Zitter RSS Reader Redesign" canvas
(https://claude.ai/artifact/BXaWK2sQxpGe5wNvJQJRva). From a tab width of 760 px the
reader shows two panes. On the left is a 320 px list column on `bg-base` with the
filters, the source picker and the entries. On the right is a reading pane on
`bg-raised`, whose column is at most 680 px wide and centred. Narrower tabs, including
most Split View panes, keep one column: the preview replaces the list, and **Back to
articles** returns to it.

- Entries are grouped under eyebrow day labels (Today, Yesterday, then the date).
  Each row shows the source and time, the title and a short excerpt. An unread entry
  has an `accent` dot and a `body-strong` title; a read title is muted. In two panes
  the open entry sits on `accent-soft`.
- The preview header shows the source, the date and an estimated reading time, then
  the title in `display-l` (Instrument Serif italic). A note in `ok` says the preview is
  text only, with no images, scripts or tracking. The actions are **Open article**
  (primary), save and mark unread. **Back to articles** sits above the preview in
  both layouts.
- With no entry selected, the reading pane invites you to choose one.
- The header shows the unread count beside the title in wide layouts.
- Opening an entry moves focus to the preview's **Back to articles**, which then
  returns focus to that entry, in both layouts.

Not yet implemented from the design: the source rail with per-feed counts (the
source picker stays), J/K keyboard navigation, the zing "new" badge, the refresh
loader, and the larger 15/16 px reading type. Each of these needs a design-system
text style or scene primitive that the panel scene does not have yet.

## State and isolation

Subscriptions, cached previews, and read/saved flags belong to the browsing
profile, shared by its spaces and windows. `ZitterRSS.json` lives in that
profile's directory. A private browsing profile starts with an empty reader,
keeps its state only in memory, and discards it with that private profile.
Explicit article navigation remains bound to the profile that opened the reader.

The profile owns the reader service, so closing a reader tab does not interrupt
a refresh. Profile destruction cancels network work and invalidates pending
parser callbacks. Each tab owns its native view, scene, keyboard routing and
observer subscription, which are torn down before its window releases profile
leases. Discovery is scoped to both the reader generation and the source
document; results from replaced documents or closed reader tabs are ignored.
Article navigation opens a separate normal tab in the originating profile,
leaving the reader and any pin available.

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
paragraphs remain readable. There is no script execution, image
loading, iframe, or automatic tracker request in the preview.

The native reader is a child of its normal tab host. The existing tab model uses
an inert `data:` document with an opaque origin and `default-src 'none'` CSP as
the navigation identity, with the browser-assigned virtual URL `zitter://rss/`.
The document contains no feed content or profile state. There is no JavaScript
binding, WebUI endpoint or web-accessible reader API. Only this exact internal
address is admitted by pin/session/timeline validation; other internal schemes,
paths, queries and fragments remain rejected. Reader UI is attached on restored
navigation and on adoption of an already loaded tab.

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
covers Command Center/shortcut entry, local feed discovery, tab close and profile
switch during a delayed response, English empty state, and captures of list,
article, narrow, 250–350 px split-pane, pinned-sidebar, focused, and scrolled
layouts in light/dark themes. Pass
`--zitter-test-rss-render-dir=/absolute/output/directory` for screenshots; repeat
with `--zitter-rust-raster=metal` to inspect the Metal renderer.
`RssReaderTabsPinsRestoreAndIsolation` checks tab deduplication, closed-pin menu
reopening, AppKit target/selector dispatch on macOS, pin/unpin, a new window's
session restoration, closed-tab reopening, opaque-document isolation, exact URL
admission, reload, Copy address, article navigation and private-profile teardown.
`RssReaderAdoptionMovementAndProfileSwitch` covers adoption of an already loaded
internal document, moving its native view between spaces of the same profile,
rejecting a move to a different profile, and assigning a new profile during a
refresh. `RssReaderNativeSplitFocusAndIndependentViews` checks independent native views,
Enter/Space routing, control focus and a real click on noninteractive reader
content before closing the active pane with the keyboard.

`RssReaderSettingsScreenAndReturn` exercises gear dispatch, the dedicated settings
screen, field focus and subscription through accessibility input, returning to
an article preview, source removal, and focus after removing the final source.
The visual helper also captures settings and source entry in a 250–350 px pane.
