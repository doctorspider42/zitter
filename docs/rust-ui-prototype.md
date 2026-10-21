# Rust UI / GPU prototype

This is an opt-in macOS ARM64 experiment, separate from the production workspace
window. It compares three implementations of the same opaque rectangle sidebar
beside a real, visible Chromium WebContents:

| Backend | State/layout | Sidebar rendering |
| --- | --- | --- |
| `views` | C++ reference | Retained Views children with solid backgrounds |
| `rust-skia` | Rust, through CXX | One custom-painted View, gfx::Canvas and Chromium's compositor |
| `rust-metal` | The same Rust scene | Instanced Metal draw into a CAMetalLayer in a native NSView |

The Rust scene owns layout, visible-row culling, selection styling and hit testing.
The Metal implementation is a small Objective-C++ platform adapter, not a Rust
Metal renderer library. Its shader is compiled into an embedded metallib at build
time, avoiding a runtime shader-source compiler. Building requires Xcode's Metal
toolchain (`xcrun metal` and `xcrun metallib`). Both candidates still use a minimal Views Widget and
WebView as the window/page transport. This first experiment does **not** establish
that Views can yet be removed from the complete application.

For Metal, macOS composites sibling native page and sidebar surfaces. Chromium
keeps its existing renderer/GPU-process path. There is no screenshot transport,
page pixel readback, CPU page bitmap or upload of web content into Metal. This
is not a single shared GPU command queue or an implemented import of Chromium
textures into another renderer. The only uploaded data is the small Rust UI
rectangle list. GPUI and CEF are not dependencies.

## Reproduce

Stage an isolated candidate without replacing the normal overlay:

```sh
python3 tools/stage_performance_candidate.py --variant zitter_ui_lab
```

Append `"//zitter_ui_lab:zitter_ui_lab"` to `root_extra_deps` in the existing
output directory's `args.gn`, keeping its other entries and compiler flags.
For decision measurements use the existing optimized output, not Debug:

```sh
cd .chromium/src
../depot_tools/autoninja -C out/ZitterOptimized -j 6 zitter_ui_lab
cd ../..
python3 tools/package_app.py --out .chromium/src/out/ZitterOptimized \
  --binary zitter_ui_lab --destination build/ZitterUiLab.app --no-register
python3 tools/benchmark_ui.py --app build/ZitterUiLab.app \
  --iterations 120 --rounds 6 --scenario both
```

Use `--interactive rust-metal` (or either other backend) to inspect the prototype.
Click a row to change its selection; the rows are schematic placeholders, not
actual browser tabs. Close the lab window when finished. The page is the local
deterministic `testdata/performance_probe.html`, with no external requests or
animations. Ordinary page mouse input, selection and scrolling use Chromium.

The harness creates a new profile for each process under its result directory.
A marker is required by the app before the lab can start. Never point the lab at
the normal Zitter profile. No sandbox-disabling flags are used. The normal app
starts its existing UI unless `--zitter-ui-lab` is explicitly supplied.

## Measurement contract

Each process checks C++/Rust scene parity at multiple sizes and selections, loads
the page, waits for its renderer visual-state callback, settles for three seconds, and
discards ten warmup updates. One measured update is in flight at a time, followed
by deterministic variable delays to vary its phase against vsync.

Metal primes its swapchain before settling. macOS can skip the first drawable
after the surface resumes; up to ten such skips may be retried during warmup,
without advancing the ten **successfully presented** warmup updates. Their count
is retained per process. Any missing presentation in the measured phase fails the
run; no measured failures are silently removed. Metal uses layer-hosting NSViews,
explicit CoreAnimation transactions and scheduled command buffers to coordinate
its native surface with the window. Every backend explicitly raises its window
before loading the fixture.

`selection` moves the selected row. `resize` also changes sidebar width and hence
the real page's viewport. All backends render the same primitive geometry and
colors. This is a comparison of retained widgets and two custom scene approaches,
not a language-only experiment: Rust/Skia changes the widget hierarchy as well.
Resize timing tracks UI presentation, not completion of the page's reflow/repaint.

Metrics:

- `scene_ms`: scene generation, including the Rust/C++ boundary for Rust modes.
- `submit_ms`: synchronous update/layout and submission, including scene creation.
  For Views this does not include deferred painting/raster work.
- `update_to_present_ms`: update to successful presentation timestamp. Views uses
  Chromium presentation feedback; Metal uses `MTLDrawable.presentedTime` and
  `CACurrentMediaTime`. These are different platform reporting paths, not a
  calibrated input-to-photon measurement. Chromium feedback flags are retained.
- `metal_gpu_ms`: Metal command-buffer GPU execution only. There is no equivalent
  Chromium GPU timing in this harness, so this is **not** a cross-backend metric.
- Browser process CPU and physical footprint during the measured phase, then
  browser CPU during two seconds with no UI update loop. These exclude renderer,
  GPU and WindowServer costs. Metal submits from the browser, whereas Chromium
  can perform work in its GPU process; browser CPU alone cannot rank total cost.

All modes run from one binary with the same compiler/profile/settings. The C++
reference uses a native `std::vector`, avoiding per-primitive Rust FFI
overhead in the baseline. Both Skia sidebars retain a native copy of the display
list for deferred painting. Six rounds
use all backend permutations to balance run order. Logs, profiles, raw samples,
binary hash, GN flags, run medians, pooled median and p95 remain in
`build/ui-performance/<UTC timestamp>/`. A partial result is retained on failure.
Trials in one process are correlated; repeat-process medians matter more than a
large pooled sample count. Do not benchmark during compilation or UI inspection.

This skeletal scene omits text shaping, editable fields/IME, accessibility,
drag-and-drop, rounded clipping, overlays/Peek and actual workspace/tab services.
It also does not measure total GPU/system memory, power or startup. No result
from it alone establishes the fastest complete browser UI. The next gate is the
same comparison with real tab labels, scrolling, an editable address palette,
overlays over WebContents, and complete process-tree/GPU/energy accounting.

## Validation

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
python3 tools/check_boundaries.py
cd .chromium/src
buildtools/mac/gn check out/ZitterOptimized //zitter_ui_lab:zitter_ui_lab
../depot_tools/autoninja -C out/Zitter -j 8 ui_lab_scene_unittests
out/Zitter/ui_lab_scene_unittests
```

The result parser rejects missing/duplicate samples, failed presentation feedback,
non-finite timings, invalid GPU timings, mismatched backends, failed scene/page
readiness, missing resources and missing idle measurements.

## Measurements and decision

Measured on **21 October 2026**, Apple M3 Pro, 36 GiB RAM, macOS 27.0.1,
ARM64 non-component Release with ThinLTO/PGO. All three backends used the same
binary, fresh isolated profiles, six process repetitions per scenario/backend and
120 measured updates per process: **36 processes / 4,320 valid samples**.

[Raw samples, per-process resources, configuration and binary hash](benchmarks/rust-ui-2026-10-21.json).
The successful archive is the complete series; earlier incomplete smoke runs are
excluded. Source commit: `fb2701f2b936a1996af056d27502e9ab0e19c3ef`. Binary SHA-256:
`b87466cb271b115029becc3b08d04f49e611cfe32465fce50d5a2043d346b464`.

All times below are milliseconds. Presentation p50/p95 pool 720 samples per row;
CPU/update and footprint are medians of the six process-level measurements.

| Scenario | Backend | Submit p50 | Present p50 | Present p95 | Browser CPU/update | Browser footprint MiB |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| selection | views | 0.126 | 17.79 | 26.93 | 4.27 | 65.7 |
| selection | rust-skia | 0.102 | 18.91 | 26.96 | 4.05 | 65.5 |
| selection | rust-metal | 0.715 | 20.72 | 24.75 | 5.30 | 184.5 |
| resize | views | 0.460 | 18.57 | 24.91 | 4.59 | 64.8 |
| resize | rust-skia | 0.257 | 17.86 | 23.85 | 4.19 | 67.8 |
| resize | rust-metal | 1.963 | 21.49 | 26.17 | 9.64 | 186.6 |

The range of the six independent process medians is:

| Backend | Selection presentation, ms | Resize presentation, ms |
| --- | ---: | ---: |
| views | 15.85–18.69 | 16.85–19.55 |
| rust-skia | 18.44–19.08 | 17.31–18.70 |
| rust-metal | 20.67–20.74 | 21.16–21.65 |

**Recommendation: carry Rust/Skia forward as the next UI prototype, retain Views
as the control, and keep direct Metal experimental.** The Rust scene already uses
the current Chromium fork and its compositing path; the small Views window/page
host remains. This is a choice of the next implementation direction, not evidence
that a complete Rust browser UI will outperform the existing one.

Observed synchronous update cost fell from 0.126 to 0.102 ms for selection
(about 19%), and from 0.460 to 0.257 ms for sidebar resizing (about 44%). Selection
presentation favored Views at the median (17.79 vs 18.91 ms), with almost identical
p95 values. Resizing favored Rust/Skia (17.86 vs 18.57 ms median, 23.85 vs 24.91 ms
p95). Per-process presentation ranges overlap, so there is no broad latency win
over Views. Rust scene generation itself took roughly 3–5 microseconds and was
slightly more expensive than the native C++ reference in this scene.

This Metal adapter had higher median presentation latency, roughly 7–8 times the
synchronous submission cost of Rust/Skia, and about 185–187 MiB browser footprint
against 65–68 MiB for the other paths. Its selection p95 was better than both
Skia paths (24.75 ms), so it did not lose on every metric. The adapter explicitly
waits for Metal command scheduling and presents through a CoreAnimation
transaction; its submission cost includes that wait. These measurements do not
establish a lower bound for an asynchronous Metal adapter, GPUI or all custom
GPU renderers. A future asynchronous adapter must preserve the same presentation
validation before its numbers can be compared.

All 4,320 measured updates had valid presentation timestamps. Metal retried 14
skipped **warmup** drawables across 12 processes (11 processes skipped one, one
skipped three). No failed measured update was removed. The embedded shader was
compiled at build time; the app did not invoke a shader-source compiler.

The two-second idle browser-CPU windows varied substantially (about 1.3%–50.1%
across processes), include other Chromium background work, and are too short to
rank steady-state idle behavior or battery life. Their raw counters are retained;
no energy or total-process-tree/GPU claim is made here.

Before replacing the production UI, repeat with real tab text, scrolling, an
editable address palette/IME, focus routing, accessibility and overlays above
WebContents. Measure the complete process tree, GPU work and power over longer
steady-state intervals. The current data supports testing a Rust scene painted
through Chromium/Skia before committing to a separate GPU rendering stack.

Validation: optimized Chromium build and signed package, all 36 regular-app runs,
44 Python tests, two Rust scene/hit-test tests, workspace CTest, GN dependency
check, Rust formatting and whitespace checks. The Metal sidebar and native page
were also inspected visually, and row selection was verified by clicking.
