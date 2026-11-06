# Remove Node.js from the AI assistant

Status: implementation completed in the working tree, 2026-11-06. Original source
baseline: `92d4395`. The inventory and sequence below describe the original migration
plan. See [native validation results](assistant-native-validation-2026-11-06.md) for
implemented behavior, completed checks and remaining platform/account validation
limits. Signed release validation is a separate release step.

## Scope and decisions

Remove the assistant's dependency on Node.js and npm while retaining all four
backends: OpenAI API, Ollama, Claude CLI and Codex. The user explicitly accepts
Codex CLI as the replacement for the TypeScript Codex SDK.

Chromium's source, its bundled Node toolchain and its build pipeline remain
outside this migration. The standalone network and Safe Browsing audit tools
(`tools/audit_background_network.mjs`, `tools/verify_safe_browsing.mjs` and their
tests) also remain unchanged. JavaScript executed inside web pages remains:
removing Node does not require removing the assistant's `page.js` DOM adapter.

The completion condition is that the assistant builds without npm installation
and runs on a machine without a system Node installation. Chromium may continue
to use its own Node during compilation. Optional Codex and Claude integrations
must invoke native executables directly, without Node-based launcher scripts.

## Current implementation

| Area | Source | Responsibility to preserve |
| --- | --- | --- |
| Native panel and process owner | `chromium/prototype/assistant_panel.cc` | Lazy launch through `ZITTER_NODE_PATH` or `node`; health/progress polling; turn, continuation and cancellation requests; process lifecycle |
| RPC server | `chromium/assistant/server.mjs` | Loopback-only HTTP, bearer authentication, Origin rejection, 16 MiB request limit, one active request per session, ten-minute turn timeout, cancellation and parent-death handling |
| Orchestration | `chromium/assistant/engine.mjs` | Prompts and tool descriptions, session state, model/permission consistency, tool loop, screenshots, history restoration, research, concurrency and caches |
| Providers | `chromium/assistant/providers.mjs` | Responses API/SSE, Ollama/NDJSON, Claude subprocess protocol, Codex SDK, structured output validation and partial answers |
| Public research | `chromium/assistant/web.mjs` | Search, extraction, DNS/IP checks, redirect validation, connection-time DNS protection, bounded downloads and memory cache |
| Browser authority | `chromium/prototype/workspace_assistant.cc` and native assistant code | Permissions, profile/tab scope, consent, browser actions, background pages and provider setup status |
| Build and distribution | `chromium/BUILD.gn`, `tools/install_chromium_overlay.py`, `tools/package_app.py` | Copying runtime scripts/manifests and placing assistant resources in signed macOS bundles |
| Tests and launch helper | Assistant `*.test.mjs`, `profile_workspace_browsertest.cc`, `tools/run_metal_sidebar.py` | 27 declared JavaScript tests, native browser contracts, runtime round trip and LaunchServices Node path injection |

Conversation and scheduled-task persistence already belongs to native code.
Keep `ZitterAssistantSessions.json`, profile ownership, schedules and permission
semantics compatible; do not redesign them as part of the runtime replacement.

## Recommended architecture

Use a native assistant helper process with a Rust orchestration core and small
C++ adapters for Chromium networking, process management and the existing panel
protocol. Build it through GN using the project's existing Rust/C++ integration
pattern. Keep model and network work asynchronous and off the browser UI thread.

For the first migration, preserve the current loopback RPC contract:
`/health`, `/turn`, `/continue`, `/progress`, `/cancel`, `/delete`. This keeps the
panel and native action/consent boundary stable while replacing the engine.
Replacing HTTP with Mojo or pipes can be considered independently after parity;
it is not necessary to remove Node and is not a migration prerequisite.

The helper owns transient sessions, provider requests and research. The browser
owns profiles, persistence, permissions, consent and execution of browser tools.
Provider output is always untrusted. A separate native process preserves crash
containment, but does not by itself provide an OS sandbox.

Prefer Chromium's existing HTTP/TLS facilities through dedicated adapters over
introducing a second Rust networking stack. Provider/research traffic must use
an isolated context without the user's cookies, cache, authentication or profile
proxy configuration. Verify that its resolver and connection APIs can enforce
research address restrictions before committing to the transport implementation.
Any additional Rust dependencies need explicit GN integration and license review;
avoid making a new Cargo package-download step a build requirement.

## Main risks and mitigations

| Risk | Severity | Required mitigation |
| --- | --- | --- |
| Browser authority moves into the model runtime or a provider agent | Critical | Keep native checks at action dispatch; preserve consent, profile scope and revocation. CLI tools, MCP and unrelated provider capabilities stay disabled. |
| Public research reaches local services or inherits browser identity | Critical | Independent network context; HTTPS and port policy; reject credentials and nonpublic IPs; validate every redirect and the addresses actually used for connections, including IPv6 and DNS rebinding cases. Checking a hostname before a generic fetch is insufficient. |
| Codex/Claude spawn a Node wrapper despite the new native engine | High | Resolve a native executable, support an explicit executable path, and test without system Node. Fail with a setup error for unsupported wrappers rather than silently restoring a Node dependency. |
| CLI authentication or isolation changes | High | Preserve temporary working/home directories, restricted configuration, screenshot handling, credential file permissions, cleanup and sanitized diagnostics. Verify login and credential refresh behavior against supported CLI versions. |
| Stop/revoke leaves work running or executes stale browser actions | High | Propagate cancellation to queued calls, HTTP streams and entire child process trees. Reject callbacks with obsolete session epochs; clean up on profile change, helper crash and browser exit. |
| Streaming differs between providers | High | Test fragmented UTF-8, split JSON/SSE/NDJSON frames, partial escapes, final structured output, provider errors and disconnects. Partial text must never trigger tool execution. |
| Concurrency/cache changes cause leaks, duplicate calls or excess costs | High | Preserve global model-call bound and session limits; test cancellation of shared in-flight work. Scope private model caches by profile/permission lifecycle and clear them on revoke/profile changes. Never use a cache hit to bypass authorization. |
| Existing conversations or scheduled tasks break | High | Preserve file schema and provider identifiers, restore bounded text history, test scheduled runs and background consent after restart. No reset of user data. |
| Native helper fails in signed packages or on Windows | High | Test packaged resource discovery, executable placement, signing and process-tree termination on both platforms. Verify actual distributed artifacts without developer PATH assumptions. |
| Security audit coverage disappears with JavaScript tests | High | Port assistant contracts before deleting their old tests. Leave the independent Node-based audit tools operational. |
| Migration expands into a Chromium/toolchain rewrite | High | Change Zitter assistant targets/adapters only. Keep upstream Node, TypeScript, WebUI and unrelated tooling untouched. |

Do not assume lower memory use or better latency merely from changing languages.
Measure helper cold start, idle RSS, time to first visible answer, streaming
responsiveness and bounded research load against the existing implementation.

## Migration sequence

### 1. Freeze behavior and establish fixtures

- Record current request/response/error shapes and all six RPC endpoints.
- Extract synthetic golden fixtures for provider requests, tool results,
  screenshots, saved-history reconstruction and research; never record real
  credentials or private browsing content.
- Run the existing assistant tests and targeted browser tests to establish a
  baseline. Record existing failures separately from migration regressions.
- Preserve `SYSTEM` and `HELP` as versioned text resources with contract checks;
  avoid changing model instructions while changing runtime behavior.
- Record limits explicitly: 32 runtime sessions, 30 restored history entries,
  two retained screenshots, 16 actions per model response, 400 tool steps per
  turn, concurrency up to 12, and bounded five-minute caches.

Exit: the behavior to reproduce is reviewable independently of Node code.

### 2. Resolve the two highest-risk integration points

- Prove a GN-built native helper can start from the panel, authenticate RPC,
  return a synthetic turn, cancel and terminate with its parent on macOS and
  Windows. Do not put a model engine into the renderer or UI thread.
- Prove isolated HTTP streaming and connection-time DNS restrictions with local
  fixtures. Decide the exact networking adapter from this result.
- Prove direct native Codex CLI invocation: text, screenshot, structured final
  response, progress, cancellation and the current authentication path.
- Specify supported CLI versions and an executable setting such as
  `ZITTER_CODEX_PATH`. Keep the persisted provider ID `codex`.
- Test isolation with hostile user/project configuration and MCP entries. Missing
  flags or unsupported isolation behavior must fail closed for that backend.

Official OpenAI documentation describes `codex exec --json` for event output and
`--output-schema` for structured final responses. These are the candidate
replacement interface, not proof of parity with the locked SDK. Pin and test a
CLI version; confirm image flags, event granularity and isolation options before
implementation. See [non-interactive mode](https://learn.chatgpt.com/docs/non-interactive-mode)
and [Codex SDK](https://learn.chatgpt.com/docs/codex-sdk).

Exit: no unresolved dependency on a Node SDK, launcher or unproven transport.

### 3. Implement the native core and providers

- Port validation, sessions, the tool loop, history, screenshot retention,
  progress, budgets, cancellation, concurrency and caches into the Rust core.
- Implement OpenAI and Ollama HTTP adapters first, using deterministic local
  streaming fixtures for both success and failure paths.
- Port Claude CLI invocation, including multimodal input and refused-tool-call
  handling. Retain explicit disabling of tools, MCP, settings and persistence.
- Replace the Codex SDK adapter with the tested native CLI adapter. Preserve
  temporary isolated configuration and native-only browser action dispatch.
- Keep all four backends behind one internal provider contract. A missing CLI
  must disable only that provider, not OpenAI/Ollama or the entire assistant.

Exit: native provider and engine contract tests pass; live checks of each backend
are tracked separately and require the appropriate installed model/account.

### 4. Port research and prove security parity

- Port public URL checks, guarded connections, redirect handling, extraction,
  DuckDuckGo search, response limits, caching and timestamps.
- Port research planning, parallel workers, evidence gathering and synthesis.
  Workers remain unable to access browser history, tabs or actions.
- Add negative fixtures for DNS rebinding, mixed public/private DNS answers,
  local redirects, credentials, non-HTTPS URLs, mapped IPv6, oversized bodies,
  stalled streams and cancellation during cache sharing.
- Test permission narrowing/revocation while requests, consent and research are
  pending. Verify that old work cannot reach the browser after cancellation.

Exit: network safeguards and research parity are demonstrated by tests, including
failure paths. Search availability and model quality remain external variables.

### 5. Switch the panel and packaging

- Launch the native helper instead of `node server.mjs`, retaining the RPC
  contract and existing session epochs. Use the current native round-trip test
  with a local model fixture to verify the new process end to end.
- Preserve lazy startup on Send, shutdown/revoke behavior, background tasks,
  scheduled tasks, saved conversations, screenshots and both UI backends.
- Update provider setup probes, status labels, localization, README and
  `docs/ai-assistant.md`: Codex uses a native CLI; npm setup is removed.
- Update GN dependencies, overlay installation and packaging for native helper
  artifacts. Scripts previously sealed as Resources must not determine the
  placement/signing of the new executable; follow platform code-signing rules.
- Verify fresh and upgraded packages. Remove stale assistant script directories
  from reused build/package outputs without touching profiles or other tools.

During development, a temporary selector may choose one engine per session.
Never mirror real prompts to both engines or replay a failed turn automatically:
that can duplicate model charges or browser actions. Rollback is an explicit
development choice or a previous release, not a hidden Node fallback.

Exit: packaged native assistant passes browser integration checks without system
Node, with all intended providers available through their native dependencies.

### 6. Remove the assistant's Node artifacts

- Delete `server.mjs`, `engine.mjs`, `providers.mjs`, `web.mjs`, `errors.mjs`,
  assistant `package.json`/lockfile and superseded `*.test.mjs` after their
  replacement tests pass.
- Remove assistant-specific Node launch errors, `ZITTER_NODE_PATH`,
  `ZITTER_CODEX_SDK_PATH`, SDK probes and the Node environment injection from
  `tools/run_metal_sidebar.py`.
- Remove the old runtime copy target or replace it with native artifact
  dependencies. Keep `page.js`, `embed.py` and required page-script generation.
- Update current setup/verification instructions and license inventories.
  Preserve historical reports as dated evidence rather than rewriting history.
- Scan assistant source, build rules and packaged output for stale Node/npm/SDK
  dependencies. Exclude upstream Chromium and the retained standalone audits
  from the assistant-specific acceptance check.

Exit: the native path is the only shipped assistant implementation.

## Acceptance and effort

Acceptance requires all four provider paths, streaming, native browser actions,
consent, research, stop/revoke, profile switching, background work, scheduled
tasks and session restoration. Offline fixtures prove protocol behavior; a
successful live check is required before claiming a provider works with a real
account. Verify macOS signed bundles and Windows packaged execution separately.

Run the assistant on clean test environments without system Node/npm and invoke
Codex/Claude through native executable paths. Inspect process trees to confirm
that no assistant component starts Node. This restriction applies to the
application runtime, not Chromium's build environment or audit scripts.

Planning estimate for one engineer: approximately 3–5 working weeks for native
helper integration, parity ports, tests and packaging, subject to the networking
and CLI spikes. This is an estimate, not a measured commitment. If the spikes
expose additional platform or authentication work, revise it before the full port.
The first implementation milestone should be phase 2's native helper plus Codex
CLI proof; it resolves the major architectural uncertainties before the rewrite.
