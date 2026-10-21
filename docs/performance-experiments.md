# Historical performance experiments

These chronological notes include intermediate failures and superseded pending states.
Use [the final report](performance.md) for the accepted protocol, results and current status.

# Browser performance work

## Acceptance overview

Effort is a subjective 1–5 scale (1: small local change; 5: engine-scale work).
The measurements below have different scopes; each row names its own scope.
The production deliverable remains pending Release/optimized build validation.

| Original item | Evidence / decision so far | Cost and effort |
| --- | --- | --- |
| Separate Release and baseline measurements | Frozen original component Release built, measured and packaged; native new-tab dispatch 103.64 → 19.43 ms, split 48.05 → 6.21 ms; normal Release app comparisons completed | Separate build/output disk space; effort 1 |
| Update individual tab rows | Retained as part of the UI group: normal-app new-tab dispatch 103.59 → 70.51 ms | No isolated attribution; effort 2 |
| Reduce snapshot/serialization work on UI | Capture stays synchronous; batched background persistence retained; close-flush regression passed | Up to 350 ms batching, flushed at close; effort 2 |
| Coalesce UI updates | Retained in the same measured UI group; explicit commands stay synchronous | Queued task/lifetime management; effort 2 |
| Set pane bounds before showing pages | Retained in UI group: normal-app split dispatch 45.75 → 38.60 ms, acknowledgement p95 103.16 → 80.56 ms | Already-loaded split pages; effort 2 |
| Lazy inactive tabs | Warm-profile restoration: realized pages 50 → 2; browser physical footprint 178.14 → 140.42 MiB; both first paints 6993 → 2694 ms | Dormant page loads deferred until shown; not total process-tree memory; effort 3 |
| Prioritize active profile | Four fresh profiles in warm test process: ready 4448 → 1545 ms; with restored page first paint 6721 → 4646 ms | Other profiles continue loading; aggregate profile batch slightly slower; effort 3 |
| Warm renderers | Keep Chromium's default one spare; two-spare experiment showed no new-tab improvement and no reliable split-tail gain | Avoid unproven extra renderer allocation; RSS sums are not physical memory; effort 2 |
| Warm connections | Retained with per-profile privacy control: local modeled 120 ms RTT first paint 354 → 242 ms; exactly one reused connection and no speculative HTTP GET | An abandoned hint can create an unused connection; respects profile settings/policy, debounce and deduplication; effort 3 |
| Non-component build, ThinLTO and PGO | Current source staged; pinned Clang and V8 profiles fetched/validated; production compilation completed; static-launch allocator ordering and main-executable LTO corrected; relink/measurements pending | Longer compile/link and one-time profile downloads; benefit still unproven; effort 4 |

Destination-seeded SiteInstance/early navigation and the wait-for-all-profile
variant were rejected after measurements failed to establish a gain. The first
visually nonempty paint latency of new tabs has **not** improved in the measured
normal Debug app; the remaining build comparisons must address that explicitly.

## Stage 1: establish a baseline and compare Release

Measured on 2026-10-20, macOS ARM64, using the isolated browser-test harness.
Production UI/lifecycle code has not been optimized in this stage. The benchmark
uses 25 loaded local tabs, two excluded warmups and 30 measured trials per scenario.
Compilation was stopped before the baseline run.

| Metric | Debug median / p95 | Component Release median / p95 |
| --- | ---: | ---: |
| New-tab synchronous dispatch | 103.64 / 107.23 ms | 19.43 / 22.98 ms |
| New tab through first visually nonempty paint notification | 1153.20 / 2012.72 ms | 864.08 / 1683.82 ms |
| Split synchronous dispatch | 48.05 / 48.96 ms | 6.21 / 7.07 ms |
| Split through frame submissions from both renderers | 99.76 / 107.42 ms | 22.85 / 23.39 ms |

Raw samples and configuration are in
`build/performance/20261020T183219062188Z.json`, with the adjacent `.log` file.
These local generated artifacts are ignored by Git.
The frozen original Release completed on 2026-10-21 with the same 30 trials,
25 tabs and measurement method, after compilation finished and other native test
runs had ended. Its raw results are `build/performance/20261020T223534922117Z.json`
and adjacent log; `build/ZitterRelease.app` preserves the original product bundle.
This isolates compiler configuration from product changes, using the browser-test
runner in both cases. Normal-app comparisons are recorded separately below.

The first-paint samples alternate between approximately 300–340 ms and 2 s.
The median falls between these two groups; it should not be read as one stable
1.15 s loading path. Renderer allocation/startup is a candidate for further
tracing; the current experiment does not establish that cause.
The original component Release also alternated strictly: 15 fast trials had a
67.61 ms median and 15 slow trials a 1669.80 ms median (split descriptively at
500 ms). Its combined 864.08 ms median is again between two groups, rather than
a typical individual load. The effect persists after compiler optimization;
non-component/helper startup measurements remain particularly relevant. This
pattern alone does not prove the source of the delay.

The split scenario uses an already loaded second tab. It includes pre-change
snapshotting, sidebar changes, visibility changes and resizing. It does not
include downloading a new split page. Renderer paint/submission signals are
proxies for visual readiness, not physical display presentation.

The separate `out/ZitterRelease` configuration changes `is_debug` to false while
keeping component linking and the same symbol settings. This isolates the first
build change. Its build, benchmark and packaging run sequentially, so the Release
measurements run after compilation finishes. An optimized build is not evidence
of an improvement until the corresponding measurements complete.

## First measured UI candidate

The isolated Debug candidate with row retention, queued shell refreshes,
background JSON serialization/batched history and pane layout ordering completed
the same 30-trial/25-tab benchmark with compilation stopped:

| Metric | Baseline median | Candidate median | Baseline p95 | Candidate p95 |
| --- | ---: | ---: | ---: | ---: |
| New-tab synchronous dispatch | 103.64 ms | 70.13 ms | 107.23 ms | 71.48 ms |
| New tab through first visual paint | 1153.20 ms | 1146.10 ms | 2012.72 ms | 2072.18 ms |
| Split synchronous dispatch | 48.05 ms | 37.59 ms | 48.96 ms | 41.13 ms |
| Split through both renderer frames | 99.76 ms | 58.52 ms | 107.42 ms | 105.62 ms |

Candidate samples/configuration: `build/performance/20261020T190746996381Z.json`.
This measures the group of UI changes, not individual attribution. The two
first-paint groups remain: faster samples improved but the slow ~2 s path did
not. Split median improved substantially; its p95 is effectively unchanged.
The Release build was resumed after this run (pipeline session `98019`).

A subsequent candidate seeded a new tab's SiteInstance with its destination URL
and started navigation before selecting/showing it. Its seven targeted tests
passed (Peek, cross-profile routing, form/frame/redirect routing, command-center
copy/move/sleep, password save/fill, split lifecycle and storage isolation).
However, its 30-trial run did not establish an improvement:

| Metric | UI candidate median/p95 | Navigation experiment median/p95 |
| --- | ---: | ---: |
| New-tab dispatch | 70.13 / 71.48 ms | 72.44 / 79.65 ms |
| First visual paint | 1146.10 / 2072.18 ms | 1188.79 / 2324.07 ms |
| Split both frames | 58.52 / 105.62 ms | 83.27 / 116.49 ms |

Samples: `build/performance/20261020T191524684760Z.json`. The Release parent was
stopped, and a process snapshot confirmed all its compiler children had finished
before measured samples started. The experiment was reverted; destination
SiteInstance seeding and early navigation are not in the retained candidate.

## Lazy contents candidate

The current candidate keeps restored URL/title metadata in native placeholder
views and creates/attaches WebContents and profile clients only when a page is
shown. Snapshot capture, palette search, page ownership lookup, beforeunload
enumeration and dormant-tab closing inspect existing contents without creating
them. The selected panes still receive clients before navigation. The profile
catalogue/keep-alives are unchanged at this stage.

`RestoredTabsStayUnrealizedUntilShown` passed: an eight-tab session starts with
only two WebContents for the selected split; dormant-tab search/close does not
realize more; title/URL retention, immutable profile ownership, snapshot restore,
closed-tab reopening and another session restart work. An outside-pair restored
selection starts with one live page. Log: `.chromium/test-candidate-lazy.log`.
This proves object-count reduction, not yet a measured byte-level RAM or startup
latency improvement. Broader profile integration tests are now running in exec
session `81082` (check actual running handle before relying on this note); their
log is `.chromium/test-candidate-lazy-integration.log`. The default performance
test and explicit manual/Arc-import tests are excluded, as is the independently
reproduced baseline `KeyboardWorkspaceSwitching` keyup failure.

### Restoration measurement prepared

`PerformanceRestoration` is an opt-in benchmark with the same iteration/tab
switches. Each trial recreates a window from fixed session/history fixtures,
using two already-loaded profiles and `--tabs` saved pages per workspace. It
reports ready time, both first-paint notifications, realized WebContents count
and browser-process resident/physical-footprint bytes. Memory sampling waits for
page load and persistence to settle and is outside the timed region. This is
warm-profile window restoration, not cold process/profile startup or total
browser-tree memory.

Run it via `tools/benchmark_browser.py --scenario restoration`. The frozen
product reference with current tests/fixtures is staged as `zitter_reference`
(`--reference-overlay .chromium/src/zitter --current-tests`) and compiled as
`out/Zitter/zitter_prototype_reference`. The latest reference build including
memory sampling completed successfully (exec `88061`). Default Debug GN
`root_extra_deps` now includes both candidate and reference test targets.

The running broad integration suite uses the previously built lazy candidate
binary. Do NOT rebuild/overwrite that executable until its launcher finishes.
After suite `81082` completes, restage/rebuild `zitter_candidate` to include the
new restoration benchmark/memory fields (product sources are unchanged since the
lazy test passed). Then smoke-test and measure BOTH reference and candidate with
matching scenario, iterations and tabs. Pause the verified Release build and let
compiler children finish before collecting samples; always resume it afterward.
The reference's newest lazy-only regression test is intentionally not applicable
to old product code; run only the opt-in restoration benchmark on that reference.

### Restoration results

The full 30-trial comparison completed with the Release build stopped and no
active compiler children. Both scenarios used 25 saved pages per workspace, two
warm profiles, two excluded warmups and an active split. The reference kept
original product code and used the same current measurement code/fixtures.

| Metric | Original median | UI + lazy median | Original p95 | UI + lazy p95 |
| --- | ---: | ---: | ---: | ---: |
| Window ready | 1159.71 ms | 657.61 ms | 1180.96 ms | 749.19 ms |
| Both pages' first visual paints | 6993.12 ms | 2693.86 ms | 7203.02 ms | 2708.97 ms |
| Realized pages (count) | 50 | 2 | — | — |
| Browser physical footprint (MiB) | 178.14 | 140.42 | — | — |
| Browser resident memory (MiB) | 773.46 | 741.27 | — | — |

Raw results are `build/performance/20261020T194233349416Z.json` (reference) and
`build/performance/20261020T194643616919Z.json` (candidate). Main-process physical
footprint fell by about 37.72 MiB (21%); this is not a total-browser-tree memory
claim. These results compare the retained UI/lazy group against the original
product, rather than isolating every individual code change. The active-profile
priority experiment below was NOT included. The Release build was resumed after
the comparison (exec pipeline `22402`, temporary driver
`.chromium/compare-restoration.py`).

The broad lazy integration suite finished successfully: **41/41 tests passed**
in `.chromium/test-candidate-lazy-integration.log` (exec `81082`, completed).
The separately reproduced baseline keyup test remains excluded, as described
below. Python tool verification now passes 22 tests; independent core contracts,
boundary checks and diff whitespace checks pass.

## Active-profile startup experiments

The atomic priority variant passed 4/4 startup/catalogue/import/write-failure
regressions (`.chromium/test-profile-startup.log`), but did **not** improve window
readiness in a 30-trial, four-fresh-profile comparison against the frozen lazy
serial reference:

| Debug metric (median / p95, ms) | Serial lazy reference | Atomic priority |
| --- | ---: | ---: |
| Window ready | 4317.85 / 6896.50 | 4389.57 / 6720.15 |
| Profile batch | 3316.35 / 5530.29 | 3341.72 / 5390.66 |
| Active-profile creation delay | 2762.35 / 4666.01 | 0 / 0 |

Raw JSON: `build/performance/20261020T200006036467Z.json` (serial) and
`build/performance/20261020T200256684217Z.json` (atomic). Browser-process physical
footprint medians were 182653560 and 182792908 bytes respectively; both had one
live blank page. This experiment is not accepted as a performance improvement.

`PerformanceProfileStartup` / `--scenario profiles --profiles 4` creates fresh,
empty profile directories each trial, with the browser process/UI globals warm.
Profile-manager/cache state accumulates across trials. These are not cold
full-process or populated-user-profile timings. The frozen comparison product is
`.chromium/src/zitter_lazy_reference`; preserve its serial loader.

A further **unverified** candidate now makes the active workspace ready after its
own profile is acquired, keeps other restored tabs as metadata-only WebViews,
and binds their BrowserContext only while they have no WebContents. Other cold
profiles begin after a 100 ms grace period; explicit selection/navigation requests
start their target immediately. Whole cross-profile intents are deferred, with
newer intents replacing older pending ones. Catalogue management stays disabled
until all profile leases exist, and callbacks identify profiles by ID + directory.
Already initialized profiles are leased synchronously to retain normal switching.
The initial six-test run passed five cases (including pending-intent cancellation,
closing during load, lazy restoration, import and write failure). A newly added
malformed-path test exposed an upstream database DCHECK when a profile directory
was actually a file. The candidate now checks cold paths on the background config
runner before calling ProfileManager, while cached profiles retain the immediate
path. That failure test passed separately (`.chromium/test-startup-failure-safe.log`).
The final startup + import confirmation passed 2/2
(`.chromium/test-early-profiles-confirm.log`).

The complete four-fresh-empty-profile comparison passed (30 measured trials per
variant, two warmups, matching updated tests, Release suspended during timing):

| Debug metric (median / p95, ms) | Serial lazy reference | Early active workspace |
| --- | ---: | ---: |
| Active window ready | 4448.17 / 6823.80 | 1544.54 / 2160.70 |
| All-profile batch | 3391.34 / 5472.68 | 3474.31 / 5480.93 |
| Active-profile creation delay | 2825.74 / 4609.15 | 0 / 0 |

Raw JSON: `build/performance/20261020T203346115995Z.json` (serial) and
`build/performance/20261020T203637973213Z.json` (early workspace). Window-ready
median improves 65.3%, p95 improves 68.3%. Total profile-batch median increases
2.4%; its p95 is essentially unchanged. Browser-process physical-footprint
medians are 183161608 and 181990128 bytes (a small difference, not evidence of a
material RAM saving); both realize one blank page. Measurements retain the warm
browser process and accumulating manager/cache-state limitation described above.
The comparison finished successfully and resumed the verified Release build
(exec `16327`, `.chromium/compare-early-profiles.log`).

Window-ready notifications alone are not proof of earlier visible page content.
`--scenario profile-page` now measures first visual paint of a restored local page
in the newly initialized active profile as well. The test change is staged on
both the candidate and frozen serial lazy reference. Exec `31403` performs GN
checks, builds both test binaries (`.chromium/build-profile-page.log`), runs 2-trial
smokes and 30-trial comparisons (`.chromium/compare-profile-page.log`), then runs
the broad post-profile integration suite
(`.chromium/test-early-profiles-integration.log`). The known baseline keyboard
keyup case and manual/live/performance cases are excluded from that functional
suite. Do not restage or rebuild these variants during their timings/tests.
The full page comparison has now completed successfully:

| Debug metric (median / p95, ms) | Serial lazy reference | Early active workspace |
| --- | ---: | ---: |
| Active window ready | 4527.57 / 6974.67 | 1623.10 / 2250.45 |
| First visual paint | 6721.32 / 9126.42 | 4646.00 / 7067.36 |
| All-profile batch | 3390.80 / 5502.70 | 3633.20 / 5777.39 |

Raw JSON: `build/performance/20261020T204205739550Z.json` (serial) and
`build/performance/20261020T204603123302Z.json` (early). First-paint median improves
30.9%, p95 improves 22.6%; all-profile batch median increases 7.1%. This is a real
visible-readiness signal benefit within this warm-process/fresh-profile/local-page
scenario, not a claim about populated profiles or cold application startup.
Browser-process RSS medians are 815783936 / 821903360 bytes; physical-footprint
medians 186389280 / 180949792 bytes. These mixed small differences do not establish
a meaningful memory improvement, and exclude renderer-process memory. Both
variants realize exactly one page.

The comparison resumed the original Release build. Exec `31403` completed the
broad functional run successfully: **43/43 passed in 910 seconds**, including
startup cancellation/closing/failure, lazy restoration, isolation, beforeunload,
history/passwords, split sessions, dynamic profiles and Arc import. Log:
`.chromium/test-early-profiles-integration.log`. The separately reproduced baseline
keyup test and manual/live/performance cases were excluded as documented above.
This is the measured early-profile variant before the further ID-queue hardening
below, so it is not a blanket test claim for all subsequent source changes.

A further source review found a catalogue mutation hazard in the background
scheduler: all cached leases can be ready before the 100 ms grace task, allowing
profile removal before a saved vector index is visited. The repository now queues
immutable profile IDs and resolves an entry only when needed, skipping removed or
already-requested entries. This hardening is **not yet staged/compiled in the
running `zitter_candidate`**. The 43-test run has finished. The current candidate has now been restaged;
exec `56156` performs GN check, rebuild (`.chromium/build-renderer-pool.log`), then
five targeted startup/dynamic-profile/import/failure/lazy regressions
(`.chromium/test-queue-ids.log`), followed only on success by renderer-pool timing
(`.chromium/compare-renderer-pool.log`). Do not restage/relink during this pipeline. The completed timing data above predates this ID-queue
hardening; final Release measurements must use the final code.

The ID-queue hardening compiled and its five targeted regressions passed
(`.chromium/test-queue-ids.log`). Exec `56156` then completed the renderer-pool
experiment successfully, with the Release build paused only during timing and
resumed afterward.

## Renderer-pool experiment: keep the default

Both configurations used the same binary, 25 tabs, 30 trials and identical
250 ms process-tree RSS sampling. Explicit `MultipleSpareRPHs:count/1` versus
`count/2` produced:

| Debug metric (median / p95, ms) | Count 1 | Count 2 |
| --- | ---: | ---: |
| New-tab dispatch | 70.40 / 72.24 | 70.28 / 72.29 |
| First visual paint | 1158.61 / 2120.71 | 1161.38 / 2113.15 |
| Split dispatch | 37.87 / 41.42 | 38.03 / 41.13 |
| Both split frames | 76.27 / 101.43 | 65.03 / 101.36 |

Raw JSON: `build/performance/20261020T210942930756Z.json` and
`build/performance/20261020T211108966005Z.json`. All 30 new tabs in both runs
consumed a prior spare, but only one spare existed before each command. It was
ready in 4/30 count-1 trials and 5/30 count-2 trials. Thus the count override did
not establish an effective two-ready-renderer pool in this tight-loop workload.
New-tab latency did not improve; the split median variation with an unchanged
tail is insufficient evidence to adopt a renderer setting for loaded splits.
Keep the default count 1. This is not a claim about every idle/burst workload.

Peak current-tree RSS sums were 12017909760 / 12441337856 bytes; peak renderer
count was 29 in both. Shared component-library pages can be counted repeatedly,
so these numbers are not physical RAM or the memory cost of one extra spare.
No extra spare was actually observed before a command, and the default has not
been increased. Strict site isolation already maintains spare creation; repeated
`WarmupSpare` returns without creating another matching spare even when the
existing one is not ready. Unconditional extra warmup calls are not justified.

## Connection warmup experiment

A separate `zitter_preconnect` overlay/`zitter_prototype_preconnect` target keeps
the measured prior candidate intact. The new native adapter debounces address
hints for 200 ms after the shell's existing search debounce, uses the same address
classifier as Enter (including bare domains), skips search and keyword queries,
and passes only an origin to the target profile's LoadingPredictor. It checks
prediction policy before scheduling and again before sending; profiles still
loading are not initialized or queued for speculative work. It revalidates live
text, routing, profile assignment/path, source workspace and visibility, and
cancels pending hints on navigation/switch/Peek/window destruction. Repeated
hints for the last profile/origin are suppressed for 5 s.

The test server uses DelayedStreamSocket below TLS with a 120 ms modeled round
trip placed on reads and writes passed directly to the OS. This preserves the
HTTP/1 server's close-after-response contract (buffered delayed writes could
otherwise be discarded at close). DNS and TCP accept remain local. Both variants
wait exactly 600 ms after typing through the real address field. Each trial uses
a fresh server origin and a warm tab; two warmups are excluded. This is a
controlled TLS/HTTP latency test, not representative internet performance.

The first implementation passed 5/5 tests covering preference disable/recheck,
query/overlay/window cancellation, routed profile cookies and socket reuse,
pending-profile suppression and assignment changes. One initial routed test had
called SearchPalette directly while OpenAddress still had a queued empty search;
that harness error was corrected by using the real TextfieldController path.
Tests now detect speculative page requests and socket non-reuse rather than
merely reporting fast timing.

A 30-trial comparison of the initial fully-qualified-address variant passed:

| Debug metric (median / p95, ms) | No hint | Hint |
| --- | ---: | ---: |
| First visual paint | 350.13 / 371.96 | 241.10 / 250.44 |
| Navigation Timing responseStart | 252.70 / 256.30 | 142.60 / 144.70 |

JSON: `build/performance/20261020T212956137408Z.json` and
`build/performance/20261020T213040606769Z.json`. Every trial accepted exactly one
TCP/TLS connection and received exactly one HTTP request. Before navigation,
control had 0 connections, hint had 1, and neither sent an HTTP request. The hint
moved connection setup earlier and reused it; it did not fetch page content.
An abandoned hint may still leave an unused idle connection until Chromium
cleans it up. Process-tree RSS peaks were 2935832576 / 2948300800 bytes with four
renderers in each; those shared-page RSS differences do not establish physical
RAM cost. Both memory sampling reports were complete.

The current expanded candidate enables the hint by default subject to Chromium's
existing per-profile prediction preference/policy. A native Privacy toggle
`Wstępne połączenia ze stronami` controls that preference and preserves imported
extended state when already enabled. It adds bare-address classification and
live-text/routing revalidation. These further changes require their own checks:
Exec `99321` compiled the expanded candidate but its eight-test run passed 7/8.
The bare-address test exposed a pre-existing address-parser defect: GURL treated
`example.com:8080` as a nonstandard scheme, bypassing Chromium's address fixer.
ResolveTyped now sends dotted candidate schemes through FixupURL, preserving its
HTTP/HTTPS behavior and still rejecting non-web schemes. The test uses a plain
HTTP endpoint matching the resolver's bare-loopback result and asserts the
resolved URL before checking the hint. The main keyword-search regression also
checks dotted host:port and numeric IP:port.

Exec `93362` is the current frozen pipeline: rebuild in
`.chromium/build-preconnect-address.log`, eight functional cases in
`.chromium/test-preconnect-address.log`, matching 2-trial smokes and 30-trial
comparisons in `.chromium/compare-preconnect-address.log`, then the broad main
43-case suite in `.chromium/test-preconnect-integration.log`. Every stage uses
`&&`; no timing or broad test runs after a preceding failure. Do not restage or
relink `zitter_preconnect` during this pipeline.
The corrected eight-case run has now passed **8/8**; its matching timing series
is underway. The final main 43-case suite remains pending/queued in that pipeline.

One additional small **repository-only, not staged** address-parser guard now
requires the dotted-host port fix to have no explicit `://`, and adds a keyword
regression rejecting `custom.scheme://example.com/path`. This preserves rejection
of explicit non-web schemes. After exec `93362` completes, restage/rebuild and run
`KeywordSearchProvidersAndPersistence` plus the bare-address preconnect case on
the final guard before production overlay installation. Do not mutate the active
pipeline's executable to add this guard mid-run.
 The eight cases include a
controlled profile loader (test-only hook) to hold a cold profile unavailable,
late edits before the shell's next search callback, and per-profile preference
isolation. The pipeline runs the broad 43-case main integration suite on this final
candidate before accepting it for the production overlay. No new production app has been
packaged yet.

The runner supports `--scenario preconnect`, opt-in `--preconnect`, and optional
matching `--sample-process-memory`. Connection diagnostics are required and reject
speculative HTTP requests, missing hints, duplicate sockets or differing think
conditions. The Python suite passes 27 tests including these checks. Native
renderer diagnostics and RSS sampling remain available; commands from unrelated
apps are not retained. All timing data above predates the pending final Release
measurements.

The original Release overlay remains unchanged. Release PID 93630 was verified
running again, roughly 32000/58590 targets. The optimized preflight uses a
separate `zitter_optimized` staged overlay and `out/ZitterOptimized` directory,
leaving the running candidate and baseline alone. `chromium/optimized.args.gn`
selects non-component, ThinLTO with optimizations, PGO phase 2, and no DCHECKs.
The pinned mac-arm PGO fetch completed successfully (exec `17373`,
`.chromium/fetch-pgo.log`). The official DEPS hook bucket was used; compressed
transfer 85.9 MiB, resulting profile 319195456 bytes. SHA256:
`cae055c65d96d9c8f3dc6a6a61759634d4b2a0d532c31c173715e59e62a1a74b`.
The exact pinned Clang accepted it with `-fprofile-use` in a compilation probe;
the same probe rejected deliberately invalid data, proving the profile flag was
actually consumed. Compiler version, Chromium revision, command and diagnostics
are retained in `.chromium/pgo-profile.json`. llvm-profdata is absent from this
trimmed compiler package, so the actual compiler reader was checked instead.

GN generation and header/dependency checking passed in exec `27195`
(`.chromium/gen-optimized.log`, `.chromium/check-optimized.log`). Resolved flags
in `.chromium/resolved-optimized.args` confirm: Release, non-component, ThinLTO,
ThinLTO optimizations, lld, PGO phase 2 and DCHECKs disabled. The separate
`zitter_optimized` overlay includes the queue-ID hardening and renderer diagnostics;
it has not been compiled and predates the new connection adapter. Restage it
with the final accepted source before building. This is configuration/input
validation, not evidence of
a faster or working optimized executable. Do not start another huge compilation
alongside the current Release baseline; finish its benchmark/package and preserve
the original first.

Full Python verification now passes **27 tests**; core contracts, boundary and
whitespace checks pass. The completed broad integration suite covered the earlier
candidate binary; keep its result distinct from the pending hardening build and
five-case confirmation in exec `56156`.
Disk inventory: Debug output 23 GiB, incomplete Release output 4.1 GiB, roughly
109 GiB free. No cleanup has been performed.

## Remaining warmup/linking evaluation

Source inspection at the pinned Chromium revision confirms that strict site
isolation already maintains a spare renderer. `MultipleSpareRPHs` is enabled by
default with `count=1`; extra idle replenishment can be tested with the runner's
`--spare-renderers 2`. Do not add unconditional `WarmupSpare` calls or claim a
larger pool helps without matching latency and renderer-memory measurements.
The public spare-manager contract warns that eager warmup can reduce performance.

Connection warmup should use the target profile's `LoadingPredictor` and its
`IsPreconnectEnabled` policy/settings check. `PreconnectURLIfAllowed` checks that
policy before socket setup. Omnibox-style top-level requests need the appropriate
same-site network anonymization key and network-restrictions token; page-link
hints use the source frame's isolation context instead. Routing to another Zitter
profile must resolve before any speculative network operation. The local HTTP
probe does not establish a DNS/TLS benefit; representative connection tests and
socket/DNS overhead accounting remain to be done. The connection candidate and its current verification state are described above;
final production acceptance remains pending.

The pinned compiler config supports ThinLTO with lld on Apple platforms, and
PGO phase 2 using the pinned `chrome/build/mac-arm.pgo.txt` profile via
`tools/update_pgo_profiles.py --target mac-arm`. The current component Release
baseline intentionally has neither enabled. Non-component/ThinLTO/PGO generation,
build correctness, comparable latency and package-size/startup measurements
remain required before choosing a production configuration. Source support alone
is not a measured performance result.

## Next stages

Goal activated on 2026-10-20 for the complete optimization list. The component
Release baseline is currently building from the unchanged installed overlay in
`.chromium/src/zitter`. Do not reinstall the repository overlay until that build,
its baseline benchmark and packaging have finished. The following changes make
up the first measured UI candidate described above:

- Preserve native tab rows when only title/URL metadata changes.
- Coalesce navigation/title callbacks into one queued shell refresh; explicit
  user commands still refresh synchronously.
- Keep snapshot capture synchronous, batch Time Machine writes for up to 350 ms,
  and serialize session/history JSON on the sequenced background writer. Flush a
  pending history batch on window destruction and include it in save completion.
- Apply pane layout before showing hidden WebContents and avoid redundant
  visibility/layout operations.

New regression tests `SidebarMetadataUpdatesPreserveRows` and
`TimeMachineFlushesPendingHistoryOnClose` passed on the isolated Debug candidate.
The first functional run passed 14 of 15 selected tests, covering split
lifecycle/session restoration, Time Machine, beforeunload batches, storage
isolation and the native shell. `KeyboardWorkspaceSwitching` failed on its final
keyup assertion; the unchanged Debug baseline failed identically in isolation.
An attempted test-only wait for native release plus renderer ACK did not resolve
it (on macOS the native helper already waits for release); that attempt was
reverted. This is a reproducible baseline issue requiring separate keyboard
investigation, not evidence of a performance-candidate regression.
`NativeKeyboardWorkspaceSwitching` passed separately.

The candidate is installed in `.chromium/src/zitter_candidate` by
`tools/stage_performance_candidate.py`, using Debug's `root_extra_deps` to reuse
Chromium dependencies without touching the baseline overlay. The selected test
log is `.chromium/test-candidate-ui.log`; isolated keyboard comparison logs are
`.chromium/test-{baseline,candidate}-keyboard-isolated.log`.
The build/benchmark/package pipeline from stage 1 is exec session `98019`; logs
are `.chromium/build-release.log` and `.chromium/build-release-bindings.log`.
Its successful completion creates `build/ZitterRelease.app` and another timing
JSON under `build/performance/`. Preserve that baseline before rebuilding it.
The Release ninja process (PID 93630) was resumed after the candidate run.
If stopping it for subsequent measurements, verify its command/identity before
signaling and resume it afterward even if the benchmark fails.

For lazy restoration, `views::WebView::web_contents()` can inspect ownership
without creating a WebContents (unlike `GetWebContents()`). A safe follow-up must
keep restored URL/title metadata, skip unrealized pages in beforeunload/capture
enumeration, and attach profile services before first showing/navigating a page.
Profile prioritization must retain the controller's activation failure contract.

1. Compare Debug and component Release, including the two first-paint groups.
2. Update sidebar rows in place and coalesce redundant UI updates; measure again.
3. Keep pre-change snapshot capture correct while moving serialization away from
   the UI thread and coalescing persistence; measure again.
4. Settle split bounds before exposing pages; check renderer frames and behavior.
5. Lazily create inactive pages and prioritize the active profile on startup.
6. Investigate renderer warmup, connection setup and non-component linking based
   on traces, with memory and isolation behavior included in the assessment.

Reproduction commands and metric definitions are in the README's performance
baseline section. Each stage should retain raw samples and report median and
p95; startup and real-site network tests remain separate experiments.

## Current long-build checkpoint

The original component Release pipeline (exec `98019`, Ninja PID 93630) completed
successfully: 58,590 build steps, the 30-trial baseline and
`build/ZitterRelease.app` packaging. Its measurement had no other native test or
build workload in progress. The original overlay's ownership hashes have remained
unchanged, and it stays preserved for reference. The optimized
non-component/ThinLTO/PGO overlay has now been refreshed from current source,
including connection changes and the normal-app probe. The optimized bindings
generation completed (18/18 actions), and production compilation is running as
Ninja PID 91593 (`out/ZitterOptimized -j 8 zitter_optimized`, exec pipeline `14369`,
log `.chromium/build-optimized-final.log`). The final selected bundle remains
pending compilation, measurements, resource comparison and functional checks.
The generated `obj/zitter_optimized/shell.ninja` compile flags were inspected:
they contain `-flto=thin` and `-fprofile-use` pointing to the pinned mac-arm
`.profdata` file. This verifies the actual shell compilation configuration in
addition to the GN argument preflight; it is not proof of a latency benefit.

## Regular application verification checkpoint (2026-10-21)

The final address-classifier guard passed both targeted native tests
(`KeywordSearchProvidersAndPersistence` and
`BareDomainUsesAddressClassifierWithoutWarmingSearchQueries`), recorded in
`.chromium/test-address-final.log`. The broad preconnect integration run finished
**42/43**, not 43/43: `ConfigurableNativeKeyboardShortcuts` failed its immediate
sidebar-toggle assertion after Cmd+S in the tools window. This is separate from
the previously reproduced baseline keyup issue. An isolated unchanged-baseline
rerun and the corresponding candidate rerun both passed. Do not dismiss
the integration failure or remove its assertion without evidence.

`AppPerformanceProbe` adds opt-in measurements to the regular executable, using
public content APIs and the packaged application's normal renderer helpers.
`tools/benchmark_app.py` runs marker-owned bundles against a local HTTP fixture
with private profile directories. Navigation measures dispatch, first visually
nonempty paint and split visual-state acknowledgements; startup measures fresh
processes/profiles restoring tabs in two profiles, with the last profile active.
Startup timestamps are observed at stderr receipt, and OS caches are warm.
Visual-state acknowledgement is not a physical screen presentation timestamp.

`build/ZitterAppReference.app` contains the unchanged product plus the measurement
probe; `build/ZitterAppCandidate.app` contains the current candidate and the same
probe. Both were built and packaged successfully. Both navigation and startup
smokes passed (two measured trials each, four tabs per profile for startup),
with normal clean shutdown and successful completion markers. These short runs
validate the tool and do not establish an improvement. The full 30-trial/25-tab
navigation comparison completed in `.chromium/compare-regular-navigation.log`.
The staging tool preserves the original baseline
overlay and validates its ownership hashes. Fixture, process-marker and failure
handling tests bring the Python suite to 31 passing tests.

| Regular Debug app metric | Original median / p95 | Current median / p95 |
| --- | ---: | ---: |
| New-tab dispatch | 103.59 / 105.82 ms | 70.51 / 72.95 ms |
| New-tab first visual paint | 1069.46 / 1872.68 ms | 1073.10 / 1948.75 ms |
| Split dispatch | 45.75 / 48.38 ms | 38.60 / 41.66 ms |
| Split visual-state acknowledgement | 52.29 / 103.16 ms | 49.02 / 80.56 ms |

Raw normal-app results are `build/performance/20261020T221801823777Z.json`
(original) and `build/performance/20261020T221923548691Z.json` (candidate).
Compilation was paused and compiler children quiesced for both. Dispatch improved
by approximately 32% for new tabs and 16% for split. Split acknowledgement p95
improved by approximately 22%, but its median changed by only 6%. New-tab first
paint did not improve; the two latency groups persist outside the browser-test
runner. Do not present these results as faster network loading.

The shortcut test now explicitly focuses its tools window before sending Cmd+S
and waits for the expected sidebar state, matching its existing workspace-switch
wait. This retains the behavioral assertion and establishes native focus as a
precondition rather than treating widget visibility as focus. Three isolated
repetitions passed in `.chromium/test-shortcut-focus.log` (exec `13616`, exit 0);
this test-only adjustment does not establish the root
cause of the earlier integration flake.

### Normal component Release navigation results

Both regular Release bundles completed the same 30 measured trials, 25 loaded
tabs and two warmups with compilation finished. The original and candidate use
the same compiler configuration and probe; normal renderer helper command lines
were observed under the Release reference bundle, rather than the Debug output.

| Regular component Release metric | Original median / p95 | Current median / p95 |
| --- | ---: | ---: |
| New-tab dispatch | 19.66 / 24.40 ms | 16.27 / 20.90 ms |
| New-tab first visual paint | 802.49 / 1576.68 ms | 794.05 / 1619.36 ms |
| Split dispatch | 5.86 / 8.08 ms | 5.16 / 6.96 ms |
| Split visual-state acknowledgement | 14.09 / 16.85 ms | 13.66 / 15.36 ms |

Raw results: `build/performance/20261020T223806964564Z.json` (original),
`build/performance/20261020T223906507479Z.json` (candidate). UI dispatch gains
persist after compiler optimization (about 17% new tab, 12% split); neither
first-paint median nor tail establishes a new-tab loading improvement. Comparing
the complete normal Debug original with the normal Release candidate, dispatch
falls from 103.59 to 16.27 ms and from 45.75 to 5.16 ms respectively. That combined
comparison includes both build and product changes. Fresh-process restoration
measurements completed before the optimized build began:

| Fresh-process component Release startup, 25 restored tabs per profile | Original median / p95 | Current median / p95 |
| --- | ---: | ---: |
| Window ready from process launch | 3374.58 / 3442.50 ms | 3191.50 / 3251.87 ms |
| First visual paint from process launch | 5614.98 / 5731.23 ms | 5509.22 / 5684.76 ms |

Raw results: `build/performance/20261020T224000198872Z.json` (original) and
`build/performance/20261020T224311694203Z.json` (candidate). Each variant uses
30 fresh processes/profile directories, two excluded process warmups and warm
OS caches. Window-ready median improved by about 5.4%, first-paint median by
about 1.9%. These normal-app, two-profile results supersede any attempt to
interpret the earlier warm-process/four-profile 65% ready-time reduction as a
cold application startup improvement. Each experiment retains its stated scope.

All 72 completed normal-app startup fixtures (including the earlier small Debug
smokes and both Release series) were checked after normal shutdown: both saved
workspaces retain the expected tab counts and profile IDs, with the last profile
active (`.chromium/verify-app-workloads.log`). The driver now checks this after
every startup trial, outside the timed interval. Tests reject lost tabs, wrong
profile assignment and a changed active workspace. The Python suite has 32 tests.

The refreshed optimized overlay now includes the current product, preconnect and
the normal-app probe. A production-target dry run exposed the trimmed checkout's
missing V8 builtins PGO profiles, in addition to the previously fetched Clang
profile. The pinned V8 downloader fetched version 15.8.6.0 with
`--check-v8-revision`; the initial invocation failed because depot_tools was not
on PATH, and the corrected invocation is logged in
`.chromium/download-v8-profiles.log`. Revision validation succeeded against
`729c273828465fe1dc5e29135b4972c14e8d0efe`; GN generation, header checks and the
complete 57,728-edge production dry run then passed. This is a dependency plan,
not a successful compilation. The optimized pipeline is queued in exec `14369`
(`.chromium/run-optimized-after-baseline.py`, log `.chromium/optimized-pipeline.log`).
It waits for original pipeline 93297 to finish and requires its new successful
timing JSON and marker-owned Release package before starting. It then builds and
packages the original-plus-probe and current regular applications using the same
component Release configuration, followed by full 30-trial/25-tab navigation and
fresh-process restoration comparisons. Afterward it generates optimized bindings
serially, builds the frozen optimized application with eight workers, packages it
as `build/ZitterOptimized.app`, runs normal-app smokes, and collects matching full
navigation/startup measurements. No compilation runs during these timing phases.
The results require review before adopting a final bundle. Preserve staged source
variants while their builds run; do not change the original baseline overlay.

Resource and final functional verification are queued separately, in sequence:

- Exec `86520`, `.chromium/measure-final-app-resources.py`, waits for the live
  optimized pipeline PID 83161 and its successful completion marker, then runs
  matching 10-trial/25-tab normal-app navigation experiments for component
  Release and ThinLTO/PGO with the existing 250 ms process-tree RSS sampler.
  This includes the Python benchmark driver and browser descendants, double
  counts shared mappings, and must not be reported as physical RAM. It also
  records executable hashes and unique regular-file bytes per development
  bundle (deduplicating inodes), not download/installer size. Log:
  `.chromium/final-app-resources.log`.
- Exec `83675`, `.chromium/verify-optimized-native.py`, waits for resource
  pipeline PID 90399 and its completion marker, then builds the optimized native
  regression target and runs all Zitter functional suites, including media,
  uploads and preconnect. It excludes explicit performance/manual/live-import
  cases and only the independently reproduced original
  `KeyboardWorkspaceSwitching` keyup failure. The separate native keyboard
  switching test remains included. Logs:
  `.chromium/{build-optimized-native-tests,test-optimized-native}.log` and
  `.chromium/optimized-native-pipeline.log`.

Do not run another build or GUI test during the timing/resource phases. These
queued jobs are not evidence that compilation, measurements or tests have passed;
inspect their live process/handles and eventual exit statuses before adoption.

## Final static build and measurement correction (2026-10-21)

The first non-component build completed all 57,710 edges. Its startup smoke
then exposed a redundant `EarlyMallocZoneRegistration()` in our `main()`
(macOS report `Chromium-2026-10-21-032037.ips`). Our executable links Chromium
directly: PartitionAlloc's constructor has already installed the zone before
main, whereas Chrome's small launcher registers a delegating zone before
loading its framework. The redundant call and launcher-only dependency were
removed. Normal startup subsequently passed; allocator checks were preserved.

Generated link flags also exposed `--lto-O0` on our executable. ThinLTO requires
an explicit target-level opt-in in addition to the GN argument. Our executable
now replaces `thinlto_optimize_default` with `thinlto_optimize_max`, matching
upstream browser targets. Its generated flags now contain `--lto-O2`; the
Chromium framework already used the optimized config. Pinned PGO profiles and
compile flags remain unchanged. Only our object/link edges needed rebuilding.

The normal-app smoke also exposed environmental occlusion: the expected page
committed HTTP 200 and its title, with a live renderer and a showing 1008×774
view, but remained OCCLUDED and never produced its first paint. This also
reproduced in both component Release controls, including the original product.
Failure-only stderr diagnostics now retain phase/trial, page load/paint status,
visibility, renderer liveness, dimensions and widget activation state.

Public/private AppKit activation experiments and direct LaunchServices launches
sometimes passed but did not reliably fix occlusion. Those changes were removed
from application code, staging and the driver. No private API remains. A brief
diagnostic standalone window was closed; subsequent tests use private fixtures.

The final driver uses Chromium's own browser-test control,
`--disable-backgrounding-occluded-windows`, identically for all regular-app
variants. Pinned `content/public/test/browser_test_base.cc` uses this switch
because test window placement is unpredictable. This measures foreground-style
rendering without OS occlusion throttling; it is not a measurement of the
physical display, Finder interaction or background-window power behavior.
Real navigation, HTTP status, first visually nonempty paint and both renderer
visual-state acknowledgements are still required. Ordinary app launches retain
normal occlusion behavior. All accepted final comparisons will be repeated with
this control; older direct-exec observations above remain historical.

The driver retains direct child-process ownership and actual exit status, and
the RSS sampler retains its existing validated descendant-only ownership. The
experimental LaunchServices PID registry was removed. 32 Python tests passed.
A temporary display-idle assertion lasts only for the acceptance pipeline.

Current sequential runner: `.chromium/resume-optimized-verification.py`, log
`.chromium/optimized-resume-pipeline.log`. It packages, checks the original
control and both optimized smokes, runs full optimized 30-trial navigation and
fresh-process restoration, then invokes
`.chromium/refresh-release-activation.py` to repeat both component controls with
the same rendering switch. Only after timing ends does it sample resources and
build/run the optimized functional suites. Native compilation does not overlap
accepted timing runs. Final adoption remains pending these results.
