# Browser performance

All original optimization items have been evaluated. The selected application is
`build/Zitter.app`: non-component Release, ThinLTO O2 and pinned Chromium PGO.
It passed 63 optimized native tests and both final packaged-app smoke scenarios.
The measured comparison copy remains `build/ZitterOptimized.app`.

## Original checklist and effort

Effort is a subjective 1–5 scale: 1 is a small local change; 5 would be engine-scale work.
UI-group measurements do not isolate the contribution of each individual change.

| Original item | Decision and evidence | Cost / effort |
| --- | --- | --- |
| Separate Release and measure a baseline | Preserved original source and app; compared original/current component Release and current non-component app with the same driver | Separate outputs/cache; 1 |
| Update individual tab rows | Retain rows for title/URL changes; rebuild only for structural changes. UI group reduces component Release new-tab dispatch 17.23 → 14.29 ms | No per-change attribution; 2 |
| Reduce snapshot/serialization work on UI | Keep pre-change capture synchronous; batch persistence and serialize/write in the background; flush on close | Up to 350 ms batching; close-flush regression covered; 2 |
| Coalesce UI updates | One weak posted refresh; explicit commands remain synchronous | Task/lifetime management; measured with UI group; 2 |
| Set pane bounds before showing pages | Retained, with idempotent pane/show setters. Component split acknowledgement median 16.30 → 10.98 ms in UI group | Already-loaded second page; not network loading; 2 |
| Lazy inactive tabs | Warm-profile restoration: realized WebContents 50 → 2; first paints 6993 → 2694 ms; browser main-process physical footprint 178.14 → 140.42 MiB | Loads dormant pages when shown; footprint is not whole-browser RAM; 3 |
| Prioritize active profile | Four fresh profiles in a warm test process: ready 4448 → 1545 ms, restored first page 6721 → 4646 ms. Normal cold-process gains are reported separately below | Other profiles load later; total profile batch can be slightly slower; 3 |
| Warm renderers | Keep Chromium's default one spare. Two-spare experiment gave no new-tab gain; no second spare was actually observed under Chromium's idle conditions | Avoid unproven extra processes; 2 |
| Warm connections | Retain a debounced, per-profile privacy-controlled origin preconnect. Modeled TLS/120 ms RTT first paint 354 → 242 ms; exactly one reused connection, zero speculative HTTP GETs | Abandoned input may leave an unused connection; preference/policy/OTR checks and deduplication; 3 |
| Non-component linking, ThinLTO, PGO | Normal-app new-tab paint p95 1569.51 → 39.57 ms versus current component Release; startup first-paint median 5296 → 2979 ms | Long first build/link, cached upstream profiles; 63 optimized functional tests passed; 4 |

The assembler suggestion was excluded. Destination-seeded SiteInstance/early
navigation and the wait-for-all-profile variant were rejected after comparable
measurements failed to establish a gain. No renderer count was raised merely
because a larger number seemed faster.

## Matched normal-app results

macOS ARM64, 12 cores, 36 GiB RAM. Each column uses the regular packaged app and
normal Chromium renderer helpers, 30 measured trials and two excluded warmups.
Navigation uses 25 loaded local tabs. Startup uses a fresh process/private data
directory per trial, two profiles with 25 saved tabs each, and selects the last
profile. OS caches remain warm; this is not a cold-machine launch.

All three variants use the same `AppPerformanceProbe`, driver, local HTTP page
and `--disable-backgrounding-occluded-windows`. Chromium's own browser-test base
uses this switch to avoid unpredictable window placement suppressing rendering.
It controls foreground-style rendering; ordinary app launches keep normal
occlusion/power behavior. First visually nonempty paint and renderer visual-state
acknowledgements are proxies for readiness, not physical monitor presentation.
Startup timestamps are measured from process launch through stderr receipt.

Values are **median / nearest-rank p95**, milliseconds.

| Metric | Original component Release | Current component Release | Current non-component ThinLTO/PGO |
| --- | ---: | ---: | ---: |
| New-tab synchronous dispatch | 17.23 / 20.61 | 14.29 / 16.50 | 7.89 / 8.29 |
| New tab through first visually nonempty paint | 793.78 / 1560.73 | 802.52 / 1569.51 | 38.70 / 39.57 |
| Split synchronous dispatch | 5.67 / 5.93 | 5.18 / 6.94 | 2.58 / 3.41 |
| Split through both visual-state acknowledgements | 16.30 / 16.45 | 10.98 / 15.91 | 8.19 / 9.30 |
| Fresh-process ready | 3277.63 / 3298.56 | 3105.54 / 3124.97 | 2905.71 / 3059.65 |
| Fresh-process first active-page paint | 5973.44 / 6032.93 | 5296.07 / 5339.14 | 2979.24 / 3141.32 |

The component new-tab medians sit between two groups: the original has 15 trials
at a 68.30 ms median and 15 at 1527.37 ms; the current component build has 15 at
60.60 ms and 15 at 1544.54 ms (descriptive split at 500 ms). All 30 optimized
trials were below 500 ms, with a 38.70 ms median. This establishes the observed
removal of that slow path in this workload, not its underlying cause.

Optimized startup also has two groups: 10 trials below 500 ms (457.40 ms median),
20 slower (3001.77 ms median). The overall median is not a uniformly repeatable
launch time. The final compiler experiment changes linking, LTO and PGO together;
it does not establish their individual contributions. These local fixtures do
not establish that Zitter is faster than every other browser or every website.

Raw JSON and adjacent logs, under ignored `build/performance/`:

| Variant | Navigation | Startup |
| --- | --- | --- |
| Original component | `20261021T015526914787Z.json` | `20261021T015719381754Z.json` |
| Current component | `20261021T015626819964Z.json` | `20261021T020041230921Z.json` |
| Current optimized | `20261021T015349398130Z.json` | `20261021T015353712756Z.json` |

Each JSON retains raw samples, executable SHA-256, GN arguments, workload and
rendering control. Earlier direct-exec/Debug/browser-test results and rejected
experiments are preserved in [the experiment log](performance-experiments.md);
they must not be mixed into this matched before/after comparison.

## Memory, network and build costs

A separate matching 10-trial/25-loaded-tab normal-app navigation run samples
`ps` every 250 ms. It includes the Python driver and its browser descendants;
it excludes unrelated processes. RSS sums double-count shared mappings and are
**not physical RAM or private footprint**. Sampling can miss brief peaks; the
faster run lasts 9.21 s versus 35.52 s, so whole-run median RSS is not a useful
steady-state comparison. These sampled timing results are not mixed with the
unsampled table above.

| Observation | Current component Release | Current optimized |
| --- | ---: | ---: |
| Sampled peak sum of RSS | 8.34 GiB | 5.64 GiB |
| Peak observed renderer count | 43 | 42 |
| Samples | 119 | 31 |
| Bundle regular-file bytes, unique per inode | 1,169,196,928 | 1,106,646,834 |

Closed tabs can leave renderer processes briefly alive; renderer counts exceed
the 25 loaded-tab target in both runs. Bundle bytes exclude symlink aliases,
but are development-bundle contents, not installer/download size or additional
filesystem allocation (packaging uses hardlinks where possible).
Resource evidence: `20261021T020342724062Z-app-resources.json`.

The lazy-restoration main-process footprint measurement above is a separate,
settled warm-profile test and must not be presented as the process-tree RSS
saving. Connection prewarming is also a separate controlled experiment: local
DNS/TCP accept, modeled 120 ms RTT, TLS, fresh origin and a 600 ms typing pause.
It proves connection reuse in that fixture; not universal Internet benefit.
Bare-host input may hint HTTP before HSTS/HTTPS upgrade, leaving an unused hint.
Searches, credentials in URLs, invalid/unsupported schemes and pending profiles
are skipped. Latest field text, workspace, stable profile identity/path and
preferences are revalidated before network activity; navigation cancels hints.

The first complete Chromium build has 57,710 edges and takes hours on this
machine. LTO uses substantial link memory and keeps Chromium's default heavy-link
concurrency limit. The upstream pinned Clang profile is 319,195,456 bytes,
SHA-256 `cae055c65d96d9c8f3dc6a6a61759634d4b2a0d532c31c173715e59e62a1a74b`.
V8 builtins profiles were fetched with revision verification. Profiles and build
outputs are cached. PGO is Chromium's upstream mac-arm profile, not a locally
trained Zitter workload profile.

## Correctness and final acceptance

Verification completed:

- 33 Python tooling tests; core CMake contracts; dependency-boundary check;
  whitespace check; preserved-original overlay manifest check.
- Lazy lifecycle/profile integration suites, preconnect's eight functional tests,
  upload/media coverage in development builds, and targeted address-classifier
  regressions. The final optimized native run passed **63/63**, all with
  status SUCCESS and no retries; the test executable used LTO O2 as well.
- Configurable shortcut test retains its assertion, but explicitly focuses the
  tools window and waits for its asynchronous sidebar toggle; three repeats
  passed. No production shortcut behavior was changed to satisfy the test.
- The original `KeyboardWorkspaceSwitching` test's final plain key-up failure was
  reproduced in both original and candidate. It remains excluded from the final
  suite. `NativeKeyboardWorkspaceSwitching` remains included. Interactive manual
  tests, live Arc import and performance scenarios are excluded from the final
  functional filter.

The static-launch smoke exposed redundant early allocator-zone registration:
our executable links Chromium directly, so PartitionAlloc's constructor has
already initialized it before main. The launcher-only registration/dependency
was removed; normal-app startup subsequently passed. LTO also requires a
per-target opt-in: both the regular executable and native test target now replace
`thinlto_optimize_default` with `thinlto_optimize_max`. Generated link flags were
checked for `--lto-O2`; actual compile flags include `-flto=thin` and the pinned
`-fprofile-use` file. No allocator validation was disabled.

The transient occlusion problem reproduced in the frozen original app too.
Failure-only diagnostics retain live renderer, page visibility, load/paint state,
view bounds and widget state. AppKit/private activation and LaunchServices
experiments were removed after proving unreliable. The final driver retains
normal direct-child ownership and real exit status. Only the documented Chromium
test rendering switch controls OS occlusion during measurements.

Current sequential runner: `.chromium/resume-optimized-verification.py`, log
`.chromium/optimized-resume-pipeline.log`. All accepted timing/resource runs ended
before the optimized functional build began. Logs for the final native build and
suite are `.chromium/build-optimized-native-tests.log` and
`.chromium/test-optimized-native.log`; summary is
`.chromium/test-optimized-native-summary.json`.

Final delivery is `build/Zitter.app`, registered with LaunchServices. Its executable
SHA-256 is `19dfbf0490f172792426d2737465a936e72fda755ef935e71c9553b179b06518`,
identical to the full measured optimized executable. Ninja reported no remaining
work. Final package navigation/split and fresh-process startup smoke scenarios
passed (two measured trials each, four-tab fixtures):
`20261021T022637434284Z.json` and `20261021T022741934229Z.json`.

Repackaging an existing Debug bundle exposed stale component dylibs in generated
`Contents/MacOS` and `Contents/Frameworks`. The packager now unlinks only stale
root dylibs absent from the selected build output; current dependencies and
unrelated files are preserved. A regression test verifies output switching and
that original build hardlinks remain intact. The final bundle now has exactly
1,106,646,834 unique-inode regular-file bytes, matching the measured clean bundle.

The initial desktop inspection was blocked by the locked Mac. After the user
unlocked it, an additional normal-app visual smoke passed without benchmark or
occlusion-control switches, on a separate private profile fixture. Accessibility
state and screenshots verified the restored page, loading a previously dormant
saved tab, and both pages of a split opened through the UI. The extra split tab
was closed and the application exited through its menu with code 0. Both
profiles retained their two saved tabs and all original fixture URLs.
Evidence: `20261021T065319135547Z-final-visual.json` and adjacent log under
`build/performance/`. The executable hash still matches the measured build.

Build and measurement commands are in the
[README](../README.md#optimized-local-build-macos-arm64).
