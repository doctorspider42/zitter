# Optional AI assistant

Open **Command Center → AI Assistant** (`Ctrl+K`, then type `>assistant`).
The assistant opens on the right with a quiet welcome screen, draft suggestions
and a composer anchored at the bottom. The pin icon reserves page space or uses
an overlay. Closing the panel stops its current task. Once enabled for this
window, the right edge opens it again. No assistant process or provider request
starts until **Send**. Suggestions only fill a draft; they do not send it or grant
browser access.

The model chip opens backend/model settings and the session picker. The history
icon opens the same session controls; the shield chip opens access grants.
**New session** is the plus icon. Copy and clear actions live in the session
controls. Closing Zitter discards conversations and in-memory caches.

Choose a backend and enter its model ID before the first message in each session.
**New session** creates an independent conversation. The session picker returns
to previous conversations. Backend, model and access grants stay fixed within a
session so old context cannot silently move to another provider or permission
scope. Sessions belong to the profile in which they were created; changing
profiles stops the current task. Create a new session for another profile.

## Runtime setup

Install Node.js 22 or later. It must be on the browser process's PATH, or set
`ZITTER_NODE_PATH` to the absolute Node executable path before launching Zitter.
The build copies the assistant scripts and locked package manifest into an
`assistant` directory beside the executable. macOS packages carry it beside
`Contents/MacOS/Chromium` too.

| Backend | Setup | Model field |
| --- | --- | --- |
| OpenAI API | Set `OPENAI_API_KEY` in Zitter's launch environment. Requests use the Responses API with `store: false`. | A model supporting structured JSON output, for example `gpt-5.4`. |
| Codex SDK | Run `npm ci --omit=dev` in the executable's `assistant` directory. Authenticate Codex normally first, or provide its supported API credentials. | A Codex-compatible model ID. |
| Claude CLI | Install and authenticate Claude Code. Its native executable must be on PATH, or set `ZITTER_CLAUDE_PATH` to its executable. | A Claude CLI alias such as `sonnet`, or an exact model ID. |
| Ollama | Start local Ollama and pull the chosen model. Optional `ZITTER_OLLAMA_URL` changes the loopback HTTP endpoint; the default is `http://127.0.0.1:11434`. | An installed model with reliable structured output, for example `qwen3:8b`. |

API keys are never entered into page fields or stored in browser preferences.
For a signed macOS app, install the SDK outside the app bundle and set
`ZITTER_CODEX_SDK_PATH` to its absolute `node_modules/@openai/codex-sdk/dist/index.js`
path. Do not modify a signed bundle with `npm ci`. Release maintainers can instead
install the SDK into the build output's `assistant` directory before packaging
and signing; the macOS packager carries that directory into the app.
Provider failures do not forward raw diagnostics into the conversation.
Codex runs in a temporary isolated home and working directory with shell tools,
web search, user MCP configuration and network tools disabled. Its existing
`auth.json`, when present, is copied into that temporary home for authentication.
Claude runs with no built-in tools, no MCP servers, no filesystem settings
sources and no session persistence. Browser actions run through Zitter's native
tool boundary, never through a provider shell. Temporary CLI files are removed
after each call; providers may apply their own retention policies.

## Sharing and browser control

Open **Access** to select independently:

- Open tabs and pinned pages in the current profile.
- Page text and interactive controls.
- Browser actions: navigation, tabs, pinning, browser commands, clicking, typing and scrolling.
- Searching the current profile's browsing history.
- Public web search and page reading.

Browser data access is off initially; public web research is on. Grants apply
only to the selected session. Page access can disclose personal information in
visible prose, so enable it only for sessions where that is appropriate.
Permission checks happen in native code, separately from model instructions.
Off-the-record profiles and browser-internal pages are excluded. Tab references
are lifetime tracked, checked against the current profile and rejected after a
tab closes. Query strings, fragments and URL credentials are removed from shared
tab/history metadata. Tools never expose cookies, browser password stores,
screenshots or input values. Password, payment, authentication, hidden,
autofill-sensitive, editable and explicitly private DOM fields are excluded.
This is data minimization, not a guarantee that arbitrary prose cannot contain
a secret. The assistant currently reads the main frame's rendered DOM; it does
not inspect cross-origin frames, shadow roots, canvas contents or downloaded files.

The assistant discovers permitted tabs automatically. References are optional.
Type `@` or use **@ Tabs** to pick the current page, another open tab or a pinned page.
References use `@current`, `@tab:<opaque-id>` and `@pin:<id>`. Opening a dormant
pin requires browser-action permission, followed by listing its live tab.

By default programmatic clicks require **Allow once** in the assistant panel.
Disable **Confirm each page click** before the first message to let that session
click autonomously. The model cannot change this choice or supply native consent.
Text entry is limited to non-sensitive
`input`/`textarea` controls; uploads and credential entry remain user actions.
Navigation and click completion indicate dispatch, not page-load completion.
The assistant must read again after a page changes.

An animated spectral edge with a violet, ice and mint light trail identifies page
control. A compact bottom capsule shows activity and the Escape hint. The center
of the page remains untinted. This indicator is browser chrome, outside the page renderer, and cannot
be removed by website styles. It does not intercept page input. **Stop** or
**Esc** interrupts the task and discards queued browser actions. The indicator
ends on completion, failure, cancellation or a profile change. Disabling
workspace animations uses a static indicator without the animation timer.

## Search and deep research

All backends share `web_search` and `web_read`. Search uses DuckDuckGo's public
HTML endpoint by default; set `BRAVE_SEARCH_API_KEY` to use Brave Search instead.
Search availability, bot challenges and incomplete page content can limit results.
Research uses a separate network client without browser cookies or authentication.
It accepts public HTTPS destinations only, checks DNS at connection time, checks
redirects, blocks private/local networks and bounds response sizes and timeouts.

Select **Deep research** with the composer mode chip before sending a question to plan complementary angles,
investigate primary sources, reviews, criticism and discussions in parallel, then
synthesize a cited report. Workers cannot use browser history or browser actions.
The default model-call concurrency is twelve; the engine supports a bounded range
of one to twelve. Each worker has a finite search/read budget. Individual source
failures remain visible to the synthesizer rather than discarding all findings.
This mode makes substantially more model calls than a normal conversation.

Stable instructions and append-only conversation prefixes support provider prompt
caching; Ollama models remain warm for ten minutes. Exact model responses and
in-flight requests share a bounded five-minute memory cache. Public page/search
results use another bounded five-minute cache. Cache hits cannot grant access to
history or browser operations. Caches are never written to the browsing profile.

## Validation

Run `node --test chromium/assistant/*.test.mjs` for provider response,
orchestration, access, network and RPC contracts. The targeted Chromium tests are
`ZitterProfilesBrowserTest.AssistantPermissionsAndPageRedaction` and
`ZitterProfilesBrowserTest.AssistantPanelIsOptionalAndPinnable`.
`ZitterProfilesBrowserTest.AssistantRuntimeBrowserToolRoundTrip` exercises the
actual Node loopback service, Ollama HTTP transport and native tab tool against
a local model fixture; it requires Node on PATH.
Use `--zitter-open-panel=assistant` for visual checks without mouse automation.
Live provider checks require the corresponding installed runtime, credentials and
model; offline tests do not prove account availability or model quality.

Provider interfaces: [OpenAI function calling](https://developers.openai.com/api/docs/guides/function-calling),
[Codex SDK](https://developers.openai.com/codex/sdk/),
[Claude CLI](https://code.claude.com/docs/en/cli-reference),
[Ollama chat](https://docs.ollama.com/api/chat).
