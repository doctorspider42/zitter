# Product and browser boundary

Zitter retains Chromium's Chrome browser backend. The product layer can be
compiled and tested without a Chromium checkout; the live browser, platform
adapters and most UI hosting still require it. This is an incremental boundary,
not a migration to a standalone `//content` embedder.

## Independent product code

`zitter/core`, `zitter/application` and `zitter/ports` use the C++ standard
library and other product headers only:

- `WorkspaceModel` owns workspace metadata. `WorkspaceController` activates a
  workspace through `WorkspaceRuntime`, without knowing about live pages.
- `browser_state.h` defines preference snapshots, site permission display state,
  browsing-data requests, profile catalogue metadata, domain routing rules and
  import options. Adapters still enforce policy and validate input; a snapshot
  is not an authorization token.
- `CommandHistory` owns learned command weights, weekly decay, score expiry,
  persistence-entry validation and the 256-entry bound. Its adapter supplies
  Unix-epoch milliseconds. Lookups use borrowed keys rather than allocating
  command and workspace strings for every comparison.
- `PinTree` indexes borrowed pin IDs, parent IDs and folder flags. It checks
  placement, detects cycles and missing parents, enforces the renderer's depth
  limit for an entire moved subtree, validates complete snapshots before UI
  admission, and enumerates descendants in input order.
  Building the index and enumerating a subtree take linear time. The metadata
  and strings must remain unchanged for the short lifetime of the index.

`tools/check_boundaries.py` checks these directories recursively, including
nested sources, and rejects Chromium, Views and platform headers. The standalone
CMake build is a second check that this code has no hidden Chromium dependency.

## Chromium and UI adapters

`WorkspaceWindow` still coordinates product commands, native views, live tabs,
profile leases and asynchronous persistence. For pin edits it projects metadata
into `PinTree`; the existing adapter continues to own URLs, page identity, save
rollback and open-pin associations. Folder deletion reuses the subtree plan
instead of repeatedly scanning the catalogue until no descendants remain.
`RestorePins` rejects duplicate IDs, cycles, orphaned nodes, leaf parents and
excessive depth before replacing the previous catalogue or updating the UI. This
also prevents invalid restore input from reaching the UI graph's fatal invariant
check. An ambiguous deletion plan leaves the catalogue unchanged.

`BrowserTools` retains shortcut conversion, URL validation, query escaping and
JSON serialization. Command ranking delegates to `CommandHistory`, converting
the platform clock at the boundary. The `ZitterTools.json` field names and units
remain unchanged. Command results calculate each candidate's score once before
stable sorting, preserving catalogue order for ties and the existing initial cap.

`browser_data.h` re-exports the independent types for existing callers and keeps
URL-, time- and file-system-specific records in the Chromium adapter. The
compatibility aliases refer to the same types; no duplicate state is maintained.

`WorkspaceShell` receives `ShellState` and emits callbacks, but still depends on
Views, Chromium utility types and platform event delivery. Rust scene/layout
code and Metal presentation remain behind their existing adapters. This change
does not give UI a separate event loop or process, and does not establish a frame
rate or presentation-latency improvement.

URL canonicalization, origin checks, navigation validation, permissions, password
storage, sandboxing and process isolation remain in the browser backend. Pure
product policies must not replace these security mechanisms.

## Verification

Run the independent checks with:

```sh
cmake -S . -B build/product-core -DCMAKE_BUILD_TYPE=Debug
cmake --build build/product-core
ctest --test-dir build/product-core --output-on-failure
python3 tools/check_boundaries.py
python3 -m unittest discover -s tests -p test_boundaries.py
```

The product contract tests cover learned ranking, expiry, target isolation,
clock rollback, malformed persisted weights and capacity, along with pin cycles,
out-of-order descendants and depth checks. Browser integration coverage lives
in `profile_workspace_browsertest.cc`: command serialization and learned ordering,
folder persistence/deletion/rollback, pin movement and corrupt/deep-tree edits.

Both `chromium/BUILD.gn` and `CMakeLists.txt` compile the same product sources.
The development `build.sh` stages `zitter/` as well as the UI sources so the
existing lab overlay cannot silently compile an older product layer.

### Validation on 2026-11-04

Based on `origin/main` at `b18548b`:

- Standalone CMake build and CTest: 2/2 contract executables passed.
- Boundary, overlay and candidate-staging Python suites: 26 tests passed using
  the depot-tools Python 3.11 runtime. The system Python 3.9 cannot run some
  existing staging fixtures that use `Path.write_text(newline=...)`.
- Optimized Chromium-backed test binary compiled and linked. The final browser
  run passed 14/14 tests with retries disabled, including invalid snapshot
  rejection, maximum-depth moves, command serialization/order, folder save
  rollback, undo and closed-pin restoration. Local output is in
  `build/decoupling-browser-tests.json` and its adjacent log.
- `gn check` passed for the isolated `:core` target; the recursive product
  boundary check, shell syntax check and whitespace check passed.

The full shell GN include check still reports pre-existing adapter dependency
issues, including printing, extension navigation and capture-device ranking.
The same failures were reproduced in the existing `zitter_ui_lab` overlay;
the printing, page-service and media-access sources there match `origin/main`.
These failures are separate from the independent core's passing dependency
check. No GN include-check bypass was added.

The first integration run exposed a fatal UI graph assertion when invalid pin
restore input was published without validation. Complete snapshot admission was
then added before state replacement, and the final run passed the regression.
This verification establishes behavior and dependency separation, not an FPS
or input-to-presentation performance result.

The [2026-11-05 matched UI benchmark](product-boundary-performance-2026-11-05.md)
measures the before/after effects and profiles the remaining native UI costs.
