# Optional AI assistant

Open **Command Center → AI Assistant** (`Ctrl+K`, then type `>assistant`).
The assistant opens on the right with a quiet welcome screen, draft suggestions
and a composer anchored at the bottom. The pin icon reserves page space or uses
an overlay. An unpinned panel hides 650 ms after the pointer leaves it; pinned
panels stay visible. Open menus, an active resize and a focused nonempty draft
keep it visible; a running task keeps working while the panel is hidden. Drag the grip on its left edge to resize it
between 360 and 760 DIP; narrow windows constrain the effective width. The chosen
width survives hiding and pin changes within the window.
Closing the panel stops its current task; background tasks keep running. Once
enabled for this window, the right edge opens it again. No assistant process or
provider request starts until **Send**. Suggestions only fill a draft; they do not
send it or grant browser access.

## Settings

**Settings → AI Assistant** owns everything except the model choice:

- **Turn on the assistant.** When it is off, the panel does not open at the window
  edge, Command Center shows only “AI Assistant is turned off” (which opens this
  page), no assistant process starts and background pages close. The other values
  are kept.
- **Backends.** Each backend shows its setup status (an API key in the
  environment, the bundled Codex SDK, `claude` on PATH, the Ollama address) and a
  switch that hides it from the panel. API keys are never typed into Zitter.
- **Models in the panel.** Visible models make up the panel's model menu. The
  default one starts every new session. Models can be added by backend and ID,
  hidden or removed.
- **Permissions for new sessions:** open and pinned tabs, page content and
  controls, clicking and tab management, history, public web research.
- **Safeguards:** confirm clicks on the visible tab, allow background work, and
  confirm clicks in the background (a separate switch, with a warning when off).
- **Parallel Deep research calls:** 2, 4, 8 or 12.

Sessions copy the permission defaults when they start. Narrowing a permission or
tightening a safeguard applies at once, also to running sessions: it stops every
task and discards backend conversation contexts while keeping the visible
transcripts. Wider access applies from a new session, so old context never
silently moves to a broader scope. Data already sent to a provider cannot be
recalled.

## The panel

The only choice in the panel is the model: the chip next to **Send** opens a
searchable menu grouped by backend. The model stays fixed within a session; picking
another one after the first message starts a new session and keeps the current one
in the session list.

The header holds **Conversation history** (history icon), **This session's access**
(shield: switches for this session's permissions and click confirmation, **Revoke
all access** and a link to Settings; before the first message any switch can
change, afterwards only narrowing works and it stops the task), **New session**,
pin and close. A click outside an open menu closes it. Revoking stops every task and terminates the
assistant runtime. Sessions belong to the profile in which they were created;
changing profiles stops their tasks.

**New session** reuses an untouched conversation (nothing sent yet) instead of
adding another one, and leaving an untouched conversation drops it.

Conversations are saved: a second after each change they are written to
`ZitterAssistantSessions.json` in Zitter's data directory, beside
`ZitterWorkspaces.json`, and they come back after a restart. Only conversations
with a sent message are saved, up to 200; past that, the least recently used idle
one is forgotten. A restored conversation keeps its model; its access never exceeds
the current defaults in Settings. The runtime does not keep tool results across a
restart: the next turn passes the last 30 prompts and answers to the model as
earlier context.

The history view covers the panel below the header. It lists the current
profile's conversations, newest first, grouped into Today, Yesterday, Last 7 days
and Earlier. Each row shows the title, a line of the last answer (or, while
searching, the text around the match), and the time or date. Search covers titles
and messages. Hovering a row shows a trash to delete that conversation (Delete
does the same on a focused row). **New session** sits above the list; the foot has
**Delete all conversations**, which needs a second click and removes only the
current profile's conversations. Messages are copied with the copy icon on each
message.

The message field is multiline: Enter sends, Shift+Enter starts a new line, and the
field grows to six lines before it scrolls. Replies render Markdown (headings,
bold, italics, inline code, links, lists, quotes, code blocks, tables and rules);
links open in a new tab. Every message has a copy button that copies its text as
written, Markdown included.

Type `@` to insert a page or a tool. Suggestions list open tabs (the current one
first), pinned pages and the tools **Deep research** and **Background**, filtered
by what follows the `@`. Arrow keys move, Enter or Tab inserts, Escape closes. The
footer says what inserting the highlighted item shares. An inserted item becomes a
chip above the field; its × removes it, and Backspace in an empty field removes the
last one. The **Deep research** and **Background** buttons under the field insert
and remove the same chips. Pages reach the model as `@current`, `@tab:<id>` and
`@pin:<id>` references.

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
- Browser actions: navigation, tabs, spaces, pinning, browser commands, mouse, keyboard,
  forms, page dialogs and scrolling.
- Searching the current profile's browsing history.
- Public web search and page reading.

Browser data access is off initially; public web research is on. Grants apply
only to the selected session. Page access can disclose personal information in
visible prose, so enable it only for sessions where that is appropriate.
Permission checks happen in native code, separately from model instructions.
Off-the-record profiles and browser-internal pages are excluded. Tab references
are lifetime tracked, checked against the current profile and rejected after a
tab closes. Query strings, fragments and URL credentials are removed from shared
tab/history metadata. Tools never expose cookies or browser password stores.
Password, one-time code, payment, hidden and explicitly private DOM fields are
excluded from reading and refuse input. Rich editors (`contenteditable`) are
ordinary content: the assistant reads and edits them. This is data minimization,
not a guarantee that arbitrary prose cannot contain a secret.

`read_page` reads the rendered DOM of the page, its open shadow roots and
same-origin frames. It lists up to 300 interactive elements, on-screen ones first:
links, buttons, form fields, `contenteditable` editors, ARIA roles such as menu
items, tabs, checkboxes and options, and elements with `onclick` or a tab stop.
Each one has its role, state, position and, for a `<select>`, its options.
Cross-origin frames, canvas contents and downloaded files are not read as text;
`screenshot` shows the page to the model as an image instead (JPEG, at most
1280 px wide, with `css_per_pixel` to turn image pixels into page coordinates).
Only the two newest screenshots stay in the conversation.

The assistant discovers permitted tabs automatically. References are optional.
Opening a dormant pin requires browser-action permission, followed by listing its
live tab.

Page tools act like a user, with real input events:

- `click` sends a real mouse click to a visible spot of the element (a covered
  element gets a scripted click). `mouse` hovers, clicks, double-clicks,
  right-clicks or drags, at an element or at x/y.
- `type` replaces the text of a field or of a rich editor element; `select` picks
  an option of a `<select>`; `scroll` scrolls the page or a scrollable element.
- For rich editors such as Google Docs, `insert_text` types at the page's keyboard
  focus; when nothing editable has focus it first gives the page's main editor a
  real click, so it works while the user's keyboard is in the assistant panel.
- `press_key` presses a key with optional Ctrl, Shift, Alt and Meta: letters,
  digits, F1–F12 and the named keys, so shortcuts such as Ctrl+C, Ctrl+V or
  Ctrl+Enter work. Shortcuts that close or switch tabs and windows (Ctrl+W, Ctrl+T,
  Ctrl+N, Ctrl+Q, Ctrl+Tab, Ctrl+Page Up/Down, Alt+F4, Ctrl+Shift+Delete) are
  reserved for the user.
- `navigate`, `open_tab`, `click`, `mouse` and `wait` return once the page has
  loaded (up to 15 s), with its URL, title and any dialog it opened. `open_tab`
  also returns the new tab's id.
- A page's alert, confirm, prompt or leave dialog blocks other page tools until
  `dialog` answers it.
- `visit` loads any HTTP(S) page, or up to six at once, in the assistant's own
  invisible reader tabs (`@reader`, `@reader:2` … `@reader:6`), signed in like the
  user's tabs.
- `move_tab`, `create_space` and `create_folder` organize tabs and spaces of the
  current profile; `list_tabs` also lists the spaces. `downloads` lists recent
  downloads (with the history permission).

All of these need the browser actions permission, except reading tools.

Consent is native and the model cannot supply it. With **Confirm clicks** on, these
wait for the user: clicks and mouse actions other than hover, Enter outside a
multi-line editor (it can submit a form), Enter with a modifier (it sends messages
in mail and chat apps), and accepting a confirm, prompt or leave dialog.

By default each such action on the visible page needs consent: a small prompt appears
beside the element (**Allow** with Enter, **Skip** with Escape, or **Until the task
ends**). Skipping tells the model the user declined that click. The model cannot
change this choice or supply native consent.

## Background work

With **Background** (the chip, `@`, or Alt+Enter to send), the assistant works in an
invisible tab of the same profile, so it has the same cookies and logins without
clicking in the user's view. In that mode `@current`, `navigate` and `open_tab` use
the hidden page, and the user's visible tabs stay read-only. While a task runs in
the foreground, the Background button under the field and **Finish in the
background** in the status capsule move it out of view: the tab it works in moves,
with its page state, and the previous tab returns (the space's only tab is copied
instead). A task that has not touched a page yet gets a fresh invisible tab.

Background tasks appear in the panel under **Background**, each with its status,
**Take over the tab** (which stops the task and shows the page as a normal tab) and
stop or close. When **Confirm clicks in the background** is on, a click waits in the
panel as a card with the element's label, **Allow**, **Skip** and **Take over the
tab**; the right-edge opener turns amber while a card waits. Background work needs
the click permission and **Allow background work** in Settings.
Background pages suppress page dialogs (a confirm returns Cancel). File uploads,
credentials and payment details remain user actions. The assistant must read
again after a page changes. A turn can take up to 400 tool steps; after that the
user sends a follow-up to continue.

An animated spectral edge with a violet, ice and mint light trail identifies page
control. A compact bottom capsule shows activity and the Escape hint. The center
of the page remains untinted. This indicator is browser chrome, outside the page renderer, and cannot
be removed by website styles. It does not intercept page input. **Stop** or
**Esc** interrupts the task and discards queued browser actions. The indicator
ends on completion, failure, cancellation or a profile change. Disabling
workspace animations uses a static indicator without the animation timer.

## Search and deep research

All backends share `web_search` and `web_read`. Search uses DuckDuckGo's public
HTML endpoint, which needs no API key or account.
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
`ZitterProfilesBrowserTest.AssistantPermissionsAndPageRedaction`,
`ZitterProfilesBrowserTest.AssistantKeyboardTypesAtFocusButNotIntoSecrets`,
`ZitterProfilesBrowserTest.AssistantPanelIsOptionalAndPinnable`,
`ZitterProfilesBrowserTest.AssistantSwitchRemovesPanelAndCommand`,
`ZitterProfilesBrowserTest.AssistantMentionsInsertRemovableChips` and
`ZitterProfilesBrowserTest.AssistantBackgroundPageSharesProfileAndCanBeTakenOver`.
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
