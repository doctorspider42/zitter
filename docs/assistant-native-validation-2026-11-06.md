# Native assistant migration validation

Date: 2026-11-06. Scope: the Zitter assistant and its toolbar shortcuts.
Chromium's Node toolchain and the standalone Node-based audit tools are unchanged.

## Implementation

- `chromium/assistant/runtime.rs` owns orchestration, structured response
  validation, sessions, prompt/history/image handling, bounded model concurrency,
  memory caches and research. Stable instructions are embedded from `system.txt`
  and `help.txt`.
- `native_main.cc` implements authenticated loopback RPC and helper lifecycle.
  `native_http.cc` uses isolated Chromium network contexts, direct connections,
  guarded/pinned research DNS, redirect checks and bounded response streams.
- `native_cli.cc` invokes native Codex and Claude executables directly. It keeps
  temporary working directories, provider isolation arguments, image inputs,
  sanitized errors, bounded output and child-process cancellation.
- The panel launches `zitter_assistant` beside the browser executable. The old
  assistant JavaScript runtime, npm manifests, SDK probing and Node launcher
  settings have been removed. `page.js` remains the Chromium DOM adapter.
- The toolbar opens page actions: summarize, add site search, block ads, check
  facts/sources, extract data, and open the conversation. Actions use a fresh
  session and retain the previous draft. Existing permission defaults determine
  whether an action sends immediately or opens an access review first.
- Fact checking uses the normal browser/research tool loop so it can read an
  authorized signed-in page before looking for independent evidence. Deep
  research remains available separately, with workers restricted to public
  research tools.

## Checks completed

| Check | Result |
| --- | --- |
| Native helper and CLI fixture, GN build on macOS ARM64 | Passed |
| Rust core tests | 11 passed |
| Native helper integration tests, with Node absent from PATH | 26 passed |
| macOS package unit tests | 12 passed |
| Windows package unit tests | 5 passed |
| Overlay installer tests with bundled Python | 15 passed |
| Native helper and browser-test GN header dependency checks | Passed |
| Assistant browser tests without Node | 16 passed |
| Selected assistant browser tests with Metal | 5 passed |
| Toolbar menu visual inspection | Passed |
| English localization coverage | 0 missing entries |
| Patch whitespace check | Passed |

The integration tests use local HTTP model fixtures and a compiled native CLI
fixture. They cover RPC authentication, model and permission consistency,
streaming, history, screenshots, cancellation, shared requests, concurrency,
retry limits, invalid UTF-8 and unsafe research URLs. The macOS layout test starts
an actual helper with ICU data under the packaged framework resource directory.

Additional manual checks passed:

- A real public HTTPS read of [example.com](https://example.com/) through the guarded native
  reader, using the local model fixture for orchestration.
- Killing a synthetic parent while its native CLI fixture was running terminated
  both the assistant helper and its provider child.
- The application and browser-test binaries compile in the macOS component
  validation configuration. Native control initialization required a small
  prerequisite correction: action, row and search controls establish their
  accessibility role before setting names/tooltips, and named cards expose a
  group role. These changes are in Zitter code.

## Browser validation

All 16 `ZitterProfilesBrowserTest.Assistant*` cases passed on macOS ARM64 with
Node absent from PATH and provider credentials/executable overrides removed from
the test environment. This includes the native runtime round trip, toolbar quick
actions, access revocation, scheduled tasks, persisted conversations, background
pages, browser actions/consent, keyboard redaction, mentions and panel behavior.

Hidden page capture now uses an empty capture-size argument, as required by
Chromium's hidden-capture API. The existing view/content viewport sizes remain
explicitly configured before capture. These are Zitter adapter changes.

Five selected cases also passed with `--zitter-rust-raster=metal`: runtime/browser
tool round trip, toolbar actions and drafts, optional/pinned panel, menu access
revocation, and background page takeover.

The actual toolbar menu body was rendered by the browser test and visually
inspected. It shows all five page actions and the conversation entry, without
clipping. Local artifact: `build/assistant-native/ui/assistant-toolbar-actions.png`.

## Auto-hide follow-up

The unpinned panel continuously monitors pointer departure, independent of child
view exit events. It waits about 650–750 ms outside before hiding and checks the
pointer again at the deadline. Drafts, menus, pending questions and active model
turns survive hiding. Resizing defers collapse and automatically resumes departure
monitoring on release; pinning suspends it. The header close button was removed;
this final presentation change was checked in the running optimized application. The neighboring click-consent bubble
counts as inside. Keyboard opening does not collapse a panel under a stationary
outside pointer, but subsequent mouse movement arms departure handling.

`AssistantAutoHideTracksPointerAndPreservesDraft` uses native mouse-input events
to cover a focused draft with an open model menu, resizing released outside and
command opening before pointer entry. All 16 assistant browser cases passed after
this correction. The optimized macOS application was rebuilt, packaged, signature
verified and relaunched locally. No release was published.

## Validation limits

Real model-account turns were not sent. Codex CLI option compatibility was
inspected with the locally installed `codex-cli 0.166.0-alpha.6`; subprocess and
image contracts were exercised with native fixtures. Windows packaging logic was
tested, but a Windows executable was not built or run on this macOS host.
No signed/notarized release or GitHub publication was requested or performed.
No comparative performance benchmark was run; this migration makes no measured
claim about lower memory use or response latency.
