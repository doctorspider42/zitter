# Optional AI assistant

Open **Command Center → AI Assistant** (`Ctrl+K`, then type `>assistant`).
The assistant opens on the right with a quiet welcome screen, draft suggestions
and a composer anchored at the bottom. The pin icon reserves page space or uses
an overlay. An unpinned panel hides after the pointer remains outside for about
650–750 ms; pinned panels stay visible. Drafts, open menus and pending questions
survive hiding, and a running task keeps working. Pointer monitoring continues
across child views and resumes after resizing. A panel opened with the keyboard
stays visible while the mouse is stationary. The adjacent click-consent bubble
also counts as inside while the pointer is over it. Drag the grip on its left edge to resize it
between 360 and 760 DIP; narrow windows constrain the effective width. The chosen
width survives hiding and pin changes within the window.
The header has no close button: unpin the panel to let it hide automatically.
Use **Stop** to stop the current task. The right edge opens the panel whenever
the assistant is enabled, including before the first conversation. Hovering never
starts a provider request: requests follow **Send**, an explicit connection test,
or a configured scheduled task. Suggestions only fill a draft; they do not send
it or grant browser access.

## Settings

**Settings → AI Assistant** owns everything except the model choice:

- **Turn on the assistant.** When it is off, the panel does not open at the window
  edge, Command Center shows only “AI Assistant is turned off” (which opens this
  page), no assistant process starts and background pages close. The other values
  are kept.
- **AI connections.** Each service has a setup status, a visibility switch and
  **Test connection**. Detected installations remain unverified until a selected
  model answers a short test message: green means success, red means failure.
  The test shares no pages or browsing history and times out after 20 seconds.
  Add, replace or remove an OpenAI API key directly here. Keys are encrypted
  through Chromium's operating-system credential protection before being saved.
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

Zitter builds and ships `zitter_assistant`, a native helper beside the browser
executable (in `Contents/MacOS` on macOS). The assistant starts it on the first
sent message. Node.js and npm are not required for any assistant backend.
The helper uses a Rust orchestration core and Chromium's networking libraries;
browser actions and permission checks remain in the browser process.

| Backend | Setup | Model field |
| --- | --- | --- |
| OpenAI API | Add your key in **Settings → AI Assistant → AI connections**. `OPENAI_API_KEY` remains an optional fallback. Requests use the Responses API with `store: false`. | A model supporting structured JSON output. |
| Codex CLI | Install and authenticate the native Codex executable. Put it on PATH or set `ZITTER_CODEX_PATH` to its absolute path. | Current defaults: `gpt-6.1-sol`, `gpt-6.1-terra`, `gpt-6-astra`, `gpt-6-luna`. Custom IDs remain supported. |
| Claude CLI | Install and authenticate native Claude Code. Put it on PATH or set `ZITTER_CLAUDE_PATH` to its absolute path. | A Claude CLI alias or exact model ID. |
| Ollama | Start local Ollama and pull the chosen model. Optional `ZITTER_OLLAMA_URL` changes the loopback HTTP endpoint; the default is `http://127.0.0.1:11434`. | An installed model with reliable structured output. |

Codex must support `exec --json`, `--output-schema`, `--ephemeral`,
`--ignore-user-config` and `--ignore-rules`. An unsupported CLI fails that backend
without falling back to Node or another provider. A launcher installed through
npm may be a JavaScript script: point `ZITTER_CODEX_PATH` at the actual native
binary instead. The same native executable requirement applies to Claude.
Do not install npm packages into an app bundle. Chromium's own build toolchain
and the independent network audit scripts are outside this runtime migration.

API keys are never entered into page fields or saved as plaintext. The settings
file contains only an OS-encrypted value; plaintext is passed privately to the
native helper through its process environment and is never added to prompts,
transcripts, status payloads or command-line arguments.
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
beside the element (**Allow** with Enter or **Skip** with Escape). The checkbox
**Don’t ask again for this session** takes effect with **Allow** and covers later
turns in the same conversation while the app remains open. A new conversation
asks again. Skipping tells the model the user declined that click. The model
cannot change this choice or supply native consent.

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
ends on completion, failure, cancellation or a profile change. The frame and the
conversation's spinners move unless the system asks for reduced motion; the
workspace animations setting does not affect them.

While the model works, the conversation shows what is happening: the step in
progress turns (a spinner, its label breathing), finished steps get a check,
failed ones a warning, and a pending consent a pulsing dot. The answer streams in
as the model writes it, for every backend: Claude through the CLI's partial
messages, OpenAI through Server-Sent Events, Ollama through its NDJSON stream and
Codex through CLI item updates. The panel polls the runtime's `/progress`
for it four times a second. The pointer can move from the panel to a click-consent
prompt without hiding it; leaving both areas hides an unpinned panel while the
consent remains pending.

The model can ask the user with `ask_user`: a question in the conversation with up
to eight answers picked with one click (or several, then **Send answer**), or by
pressing the answer's number while the field is empty. The user may also type
their own answer in the field. The turn waits for the answer even if the unpinned
panel hides; reopening it restores the question and draft. The model must use it before completing a purchase,
payment, booking, sent message, deletion, publication or account change; native
click consent covers only one click, not such an approval. It also uses it to
choose between alternatives or to get details only the user knows. A question
left open when Zitter closes cannot be answered after a restart.

## Scheduled tasks

A scheduled task is an instruction the assistant runs by itself: "every morning
summarize the news", "every Friday add the new lottery codes to my OneNote". The
header's calendar button opens **Scheduled tasks**, which covers the panel like
the history. Each task shows its name, instruction, schedule, model, access, its
last run (done, needed you, failed) and the next one. A switch pauses or resumes
it, **Run** starts it now, the trash deletes it, and clicking the card opens the
editor.

- **Editor** (**New task**, or a card): the instruction, the schedule (every day,
  on chosen weekdays, every hour, or once, at a time of day), the model (a click
  moves to the next model from Settings) and the task's own access, including
  "Ask before every click". **Delete** removes an existing task.
- **From a conversation:** asked for something recurring, the model calls
  `schedule_task`. The conversation shows a proposal (instruction, schedule,
  model, access) with **Schedule**, **Change settings** (opens the editor) and
  **Just once, now**. A proposed task never gets wider access than the
  conversation it came from. `list_tasks` lets the model see the user's tasks.
- **Runs:** the panel checks every 30 seconds. A due task starts as its own
  conversation, which opens with a "Started automatically" banner and a link to
  the editor. When it may act on pages it works in an invisible tab (background
  work must be allowed in Settings); otherwise it runs without page actions.
  Access is cut to today's defaults in Settings. Clicks wait for consent only when
  the task asks for it.
- **Missed runs:** the panel exists only while Zitter runs. A run missed while
  Zitter was closed happens once, shortly after start. When saved tasks exist, the
  panel is created hidden at startup so tasks run even if nobody opens it.
- **Storage:** tasks are saved in `ZitterAssistantSessions.json` with the
  conversations, per profile. A task runs only while its profile is active.

## Site search

On a page, the user can ask the assistant to add the site's search ("dodaj
wyszukiwarkę dla tej strony"). The model finds the site's search URL (from the
page's search form, or by searching once) and calls `add_search_engine` with a
name, a short keyword and the URL (`%s` or `{query}` for the query). The
conversation asks **Add with keyword …** or **Don't add**; a typed answer is used
as another keyword. The engine joins Settings → Search engines, so typing
`gr solaris` in the address bar searches Goodreads.

## Toolbar button

The extensions bar shows the assistant's spark while the assistant is enabled.
Clicking it opens a local menu without starting a model or sharing page content:

- **Summarize the key points**: claims, figures, conclusions and next steps.
- **Add this site’s search**: discover its search URL and propose a keyword.
- **Check facts and sources**: compare the page's claims with independent and
  primary sources after reading the current page, including authorized signed-in content.
- **Extract data and compare**: build a table of prices, terms, differences and
  missing information from the page.
- **Block ads with the assistant**: inspect, test and preview additional rules.
- **New conversation**: open a fresh assistant session and preserve the previous draft.
- **AI settings**: go directly to AI connections and model configuration.

Page actions create a fresh session with the current page reference. Existing
work and an earlier draft stay intact. If the saved permission defaults already
allow the required page access, research and actions, the shortcut sends its
instruction immediately. Otherwise it prepares a draft and opens the access
menu; permissions never expand automatically. The user can review access and
send the draft. Page actions are unavailable on internal or unsupported pages.

The spark keeps its activity arc, open-panel tint and unread-result count.
Opening the quick-action menu alone does not mark results as read; opening the
conversation does. Disabling the assistant removes both the button and menu.

The model starts from what the user sees: a question about "this" or without a
clear subject is taken to be about the current tab, and a request to find
something opens it for the user. A general question is simply answered.

Pages that draw their content on a canvas (Google Docs, Sheets, Slides, maps, or
canvases covering a quarter of the viewport) report `canvas: true` from
`read_page`, which then attaches their screenshot, so the model reads the
content it could not get as text.

Each tool call is also saved with the conversation as a hidden trace entry: the
tool, its arguments and the gist of its result (errors, outcome fields and sizes,
never screenshots or whole pages). It is not shown in the panel; it is there to
inspect a session afterwards in `ZitterAssistantSessions.json`.

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

Build `//zitter:zitter_assistant`, `//zitter:assistant_engine_unittests_exe`
and the test-only `//zitter:assistant_cli_fixture`. Run the resulting
`assistant_engine_unittests` executable, then:

```sh
ZITTER_ASSISTANT_TEST_BINARY=/absolute/build/path/zitter_assistant \
  python3 -m unittest discover -s tests -p test_native_assistant.py -v
```

The Python suite exercises the actual helper, local HTTP model fixtures and a
native CLI fixture without Node, model accounts or external API calls. It covers
RPC authentication, provider isolation flags, screenshot input, streaming,
continuations, cache sharing, permissions, cancellation and unsafe research URLs.
The Rust tests cover response decoding, partial JSON, model IDs, timestamps and
stable model instructions.

Run the Chromium `ZitterProfilesBrowserTest.Assistant*` cases for native
permissions, redaction, keyboard input, panel state, background work, schedules,
restoration and quick actions. `AssistantRuntimeBrowserToolRoundTrip` uses the
actual native helper and a local Ollama fixture. The toolbar test checks menu
opening without a model request; `AssistantQuickActionsKeepDraftsAndAccess`
checks draft preservation and unchanged permissions.

Use `--zitter-open-panel=assistant` for visual checks without mouse automation.
Live provider checks require the corresponding installed runtime, credentials and
model; offline tests do not prove account availability or model quality.

Provider interfaces: [OpenAI function calling](https://developers.openai.com/api/docs/guides/function-calling),
[Codex CLI](https://learn.chatgpt.com/docs/non-interactive-mode),
[Claude CLI](https://code.claude.com/docs/en/cli-reference),
[Ollama chat](https://docs.ollama.com/api/chat).
