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

Downloaded lists are **data Zitter downloads**, never compiled in and never
part of the installer. They go to `<user data>/Zitter Filter Lists/` with their
own license headers intact. The Settings page and this document name them with
attribution:

| List | Switch | Source and license |
| --- | --- | --- |
| EasyList | always on | https://easylist.to/easylist/easylist.txt, © The EasyList authors, GPLv3 / CC BY-SA 3.0 |
| EasyPrivacy | always on | https://easylist.to/easylist/easyprivacy.txt, © The EasyList authors, GPLv3 / CC BY-SA 3.0 |
| Zitter's stand-ins (Zamienniki Zittera) | on by default | built in, `chromium/prototype/adblock_zitter_list.txt`, part of Zitter (Apache-2.0) |
| Adblock Warning Removal List | on by default | https://easylist-downloads.adblockplus.org/antiadblockfilters.txt, © The EasyList authors, same license as EasyList |
| uBlock filters | off by default | https://ublockorigin.github.io/uAssets/filters/filters.txt, © The uBlock Origin authors, GPLv3 |

Not offered: the Polish anti-adblock lists (Certyficate.IT's Official Polish
Filters and `olegwukr/polish-privacy-filters`) are CC BY-NC-SA 4.0, which
rules out commercial use, and the second has not changed since 2023.

### Built-in lists: blocking that pages cannot see

Most anti-adblock detection checks whether an ad library loaded. **Zitter's
stand-ins** answer the blocked request with a harmless body instead of an
error:

- AdSense (`adsbygoogle.js`, `show_ads.js`);
- Google Publisher Tags (`gpt.js`);
- Google Analytics (`analytics.js`, `ga.js`) and Tag Manager (`gtm.js`,
  `gtag/js`);
- Amazon's `apstag.js`;
- BlockAdBlock and FuckAdBlock wherever they are hosted;
- the ad pixels bait checks load.

The rules use `$redirect-rule`, which acts only on a request another rule
already blocks, so an exception, a site switched off, or the list itself
turned off still wins. Nothing extra is fetched; the request still never
leaves the machine. adblock-rust returns a redirect even when nothing blocks
the request, so the bridge answers with one only when `should_block()` holds.

The **Adblock Warning Removal List** removes the "please turn off your ad
blocker" messages and keeps bait elements visible on the sites it knows.

**uBlock filters** reaches much further: about 24 000 rules, many of them
`+js()` against detectors, but it adds 2.5 MB to download. Of its scriptlet
uses, about 93 % name ones Zitter ships under the same names (`aopr`, `acs`,
`set`, `aeld`, `nostif`, `aopw`, `nowoif`, …). The rest, uBO's `trusted-*`
family and a few rare ones, do nothing.

Lists in uBlock's format need two things adblock-rust does not do:

- `!#include file` lines pull in more files from the list's directory (most of
  uBlock filters lives in `filters-2020.txt` … `filters-2026.txt`). The
  download fetches them, at most 24 plain file names beside the list, and puts
  them in place. If one is missing, the old copy stays.
- `!#if` / `!#else` / `!#endif` blocks are resolved when the engine is built
  (`preprocess` in `adblock.rs`), the way uBlock Origin reads them on desktop
  Chromium: `ext_ublock`, `env_chromium` and `cap_user_stylesheet` are true;
  mobile, Firefox, AdGuard, ABP and HTML filtering are false. adblock-rust
  would otherwise keep both branches.

The switches are per profile (`zitter.ad_blocking_lists`, list id → on, only
lists switched away from their default). A list nobody has switched on is
never downloaded; switching one on fetches it at once and rebuilds the engine.

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
   machine, and counts it for the tab. A `$redirect=` (or `$redirect-rule=`)
   rule instead answers with a 200 response carrying one of Zitter's own
   bodies (see "Scriptlets and redirect bodies"), so a detector that watches
   for failed bait loads sees a success. The response allows any origin and
   is basic, CORS or opaque the way a real one would be;
4. checks a tab's rules on trial (`adblock_add_rules` with `check`) before the
   profile's engine.

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
- Zitter's own `:min-size(width, height)`: an element at least that many CSS
  pixels. Ad slots keep their sizes when class names and click URLs are
  generated (wp.pl routes every link, news included, through
  `www.wp.pl/<random token>`). Rules that look at layout (`:min-size()`,
  `:matches-css()`) keep what they hid hidden while it is in the page, since a
  hidden element has no size;
- actions, last in the rule: `:remove()`, `:style()` (nothing that loads, so no
  `url()`), `:remove-attr()`, `:remove-class()`;
- **slot collapsing**: once a rule hides an ad, a parent with no text of its
  own whose every child is hidden or empty (no words but an ad label, no
  media bigger than 160×60, which covers label badges, AdChoices and close
  buttons) is hidden too, up to six levels. It never collapses `main`,
  `header`, `nav`, `article`, `footer`, `aside` or `form`. Without this, wp.pl
  kept a 120 px strip and a 316 px gap where its banners were;
- elements they hide get a random per-page attribute that one style rule
  hides; ones no rule matches any more get it removed;
- a `MutationObserver` re-runs them as the page changes, at most every 150 ms
  and less often when a pass is slow; `<html>` and `<body>` are only ever
  restyled, never hidden or removed.

adblock-rust is built without its `css-validation` feature, so it keeps a
procedural selector as one string in `hide_selectors`, and rules with an action
come back as JSON in `procedural_actions`; the bridge passes both on. This also
makes the lists' own `#?#` and action rules work.

`:mask(x, y, width, height)` is Zitter's own action: an opaque box in the
page's background color over that region of the element (CSS pixels, or
percents of its box), placed in document coordinates and following scrolling
and resizing. It takes no clicks. It is meant for an ad painted at a fixed
spot inside a canvas or video.

### Scriptlets and redirect bodies

uBO's scriptlets and redirect resources are GPL-3.0, and so are AdGuard's, so
Zitter ships its own in `chromium/prototype/adblock_resources.js` (Apache-2.0).
They are written from the public descriptions of uBO's names and arguments,
so rules people know work:

| Scriptlet (aliases) | What it does |
| --- | --- |
| `set-constant` (`set`) | pins a global or a path under one (`a.b.c`, also created later) to `true`, `false`, `null`, `undefined`, `noopFunc`, `trueFunc`, `falseFunc`, `emptyArr`, `emptyObj`, `''` or a number |
| `abort-on-property-read` / `-write` (`aopr`, `aopw`) | a script that reads or assigns the path stops with an error the page never sees |
| `abort-current-script` (`acs`) | the same, only for the running script whose source or URL matches a needle |
| `prevent-setTimeout`, `prevent-setInterval` (`nostif`, `nosiif`) | timers whose code (and delay) match run nothing |
| `prevent-addEventListener` (`aeld`) | matching listeners are never added |
| `prevent-fetch`, `prevent-xhr` (`no-fetch-if`, `no-xhr-if`) | matching requests resolve with an empty 200 |
| `json-prune` | deletes paths (`a.b`, `[]` for array items, `*` for any key) from `JSON.parse` and `Response.json`, optionally only when other paths exist |
| `remove-attr`, `remove-class` (`ra`, `rc`) | keep removing attributes or classes as the page sets them |
| `set-cookie`, `set-local-storage-item`, `set-session-storage-item` | the site's own "notice dismissed" flag, from a safe set of values |
| `no-window-open-if` (`nowoif`), `noeval-if` | no matching pop-unders, no matching `eval` |
| `zitter-skip-draw` | Zitter's own: images whose URL (or natural size `WxH`) matches are not painted into a 2D canvas, and WebGL gets a transparent pixel; sources are followed through `<img>`, `<video>`, `ImageBitmap` and fetched blobs |

Needles are literal text or `/regex/`; `!` negates. Every wrapped native is
masked by a shared helper, so `fetch.toString()` still reads
`function fetch() { [native code] }`.

Redirect bodies: `noop.js`, `noop.txt`, `noop.json`, `noop.html`, `noop.css`,
`1x1.gif`, `2x2.png`, and stand-ins for Google Publisher Tags
(`googletagservices_gpt.js`: `googletag` with a working `cmd` queue and empty
slots, because many pages wait on it before they render), Google Analytics
(`google-analytics_analytics.js`) and AdSense (`googlesyndication_adsbygoogle.js`).

How they run:

- adblock-rust gets the file's sections through `Engine::use_resources` and
  assembles each host's script from the `+js()` rules that name it
  (`url_cosmetic_resources().injected_script`), including the helpers it
  needs. The bridge wraps it in one closure, so nothing becomes a page global.
- `check_rule` refuses a scriptlet or redirect body Zitter does not ship and
  lists the ones it has.
- **Main world at document start.** `tools/content_blocking_patch.py` adds
  `RendererPreferences::zitter_page_scripts`, a list of `host\nscript` entries,
  and calls `chrome/renderer/zitter_page_scripts.cc` (kept in
  `chromium/renderer/`) from `ChromeContentRendererClient::RunScriptsAtDocumentStart`.
  For every frame, before any page script, it runs the scripts whose host is
  the frame's host or a parent domain of it. It uses the inherited origin for
  `about:blank` and `srcdoc` frames, which ad code uses to fetch clean natives.
- `ContentBlockingTabHelper` fills that list per tab: the profile's page
  scripts for hosts where blocking is on, a trial's, and the diagnosis
  probe. It syncs the renderer preferences only when the list changes.

Main-world code is visible to the page in principle (it shares the page's
objects); the wrappers keep their native look, and the helpers live in a
closure.

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

Design: [Zitter — Ad blocking preferences](https://claude.ai/artifact/XxZgCge5RjuXEg2YwdgBMC).
The category **Blokowanie reklam** ("Ad blocking") comes last in the preference
catalogue in `sidebar_scene.rs`. Its navigation entry is a shield (glyph 11,
drawn from the same rounded rectangles as the other category icons). The
category has five cards, each with a title and one short line:

- **Blokuj reklamy i śledzenie** (toggle, pref `zitter.ad_blocking`, default
  on, per profile);
- **Ukrywaj blokowanie przed stronami** (toggle `ad_blocking_stealth`): one
  switch for Zitter's stand-ins and the Adblock Warning Removal List
  together, the two lists that keep pages from seeing the blocking;
- **Listy filtrów** (row kind `AdBlocking`):
  - the lists' state ("Aktualne · 5 lis") and **Aktualizuj** on one line;
  - one compact row per list: name, rule count in `url` (mono), then
    "Zawsze" or the list's switch (uBlock filters, with "about 2.5 MB");
  - the attribution EasyList's license asks for, behind a **Źródła i
    licencje** chip;
- **Witryny bez blokowania** (`AdBlockingSites`): the allow list, each site
  with **Włącz**; the card's line says how to add one;
- **Twoje reguły** (`AdBlockingRules`): a summary ("Reguły: 10 · witryny: 3"
  and each site with its count, "(nie działa)" for a site with a stale rule)
  and **Edytuj reguły**.

The rules themselves are edited in **`AdBlockingRulesPanel`**
(`ad_blocking_rules_panel.{h,cc}`), a window-modal dialog over Settings built
on `PanelScene` like the passwords panel. It has two views of the same pref
(`zitter.ad_blocking_rules`), and a × to close.

- **Lista**:
  - the sites as chips ("Wszystkie" first, each with its count);
  - the chosen site's rules in `url` with their note (the comment line above
    the batch), a copy and a remove action. A stale rule says "Nic nie ukrywa
    od kilku wizyt" and offers **Napraw z asystentem**, the shield's repair
    for that one rule;
  - a field to add a rule, checked before it is saved.
- **Tekst**: the whole list in a `Textarea` in the mono face, notes
  included. Every edit is checked line by line (`ContentBlocking::TextErrors`):
  the first four errors show by line number, and **Zapisz** stays off until
  there are none. Saving replaces the list (`SetRulesText`) and drops the
  stale counters of rules that are gone.

Rules are grouped by `ContentBlocking::RuleSite`: the first host before `##`,
else `domain=`, else the `||host`.

Differences from the design:
- the text view has no line numbers or syntax colours (a plain `Textarea`;
  the app's own editor is single-line);
- the site list uses the filter chips of the scene rather than full-width
  rows;
- the cards carry no icon tiles, like every other Settings card.

### List updates

- At startup the service reads the files. A list some loaded profile has on,
  missing or older than four days, is downloaded 30 s after startup (so it
  never competes with the first page), through the system network context: no
  cookies, no profile data.
- Every 24 h while running, the same check.
- **Aktualizuj teraz** downloads at once.
- A download must be HTTP 200, at most 16 MB with its includes, start with
  `[Adblock` or `! Title:` and parse to at least 1 000 rules; otherwise the old
  file stays.
- This is new background traffic, under the traffic annotation
  `zitter_filter_lists`: GETs to easylist.to and
  easylist-downloads.adblockplus.org every four days, and, with uBlock filters
  on, to ublockorigin.github.io (the list and its includes). The next network
  audit (`docs/network-audit-2026-10-23.md` is the last) should list it.

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
  - **image links with no words** of at least 300×90 (banners; every teaser has
    a headline), with the rule
    `site##a[href]:has(img,picture,video,canvas):not(:has-text(/\p{L}/u)):min-size(300, 90)`;
  - a layer holding nothing but the label (a page wallpaper) counts as the ad
    whatever its size, and its rule gets `:not(:min-text-length(n))`, so menu
    items that read "Reklama" stay;
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
   With scriptlets or exceptions among the rules the question says what
   each kind does: "Zmienić blokowanie na example.com? Ukryte elementy: 0,
   reguły sieciowe: 0, skrypty: 0, wyjątki (przepuszczają): 1.";
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

**`adblock_add_rules` with `check: true`** saves nothing and asks nothing.
It validates the rules the same way, then puts them on trial in this tab only:

- an engine of the lists, the profile's own rules and the tried ones, built
  in the background, takes the profile engine's place in this tab, for its
  page scripts and its requests. It is the whole engine, so an exception on
  trial (`@@||host/file.js^$domain=site`) can lift a list's block; a first
  version that held only the tried rules could not, which the live test on
  detectadblock.com caught;
- the tab reloads;
- the hiding rules apply after the load.

The model then looks (screenshot, `read_page`) and calls again without `check`
to save, with consent. Leaving the site, or saving, ends the trial. The tool's
description tells the model to always try scriptlet and redirect rules first.

**`adblock_diagnose {tab}`** (page and action permissions) is for
anti-adblock walls, ads that come back, and ads painted into a canvas. It
sets a probe (`content_blocking_probe.js`) as the tab's main-world script for
one reload, waits for the load and six seconds more, and reads the probe's log
through a `CustomEvent` whose name is a random nonce only the isolated world
knows. The log holds:

- `bait_reads`: who measures elements named like ads (`offsetHeight`,
  `getComputedStyle`, `getBoundingClientRect`);
- `failed_loads` and `error_listeners`: which loads failed (blocked bait) and
  who listens for that;
- `globals`: reads and writes of detector globals (`canRunAds`,
  `isAdBlockActive`, `googletag`…);
- `walls` and `scroll_locks`: big or fixed elements whose text asks to turn
  blocking off, the script that inserted them, and `overflow: hidden` on
  `<html>`/`<body>`;
- `canvas`: every image drawn into a 2D or WebGL canvas, with its source,
  size and draw count.

Each entry names its script (`by`, from the call stack), so the model can
write `abort-current-script`, `set-constant`, `$redirect` or `zitter-skip-draw`
rules. Then it tries them with `check`. The probe is removed after the one load.

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
    `span:has-text(…):upward(1)`; a `:min-size()` banner rule keeps the banner
    hidden and collapses its slot; a rule that matches nothing and one with a bad
    regex come back as invalid, before any preview; the label rule hides the
    ad at once and, through the engine, on a fresh load, together with a
    `:style()` action rule; an ad added later with the same label is hidden as
    it appears; three missed loads make a rule stale and one hit clears it;
    `replaces` swaps the stale rule for a new one and refuses rules that were
    never saved.
  - `RulesEditorListsBySiteAndEditsAsText`: rules group by site (`RuleSite`
    for cosmetic, `domain=` and `||host` rules); the editor lists every site
    and then one; adding checks the rule; text mode counts a bad line, saves
    nothing until it is fixed, then replaces the rules notes included; the
    "Ukrywaj blokowanie" switch turns both of its lists off and on.
  - `BuiltInStandInsAndOptionalLists` (`testdata/adblock_detectors.html`):
    Zitter's stand-ins and the Adblock Warning Removal List are on by default
    and uBlock filters off; a BlockAdBlock file the test list blocks loads as
    Zitter's stand-in, and with the stand-ins switched off it fails again; the
    switches are per profile and required lists have none.
  - `ScriptletsRedirectsAndCanvasBeatAnAntiAdblockWall`
    (`testdata/adblock_wall.html`): the page walls itself off because its bait
    script is blocked and `canRunAds` is missing, and it paints an ad into a
    canvas. `adblock_diagnose` names the failed load, the global, the bait
    element, the wall and the canvas draw, and leaves no probe behind. Unknown
    scriptlets and bodies are refused. `check` with `set-constant`, a `noop.js`
    redirect and `zitter-skip-draw` reloads the tab without the wall, with the
    bait loaded and the canvas empty, saving nothing; saved, the same holds on
    a fresh load through the profile's engine, and `toString` stays native and
    no helper becomes a global. A `:mask()` covers half the canvas; the site
    switch removes the scripts.

## Known limits

- Requests go through a browser-side URLLoaderFactory proxy on the UI thread, as
  with extensions' webRequest. Matching takes microseconds, but every
  subresource now makes that hop.
- A request is checked once: a redirect to a blocked URL is not re-checked.
- Until the lists are read at startup (or downloaded on first run), pages load
  unfiltered.
- Generic hiding sees classes and IDs at commit, DOMContentLoaded, load and 3 s
  later; elements added after that only get the site's and complex generic rules.
- Scriptlets and redirect bodies are Zitter's own set (above); rules naming
  uBO-only ones (`trusted-*`, `href-sanitizer`, …) are refused, as are the
  procedural operators `:watch-attr()`, `:others()`, `:shadow()` and
  `:matches-prop()`.
- Scriptlets reach a new frame through renderer preferences: a page that
  opens right after rules change may load once without them.
- Procedural rules are re-run on DOM changes, not on scroll or style changes
  alone.
- Server-side gating (content only after an ad impression) and ads stitched
  into a video stream cannot be fixed in the page; see
  [ad-blocking-canvas-and-anti-adblock.md](ad-blocking-canvas-and-anti-adblock.md).

## Status

Implemented as planned above, in `chromium/prototype/adblock.rs` (engine bridge),
`content_blocking.{h,cc,js}` (lists, engines, request proxy, cosmetic hiding,
tab counts), `extensions_bar.cc` (shield), `sidebar_scene.rs` and
`workspace_shell.cc` (Settings), `workspace_assistant.cc`, `page.js` and
`engine.mjs` (assistant tools). Procedural rules, label anchors and stale-rule
repair were added afterwards in the same files. Then came scriptlets, redirect
bodies, the main-world hook, `:mask()`, `zitter-skip-draw`, `adblock_diagnose`
and `check`, in `adblock_resources.js`, `content_blocking_probe.js`,
`chromium/renderer/zitter_page_scripts.cc` and `tools/content_blocking_patch.py`.
