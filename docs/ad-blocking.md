# Built-in ad blocking

Zitter blocks ads and trackers itself, with no extension. It uses public filter
lists for the web at large. For a site those lists miss, the AI assistant reads
the page and writes rules for it, and the user approves them.

This document records the engine decision, the design and its limits. The code is
in `chromium/prototype/content_blocking*`, `chromium/prototype/adblock.rs`,
`chromium/prototype/content_blocking.js` and the assistant tools in
`chromium/prototype/workspace_assistant.cc`.

## Decision: Brave's adblock-rust, vendored

Zitter is Apache-2.0. GPL code cannot be embedded, so uBlock Origin's engine and
its scriptlet/redirect resources are out. Two candidates remained:

| | adblock-rust (Brave) | `components/subresource_filter` (Chromium) |
|---|---|---|
| License | MPL-2.0: file-level copyleft, fine to link from Apache code if its files stay unmodified (or modified files are published) | BSD, already in the tree |
| Network rules | Full ABP/uBO syntax: `||host^`, wildcards, regex, `$third-party`, `$domain=`, types, `$important`, `@@` | URL rules only. The ruleset has to be converted offline (`ruleset_converter`) into its flatbuffer format |
| Cosmetic rules | Yes: `##`, `#@#`, generic/specific, `generichide`, class/ID lookup for generic rules | None. We would have to write our own |
| Activation | Our choice (a URLLoaderFactory proxy) | Tied to Safe Browsing's ad-intervention activation and a single global ruleset delivered by the component updater |
| Per-profile user rules | One engine per profile, rebuilt in the background | Needs re-indexing of the global ruleset, or a second filtering pass of our own |
| Build cost | About ten crates Chromium does not vendor (see below) | None |
| Maintenance | Brave ships it to tens of millions of users; matches what EasyList authors test against | Maintained, but built for one Google-curated list |

**adblock-rust it is.** The killer feature, assistant-written rules, needs
cosmetic filtering and the exact syntax list authors and models know (ABP/uBO).
With `subresource_filter` we would rebuild most of adblock-rust ourselves:
the cosmetic engine, `$domain`/exception semantics and per-profile rules. The
build cost is one-off.

How it is embedded:

- `third_party/rust/` holds the sources of `adblock` 0.13.5 and the crates
  Chromium does not vendor at a compatible version: `arrayvec`, `bitflags` (with
  the `serde` feature, which Chromium's copy lacks), `flatbuffers`, `idna`,
  `idna_adapter`, `percent-encoding`, `rustc-hash`, `seahash`, `thiserror` 1 and
  `thiserror-impl` 1. The rest (`regex`, `serde`, `serde_json`, `memchr`,
  `base64`, `itertools`, `smallvec`, `utf8_iter`, ICU) comes from Chromium's
  `//third_party/rust`. `tools/vendor_adblock.py` downloads the exact versions
  with Chromium's own cargo, checks their checksums against `Cargo.lock`, and
  writes `third_party/rust/BUILD.gn` with `cargo_crate` targets. No build
  scripts run: the two crates that have one (`flatbuffers`, `thiserror`) only
  probe for nightly features we do not use.
- The vendored files are not modified, so MPL-2.0 asks only for the notice and
  for its source to stay available; both are in this repository
  (`third_party/rust/adblock/LICENSE`).
- `adblock` is built with `default-features = false`: no `embedded-domain-resolver`
  (Chromium's `net::registry_controlled_domains` resolves eTLD+1 instead, through
  a cxx callback), no CSS validation, no resource assembler, no object pooling.
- `chromium/prototype/adblock.rs` is our cxx bridge: parse a list, check a request,
  return cosmetic selectors, check a single rule's syntax.

### Filter lists

EasyList and EasyPrivacy are dual-licensed GPLv3 / CC BY-SA 3.0. They are **data
Zitter downloads**, never compiled in and never part of the installer. They go to
`<user data>/Zitter Filter Lists/` with their own license headers intact. The
Settings page and this document name them with attribution:

- EasyList, https://easylist.to/easylist/easylist.txt, © The EasyList authors,
  GPLv3 / CC BY-SA 3.0.
- EasyPrivacy, https://easylist.to/easylist/easyprivacy.txt, © The EasyList
  authors, GPLv3 / CC BY-SA 3.0.

uBO's own lists (`uBlock filters`) are GPLv3-only and depend on uBO scriptlets,
so they are not offered.

## Design

### Engines

- `ContentBlockingService` (browser process, one per run) owns the list texts:
  it reads them from disk at startup and downloads them when they are missing or
  older than four days.
- Each regular profile has an engine: list texts plus that profile's own rules,
  parsed on a background thread. While a profile's engine is being rebuilt the
  previous one keeps serving. Off-the-record profiles use their original
  profile's engine and settings.
- Parsing EasyList plus EasyPrivacy (≈ 90 000 rules) takes a fraction of a
  second off the UI thread; matching one request takes microseconds.

### Network blocking

`zitter::BrowserClient::WillCreateURLLoaderFactory` (app) wraps every
frame and worker URLLoaderFactory of a regular web profile with a small proxy
(`ContentBlockingFactory`). For each request it:

1. skips main-frame documents (a blocked top-level page would show only an error),
   non-HTTP(S) URLs, and pages whose site is on the per-site allow list;
2. asks the profile's engine with the request URL, the top-level page URL (from
   the factory's `IsolationInfo`) and the request type (from
   `network::mojom::RequestDestination`: script, image, stylesheet, sub_frame,
   font, media, xmlhttprequest, ping, other);
3. fails a match with `net::ERR_BLOCKED_BY_CLIENT` before any byte leaves the
   machine, and counts it for the tab.

Known limit: a request allowed at start that redirects to a blocked URL is not
re-checked (no `FollowRedirect` interception). Lists rarely depend on it.

Browser tests use Chromium's stock client, so the fixture installs a client with
the same hook (`content::ScopedContentBrowserClientSetting`).

### Cosmetic hiding

`ContentBlockingTabHelper` (a `WebContentsObserver`) runs
`chromium/prototype/content_blocking.js` in Zitter's isolated world in each committed
HTTP(S) frame:

1. at commit: the site's specific selectors (`example.com##.ad`) as one
   `display: none !important` rule per selector, so one invalid selector cannot
   disable the rest;
2. at DOMContentLoaded and at load: the class names and IDs seen in the page go
   back to the engine, which returns the generic selectors (`##.ad-banner`) that
   match them, unless the site has a `generichide` exception.

Plain CSS selectors go into the style sheet (`:has()` works natively). Rules
with uBlock Origin's procedural operators or actions run in the same script,
which implements them itself from uBO's public syntax documentation (no uBO
code):

- operators: `:has-text()`, `:upward(n|selector)`, `:matches-attr()`,
  `:matches-css()`, `:matches-css-before()`, `:matches-css-after()`,
  `:min-text-length()`, `:matches-path()`, `:xpath()`, and `:has()` / `:not()`
  (with their ABP aliases) when they wrap a procedural operator;
- actions, last in the rule: `:remove()`, `:style()` (nothing that loads, so no
  `url()`), `:remove-attr()`, `:remove-class()`;
- elements they hide get a random per-page attribute that one style rule
  hides; ones no rule matches any more get it removed;
- a `MutationObserver` re-runs them as the page changes, at most every 150 ms
  and less often when a pass is slow; `<html>` and `<body>` are only ever
  restyled, never hidden or removed.

adblock-rust is built without its `css-validation` feature, so it keeps a
procedural selector as one string in `hide_selectors`, and rules with an action
come back as JSON in `procedural_actions`; the bridge passes both on. This also
makes the lists' own `#?#` and action rules work. Scriptlets (`+js()`) are
still refused: their resources are uBO's GPL code.

### Per-site switch and the toolbar shield

The shield is the first button of the extensions bar, before the fingerprint
button, whenever ad blocking is on for the profile. It follows the fingerprint
button's 28 px IconButton: muted ink, `bg_hover` on hover, `accent_soft` while
its panel is open. The extension badge at its bottom right (neutral `ink` with
`bg-raised` text, design: Extensions) counts what was blocked on the page:
requests plus hidden elements (99+ at most). On a site
where blocking is off it is drawn crossed out.

Its panel (the fingerprint panel's 296 px bubble):

- a card with the count: "Zablokowano na tej stronie" and the number, or
  "Blokowanie wyłączone na tej stronie" ("Blocking is off on this site");
- **Wyłącz / Włącz blokowanie na tej stronie**, the switch for the site (host,
  `www.` removed); flipping it reloads the page;
- **Zablokuj więcej z asystentem** opens the assistant panel with the prompt
  for this page (shown only while the assistant is enabled);
- **Ustawienia blokowania**, the Settings category.

The allow list is the profile pref `zitter.ad_blocking_allowed_sites` (list of
hosts). Matching is by host and its parents, so allowing `example.com` also
covers `news.example.com`.

### Settings

A new category **Blokowanie reklam** ("Ad blocking") at the end of the
preference catalogue in `sidebar_scene.rs`:

- **Blokuj reklamy i elementy śledzące** (toggle, pref `zitter.ad_blocking`,
  default on, per profile);
- **Listy, wyjątki i Twoje reguły**, one card (row kind `AdBlocking`, built
  natively like the assistant's model list) with three parts:
- **Listy filtrów**: EasyList and EasyPrivacy with their rule counts, last update,
  attribution, and **Aktualizuj teraz**;
- **Witryny bez blokowania**: the allow list, each with a remove button;
- **Twoje reguły**: the profile's own rules (pref `zitter.ad_blocking_rules`, a
  list of strings), each in the `url` style (mono, it is code) with a remove
  button, and a field to add a rule. A batch's note ("Asystent: site, date")
  shows under its first rule only. A rule the engine cannot parse is refused
  with a message.

### List updates

- At startup the service reads both files. A list missing or older than four
  days is downloaded 30 s after startup (so it never competes with the first
  page), through the system network context: no cookies, no profile data.
- Every 24 h while running, the same check.
- **Aktualizuj teraz** downloads at once.
- A download must be HTTP 200, at most 8 MB, start with `[Adblock Plus` and
  parse to at least 1 000 rules; otherwise the old file stays.
- This is new background traffic (two GETs to easylist.to every four days,
  traffic annotation `zitter_filter_lists`); the next network audit
  (`docs/network-audit-2026-10-23.md` is the last) should list it.

## The assistant flow

The user asks in the shield panel (**Zablokuj więcej z asystentem**), or types in
the assistant panel, for example "zablokuj reklamy na tej stronie". Two browser
tools serve it:

**`adblock_inspect {tab}`** (needs the page permission). The page script
(`page.js`, tool `adblock_inspect`) returns:

- `candidates`: up to 80 elements that look like ads:
  - boxes under an **ad label**: a text node or a CSS `content` label that reads
    "REKLAMA", "Autopromocja", "Materiał sponsorowany", "Mat. promocyjny",
    "Sponsored", "Advertisement", "Anzeige" and similar, outside navigation and
    footers. From the label it climbs to the first ancestor big enough to be an
    ad that holds media, a link or real height, never into navigation, forms
    or much more text than an ad carries, and adds `rule_hint`, a
    procedural rule anchored on that label
    (`site##span:has-text(/^\s*REKLAMA\s*$/i):upward(2)`), with how many
    elements it matches; a rule found again adds no second candidate. Portals such as wp.pl generate their class names on
    every deploy, but must keep the label: Polish law requires ads to be
    marked;
  - iframes from other sites, big images that link to another site, and
    `<canvas>` or `<video>` elements of a standard ad size (HTML5 creatives);
  - elements whose id/class/aria-label/data attributes say ad, sponsor, promo,
    banner, `taboola`, `outbrain` and similar, standard ad sizes, and fixed
    overlays covering the page;
  - elements already hidden by Zitter's rules.

  Each has a ready unique selector, a stable selector (classes/IDs without
  random-looking tokens), tag, size, position, text sample and
  `hidden_by_zitter`. The walk covers up to 30 000 elements in about 2.5 s.
- `requests`: third-party resource URLs from the Resource Timing API (up to 150,
  query strings cut), with initiator type and whether Zitter blocked them.
- `site`, `blocked_on_page` and `blocking: on/off`.

The model may add `screenshot` to see the page as the user does.

**`adblock_add_rules {tab, rules: string[], replaces?: string[], reason?: string}`**
(needs the page and action permissions). It:

1. validates every rule with the engine's parser and refuses the whole call
   if any is invalid, with each error, so the model can correct and retry.
   Unbalanced brackets, scriptlets and the operators Zitter does not run
   (`:watch-attr()`, `:others()`, `:shadow()`…) are invalid here;
2. only allows rules scoped to the page's site: cosmetic rules must name the site
   (`example.com##…`), network rules need `$domain=example.com`, or a `||host^`
   pattern for a third-party host on that page. Broad rules such as `##div` are
   refused, so a model cannot hide the web;
3. runs every hiding rule in the page: a rule the page script cannot compile
   (a bad regex or selector) or one that matches nothing on the page now is
   refused the same way. A rule that hides nothing now is a guess;
4. **preview**: highlights every element the cosmetic rules would hide (a
   dashed accent outline and a tint, via `content_blocking.js`) and asks for **native
   consent**, the same consent bubble as an assistant click: "Ukryć 4 elementy
   na example.com?" with **Zezwól** / **Pomiń**;
5. on yes: appends the rules to the profile's own list (deduplicated, with a
   `! Asystent: example.com, 2026-11-05` comment in the UI language, "Assistant:"
   in English), rebuilds the engine, applies
   the new cosmetic rules at once, removes the rules named in `replaces` (only
   this site's saved rules, exactly as saved) and returns
   `{saved, hidden_now, network_rules, replaced}`; on no: removes the preview
   and returns `skipped`. With `replaces` the consent question also says how
   many old rules go.

The tool's description lists the procedural operators and actions, and tells
the model to anchor rules on what an ad must show when its class names look
generated: its label, a link or image host, an attribute, a fixed size. It
starts from `rule_hint`. Sponsored items mixed into real content may be hidden
when they carry their own label. Anti-adblock walls are removed with
`:remove()`, and scrolling comes back with `body:style(overflow: auto)` or
`:remove-class()` on `<html>`/`<body>`. The model must never pin an element by
its position (`:nth-of-type` chains): on a front page that slot holds an article
on the next visit.

With the Claude CLI backend, a screenshot switches the request to `stream-json`
input, where the model tends to call browser tools natively; the CLI refuses
("No such tool") and the turn could end without saving anything. A note after
the images (`IMAGE_NOTE` in `providers.mjs`) steers it back to `actions`, and
when the answer still asks for nothing, the refused calls become the turn's
actions (`refusedCalls`). `adblock_add_rules` also reads `rules` sent as one
string, one rule per line.

Why native consent instead of `ask_user`: `ask_user` runs through the model,
which could skip it or misreport the answer. Saving rules changes the user's
browser for good, so it gets the same unforgeable consent as a click, with the
tool's own question in the bubble (`prompt` in the tool result). "Allow for
this task" and unconfirmed background work behave as they do for clicks; the
"confirm clicks" setting never skips it.

The shield's **Zablokuj więcej z asystentem** sends "Zablokuj reklamy na tej
stronie, których nie blokują listy filtrów." to the selected assistant session
(`AssistantPanel::Ask`). The session's permissions decide what the model may
do; without page and action permissions it says what is missing.

### Rules that stop working

Sites change their markup, so a rule that worked can quietly stop matching.
Twelve seconds after each load of a page (late and lazy ads have loaded by
then) the tab asks the page script how many elements each of the profile's own
hiding rules for that site has acted on since the load (`count`: the most a
procedural rule matched at once, or what a plain rule matches now). The counts
go to `zitter.ad_blocking_rule_misses` (rule → loads in a row with no match):
a hit clears the entry, a miss adds one. After `kStaleLoads` (3) misses in a row
the rule is stale. Network rules are not counted.

For a page with stale rules, the shield's assistant row reads **Napraw
niedziałające reguły z asystentem** ("Fix broken rules with the assistant"). It
sends the assistant those rules and asks it to find the ads again and save new
rules with `replaces` set to the old ones. Adding or removing a rule clears its
counter.

The system prompt in `engine.mjs` describes the tools and the order: inspect, look
(screenshot if unsure), propose only rules for what the user sees as ads, never
for content they came for, prefer specific stable selectors and site-scoped
network rules, then `adblock_add_rules` once with all of them, then summarize.

Rules stay editable and removable in **Settings → Blokowanie reklam → Twoje
reguły**.

## Tests

- Rust (`adblock.rs`): parsing, network matching with types and third-party,
  exceptions, cosmetic selectors, `generichide`, rule validation and the
  site-scope check.
- Node (`node --test` in `chromium/assistant`): the system prompt names both
  tools; the engine routes them to the browser.
- Python (`tests/test_content_blocking_patch.py`): the Chromium patch installs
  the crates and prefs idempotently and refuses unowned files; the vendored
  crates match their pins.
- Browser (`chromium/prototype/content_blocking_browsertest.cc`, its own file
  like the fingerprint tests, with `testdata/adblock_probe.html`):
  - `BlocksFixtureAdsAndTheShieldTurnsItOffPerSite`: with a test list the ad
    host's script and image fail while the page's own script loads; generic and
    site rules hide their elements, content and the unlisted ad stay; the tab
    counts 2 requests and the shield shows 4; the shield's site switch reloads
    with everything loaded, per profile; Settings turns it back on; the profile
    switch removes the shield and stops blocking.
  - `AssistantAddedRuleHidesAnElementAfterConsent`: `adblock_inspect` finds the
    unlisted ad with a stable selector; rules for the whole web are refused;
    unconfirmed `adblock_add_rules` previews and asks, also with click
    confirmation off; confirmed, it saves and hides at once, and a fresh load
    hides it through the engine; removing the rule in Settings brings it back;
    a typed rule is validated.
  - `ProceduralRulesAnchorOnTheAdLabel`: `adblock_inspect` finds an ad with a
    generated class by its "REKLAMA" label and suggests
    `span:has-text(…):upward(1)`; a rule that matches nothing and one with a bad
    regex come back as invalid, before any preview; the label rule hides the
    ad at once and, through the engine, on a fresh load, together with a
    `:style()` action rule; an ad added later with the same label is hidden as
    it appears; three missed loads make a rule stale and one hit clears it;
    `replaces` swaps the stale rule for a new one and refuses rules that were
    never saved.

## Known limits

- Requests go through a browser-side URLLoaderFactory proxy on the UI thread, as
  with extensions' webRequest. Matching takes microseconds, but every
  subresource now makes that hop.
- A request is checked once: a redirect to a blocked URL is not re-checked.
- Until the lists are read at startup (or downloaded on first run), pages load
  unfiltered.
- Generic hiding sees classes and IDs at commit, DOMContentLoaded, load and 3 s
  later; elements added after that only get the site's and complex generic rules.
- Scriptlets and `$redirect` resources are not supported (uBO's resources are
  GPL). Procedural operators that need them or their engine
  (`:watch-attr()`, `:others()`, `:shadow()`, `:matches-prop()`) are refused.
- Procedural rules are re-run on DOM changes, not on scroll or style changes
  alone.
- Ads drawn into a `<canvas>` and anti-adblock scripts that do more than show a
  wall are not handled yet; see
  [ad-blocking-canvas-and-anti-adblock.md](ad-blocking-canvas-and-anti-adblock.md).

## Status

Implemented as planned above, in `chromium/prototype/adblock.rs` (engine bridge),
`content_blocking.{h,cc,js}` (lists, engines, request proxy, cosmetic hiding,
tab counts), `extensions_bar.cc` (shield), `sidebar_scene.rs` and
`workspace_shell.cc` (Settings), `workspace_assistant.cc`, `page.js` and
`engine.mjs` (assistant tools). Procedural rules, label anchors and stale-rule
repair were added afterwards in the same files.
