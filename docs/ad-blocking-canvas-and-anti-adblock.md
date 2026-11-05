# Ad blocking: anti-adblock and canvas ads (plan)

Status: **plan, not implemented.** It follows [ad-blocking.md](ad-blocking.md),
which describes what ships: network rules, cosmetic and procedural hiding,
assistant-written rules and stale-rule repair.

What is still missing falls into two groups:

- **Anti-adblock**: pages that notice blocking and then nag, lock scrolling,
  withhold the article or reinsert the ad.
- **Canvas ads**: ads drawn as pixels, which leave no element to hide.

Both need one thing Zitter does not have yet: Zitter's own code running in
the page's **main world** before the page's scripts. Today Zitter only runs in an
isolated world, after commit.

## 1. Anti-adblock

### How pages detect blocking

| Technique | What the page checks | What trips it today |
| --- | --- | --- |
| Bait element | Creates `<div class="adsbox ad-banner">` and reads `offsetHeight` | Generic hiding hides it (height 0) |
| Bait request | Loads `/ads.js` or `adsbygoogle.js` and listens for `onerror` | The network rule fails the request |
| Ad globals | `window.googletag`, `adsbygoogle.loaded`, a flag the ad script sets | The script never ran |
| Timing or beacons | No impression beacon within N seconds | No ad was loaded |
| Commercial detectors | Google Funding Choices, Admiral, Sourcepoint, BlockAdBlock | All of the above, plus their own checks |
| Server side | The article body arrives only after an ad beacon | Nothing in the page to fix |

### What already works

Since procedural rules landed, a **soft wall** (an overlay plus a scroll lock)
can be removed with plain rules:

```
example.com##div:has-text(/wyłącz adblock|disable your ad ?blocker/i):upward(3):remove()
example.com##body:style(overflow: auto)
example.com##html:remove-class(/^(no-?scroll|locked)$/)
```

The assistant knows these actions. They do not help when the page:

- withholds content;
- puts the wall back from a timer;
- redirects to a "please disable" page.

### Plan

**Step A: run in the page's main world at document start (the base for
everything below).**

- Add a renderer-side hook to the overlay: a `content::RenderFrameObserver`
  that runs a script synchronously in `DidCreateScriptContext` for the main
  world, before any page script. This is how Brave injects its cosmetic
  filtering and scriptlets.
- The script comes from the browser per frame URL over a small mojo interface,
  like the isolated-world calls today.
- It needs a `ContentRendererClient` patch in `tools/` (Zitter has no renderer
  code yet), plus browser tests that check it runs before an inline
  `<script>`.
- Rejected alternatives:
  - A bundled component extension with an MV3 `world: "MAIN"`,
    `runAt: "document_start"` content script needs the extension system for
    every frame and shows up in the extensions UI.
  - DevTools `Page.addScriptToEvaluateOnNewDocument` attaches a debugger to
    every tab.

**Step B: Zitter's own scriptlet library (Apache-2.0, written from scratch).**

uBO's scriptlets are GPL-3.0, as is AdGuard's Scriptlets library, so neither
ships. Zitter writes its own from their documented behavior, under the same
names and arguments, so rules people already know work:

- `set-constant`: pins `adsbygoogle.loaded` or `canRunAds` to the value that
  passes;
- `abort-on-property-read` and `abort-on-property-write`: stop a detector at
  the global it touches;
- `abort-current-script`: stop an inline script that mentions a needle;
- `prevent-setTimeout` and `prevent-setInterval`: stop the "wall again in 3 s"
  timer;
- `prevent-fetch` and `prevent-xhr`: bait requests get a fake success;
- `prevent-addEventListener`: drop the detector's `load` or `error` listener;
- `json-prune`: strip ad fields from the site's JSON API responses;
- `set-cookie` and `set-local-storage-item`: set the site's own "adblock
  notice dismissed" flag;
- `no-window-open-if`: no pop-under.

How it fits together:

- Each scriptlet gets a test page in `testdata/` and a browser test.
- adblock-rust already parses `site##+js(name, args)` and assembles the script
  (`injected_script` in `url_cosmetic_resources`) from resources you register
  with `Engine::use_resources`. Zitter registers its own library there and
  passes the result to step A.
- `check_rule` then accepts `+js()` for scriptlets that exist and refuses
  unknown names.

**Step C: neutral answers instead of errors.**

Many detectors only notice the failed request. With `$redirect=` (also parsed
by adblock-rust) the request proxy answers from a resource Zitter ships instead
of failing:

- `noop.js`, `noop.txt`, `noop.html`, `1x1.gif`, `noop.json`;
- a surrogate of Google Publisher Tags: `googletag` with `cmd.push` that runs
  queued callbacks, `defineSlot` and `pubads()` returning no-op objects. Many
  pages wait on this API before they render.

These are Zitter's own files, again because uBO's are GPL. This needs a branch in the
URLLoader proxy that completes a blocked request with a body instead of
`ERR_BLOCKED_BY_CLIENT`.

**Step D: the assistant finds the detector.**

A new tool `adblock_diagnose {tab}`:

- reloads the tab with main-world probes from step A;
- records what reads bait-element sizes, what listens to blocked requests'
  errors, and which globals are read before a wall appears, each with the
  script URL and a short stack;
- adds what changed in the DOM when the wall appeared.

The model then writes `+js()` and `$redirect` rules. `adblock_add_rules` gains a
check mode for them: apply the rules for one reload of this tab only, take a
screenshot, and report whether the wall is gone. Only then does it ask for
consent and save. Cosmetic rules get this check from the page today; script
rules need a reload to show their effect.

**Step E: limits to state honestly.**

- Server-side gating (the content arrives only after an ad impression) cannot
  be fixed in the page.
- Detectors change, and a rule that works today may need repair next month.
  Stale-rule repair counts only hiding rules today; script rules would need
  their own success signal (for example "the wall element did not appear").
- The per-site switch stays the user's way out.

### Order and rough size

| Step | Size (estimate) | Unblocks |
| --- | --- | --- |
| A. Main-world hook | 2–3 days with tests | B, C (surrogates that run JS), D, canvas level 3 |
| B. Scriptlets (≈12) | ~1 week with test pages | Most detectors |
| C. Redirect resources + GPT surrogate | 2–3 days | Detectors that watch request errors |
| D. `adblock_diagnose` + check mode | 2–3 days | The assistant writing B and C rules itself |

## 2. Canvas ads

### Where they come from

1. **An ad creative that draws into its own `<canvas>`**, inside an ad slot or
   iframe (HTML5 banners from Google Web Designer or Adobe Animate). This is the
   common case. The `<canvas>` is a DOM element, so hiding it, its iframe or its
   labelled box works today. `adblock_inspect` should also list `<canvas>` and
   `<video>` elements of standard ad sizes as candidates.
2. **An app drawn entirely on one canvas** (Flutter web, game portals, map
   viewers) with an ad painted somewhere inside it. No element to hide.
3. **Ads stitched into a video stream** on the server side. The same pixels as
   the content.

### Plan

**Level 1: canvas and video as candidates (done).** In the `adblock_inspect`
walk, `<canvas>` and `<video>` elements of standard ad sizes become
candidates; inside a labelled or third-party box they are found through that
box. This covers case 1.

**Level 2: mask a region (case 2, static layouts only).**

- A new Zitter-only action, `site##canvas#app:mask(x, y, w, h)`, places an
  opaque box above that region of the element. The coordinates are relative
  to the canvas and follow its resizes.
- The assistant gets the region from a screenshot (the model sees it) and the
  canvas box from `read_page`.
- This is fragile: when the app scrolls inside its canvas, the mask is wrong,
  so it only suits ads at fixed positions (a bottom banner in a game).
- The rule must say it is Zitter syntax, so other blockers ignore it.

**Level 3: stop the draw (case 2, the real fix; needs step A).**

- Main-world hooks on `CanvasRenderingContext2D.drawImage`, `putImageData`,
  `createPattern` and WebGL `texImage2D` / `texSubImage2D` record where each
  drawn image came from: the `src` of an `<img>`, the URL behind an
  `ImageBitmap` or a `fetch`, its size and the target rectangle.
- A new tool, `canvas_inspect {tab}`, returns that list for the model, with a
  crop of each image so it can see which one is the ad.
- A rule such as `site##canvas:skip-draw(src=/\/creatives\//)` or
  `skip-draw(size=300x250)` turns those draws into no-ops.
- When the ad image comes from an ad host, the network rule already blocks
  it. This is for first-party CDNs with random paths (the wp.pl pattern).
- A perceptual hash of the creative is a later option, for when the source
  URL and size reveal nothing.
- Cost: the hooks wrap hot drawing calls, so they install only on sites with a
  `skip-draw` rule.

**Level 4: video.** Out of scope. Server-side ad insertion has no separate
request or element to block. The only realistic move is the player's own skip
and its manifest, which differs per site.

### Order and rough size

| Level | Size (estimate) | Needs |
| --- | --- | --- |
| 1. Canvas and video candidates | done | Nothing new |
| 2. `:mask()` | 2 days | Nothing new (isolated world can draw the mask) |
| 3. `skip-draw` + `canvas_inspect` | ~1 week | Step A |

## Recommendation

Level 1 is done and covers most "canvas ads" seen on news sites. Next:

1. Step A, because every remaining anti-adblock and canvas measure builds on
   it.
2. Then B and C together, since detectors mostly combine bait elements and
   bait requests.
3. Then D, so the assistant can write those rules itself.
4. Level 3 last, when a real site needs it.
