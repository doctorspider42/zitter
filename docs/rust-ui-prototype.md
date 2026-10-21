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

Pending optimized measurements. Debug smoke checks are not decision evidence.
