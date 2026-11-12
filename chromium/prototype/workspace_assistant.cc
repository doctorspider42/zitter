// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/workspace_window.h"

#include <map>
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "chromium/prototype/assistant_connection.h"
#include "chromium/prototype/assistant_panel.h"
#include "chromium/assistant/native_cli_path.h"
#include "chromium/prototype/assistant_page_tools.h"
#include <algorithm>
#include <array>
#include <limits>
#include "base/functional/callback_helpers.h"
#include "base/base64.h"
#include "base/barrier_callback.h"
#include "base/environment.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/i18n/time_formatting.h"
#include "chromium/prototype/ad_blocking_rules_panel.h"
#include "chromium/prototype/content_blocking.h"
#include "chromium/prototype/extensions_bar.h"
#include "content_blocking_probe_script.h"
#include "chromium/prototype/page_dialogs.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/cstring_view.h"
#include "base/strings/string_split.h"
#include "base/task/thread_pool.h"
#include "chromium/prototype/i18n.h"
#include "base/task/single_thread_task_runner.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/events/base_event_utils.h"
#include "chromium/prototype/devtools_console_reader.h"
#include "content/public/browser/document_user_data.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/weak_document_ptr.h"
#include "ui/events/event.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/browser/history/history_service_factory.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/common/isolated_world_ids.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view_tracker.h"

namespace zitter::prototype {
namespace {
// Chrome starts its worlds at TRANSLATE (2). Reserve the first embedder slot
// (1), below all Chrome and dynamically allocated extension worlds.
constexpr int kAssistantWorld = content::ISOLATED_WORLD_ID_CONTENT_END;
static_assert(kAssistantWorld > content::ISOLATED_WORLD_ID_GLOBAL && kAssistantWorld < ISOLATED_WORLD_ID_TRANSLATE);
base::Value Error(std::string message) { return base::Value(base::DictValue().Set("error", std::move(message))); }
std::string SafeURL(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS()) return {};
  GURL::Replacements strip; strip.ClearUsername(); strip.ClearPassword(); strip.ClearQuery(); strip.ClearRef();
  return url.ReplaceComponents(strip).spec();
}
// What Settings can say about each backend without a network request.
struct BackendProbe { bool openai = false, codex = false, claude = false; };
// Keyboard input at the page's focus, like a user typing: rich editors (Google
// Docs, contenteditable) take text only this way. The focused element is
// checked first; protected fields refuse, and Enter outside a multi-line
// editor (it may submit a form) needs the same native consent as a click.
// The assistant's own invisible reader tabs: `visit` loads any page there.
// "@reader" is the first, "@reader:2" to "@reader:6" the others.
constexpr char kReader[] = "__reader__";
constexpr size_t kReaders = 6;
std::string ReaderKey(const std::string& tab) {
  if (tab == "@reader") return kReader;
  if (int number = 0; tab.starts_with("@reader:") && base::StringToInt(tab.substr(8), &number) && number >= 1 && number <= int(kReaders))
    return number == 1 ? std::string(kReader) : kReader + base::NumberToString(number);
  return {};
}
// The page's open alert, confirm, prompt or leave dialog, if any.
std::optional<base::DictValue> OpenDialog(content::WebContents* page) {
  auto* dialogs = page ? PageDialogs::FromWebContents(page) : nullptr;
  if (!dialogs || !dialogs->dialog_open()) return std::nullopt;
  const auto type = dialogs->dialog_type();
  const char* kind = dialogs->leave_dialog_open() ? "leave" : type == content::JAVASCRIPT_DIALOG_TYPE_ALERT ? "alert"
                   : type == content::JAVASCRIPT_DIALOG_TYPE_CONFIRM ? "confirm" : "prompt";
  base::DictValue dialog;
  dialog.Set("kind", kind);
  dialog.Set("message", base::UTF16ToUTF8(dialogs->dialog_message()));
  return dialog;
}
// Calls `done(true)` once the page has stopped loading an HTTP(S) document, or
// `done(false)` at the deadline.
void WhenLoaded(base::WeakPtr<content::WebContents> page, base::TimeTicks deadline, base::OnceCallback<void(bool)> done) {
  if (!page) { std::move(done).Run(false); return; }
  // A dialog holds the page until it is answered.
  if ((!page->IsLoading() && page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) || OpenDialog(page.get())) { std::move(done).Run(true); return; }
  if (base::TimeTicks::Now() > deadline) { std::move(done).Run(false); return; }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE,
      base::BindOnce(&WhenLoaded, page, deadline, std::move(done)), base::Milliseconds(200));
}
void DragBetween(content::WebContents* page, content::RenderWidgetHost* host, gfx::PointF from, gfx::PointF to) {
  using Type = blink::WebInputEvent::Type;
  using Button = blink::WebPointerProperties::Button;
  const auto a = AssistantWidgetPoint(page, from.x(), from.y()), b = AssistantWidgetPoint(page, to.x(), to.y());
  SendAssistantMouse(host, Type::kMouseMove, a, Button::kNoButton, 0, 0);
  SendAssistantMouse(host, Type::kMouseDown, a, Button::kLeft, blink::WebInputEvent::kLeftButtonDown, 1);
  for (int step = 1; step <= 12; ++step) {
    const gfx::PointF at(a.x() + (b.x() - a.x()) * step / 12, a.y() + (b.y() - a.y()) * step / 12);
    SendAssistantMouse(host, Type::kMouseMove, at, Button::kLeft, blink::WebInputEvent::kLeftButtonDown, 0);
  }
  SendAssistantMouse(host, Type::kMouseUp, b, Button::kLeft, 0, 1);
}
struct KeyboardInput {
  std::string tool, label;
  std::u16string text;
  ui::KeyboardCode code = ui::VKEY_UNKNOWN;
  int flags = 0;
  char16_t character = 0;
  bool confirmed = false, clicked = false, focused = false;
};
void TypeAtFocus(base::WeakPtr<content::WebContents> page, KeyboardInput request, base::OnceCallback<void(base::Value)> done);
// Where would typing land? Without an editable focus, insert_text first gives the
// page's main editor a real click (once), then looks again.
void CheckFocus(base::WeakPtr<content::WebContents> page, KeyboardInput request, base::OnceCallback<void(base::Value)> done) {
  if (!page) { std::move(done).Run(Error("Page is not ready")); return; }
  auto* view = page->GetRenderWidgetHostView();
  auto* host = view ? view->GetRenderWidgetHost() : nullptr;
  if (!host) { std::move(done).Run(Error("Page is not ready")); return; }
  // Page focus lets editors such as Google Docs restore their own text target.
  host->Focus();
  RunAssistantPageOperation(page.get(), base::DictValue().Set("tool", "focus_info"), base::BindOnce(
      [](base::WeakPtr<content::WebContents> page, KeyboardInput request, base::OnceCallback<void(base::Value)> done, base::Value focus) {
        const auto* info = focus.GetIfDict();
        if (!page || !info) { std::move(done).Run(Error("Page is not ready")); return; }
        if (info->FindBool("secret").value_or(true)) { std::move(done).Run(Error("The focused field is protected")); return; }
        const bool editable = info->FindBool("editable").value_or(false), rich = info->FindBool("rich").value_or(false);
        if (request.tool == "insert_text" && !editable) {
          if (request.clicked && !request.focused) {
            // Editors such as Google Docs read keys from a hidden editable frame:
            // when the click did not reach it, focus it directly.
            request.focused = true;
            RunAssistantPageOperation(page.get(), base::DictValue().Set("tool", "focus_editor"), base::BindOnce(
                [](base::WeakPtr<content::WebContents> page, KeyboardInput request, base::OnceCallback<void(base::Value)> done, base::Value value) {
                  if (!page || !value.is_dict() || !value.GetDict().FindBool("ok").value_or(false)) {
                    std::move(done).Run(Error("No editor took the keyboard; click into the document area first")); return;
                  }
                  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE,
                      base::BindOnce(&CheckFocus, page, std::move(request), std::move(done)), base::Milliseconds(100));
                }, page, std::move(request), std::move(done)));
            return;
          }
          if (request.clicked) { std::move(done).Run(Error("No editor took the keyboard; click into the document area first")); return; }
          RunAssistantPageOperation(page.get(), base::DictValue().Set("tool", "editor_point"), base::BindOnce(
              [](base::WeakPtr<content::WebContents> page, KeyboardInput request, base::OnceCallback<void(base::Value)> done, base::Value point) {
                const auto* p = point.GetIfDict();
                auto* view = page ? page->GetRenderWidgetHostView() : nullptr;
                if (!view || !p || !p->FindDouble("x")) { std::move(done).Run(Error("Nothing editable has focus and the page has no editor to click")); return; }
                ClickAssistantPageAt(page.get(), view->GetRenderWidgetHost(), *p->FindDouble("x"), *p->FindDouble("y"));
                request.clicked = true;
                // Give the editor a moment to move its focus before checking again.
                base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE,
                    base::BindOnce(&CheckFocus, page, std::move(request), std::move(done)), base::Milliseconds(300));
              }, page, std::move(request), std::move(done)));
          return;
        }
        const bool sends = request.flags & (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN);
        if (request.code == ui::VKEY_RETURN && (!rich || sends) && !request.confirmed) {
          std::move(done).Run(base::Value(base::DictValue().Set("confirmation_required", true).Set("label", request.label).Set("key", true))); return;
        }
        TypeAtFocus(page, std::move(request), std::move(done));
      }, page, std::move(request), std::move(done)));
}
void TypeAtFocus(base::WeakPtr<content::WebContents> page, KeyboardInput request, base::OnceCallback<void(base::Value)> done) {
  auto* view = page ? page->GetRenderWidgetHostView() : nullptr;
  auto* host = view ? view->GetRenderWidgetHost() : nullptr;
  if (!host) { std::move(done).Run(Error("Page is not ready")); return; }
  if (request.tool == "insert_text") {
    for (const char16_t unit : request.text) {
      if (unit == u'\n') PressAssistantKey(host, ui::VKEY_RETURN, 0, u'\r');
      else if (unit != u'\r') TypeAssistantCharacter(host, unit);
    }
    std::move(done).Run(base::Value(base::DictValue().Set("ok", true).Set("clicked_editor", request.clicked)
        .Set("detail", request.clicked ? "Clicked into the page's editor, then typed; the caret may not be at the end (press_key ctrl+end)" : "Typed at the focus")));
    return;
  }
  PressAssistantKey(host, request.code, request.flags, request.character);
  std::move(done).Run(base::Value(base::DictValue().Set("ok", true)));
}
void KeyboardTool(content::WebContents* page, std::string tool, base::DictValue args, bool confirmed,
                  base::OnceCallback<void(base::Value)> done) {
  KeyboardInput request;
  request.tool = tool; request.confirmed = confirmed;
  const auto* text = args.FindString("text");
  const auto* key = args.FindString("key");
  if (tool == "insert_text") {
    if (!text || text->empty() || text->size() > 8000) { std::move(done).Run(Error("Text of 1 to 8000 characters is required")); return; }
    request.text = base::UTF8ToUTF16(*text);
  } else {
    // Parse the key now, so a refused key never reaches the page.
    request.label = key ? *key : std::string();
    if (!key || !ParseAssistantKey(*key, request.code, request.flags, request.character)) {
      std::move(done).Run(Error("Unsupported or reserved key (tab and window shortcuts stay with the user)")); return;
    }
  }
  CheckFocus(page->GetWeakPtr(), std::move(request), std::move(done));
}
// Where the page stands after an action: loaded (or still loading), and any
// dialog it opened.
void Settle(base::WeakPtr<content::WebContents> page, base::TimeDelta delay, base::OnceCallback<void(base::Value)> done) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE, base::BindOnce(
      [](base::WeakPtr<content::WebContents> page, base::OnceCallback<void(base::Value)> done) {
        WhenLoaded(page, base::TimeTicks::Now() + base::Seconds(15), base::BindOnce(
            [](base::WeakPtr<content::WebContents> page, base::OnceCallback<void(base::Value)> done, bool loaded) {
              if (!page) { std::move(done).Run(Error("The page closed")); return; }
              base::DictValue result;
              result.Set("ok", true);
              result.Set("url", SafeURL(page->GetLastCommittedURL()));
              result.Set("title", base::UTF16ToUTF8(page->GetTitle()));
              if (!loaded) result.Set("still_loading", true);
              if (auto dialog = OpenDialog(page.get())) result.Set("dialog", std::move(*dialog));
              std::move(done).Run(base::Value(std::move(result)));
            }, page, std::move(done)));
      }, page, std::move(done)), delay);
}
// A point for mouse input: an element from the last read_page, or x/y in page pixels.
void ResolvePoint(base::WeakPtr<content::WebContents> page, const base::DictValue& args, const std::string& prefix,
                  base::OnceCallback<void(std::optional<gfx::PointF>, std::string)> done) {
  if (!page) { std::move(done).Run(std::nullopt, "Page is not ready"); return; }
  if (const auto* element = args.FindString(prefix + "element")) {
    RunAssistantPageOperation(page.get(), base::DictValue().Set("tool", "point").Set("element", *element), base::BindOnce(
        [](base::OnceCallback<void(std::optional<gfx::PointF>, std::string)> done, base::Value value) {
          const auto* dict = value.GetIfDict();
          if (!dict || !dict->FindDouble("x")) {
            const auto* error = dict ? dict->FindString("error") : nullptr;
            std::move(done).Run(std::nullopt, error ? *error : "Element not found; read the page again"); return;
          }
          const auto* label = dict->FindString("label");
          std::move(done).Run(gfx::PointF(*dict->FindDouble("x"), *dict->FindDouble("y")), label ? *label : std::string());
        }, std::move(done)));
    return;
  }
  const auto x = args.FindDouble(prefix + "x"), y = args.FindDouble(prefix + "y");
  if (!x || !y || *x < 0 || *y < 0 || *x > 20000 || *y > 20000) { std::move(done).Run(std::nullopt, "An element or x and y are required"); return; }
  std::move(done).Run(gfx::PointF(*x, *y), base::StringPrintf("%d, %d", int(*x), int(*y)));
}
// hover, click, double_click, right_click or drag; all but hover need consent.
void MouseTool(content::WebContents* page, base::DictValue args, bool confirmed, base::OnceCallback<void(base::Value)> done) {
  const auto* raw = args.FindString("action");
  std::string action = raw ? *raw : "click";
  static constexpr std::string_view kActions[] = {"hover", "click", "double_click", "right_click", "drag"};
  if (std::ranges::find(kActions, action) == std::end(kActions)) { std::move(done).Run(Error("Unknown mouse action")); return; }
  auto weak = page->GetWeakPtr();
  auto first = args.Clone();
  ResolvePoint(weak, first, "", base::BindOnce(
      [](base::WeakPtr<content::WebContents> page, base::DictValue args, std::string action, bool confirmed,
         base::OnceCallback<void(base::Value)> done, std::optional<gfx::PointF> from, std::string label) {
        if (!from) { std::move(done).Run(Error(label)); return; }
        auto* view = page ? page->GetRenderWidgetHostView() : nullptr;
        auto* host = view ? view->GetRenderWidgetHost() : nullptr;
        if (!host) { std::move(done).Run(Error("Page is not ready")); return; }
        if (action != "hover" && !confirmed) {
          std::move(done).Run(base::Value(base::DictValue().Set("confirmation_required", true)
              .Set("label", action == "click" ? label : action + ": " + label))); return;
        }
        host->Focus();
        if (action == "drag") {
          ResolvePoint(page, args, "to_", base::BindOnce(
              [](base::WeakPtr<content::WebContents> page, gfx::PointF from, base::OnceCallback<void(base::Value)> done,
                 std::optional<gfx::PointF> to, std::string error) {
                auto* view = page ? page->GetRenderWidgetHostView() : nullptr;
                if (!to || !view) { std::move(done).Run(Error(to ? "Page is not ready" : error)); return; }
                DragBetween(page.get(), view->GetRenderWidgetHost(), from, *to);
                Settle(page, base::Milliseconds(300), std::move(done));
              }, page, *from, std::move(done)));
          return;
        }
        if (action == "hover") {
          SendAssistantMouse(host, blink::WebInputEvent::Type::kMouseMove, AssistantWidgetPoint(page.get(), from->x(), from->y()),
                    blink::WebPointerProperties::Button::kNoButton, 0, 0);
        } else {
          ClickAssistantPageAt(page.get(), host, from->x(), from->y(),
                  action == "right_click" ? blink::WebPointerProperties::Button::kRight : blink::WebPointerProperties::Button::kLeft,
                  action == "double_click" ? 2 : 1);
        }
        Settle(page, base::Milliseconds(400), std::move(done));
      }, weak, std::move(args), std::move(action), confirmed, std::move(done)));
}
// Ad blocking (docs/ad-blocking.md): what on the page looks like an ad, with
// selectors and third-party requests, for the model to write rules from.
void AdblockInspect(content::WebContents* page, base::OnceCallback<void(base::Value)> done) {
  RunAssistantPageOperation(page, base::DictValue().Set("tool", "adblock_inspect")
      .Set("site", ContentBlocking::RegistrableDomain(page->GetLastCommittedURL())), base::BindOnce(
      [](base::WeakPtr<content::WebContents> page, base::OnceCallback<void(base::Value)> done, base::Value value) {
        if (!page || !value.is_dict()) { std::move(done).Run(Error("The page could not be inspected")); return; }
        auto result = std::move(value).TakeDict();
        auto* profile = Profile::FromBrowserContext(page->GetBrowserContext());
        const GURL url = page->GetLastCommittedURL();
        result.Set("site", ContentBlocking::RegistrableDomain(url));
        result.Set("blocking", ContentBlocking::EnabledFor(profile, url) ? "on" : "off");
        if (auto* helper = ContentBlockingTabHelper::FromWebContents(page.get())) {
          result.Set("blocked_on_page", helper->blocked());
          base::ListValue blocked;
          for (const auto& blocked_url : helper->blocked_urls()) if (blocked.size() < 60) blocked.Append(blocked_url);
          result.Set("blocked_requests", std::move(blocked));
        }
        result.Set("rule_scope", "Every rule must name this site: site##selector for elements, ||host^$domain=site "
                                 "for third-party requests, or ||site/path for the site's own files.");
        std::move(done).Run(base::Value(std::move(result)));
      }, page->GetWeakPtr(), std::move(done)));
}
void SaveAdblockRules(content::WebContents* page, std::vector<std::string> rules, std::vector<std::string> replaces, const std::string& site,
                      int network, base::OnceCallback<void(base::Value)> done);
void TryAdblockRules(content::WebContents* page, std::vector<std::string> rules, base::OnceCallback<void(base::Value)> done);
void ReloadAndTry(content::WebContents* page, std::vector<std::string> rules, base::OnceCallback<void(base::Value)> done);
// Reloads `page`, waits until it has loaded and then `linger` more, for
// detectors and late ads, then runs `then`.
void ReloadAndWait(content::WebContents* page, base::TimeDelta linger, base::OnceCallback<void(base::WeakPtr<content::WebContents>)> then) {
  page->GetController().Reload(content::ReloadType::NORMAL, true);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE, base::BindOnce(
      [](base::WeakPtr<content::WebContents> page, base::TimeDelta linger, base::OnceCallback<void(base::WeakPtr<content::WebContents>)> then) {
        WhenLoaded(page, base::TimeTicks::Now() + base::Seconds(20), base::BindOnce(
            [](base::WeakPtr<content::WebContents> page, base::TimeDelta linger, base::OnceCallback<void(base::WeakPtr<content::WebContents>)> then, bool) {
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE, base::BindOnce(std::move(then), page), linger);
            }, page, linger, std::move(then)));
      }, page->GetWeakPtr(), linger, std::move(then)), base::Milliseconds(500));
}
// Anti-adblock and canvas ads (docs/ad-blocking.md): reloads the tab once with
// a probe in the page's own world and returns what it saw. Nothing is kept.
void AdblockDiagnose(content::WebContents* page, base::OnceCallback<void(base::Value)> done) {
  auto* helper = ContentBlockingTabHelper::FromWebContents(page);
  if (!helper || !page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) { std::move(done).Run(Error("Only web pages can be diagnosed")); return; }
  std::string nonce = base::Uuid::GenerateRandomV4().AsLowercaseString();
  std::erase(nonce, '-');
  std::string probe = kContentBlockingProbeScript;
  base::ReplaceFirstSubstringAfterOffset(&probe, 0, "ZITTER_NONCE", nonce);
  helper->SetProbe(std::move(probe));
  ReloadAndWait(page, base::Seconds(6), base::BindOnce(
      [](std::string nonce, base::OnceCallback<void(base::Value)> done, base::WeakPtr<content::WebContents> page) {
        if (!page) { std::move(done).Run(Error("The page closed")); return; }
        RunAssistantPageOperation(page.get(), base::DictValue().Set("tool", "probe_log").Set("nonce", nonce), base::BindOnce(
            [](base::WeakPtr<content::WebContents> page, base::OnceCallback<void(base::Value)> done, base::Value log) {
              if (!page) { std::move(done).Run(Error("The page closed")); return; }
              auto* helper = ContentBlockingTabHelper::FromWebContents(page.get());
              if (helper) helper->SetProbe(std::string());
              if (!log.is_dict()) { std::move(done).Run(Error("The probe log could not be read")); return; }
              auto result = std::move(log).TakeDict();
              result.Set("site", ContentBlocking::RegistrableDomain(page->GetLastCommittedURL()));
              if (helper) {
                base::ListValue blocked;
                for (const auto& url : helper->blocked_urls()) if (blocked.size() < 60) blocked.Append(url);
                result.Set("blocked_requests", std::move(blocked));
              }
              result.Set("note", "One reload with probes in the page's own world. bait_reads, error_listeners and globals name the "
                                 "script (by) that checks for blocking; walls and scroll_locks are what it did; canvas lists every "
                                 "image drawn into a canvas. Turn them into +js() or $redirect rules, then adblock_add_rules with "
                                 "check: true before saving.");
              std::move(done).Run(base::Value(std::move(result)));
            }, page, std::move(done)));
      }, std::move(nonce), std::move(done)));
}
// Checks the rules, outlines what they hide and asks for native consent; with
// consent, saves them as the profile's own and applies them at once.
void AdblockAddRules(content::WebContents* page, base::DictValue args, bool confirmed, base::OnceCallback<void(base::Value)> done) {
  auto* profile = Profile::FromBrowserContext(page->GetBrowserContext());
  auto* helper = ContentBlockingTabHelper::FromWebContents(page);
  if (!profile || profile->IsOffTheRecord() || !helper) { std::move(done).Run(Error("Ad blocking rules cannot be saved here")); return; }
  const GURL url = page->GetLastCommittedURL();
  const std::string site = ContentBlocking::RegistrableDomain(url);
  std::vector<std::string> rules, selectors;
  base::ListValue invalid;
  const auto* list = args.FindList("rules");
  // Models sometimes send the rules as one string, one rule per line.
  base::ListValue lines;
  if (!list && args.FindString("rules")) {
    for (const auto& line : base::SplitString(*args.FindString("rules"), "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY))
      lines.Append(line);
    list = &lines;
  }
  if (!list || list->empty() || list->size() > 50) { std::move(done).Run(Error("Give rules: 1 to 50 filter rules as strings")); return; }
  // check: try the rules in this tab only, with a reload, before asking to save.
  const bool check = args.FindBool("check").value_or(false);
  for (const auto& item : *list) {
    std::string rule = item.is_string() ? std::string(base::TrimWhitespaceASCII(item.GetString(), base::TRIM_ALL)) : std::string();
    std::string problem = rule.empty() || rule.size() > 1000 ? "a rule of 1 to 1000 characters is required" : ContentBlocking::RuleError(rule);
    if (problem.empty() && !ContentBlocking::RuleInScope(rule, site))
      problem = "the rule must apply only to " + site + " (" + site + "##selector, ||host^$domain=" + site + " or ||" + site + "/path)";
    if (!problem.empty()) { invalid.Append(base::DictValue().Set("rule", rule).Set("error", problem)); continue; }
    if (auto selector = ContentBlocking::HidingSelector(rule); !selector.empty()) selectors.push_back(std::move(selector));
    rules.push_back(std::move(rule));
  }
  // Own rules the new ones take over from, such as rules that stopped working.
  std::vector<std::string> replaces;
  if (const auto* old = args.FindList("replaces")) {
    const auto own = ContentBlocking::Rules(profile);
    for (const auto& item : *old) {
      const std::string rule = item.is_string() ? std::string(base::TrimWhitespaceASCII(item.GetString(), base::TRIM_ALL)) : std::string();
      if (std::ranges::contains(own, rule) && ContentBlocking::RuleInScope(rule, site)) replaces.push_back(rule);
      else invalid.Append(base::DictValue().Set("rule", rule).Set("error", "replaces names only this site's saved rules, exactly as saved"));
    }
  }
  if (!invalid.empty()) {
    std::move(done).Run(base::Value(base::DictValue().Set("error", "Some rules are invalid; nothing was saved. Fix them and call again.")
        .Set("invalid", std::move(invalid))));
    return;
  }
  // The page runs every hiding rule first: one it cannot parse, or one that
  // hides nothing here now, is a guess and goes back to the model. Then what
  // will disappear stays outlined while the user decides.
  helper->Preview(selectors, base::BindOnce(
      [](base::WeakPtr<content::WebContents> page, std::vector<std::string> rules, std::vector<std::string> replaces, std::string site, bool confirmed,
         bool check, base::OnceCallback<void(base::Value)> done, base::DictValue preview) {
        if (!page) { std::move(done).Run(Error("The page closed")); return; }
        base::ListValue invalid;
        if (const auto* checked = preview.FindList("rules")) for (const auto& item : *checked) {
          if (!item.is_dict()) continue;
          const auto* selector = item.GetDict().FindString("selector");
          const auto* error = item.GetDict().FindString("error");
          const int matches = item.GetDict().FindInt("matches").value_or(0);
          if (!selector || (!error && matches > 0)) continue;
          const auto rule = std::ranges::find_if(rules, [&](const std::string& r) { return ContentBlocking::HidingSelector(r) == *selector; });
          invalid.Append(base::DictValue().Set("rule", rule == rules.end() ? *selector : *rule)
              .Set("error", error ? "the page could not run it: " + *error
                                  : std::string("it matches nothing on this page now: anchor it on what the ad shows "
                                                "(its label text, an attribute, its size) and check it against adblock_inspect")));
        }
        auto* helper = ContentBlockingTabHelper::FromWebContents(page.get());
        if (!invalid.empty()) {
          if (helper) helper->Preview({}, base::DoNothing());
          std::move(done).Run(base::Value(base::DictValue().Set("error", "Some rules do not work on this page; nothing was saved. Fix them and call again.")
              .Set("invalid", std::move(invalid))));
          return;
        }
        const int count = preview.FindInt("count").value_or(0);
        int network = 0;
        for (const auto& rule : rules) if (rule.find('#') == std::string::npos) ++network;
        if (check) { TryAdblockRules(page.get(), std::move(rules), std::move(done)); return; }
        if (!confirmed) {
          // Exceptions let something through: the question says so.
          int exceptions = 0, scriptlets = 0;
          for (const auto& rule : rules) {
            if (rule.starts_with("@@") || rule.find("#@#") != std::string::npos) ++exceptions;
            else if (rule.find("#+js(") != std::string::npos) ++scriptlets;
          }
          network -= std::min(network, exceptions);
          const auto prompt = exceptions || scriptlets
              ? TF(u"Zmienić blokowanie na {}? Ukryte elementy: {}, reguły sieciowe: {}, skrypty: {}, wyjątki (przepuszczają): {}.",
                   {base::UTF8ToUTF16(site), base::NumberToString16(count), base::NumberToString16(network), base::NumberToString16(scriptlets),
                    base::NumberToString16(exceptions)})
              : !replaces.empty()
              ? TF(u"Zablokować reklamy na {}? Ukryte elementy: {}, reguły sieciowe: {}, zastąpione stare reguły: {}.",
                   {base::UTF8ToUTF16(site), base::NumberToString16(count), base::NumberToString16(network), base::NumberToString16(replaces.size())})
              : network ? TF(u"Zablokować reklamy na {}? Ukryte elementy: {}, reguły sieciowe: {}.",
                                           {base::UTF8ToUTF16(site), base::NumberToString16(count), base::NumberToString16(network)})
                                      : TF(u"Ukryć na {} zaznaczone elementy ({})?", {base::UTF8ToUTF16(site), base::NumberToString16(count)});
          std::move(done).Run(base::Value(base::DictValue().Set("confirmation_required", true).Set("label", site)
              .Set("prompt", base::UTF16ToUTF8(prompt)).Set("preview", count)));
          return;
        }
        SaveAdblockRules(page.get(), std::move(rules), std::move(replaces), site, network, std::move(done));
      }, page->GetWeakPtr(), std::move(rules), std::move(replaces), site, confirmed, check, std::move(done)));
}
// adblock_add_rules check: the rules act in this tab only (scriptlets and
// network rules from the next load, hiding rules at once after it), the tab
// reloads, and the model looks before anything is saved.
void TryAdblockRules(content::WebContents* page, std::vector<std::string> rules, base::OnceCallback<void(base::Value)> done) {
  auto* helper = ContentBlockingTabHelper::FromWebContents(page);
  helper->Preview({}, base::DoNothing());
  helper->SetTrial(rules, base::BindOnce(
      [](base::WeakPtr<content::WebContents> page, std::vector<std::string> rules, base::OnceCallback<void(base::Value)> done, std::string error) {
        if (!page) { std::move(done).Run(Error("The page closed")); return; }
        if (!error.empty()) { std::move(done).Run(Error("The rules could not be tried: " + error)); return; }
        ReloadAndTry(page.get(), std::move(rules), std::move(done));
      }, page->GetWeakPtr(), rules, std::move(done)));
}
void ReloadAndTry(content::WebContents* page, std::vector<std::string> rules, base::OnceCallback<void(base::Value)> done) {
  ReloadAndWait(page, base::Seconds(4), base::BindOnce(
      [](std::vector<std::string> rules, base::OnceCallback<void(base::Value)> done, base::WeakPtr<content::WebContents> page) {
        auto* helper = page ? ContentBlockingTabHelper::FromWebContents(page.get()) : nullptr;
        if (!helper) { std::move(done).Run(Error("The page closed")); return; }
        std::vector<std::string> selectors;
        for (const auto& rule : rules) if (auto selector = ContentBlocking::HidingSelector(rule); !selector.empty()) selectors.push_back(std::move(selector));
        helper->HideNow(selectors, base::BindOnce(
            [](base::WeakPtr<content::WebContents> page, base::OnceCallback<void(base::Value)> done, int hidden) {
              auto* helper = page ? ContentBlockingTabHelper::FromWebContents(page.get()) : nullptr;
              if (!helper) { std::move(done).Run(Error("The page closed")); return; }
              std::move(done).Run(base::Value(base::DictValue().Set("checked", true).Set("hidden_now", hidden)
                  .Set("blocked_requests", helper->blocked_requests())
                  .Set("detail", "The rules are on trial in this tab only and the tab reloaded with them; nothing is saved. "
                                 "Look (screenshot, read_page) whether the ads or the wall are gone and the content works; then call "
                                 "adblock_add_rules again without check to save them, or with other rules. Leaving the site ends the trial.")));
            }, page, std::move(done)));
      }, std::move(rules), std::move(done)));
}
// With consent: the rules join the profile's own and hide at once.
void SaveAdblockRules(content::WebContents* page, std::vector<std::string> rules, std::vector<std::string> replaces, const std::string& site,
                      int network, base::OnceCallback<void(base::Value)> done) {
  auto* profile = Profile::FromBrowserContext(page->GetBrowserContext());
  auto* helper = ContentBlockingTabHelper::FromWebContents(page);
  if (!helper) { std::move(done).Run(Error("Ad blocking rules cannot be saved here")); return; }
  helper->ClearTrial();
  std::vector<std::string> selectors;
  for (const auto& rule : rules) if (auto selector = ContentBlocking::HidingSelector(rule); !selector.empty()) selectors.push_back(std::move(selector));
  helper->Preview({}, base::DoNothing());
  std::string error;
  base::Time::Exploded now;
  base::Time::Now().LocalExplode(&now);
  const auto today = base::StringPrintf("%04d-%02d-%02d", now.year, now.month, now.day_of_month);
  if (!ContentBlocking::AddRules(profile, rules, base::UTF16ToUTF8(TF(u"Asystent: {}, {}", {base::UTF8ToUTF16(site), base::UTF8ToUTF16(today)})), &error)) {
    std::move(done).Run(Error("Rules were not saved: " + error)); return;
  }
  for (const auto& rule : replaces) if (!std::ranges::contains(rules, rule)) ContentBlocking::RemoveRule(profile, rule);
  const size_t saved = rules.size();
  helper->HideNow(selectors, base::BindOnce([](size_t saved, int network, int replaced, base::OnceCallback<void(base::Value)> done, int hidden) {
    std::move(done).Run(base::Value(base::DictValue().Set("ok", true).Set("saved", static_cast<int>(saved)).Set("hidden_now", hidden)
        .Set("network_rules", network).Set("replaced", replaced)
        .Set("detail", "Saved in Settings > Ad blocking > Your rules; network rules apply from the next load.")));
  }, saved, network, static_cast<int>(replaces.size()), std::move(done)));
}
// Consent belongs to the loaded document, even when a navigation reuses the
// same tab or frame. This identifier never leaves the native consent flow.
class AssistantDebugDocument final
    : public content::DocumentUserData<AssistantDebugDocument> {
 public:
  const std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();

 private:
  friend class content::DocumentUserData<AssistantDebugDocument>;
  explicit AssistantDebugDocument(content::RenderFrameHost* frame)
      : DocumentUserData(frame) {}
  DOCUMENT_USER_DATA_KEY_DECL();
};
DOCUMENT_USER_DATA_KEY_IMPL(AssistantDebugDocument);



void RunAssistantDebugTool(content::WebContents* page, const std::string& tool,
                           base::DictValue args, bool confirmed,
                           base::OnceCallback<void(base::Value)> done) {
  auto* frame = page->GetPrimaryMainFrame();
  if (!frame->IsRenderFrameLive() || OpenDialog(page) ||
      !DevToolsWindow::AllowDevToolsFor(Profile::FromBrowserContext(page->GetBrowserContext()), page)) {
    std::move(done).Run(Error("Developer tools are unavailable for this page"));
    return;
  }
  const auto* selector = args.FindString("selector");
  if ((tool == "debug_dom" || tool == "debug_edit_dom") &&
      (!selector || selector->empty() || selector->size() > 240)) {
    std::move(done).Run(Error("Give a CSS selector of up to 240 bytes"));
    return;
  }
  if (tool == "debug_edit_dom") {
    const auto* operation = args.FindString("operation");
    const auto* value = args.FindString("value");
    const auto* property = args.FindString("property");
    if (!operation || !std::ranges::contains(std::to_array<std::string_view>({
            "text", "style", "add_class", "remove_class", "hide", "show"}), *operation) ||
        (value && value->size() > 1000) || (property && property->size() > 64)) {
      std::move(done).Run(Error("Give a supported DOM edit with bounded arguments"));
      return;
    }
  }
  auto* approval = AssistantDebugDocument::GetOrCreateForCurrentDocument(frame);
  if (!confirmed) {
    std::u16string prompt = tool == "debug_console" ? T(u"Udostępnić konsolę tej karty?") :
        tool == "debug_dom" ? T(u"Sprawdzić DOM tej karty?") :
        tool == "debug_edit_dom" ? T(u"Zmienić DOM tej karty?") : T(u"Przeładować kartę bez pamięci podręcznej?");
    std::string label = frame->GetLastCommittedOrigin().Serialize();
    if (selector) label += " · " + *selector;
    std::move(done).Run(base::Value(base::DictValue().Set("confirmation_required", true)
        .Set("prompt", base::UTF16ToUTF8(prompt)).Set("label", label).Set("debug_document", approval->id)));
    return;
  }
  const auto* approved = args.FindString("_debug_document");
  if (!approved || *approved != approval->id) {
    std::move(done).Run(Error("The approved document changed; ask again for this page"));
    return;
  }
  if (tool == "debug_console") {
    auto reader = std::make_unique<DevToolsConsoleReader>(page, std::clamp(args.FindInt("limit").value_or(25), 1, 40));
    if (!reader->Start()) {
      std::move(done).Run(Error("Could not read the page console"));
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE, base::BindOnce(
        [](std::unique_ptr<DevToolsConsoleReader> reader, base::OnceCallback<void(base::Value)> done) {
          auto result = reader->Result();
          reader.reset();
          std::move(done).Run(std::move(result));
        }, std::move(reader), std::move(done)), base::Milliseconds(650));
    return;
  }
  if (tool == "debug_hard_reload") {
    page->GetController().Reload(content::ReloadType::BYPASSING_CACHE, true);
    Settle(page->GetWeakPtr(), base::Milliseconds(300), std::move(done));
    return;
  }
  args.Remove("_debug_document");
  args.Set("tool", tool);
  RunAssistantPageOperation(page, std::move(args), std::move(done));
}

// Everything the assistant does inside one page, visible or in the background.
// `confirmed` is native user consent, never a model-supplied argument.
void ActOnPage(content::WebContents* page, std::string tool, base::DictValue args, bool confirmed, base::OnceCallback<void(base::Value)> done) {
  auto dialog = OpenDialog(page);
  if (tool == "dialog") {
    if (!dialog) { std::move(done).Run(Error("No dialog is open on this page")); return; }
    const bool accept = args.FindBool("accept").value_or(false);
    if (accept && *dialog->FindString("kind") != "alert" && !confirmed) {
      std::string message;
      base::TruncateUTF8ToByteSize(*dialog->FindString("message"), 100, &message);
      std::move(done).Run(base::Value(base::DictValue().Set("confirmation_required", true).Set("label", "OK: " + message))); return;
    }
    const auto* text = args.FindString("text");
    const std::u16string answer = text ? base::UTF8ToUTF16(text->substr(0, 4000)) : std::u16string();
    PageDialogs::FromWebContents(page)->HandleJavaScriptDialog(page, accept, text ? &answer : nullptr);
    Settle(page->GetWeakPtr(), base::Milliseconds(300), std::move(done));
    return;
  }
  if (dialog && tool != "screenshot") {
    std::move(done).Run(base::Value(base::DictValue().Set("error", "The page shows a dialog; answer it with the dialog tool first")
        .Set("dialog", std::move(*dialog)))); return;
  }
  if (tool == "screenshot") {
    CaptureAssistantPage(page->GetWeakPtr(), std::move(done));
    return;
  }
  if (tool == "wait") {
    Settle(page->GetWeakPtr(), base::Milliseconds(std::clamp(args.FindInt("ms").value_or(1000), 0, 10000)), std::move(done)); return;
  }
  if (tool == "insert_text" || tool == "press_key") { KeyboardTool(page, tool, std::move(args), confirmed, std::move(done)); return; }
  if (tool == "mouse") { MouseTool(page, std::move(args), confirmed, std::move(done)); return; }
  if (tool == "adblock_inspect") { AdblockInspect(page, std::move(done)); return; }
  if (tool == "adblock_add_rules") { AdblockAddRules(page, std::move(args), confirmed, std::move(done)); return; }
  if (tool == "adblock_diagnose") { AdblockDiagnose(page, std::move(done)); return; }
  if (tool == "adblock_clear_preview") {
    if (auto* helper = ContentBlockingTabHelper::FromWebContents(page)) helper->Preview({}, base::DoNothing());
    std::move(done).Run(base::Value(base::DictValue().Set("ok", true))); return;
  }
  args.Set("tool", tool);
  args.Set("confirmed", confirmed);
  if (tool == "click") {
    RunAssistantPageOperation(page, std::move(args), base::BindOnce(
        [](base::WeakPtr<content::WebContents> page, base::OnceCallback<void(base::Value)> done, base::Value value) {
          const auto* result = value.GetIfDict();
          auto* view = page ? page->GetRenderWidgetHostView() : nullptr;
          if (const auto* point = result ? result->FindDict("point") : nullptr; point && view) {
            view->GetRenderWidgetHost()->Focus();
            ClickAssistantPageAt(page.get(), view->GetRenderWidgetHost(), point->FindDouble("x").value_or(0), point->FindDouble("y").value_or(0));
            Settle(page, base::Milliseconds(400), std::move(done)); return;
          }
          if (page && result && result->FindBool("ok").value_or(false)) { Settle(page, base::Milliseconds(400), std::move(done)); return; }
          std::move(done).Run(std::move(value));
        }, page->GetWeakPtr(), std::move(done)));
    return;
  }
  if (tool == "type") {
    const auto* raw = args.FindString("text");
    std::string text = raw ? *raw : std::string();
    RunAssistantPageOperation(page, std::move(args), base::BindOnce(
        [](base::WeakPtr<content::WebContents> page, std::string text, bool confirmed, base::OnceCallback<void(base::Value)> done, base::Value value) {
          // A rich editor: its content is selected; keyboard input replaces it.
          if (page && value.is_dict() && value.GetDict().FindBool("keyboard").value_or(false)) {
            if (text.empty()) KeyboardTool(page.get(), "press_key", base::DictValue().Set("key", "delete"), confirmed, std::move(done));
            else KeyboardTool(page.get(), "insert_text", base::DictValue().Set("text", text), confirmed, std::move(done));
            return;
          }
          std::move(done).Run(std::move(value));
        }, page->GetWeakPtr(), std::move(text), confirmed, std::move(done)));
    return;
  }
  if (tool == "read_page") {
    ReadAssistantPage(page, std::move(done));
    return;
  }
  RunAssistantPageOperation(page, std::move(args), std::move(done));
}
BackendProbe ProbeBackends(bool saved_key, std::string codex_path, std::string claude_path) {
  auto env = base::Environment::Create();
  BackendProbe probe;
  probe.openai = saved_key || !env->GetVar("OPENAI_API_KEY").value_or("").empty() ||
                 !env->GetVar("ZITTER_OPENAI_API_KEY").value_or("").empty();
  probe.codex = !assistant::NativeCommand("ZITTER_CODEX_PATH", "codex", codex_path).empty();
  probe.claude = !assistant::NativeCommand("ZITTER_CLAUDE_PATH", "claude", claude_path).empty();
  return probe;
}
}

WorkspaceWindow::AssistantBackgroundPage::AssistantBackgroundPage() = default;
WorkspaceWindow::AssistantBackgroundPage::AssistantBackgroundPage(AssistantBackgroundPage&&) = default;
WorkspaceWindow::AssistantBackgroundPage& WorkspaceWindow::AssistantBackgroundPage::operator=(AssistantBackgroundPage&&) = default;
WorkspaceWindow::AssistantBackgroundPage::~AssistantBackgroundPage() = default;

void WorkspaceWindow::SetAssistantSettings(AssistantSettings settings) {
  if (is_private()) return;
  const bool changed = assistant_settings_.encrypted_api_key != settings.encrypted_api_key ||
      assistant_settings_.codex_path != settings.codex_path || assistant_settings_.claude_path != settings.claude_path;
  if (changed) {
    if (assistant_settings_.encrypted_api_key != settings.encrypted_api_key) settings.available[0] = false;
    if (assistant_settings_.codex_path != settings.codex_path) settings.available[1] = false;
    if (assistant_settings_.claude_path != settings.claude_path) settings.available[2] = false;
    ++assistant_connection_generation_;
    assistant_checks_pending_.clear();
    assistant_backend_status_.clear();
    if (assistant_) assistant_->CredentialsChanged();
  }
  assistant_settings_ = std::move(settings);
  if (assistant_settings_.models.empty()) assistant_settings_.models = AssistantSettings().models;
  SaveConfiguration();
  shell_->SetAssistantAllowed(assistant_settings_.enabled);
  if (assistant_) assistant_->SettingsChanged();
  if (!assistant_settings_.enabled) {
    std::vector<std::string> keys;
    for (const auto& [key, page] : assistant_background_) keys.push_back(key);
    for (const auto& key : keys) AssistantDiscardBackground(key);
  }
  shell_->RefreshAssistantPreferences();
}
base::DictValue WorkspaceWindow::AssistantSettingsForShell() {
  if (is_private() || !configuration_ready_ || configuration_invalid_)
    return base::DictValue().Set("enabled", false);
  if (!assistant_status_pending_ && assistant_backend_status_.empty()) {
    assistant_status_pending_ = true;
    base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&ProbeBackends, !assistant_settings_.encrypted_api_key.empty(),
                       assistant_settings_.codex_path, assistant_settings_.claude_path), base::BindOnce([](base::WeakPtr<WorkspaceWindow> window,uint64_t generation, BackendProbe probe) {
          if (!window) return;
          window->assistant_status_pending_ = false;
          if(generation!=window->assistant_connection_generation_){window->shell_->RefreshAssistantPreferences();return;}
          auto row = [](int tone, std::u16string state, std::u16string detail) {
            return base::DictValue().Set("tone", tone).Set("state", base::UTF16ToUTF8(state)).Set("detail", base::UTF16ToUTF8(detail));
          };
          auto& settings = window->assistant_settings_;
          const std::array available = {probe.openai, probe.codex, probe.claude, settings.ollama_configured};
          base::ListValue status;
          status.Append(probe.openai ? row(0,T(u"Do sprawdzenia"),T(u"Klucz API jest skonfigurowany. Sprawdź połączenie.")) : row(2,T(u"Brak klucza API"),T(u"Dodaj klucz, aby połączyć konto OpenAI.")));
          status.Append(probe.codex ? row(0,T(u"Do sprawdzenia"),T(u"Wykryto Codex. Test sprawdzi logowanie i odpowiedź modelu.")) : row(2,T(u"Wymaga konfiguracji"),T(u"Zainstaluj Codex i zaloguj się na swoje konto.")));
          status.Append(probe.claude ? row(0,T(u"Do sprawdzenia"),T(u"Wykryto Claude Code. Test sprawdzi logowanie i odpowiedź modelu.")) : row(2,T(u"Wymaga konfiguracji"),T(u"Zainstaluj Claude Code i zaloguj się na swoje konto.")));
          status.Append(row(0,T(u"Do sprawdzenia"),T(u"Uruchom Ollama i pobierz wybrany model, a potem sprawdź połączenie.")));
          for(size_t i=0;i<status.size();++i){
            if(i<window->assistant_backend_status_.size()&&window->assistant_backend_status_[i].GetDict().FindBool("checked").value_or(false))continue;
            settings.available[i] = available[i];
            if(i<window->assistant_backend_status_.size())window->assistant_backend_status_[i]=std::move(status[i]);
            else window->assistant_backend_status_.Append(std::move(status[i]));
          }
          if (window->assistant_) window->assistant_->BackendAvailabilityChanged();
          window->shell_->RefreshAssistantPreferences();
        }, weak_factory_.GetWeakPtr(),assistant_connection_generation_));
  }
  base::ListValue models;
  for (const auto& model : assistant_settings_.models)
    if (assistant_settings_.BackendEnabled(model.provider) && assistant_settings_.BackendAvailable(model.provider))
      models.Append(base::DictValue().Set("provider", model.provider).Set("id", model.id).Set("visible", model.visible)
        .Set("label", base::UTF16ToUTF8(AssistantProviderLabel(model.provider))));
  base::ListValue backends;
  for (size_t i = 0; i < kAssistantProviders.size(); ++i)
    backends.Append(base::DictValue().Set("provider", kAssistantProviders[i]).Set("label", base::UTF16ToUTF8(AssistantProviderLabel(kAssistantProviders[i])))
        .Set("enabled", assistant_settings_.backends[i]));
  return base::DictValue().Set("enabled", assistant_settings_.enabled && !configuration_invalid_).Set("backends", std::move(backends))
      .Set("codex_path", assistant_settings_.codex_path).Set("claude_path", assistant_settings_.claude_path)
      .Set("has_saved_key",!assistant_settings_.encrypted_api_key.empty()).Set("status", assistant_backend_status_.Clone()).Set("models", std::move(models)).Set("default_model", assistant_settings_.default_model);
}
// Credentials are decrypted only for a provider operation, never exposed to settings,
// transcripts, model prompts, command-line arguments or diagnostic output.
void WorkspaceWindow::WithAssistantApiKey(base::OnceCallback<void(std::string)> callback){
  const auto encrypted=assistant_settings_.encrypted_api_key;
  if(encrypted.empty()){std::move(callback).Run("");return;}
  g_browser_process->os_crypt_async()->GetInstance(base::BindOnce(
      [](base::OnceCallback<void(std::string)> callback,std::string encrypted,scoped_refptr<os_crypt_async::Encryptor> encryptor){
        std::move(callback).Run(DecryptAssistantApiKey(encrypted, *encryptor));
      },std::move(callback),encrypted));
}
void WorkspaceWindow::CheckAssistantConnection(std::string provider, bool discover) {
  const auto found = std::ranges::find(kAssistantProviders, provider);
  if (found == kAssistantProviders.end() || (discover && provider != "ollama") ||
      !assistant_checks_pending_.insert(provider).second) return;
  const size_t index = found - kAssistantProviders.begin();
  std::string model;
  for (const auto& item : assistant_settings_.models) {
    if (item.provider != provider || !item.visible) continue;
    if (model.empty() || assistant_settings_.default_model == provider + "/" + item.id) model = item.id;
  }
  while (assistant_backend_status_.size() < kAssistantProviders.size())
    assistant_backend_status_.Append(base::DictValue());
  if (model.empty() && !discover) {
    assistant_checks_pending_.erase(provider);
    assistant_backend_status_[index] = base::Value(base::DictValue().Set("tone", 2).Set("checked", true)
        .Set("state", base::UTF16ToUTF8(T(u"Wymaga konfiguracji")))
        .Set("detail", base::UTF16ToUTF8(T(u"Dodaj lub wykryj model i spróbuj ponownie."))));
    shell_->RefreshAssistantPreferences();
    return;
  }
  assistant_backend_status_[index] = base::Value(base::DictValue().Set("tone", 0).Set("checked", true).Set("checking", true)
      .Set("state", base::UTF16ToUTF8(T(u"Sprawdzam połączenie…")))
      .Set("detail", base::UTF16ToUTF8(discover ? T(u"Szukam modeli Ollama…") : T(u"Czekam na odpowiedź modelu"))));
  shell_->RefreshAssistantPreferences();
  base::EnvironmentMap environment;
  assistant_settings_.ApplyCliPaths(environment);
  WithAssistantApiKey(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string provider,
      std::string model, bool discover, size_t index, uint64_t generation, base::EnvironmentMap environment,
      std::string key) {
    if (!window || window->assistant_connection_generation_ != generation) return;
    base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE,
        {base::MayBlock(), base::WithBaseSyncPrimitives(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&CheckAssistantProvider, provider, model, discover, std::move(environment), std::move(key)),
        base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string provider, std::string model,
            size_t index, uint64_t generation, base::DictValue reply) {
          if (!window || window->assistant_connection_generation_ != generation) return;
          window->assistant_checks_pending_.erase(provider);
          auto settings = window->assistant_settings_;
          bool ok = reply.FindBool("ok").value_or(false);
          std::u16string detail;
          if (const auto* names = reply.FindList("models")) {
            ok = !names->empty();
            for (const auto& value : *names) {
              if (!value.is_string() || settings.models.size() >= 32) break;
              const auto& id = value.GetString();
              if (std::ranges::none_of(settings.models, [&](const auto& existing) {
                    return existing.provider == provider && existing.id == id;
                  })) settings.models.push_back({provider, id});
            }
            if (ok) settings.ollama_configured = true;
            detail = ok ? TF(u"Wykryte modele Ollama: {}", {base::NumberToString16(names->size())})
                        : T(u"Ollama działa. Pobierz model albo dodaj jego nazwę ręcznie.");
          } else if (ok) {
            if (provider == "ollama") settings.ollama_configured = true;
            detail = TF(u"Model {} odpowiedział poprawnie", {base::UTF8ToUTF16(model)});
          } else {
            const auto* error = reply.FindString("error");
            detail = AssistantConnectionError(error ? *error : "");
          }
          if (ok) settings.available[index] = true;
          window->assistant_backend_status_[index] = base::Value(base::DictValue().Set("tone", ok ? 1 : 2).Set("checked", true)
              .Set("state", base::UTF16ToUTF8(ok ? T(u"Połączono") : T(u"Nie udało się połączyć")))
              .Set("detail", base::UTF16ToUTF8(detail)));
          if (ok) window->SetAssistantSettings(std::move(settings));
          else window->shell_->RefreshAssistantPreferences();
        }, window, provider, model, index, generation));
  }, weak_factory_.GetWeakPtr(), provider, model, discover, index, assistant_connection_generation_, std::move(environment)));
}
// Settings rows send small commands, validated here.
void WorkspaceWindow::WriteAssistantSettings(base::DictValue value) {
  if (is_private()) return;
  if (configuration_invalid_ || !assistant_settings_.enabled) return;
  auto settings = assistant_settings_;
  const auto* command = value.FindString("command");
  const auto* provider = value.FindString("provider");
  const auto* id = value.FindString("id");
  auto find = [&]() -> AssistantModel* {
    for (auto& model : settings.models) if (provider && id && model.provider == *provider && model.id == *id) return &model;
    return nullptr;
  };
  if (!command) return;
  if (*command == "check" && provider) { CheckAssistantConnection(*provider); return; }
  if (*command == "discover" && provider) { CheckAssistantConnection(*provider, true); return; }
  if (*command == "cli_path" && provider && (*provider == "codex" || *provider == "claude")) {
    const auto* supplied = value.FindString("path");
    if (!supplied) return;
    const std::string path(base::TrimWhitespaceASCII(*supplied, base::TRIM_ALL));
    if (!path.empty() && (path.size() > 4096 || !base::FilePath::FromUTF8Unsafe(path).IsAbsolute())) return;
    (*provider == "codex" ? settings.codex_path : settings.claude_path) = path;
    SetAssistantSettings(std::move(settings));
    return;
  }
  if(*command=="save_key"){
    const auto* supplied=value.FindString("key");if(!supplied)return;
    const std::string key(base::TrimWhitespaceASCII(*supplied,base::TRIM_ALL));
    if(key.empty()||key.size()>4096||key.find_first_of("\r\n")!=std::string::npos)return;
    g_browser_process->os_crypt_async()->GetInstance(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window,std::string key,scoped_refptr<os_crypt_async::Encryptor> encryptor){
      if(!window||!window->assistant_settings_.enabled)return;
      std::string encrypted;
      if(!encryptor->IsEncryptionAvailable()||!encryptor->EncryptString(key,&encrypted)){
        window->shell_->ShowToast(T(u"Nie udało się bezpiecznie zapisać klucza API"),{}, {});return;
      }
      auto settings=window->assistant_settings_;settings.encrypted_api_key=base::Base64Encode(encrypted);
      window->SetAssistantSettings(std::move(settings));
    },weak_factory_.GetWeakPtr(),key));return;
  }
  if(*command=="remove_key"){
    settings.encrypted_api_key.clear();
  } else if (*command == "backend" && provider) {
    for (size_t i = 0; i < kAssistantProviders.size(); ++i)
      if (*provider == kAssistantProviders[i]) settings.backends[i] = value.FindBool("enabled").value_or(true);
  } else if (*command == "visible") {
    auto* model = find(); if (!model) return;
    model->visible = value.FindBool("visible").value_or(true);
  } else if (*command == "default") {
    auto* model = find(); if (!model) return;
    model->visible = true; settings.default_model = model->provider + "/" + model->id;
  } else if (*command == "remove") {
    if (!find() || settings.models.size() <= 1) return;
    std::erase_if(settings.models, [&](const auto& model) { return model.provider == *provider && model.id == *id; });
    if (settings.default_model == *provider + "/" + *id) settings.default_model = settings.models.front().provider + "/" + settings.models.front().id;
  } else if (*command == "add") {
    if (!provider || !id || std::ranges::find(kAssistantProviders, *provider) == kAssistantProviders.end()) return;
    const auto trimmed = std::string(base::TrimWhitespaceASCII(*id, base::TRIM_ALL));
    if (trimmed.empty() || trimmed.size() > 120 || settings.models.size() >= 32) return;
    for (const auto& model : settings.models) if (model.provider == *provider && model.id == trimmed) return;
    settings.models.push_back({*provider, trimmed, true});
    if (*provider == "ollama") {
      settings.ollama_configured = true;
      settings.available[3] = true;
    }
  } else return;
  SetAssistantSettings(std::move(settings));
}

views::WebView* WorkspaceWindow::AssistantTabView(std::string id) {
  if (id.starts_with("@tab:")) id.erase(0, 5);
  if (id == "@current" || id == "current") {
    for (const auto& host : hosts_) if (host.workspace_id == active_id()) return host.view;
    return nullptr;
  }
  const auto found = assistant_targets_.find(id);
  return found != assistant_targets_.end() ? static_cast<views::WebView*>(found->second->view()) : nullptr;
}
bool WorkspaceWindow::AssistantStartBackground(const std::string& key, const std::string& from_tab, const GURL& url) {
  auto* profile = AssistantProfile();
  if (!profile || profile->IsOffTheRecord() || key.empty() || assistant_background_.contains(key)) return false;
  AssistantBackgroundPage page;
  page.profile_id = AssistantProfileId(); page.workspace_id = active_id();
  if (!from_tab.empty()) {
    // Move the visible tab itself, keeping forms and history, out of the user's view.
    auto* view = AssistantTabView(from_tab);
    Host* source = nullptr;
    for (auto& host : hosts_) if (host.profile_id == page.profile_id && std::ranges::find(host.tabs, view) != host.tabs.end()) source = &host;
    if (!view || !source) return false;
    for (const auto& [target, tracker] : assistant_targets_) if (tracker->view() == view) page.alias = target;
    if (source->tabs.size() < 2) {
      // The space's only tab stays; the background gets a copy of its page.
      const GURL copy = TabUrl(*source, view);
      page.view = CreatePage(profile);
      auto* contents = EnsurePage(page.view);
      page.view->SetBoundsRect(gfx::Rect(shell_->content_host()->size()));
      contents->Resize(gfx::Rect(shell_->content_host()->size()));
      page.rendering = contents->IncrementCapturerCount(gfx::Size(), /*stay_hidden=*/true, /*stay_awake=*/true, /*is_activity=*/false);
      contents->UpdateWebContentsVisibility(content::Visibility::HIDDEN);
      if (copy.SchemeIsHTTPOrHTTPS()) contents->GetController().LoadURL(copy, content::Referrer(), ui::PAGE_TRANSITION_TYPED, {});
      assistant_background_[key] = std::move(page);
      UpdateShell();
      return true;
    }
    source->popup_tabs.erase(view);
    std::erase_if(source->pin_tabs, [&](const auto& item) { return item.second == view; });
    std::erase(source->tabs, view);
    auto* survivor = LeaveSplit(*source, view);
    source->view = survivor ? survivor : source->tabs[std::min(source->active_tab, source->tabs.size() - 1)].get();
    source->active_tab = std::find(source->tabs.begin(), source->tabs.end(), source->view) - source->tabs.begin();
    page.workspace_id = source->workspace_id;
    page.view = view;
    if (source->workspace_id == active_id()) SelectTab(source->active_tab);
    view->SetVisible(false);
  } else {
    page.view = CreatePage(profile);
  }
  auto* contents = EnsurePage(page.view);
  // Hidden, but sized like the page area so layout and visibility checks hold.
  page.view->SetBoundsRect(gfx::Rect(shell_->content_host()->size()));
  contents->Resize(gfx::Rect(shell_->content_host()->size()));
  page.rendering = contents->IncrementCapturerCount(gfx::Size(), /*stay_hidden=*/true, /*stay_awake=*/true, /*is_activity=*/false);
  contents->UpdateWebContentsVisibility(content::Visibility::HIDDEN);
  if (from_tab.empty() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() && !url.has_password())
    contents->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_TYPED, {});
  assistant_background_[key] = std::move(page);
  UpdateShell();
  return true;
}
bool WorkspaceWindow::AssistantTakeOver(const std::string& key) {
  auto found = assistant_background_.find(key);
  if (found == assistant_background_.end()) return false;
  auto page = std::move(found->second); assistant_background_.erase(found);
  page.rendering.RunAndReset();
  const auto target = model_.Find(page.workspace_id) ? page.workspace_id : active_id();
  if (!Select(target)) { assistant_background_[key] = std::move(page); return false; }
  for (auto& host : hosts_) if (host.workspace_id == target && host.profile_id == page.profile_id) {
    host.tabs.push_back(page.view.get());
    SelectTab(host.tabs.size() - 1);
    return true;
  }
  // The space now belongs to another profile: never show the page there.
  shell_->content_host()->RemoveChildViewT(page.view.get());
  return false;
}
void WorkspaceWindow::AssistantDiscardBackground(const std::string& key) {
  auto found = assistant_background_.find(key);
  if (found == assistant_background_.end()) return;
  auto page = std::move(found->second); assistant_background_.erase(found);
  page.rendering.RunAndReset();
  if (page.view && page.view->parent()) page.view->parent()->RemoveChildViewT(page.view.get());
  UpdateShell();
}
content::WebContents* WorkspaceWindow::assistant_background_for_testing(const std::string& key) const {
  const auto found = assistant_background_.find(key);
  return found == assistant_background_.end() ? nullptr : found->second.view->web_contents();
}
void WorkspaceWindow::AssistantElementBounds(std::string tab, std::string element,
    base::OnceCallback<void(std::optional<gfx::Rect>)> done) {
  auto* view = AssistantTabView(std::move(tab));
  auto* page = view && view->GetVisible() ? view->web_contents() : nullptr;
  if (!page) { std::move(done).Run(std::nullopt); return; }
  const double zoom = blink::ZoomLevelToZoomFactor(content::HostZoomMap::GetZoomLevel(page));
  auto tracker = std::make_unique<views::ViewTracker>(); tracker->SetView(view);
  RunAssistantPageOperation(page, base::DictValue().Set("tool", "rect").Set("element", element), base::BindOnce(
      [](base::OnceCallback<void(std::optional<gfx::Rect>)> done, std::unique_ptr<views::ViewTracker> tracker, double zoom, base::Value value) {
        const auto* dict = value.GetIfDict();
        if (!dict || !tracker->view() || !dict->FindDouble("width")) { std::move(done).Run(std::nullopt); return; }
        gfx::Rect rect(std::lround(dict->FindDouble("x").value_or(0) * zoom), std::lround(dict->FindDouble("y").value_or(0) * zoom),
                       std::lround(dict->FindDouble("width").value_or(0) * zoom), std::lround(dict->FindDouble("height").value_or(0) * zoom));
        rect.Intersect(tracker->view()->GetLocalBounds());
        if (rect.IsEmpty()) { std::move(done).Run(std::nullopt); return; }
        views::View::ConvertRectToScreen(tracker->view(), &rect);
        std::move(done).Run(rect);
      }, std::move(done), std::move(tracker), zoom));
}

Profile* WorkspaceWindow::AssistantProfile() const {
  return model_.active() ? ProfileFor(model_.active()->profile_id) : nullptr;
}
std::string WorkspaceWindow::AssistantProfileId() const {
  return model_.active() ? model_.active()->profile_id : std::string();
}
void WorkspaceWindow::AskAssistant(std::u16string prompt) {
  if (!assistant_settings_.enabled) return;
  ShowAssistant();
  if (assistant_) assistant_->Ask(std::move(prompt));
}
void WorkspaceWindow::OpenAdBlockingRules() {
  auto* profile = AssistantProfile();
  if (!profile || profile->IsOffTheRecord() || !shell_ || !shell_->general_widget()) return;
  if (!ad_blocking_rules_panel_) ad_blocking_rules_panel_ = std::make_unique<AdBlockingRulesPanel>();
  const auto id = AssistantProfileId();
  using ResolveProfile = base::RepeatingCallback<Profile*()>;
  const auto resolve = base::BindRepeating(
      [](base::WeakPtr<WorkspaceWindow> window, const std::string& id) {
        return window ? window->ProfileFor(id) : nullptr;
      }, weak_factory_.GetWeakPtr(), id);
  AdBlockingRulesPanel::Callbacks callbacks;
  callbacks.read = base::BindRepeating([](const ResolveProfile& resolve) {
    return ContentBlocking::Rules(resolve.Run());
  }, resolve);
  callbacks.add = base::BindRepeating([](const ResolveProfile& resolve, std::string rule) {
    std::string error;
    if (!ContentBlocking::AddRules(resolve.Run(), {rule}, {}, &error) && error.empty())
      error = base::UTF16ToUTF8(T(u"Nie udało się zapisać ustawień."));
    return error;
  }, resolve);
  callbacks.remove = base::BindRepeating([](const ResolveProfile& resolve, std::string rule) {
    ContentBlocking::RemoveRule(resolve.Run(), rule);
  }, resolve);
  callbacks.save = base::BindRepeating([](const ResolveProfile& resolve, std::string text) {
    return ContentBlocking::SetRulesText(resolve.Run(), text);
  }, resolve);
  callbacks.stale = base::BindRepeating([](const ResolveProfile& resolve, std::string rule) {
    return ContentBlocking::RuleStale(resolve.Run(), rule);
  }, resolve);
  callbacks.repair = base::BindRepeating([](WorkspaceWindow* window, std::string rule) {
    // The same request as the shield's repair, for one rule.
    window->AskAssistant(TF(u"Moja reguła blokowania na {} od kilku wizyt nic nie ukrywa: {} Znajdź tę reklamę ponownie i zapisz nową regułę, zastępując nią starą (replaces).",
                            {base::UTF8ToUTF16(ContentBlocking::RuleSite(rule)), base::UTF8ToUTF16(rule)}));
  }, base::Unretained(this));
  ad_blocking_rules_panel_->Show(id, shell_->general_widget(), std::move(callbacks));
}
base::DictValue WorkspaceWindow::ReadAdBlocking() {
  auto* profile = AssistantProfile();
  auto& blocking = ContentBlocking::Get();
  base::ListValue lists;
  for (const auto& list : blocking.lists())
    lists.Append(base::DictValue().Set("id", list.id).Set("name", list.name).Set("rules", static_cast<int>(list.rules))
        .Set("optional", list.optional).Set("builtin", list.builtin).Set("enabled", ContentBlocking::ListEnabled(profile, list.id))
        .Set("updated", list.updated.is_null() || list.builtin ? std::string() : base::UTF16ToUTF8(base::TimeFormatShortDate(list.updated))));
  base::ListValue sites, rules, rule_sites;
  for (const auto& site : ContentBlocking::AllowedSites(profile)) sites.Append(site);
  // The rules summary: how many rules each site has, and whether one stopped working.
  std::vector<std::string> order;
  std::map<std::string, std::pair<int, bool>> per_site;
  for (const auto& rule : ContentBlocking::Rules(profile)) {
    rules.Append(rule);
    if (rule.starts_with("!")) continue;
    const auto site = ContentBlocking::RuleSite(rule);
    if (!per_site.contains(site)) order.push_back(site);
    auto& [count, stale] = per_site[site];
    ++count;
    stale |= ContentBlocking::RuleStale(profile, rule);
  }
  for (const auto& site : order)
    rule_sites.Append(base::DictValue().Set("site", site).Set("count", per_site[site].first).Set("stale", per_site[site].second));
  base::Time updated;
  for (const auto& list : blocking.lists()) if (!list.builtin && list.updated > updated) updated = list.updated;
  return base::DictValue().Set("lists", std::move(lists)).Set("updating", blocking.updating())
      .Set("error", blocking.last_error()).Set("sites", std::move(sites)).Set("rules", std::move(rules))
      .Set("rule_sites", std::move(rule_sites))
      .Set("updated", updated.is_null() ? std::string() : base::UTF16ToUTF8(base::TimeFormatShortDate(updated)))
      .Set("available", profile && !profile->IsOffTheRecord());
}
std::string WorkspaceWindow::WriteAdBlocking(base::DictValue command) {
  auto* profile = AssistantProfile();
  const auto* name = command.FindString("command");
  if (!profile || profile->IsOffTheRecord() || !name) return {};
  if (*name == "update") { ContentBlocking::Get().UpdateLists(); return {}; }
  if (const auto* list = command.FindString("list"); list && *name == "list") {
    ContentBlocking::SetListEnabled(profile, *list, command.FindBool("enabled").value_or(false));
    return {};
  }
  if (const auto* site = command.FindString("site"); site && *name == "site_remove") { ContentBlocking::SetSiteAllowed(profile, *site, false); return {}; }
  const auto* rule = command.FindString("rule");
  if (!rule) return {};
  if (*name == "rule_remove") { ContentBlocking::RemoveRule(profile, *rule); return {}; }
  if (*name == "rule_add") {
    if (rule->size() > 1000) return base::UTF16ToUTF8(T(u"Reguła jest za długa."));
    std::string error;
    if (!ContentBlocking::AddRules(profile, {*rule}, {}, &error))
      return base::UTF16ToUTF8(TF(u"Tej reguły nie da się użyć: {}", {base::UTF8ToUTF16(ContentBlocking::RuleError(*rule))}));
  }
  return {};
}
void WorkspaceWindow::NewAssistantConversation() {
  const bool existed = assistant_ != nullptr;
  ShowAssistant();
  if (existed && assistant_settings_.enabled) assistant_->StartNewConversation();
}
void WorkspaceWindow::ShowAssistant(bool hover) {
  EnsureAssistant();
  if (assistant_ && assistant_settings_.enabled) assistant_->Show(hover);
}

base::DictValue WorkspaceWindow::AssistantContext(const AssistantPermissions& permissions) {
  base::DictValue context;
  if (!permissions.tabs || !AssistantProfile() || AssistantProfile()->IsOffTheRecord()) return context;
  std::erase_if(assistant_targets_, [](const auto& entry) { return !entry.second->view(); });
  base::ListValue tabs, pins;
  const auto profile = AssistantProfileId();
  for (const auto& host : hosts_) {
    if (host.profile_id != profile) continue;
    for (size_t i = 0; i < host.tabs.size(); ++i) {
      auto* view = host.tabs[i].get();
      const auto url = SafeURL(TabUrl(host, view));
      if (url.empty()) continue;
      const auto id = AssistantTargetFor(view);
      tabs.Append(base::DictValue().Set("id", id).Set("mention", "@tab:" + id).Set("space", host.workspace_id)
          .Set("title", base::UTF16ToUTF8(TabTitle(host, view))).Set("url", url)
          .Set("current", host.workspace_id == active_id() && host.active_tab == i));
    }
  }
  for (const auto& workspace : model_.workspaces()) {
    if (workspace.profile_id != profile) continue;
    for (const auto& pin : pinned_items(workspace.id)) {
      if (pin.folder || SafeURL(pin.url).empty()) continue;
      pins.Append(base::DictValue().Set("id", pin.id).Set("mention", "@pin:" + pin.id)
          .Set("title", pin.title).Set("url", SafeURL(pin.url)));
    }
  }
  base::ListValue spaces;
  for (const auto& workspace : model_.workspaces())
    if (workspace.profile_id == profile)
      spaces.Append(base::DictValue().Set("id", workspace.id).Set("name", workspace.name).Set("current", workspace.id == active_id()));
  context.Set("tabs", std::move(tabs)); context.Set("pins", std::move(pins)); context.Set("spaces", std::move(spaces));
  return context;
}
void WorkspaceWindow::LoadAssistantSessions(base::OnceCallback<void(std::optional<base::DictValue>)> done) {
  if (is_private()) { std::move(done).Run(std::nullopt); return; }
  // The writer's sequence: a load never reads a half-written file.
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce([](base::FilePath path) -> std::optional<base::DictValue> {
    std::string json;
    if (!base::ReadFileToStringWithMaxSize(path, &json, 64 * 1024 * 1024)) return std::nullopt;
    return base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  }, assistant_sessions_path()), std::move(done));
}
void WorkspaceWindow::SaveAssistantSessions(base::DictValue sessions) {
  if (is_private()) return;
  std::string json;
  if (!base::JSONWriter::Write(sessions, &json)) return;
  config_runner_->PostTask(FROM_HERE, base::BindOnce([](base::FilePath path, std::string json) {
    base::ImportantFileWriter::WriteFileAtomically(path, json);
  }, assistant_sessions_path(), std::move(json)));
}
WorkspaceShell* WorkspaceWindow::shell() const {
  return shell_.get();
}

const AssistantSettings& WorkspaceWindow::assistant_settings() const {
  return assistant_settings_;
}

bool WorkspaceWindow::AssistantHasBackground(const std::string& key) const {
  return assistant_background_.contains(key);
}

content::BrowserContext* WorkspaceWindow::AssistantBrowserContext() const {
  return AssistantProfile();
}

void WorkspaceWindow::EnsureAssistant() {
  if (is_private()) return;
  if (assistant_ || !assistant_settings_.enabled) return;
  AssistantSettingsForShell();
  auto panel = std::make_unique<AssistantPanel>(static_cast<AssistantHost*>(this));
  assistant_ = panel.get();
  shell_->SetAssistantView(panel.release());
}
void WorkspaceWindow::AssistantStateChanged() {
  if (!extensions_bar_) return;
  ExtensionsBar::AssistantState state;
  state.enabled = assistant_settings_.enabled;
  state.open = assistant_ && assistant_->GetVisible();
  state.working = assistant_ && assistant_->working();
  state.unread = assistant_ ? assistant_->unread() : 0;
  extensions_bar_->SetAssistantState(state);
}
void WorkspaceWindow::ToggleAssistantFromBar() {
  if (assistant_ && assistant_->GetVisible()) { assistant_->Collapse(); if (assistant_->GetVisible() && !assistant_->pinned()) { assistant_->SetVisible(false); shell_->LayoutAssistant(false); } }
  else ShowAssistant();
  AssistantStateChanged();
}
std::string WorkspaceWindow::AssistantTargetFor(views::View* view) {
  for (const auto& [key, target] : assistant_targets_) if (target->view() == view) return key;
  const auto id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto target = std::make_unique<views::ViewTracker>(); target->SetView(view);
  assistant_targets_[id] = std::move(target);
  return id;
}

void WorkspaceWindow::AssistantTool(std::string tool, base::DictValue args, AssistantPermissions permissions,
                                    base::OnceCallback<void(base::Value)> done, bool user_confirmed, std::string background) {
  auto fail = [&](std::string text) { std::move(done).Run(Error(std::move(text))); };
  auto* profile = AssistantProfile();
  if (!profile || profile->IsOffTheRecord()) { fail("Assistant is unavailable in private profiles"); return; }
  if (!assistant_settings_.enabled) { fail("Assistant is turned off in Settings"); return; }
  const AssistantBackgroundPage* hidden = nullptr;
  if (!background.empty()) {
    const auto found = assistant_background_.find(background);
    if (found == assistant_background_.end() || found->second.profile_id != AssistantProfileId()) { fail("Background page expired or belongs to another profile"); return; }
    hidden = &found->second;
  }
  if (tool == "list_tabs") {
    if (!permissions.tabs) { fail("Tab discovery permission is disabled"); return; }
    std::move(done).Run(base::Value(AssistantContext(permissions))); return;
  }
  if (tool == "history") {
    if (!permissions.history) { fail("History permission is disabled"); return; }
    const auto* query = args.FindString("query");
    if (!query || query->empty() || query->size() > 600) { fail("A history query is required"); return; }
    auto* service = HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS);
    if (!service) { fail("History is unavailable"); return; }
    history::QueryOptions options; options.max_count = 30;
    service->QueryHistory(base::UTF8ToUTF16(*query), options, base::BindOnce(
        [](base::OnceCallback<void(base::Value)> done, history::QueryResults rows) {
          base::ListValue entries;
          for (const auto& row : rows) {
            const auto url = SafeURL(row.url()); if (url.empty()) continue;
            entries.Append(base::DictValue().Set("title", base::UTF16ToUTF8(row.title())).Set("url", url));
          }
          std::move(done).Run(base::Value(std::move(entries)));
        }, std::move(done)), &history_tasks_);
    return;
  }
  if (tool == "browser_command") {
    if (!permissions.actions) { fail("Browser actions permission is disabled"); return; }
    const auto* id = args.FindString("id");
    static constexpr auto commands = std::to_array<std::string_view>({"new_tab", "split", "close_split", "reload", "back", "forward", "find",
        "settings", "downloads", "history", "timeline", "save_snapshot", "reopen", "sidebar"});
    if (!id || std::ranges::find(commands, *id) == commands.end()) { fail("Unknown or protected browser command"); return; }
    if (*id == "save_snapshot") { std::move(done).Run(base::Value(base::DictValue().Set("ok", SaveSnapshot("AI assistant")))); return; }
    std::move(done).Run(base::Value(base::DictValue().Set("ok", RunShortcut(*id)))); return;
  }
  if (tool == "downloads") {
    if (!permissions.history) { fail("Downloads need the history permission"); return; }
    content::DownloadManager::DownloadVector items;
    if (auto* manager = profile->GetDownloadManager()) manager->GetAllDownloads(&items);
    std::ranges::sort(items, [](const auto& a, const auto& b) { return a->GetStartTime() > b->GetStartTime(); });
    base::ListValue list;
    for (const auto& item : items) {
      if (list.size() >= 25) break;
      const auto state = item->GetState();
      list.Append(base::DictValue().Set("file", item->GetTargetFilePath().BaseName().AsUTF8Unsafe()).Set("url", SafeURL(item->GetURL()))
          .Set("state", state == download::DownloadItem::COMPLETE ? "complete" : state == download::DownloadItem::IN_PROGRESS ? "in_progress"
                      : state == download::DownloadItem::CANCELLED ? "cancelled" : "interrupted")
          .Set("percent", item->PercentComplete()).Set("bytes", static_cast<double>(item->GetTotalBytes())));
    }
    std::move(done).Run(base::Value(std::move(list))); return;
  }
  if (tool == "create_space" || tool == "create_folder") {
    if (!permissions.actions) { fail("Browser actions permission is disabled"); return; }
    const auto* raw = args.FindString("name");
    const std::string name = raw ? std::string(base::TrimWhitespaceASCII(*raw, base::TRIM_ALL)) : std::string();
    if (name.empty() || name.size() > 120) { fail("A name of up to 120 characters is required"); return; }
    if (tool == "create_folder") { std::move(done).Run(base::Value(base::DictValue().Set("ok", CreatePinnedFolder(active_id(), {}, name)))); return; }
    if (!AddWorkspace(name, AssistantProfileId())) { fail("The space could not be created now"); return; }
    std::string id;
    for (const auto& workspace : model_.workspaces()) if (workspace.name == name && workspace.profile_id == AssistantProfileId()) id = workspace.id;
    std::move(done).Run(base::Value(base::DictValue().Set("ok", true).Set("space", id))); return;
  }
  if (tool == "move_tab") {
    if (!permissions.actions || !permissions.tabs) { fail("Moving tabs needs the tab and action permissions"); return; }
    const auto* tab = args.FindString("tab"); const auto* space = args.FindString("space");
    auto* view = tab ? AssistantTabView(*tab) : nullptr;
    if (!view || !space) { fail("A listed tab and a space are required"); return; }
    std::string target;
    for (const auto& workspace : model_.workspaces())
      if (workspace.profile_id == AssistantProfileId() && (workspace.id == *space || base::EqualsCaseInsensitiveASCII(workspace.name, *space))) target = workspace.id;
    if (target.empty()) { fail("No such space in this profile"); return; }
    if (!MoveTabToSpace(EnsurePage(view), target, std::numeric_limits<size_t>::max())) {
      fail("The tab could not move there: the space must be awake and the tab cannot be its space's only tab"); return;
    }
    std::move(done).Run(base::Value(base::DictValue().Set("ok", true))); return;
  }
  if (tool == "visit") {
    // Any page, in an invisible tab of this profile (same logins), read once it
    // has loaded; the user's tabs stay untouched. Then usable as @reader.
    if (!permissions.pages || !permissions.actions) { fail("Visiting pages needs the page and action permissions"); return; }
    std::vector<GURL> urls;
    if (const auto* raw = args.FindString("url")) urls.emplace_back(*raw);
    if (const auto* list = args.FindList("urls")) for (const auto& item : *list) if (item.is_string()) urls.emplace_back(item.GetString());
    if (urls.empty() || urls.size() > kReaders) { fail("Give url, or urls with up to 6 addresses"); return; }
    for (const auto& url : urls)
      if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password()) { fail("Only HTTP(S) URLs without credentials are allowed"); return; }
    const bool many = urls.size() > 1;
    auto collect = base::BarrierCallback<base::Value>(urls.size(), base::BindOnce(
        [](base::OnceCallback<void(base::Value)> done, bool many, std::vector<base::Value> pages) {
          if (!many) { std::move(done).Run(std::move(pages[0])); return; }
          base::ListValue list;
          for (auto& page : pages) list.Append(std::move(page));
          std::move(done).Run(base::Value(base::DictValue().Set("pages", std::move(list))));
        }, std::move(done), many));
    for (size_t i = 0; i < urls.size(); ++i) {
      const std::string key = i == 0 ? std::string(kReader) : kReader + base::NumberToString(i + 1);
      const std::string alias = i == 0 ? "@reader" : "@reader:" + base::NumberToString(i + 1);
      if (auto found = assistant_background_.find(key); found != assistant_background_.end() && found->second.profile_id != AssistantProfileId())
        AssistantDiscardBackground(key);
      content::WebContents* page = nullptr;
      if (auto found = assistant_background_.find(key); found != assistant_background_.end()) {
        page = EnsurePage(found->second.view);
        page->GetController().LoadURL(urls[i], content::Referrer(), ui::PAGE_TRANSITION_TYPED, {});
      } else if (AssistantStartBackground(key, {}, urls[i])) {
        page = assistant_background_[key].view->web_contents();
      }
      if (!page) { collect.Run(Error("Could not open the reader tab")); continue; }
      WhenLoaded(page->GetWeakPtr(), base::TimeTicks::Now() + base::Seconds(20), base::BindOnce(
          [](base::WeakPtr<content::WebContents> page, std::string alias, bool many, base::OnceCallback<void(base::Value)> done, bool) {
            if (!page) { std::move(done).Run(Error("The reader tab closed")); return; }
            if (auto dialog = OpenDialog(page.get())) {
              std::move(done).Run(base::Value(base::DictValue().Set("tab", alias).Set("dialog", std::move(*dialog)))); return;
            }
            RunAssistantPageOperation(page.get(), base::DictValue().Set("tool", "read_page"), base::BindOnce(
                [](base::WeakPtr<content::WebContents> page, std::string alias, bool many, base::OnceCallback<void(base::Value)> done, base::Value value) {
                  if (!page || !value.is_dict()) { std::move(done).Run(Error("The page could not be read")); return; }
                  auto result = std::move(value).TakeDict();
                  result.Set("tab", alias);
                  result.Set("url", SafeURL(page->GetLastCommittedURL()));
                  result.Set("title", base::UTF16ToUTF8(page->GetTitle()));
                  // Several pages share one reply: keep each one shorter.
                  if (const auto* text = result.FindString("text"); many && text && text->size() > 10000) {
                    std::string shorter;
                    base::TruncateUTF8ToByteSize(*text, 10000, &shorter);
                    result.Set("text", shorter);
                  }
                  std::move(done).Run(base::Value(std::move(result)));
                }, page, std::move(alias), many, std::move(done)));
          }, page->GetWeakPtr(), alias, many, collect));
    }
    return;
  }
  const bool debug = IsAssistantDebugTool(tool);
  if (debug && (hidden || !permissions.pages || !permissions.tabs)) {
    fail("Debugging needs page and tab access in a foreground conversation"); return;
  }
  const bool read = tool == "debug_console" || tool == "debug_dom" || tool == "read_page" || tool == "screenshot" || tool == "wait" || tool == "adblock_inspect" || tool == "adblock_clear_preview";
  const bool mutation = tool == "debug_edit_dom" || tool == "debug_hard_reload" || tool == "open_tab" || tool == "navigate" || tool == "activate_tab" || tool == "close_tab" ||
                        tool == "click" || tool == "type" || tool == "scroll" || tool == "pin_tab" || tool == "unpin_tab" ||
                        tool == "insert_text" || tool == "press_key" || tool == "select" || tool == "mouse" || tool == "dialog" ||
                        tool == "adblock_add_rules" || tool == "adblock_diagnose";
  if (!read && !mutation) { fail("Unknown browser tool"); return; }
  if (read ? !permissions.pages : !permissions.actions) { fail("Required page/action permission is disabled"); return; }
  if ((tool == "adblock_add_rules" || tool == "adblock_diagnose") && !permissions.pages) { fail("Ad blocking tools need the page permission"); return; }
  // In the background the session's own page replaces @current, and the
  // user's visible tabs stay read-only.
  const auto* requested = args.FindString("tab");
  std::string requested_id = requested ? *requested : std::string();
  if (requested_id.starts_with("@tab:")) requested_id.erase(0, 5);
  const auto reader_key = ReaderKey(requested_id);
  const bool reader = !reader_key.empty();
  if (debug && (reader || requested_id.starts_with("@pin:"))) {
    fail("Debugging requires an open tab in this profile"); return;
  }
  if (reader) {
    const auto found = assistant_background_.find(reader_key);
    if (found == assistant_background_.end() || found->second.profile_id != AssistantProfileId()) { fail("Open a page with visit first"); return; }
    hidden = &found->second;
  }
  const bool own_page = hidden && (reader || requested_id.empty() || requested_id == "@current" || requested_id == "current" || requested_id == hidden->alias);
  if (hidden && (tool == "open_tab" || own_page)) {
    if (tool == "activate_tab" || tool == "close_tab" || tool == "pin_tab" || tool == "unpin_tab") { fail("The background page cannot be activated, closed or pinned; the user takes it over"); return; }
    auto* page = EnsurePage(hidden->view);
    if (tool == "open_tab" || tool == "navigate") {
      const auto* raw = args.FindString("url"); const GURL url(raw ? *raw : "");
      if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password()) { fail("Only HTTP(S) URLs without credentials are allowed"); return; }
      page->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_TYPED, {});
      Settle(page->GetWeakPtr(), base::Milliseconds(300), std::move(done)); return;
    }
    if (!page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() || page->IsCrashed()) { fail("Background page is not ready; navigate @current first"); return; }
    if (page->GetSize().IsEmpty()) page->Resize(gfx::Rect(shell_->content_host()->size()));
    ActOnPage(page, tool, std::move(args), user_confirmed, std::move(done));
    return;
  }
  if (hidden && mutation) { fail("In the background act only in @current; the user's visible tabs are read-only"); return; }
  if (tool == "open_tab") {
    const auto* raw = args.FindString("url"); const GURL url(raw ? *raw : "");
    if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password()) { fail("Only HTTP(S) URLs without credentials are allowed"); return; }
    if (!NavigateFromBar(url, true)) { fail("The tab could not be opened"); return; }
    views::WebView* opened = nullptr;
    for (const auto& host : hosts_) if (host.workspace_id == active_id()) opened = host.view;
    auto* page = opened ? EnsurePage(opened) : nullptr;
    if (!page) { std::move(done).Run(base::Value(base::DictValue().Set("ok", true))); return; }
    // The new tab's id, so the next action can target it without listing tabs.
    Settle(page->GetWeakPtr(), base::Milliseconds(300), base::BindOnce(
        [](std::string id, base::OnceCallback<void(base::Value)> done, base::Value value) {
          if (value.is_dict() && !id.empty()) value.GetDict().Set("tab", "@tab:" + id);
          std::move(done).Run(std::move(value));
        }, permissions.tabs ? AssistantTargetFor(opened) : std::string(), std::move(done)));
    return;
  }
  if (!permissions.tabs) { fail("Enable tab discovery to target browser pages"); return; }
  const auto* target = args.FindString("tab");
  if (!target) { fail("Missing tab reference"); return; }
  std::string id = *target;
  if (id.starts_with("@tab:")) id.erase(0, 5);
  if (id.starts_with("@pin:")) {
    if (!permissions.actions) { fail("Opening a pin requires browser actions permission"); return; }
    for (const auto& workspace : model_.workspaces()) {
      if (workspace.profile_id != AssistantProfileId()) continue;
      for (const auto& pin : pinned_items(workspace.id)) if (pin.id == id.substr(5) && !pin.folder) {
        if (!Select(workspace.id)) { fail("Workspace is not ready"); return; }
        OpenPinnedItem(pin.id);
        std::move(done).Run(base::Value(base::DictValue().Set("ok", true).Set("detail", "Pin opened. List tabs before reading it."))); return;
      }
    }
    fail("Pin no longer exists in this profile"); return;
  }
  views::WebView* view = nullptr;
  if (id == "@current" || id == "current") {
    for (const auto& host : hosts_) if (host.workspace_id == active_id()) view = host.view;
  } else {
    const auto found = assistant_targets_.find(id);
    if (found != assistant_targets_.end() && found->second->view()) view = static_cast<views::WebView*>(found->second->view());
  }
  Host* owner = nullptr; size_t index = 0;
  for (auto& host : hosts_) {
    if (host.profile_id != AssistantProfileId()) continue;
    for (size_t i = 0; i < host.tabs.size(); ++i) if (host.tabs[i] == view) { owner = &host; index = i; }
  }
  if (!owner || !view) { fail("Tab expired or belongs to another profile"); return; }
  if (tool == "pin_tab" || tool == "unpin_tab") {
    if (!Select(owner->workspace_id)) { fail("Workspace is not ready"); return; }
    if (tool == "pin_tab") { std::move(done).Run(base::Value(base::DictValue().Set("ok", PinTab(index, {}, {})))); return; }
    for (const auto& [pin, pinned_view] : owner->pin_tabs) if (pinned_view == view) {
      const auto pin_id = pin;
      std::move(done).Run(base::Value(base::DictValue().Set("ok", UnpinItem(pin_id, owner->tabs.size())))); return;
    }
    fail("Tab is not pinned"); return;
  }
  if (tool == "activate_tab") {
    if (!Select(owner->workspace_id)) { fail("Workspace is not ready"); return; }
    SelectTab(index); std::move(done).Run(base::Value(true)); return;
  }
  if (tool == "close_tab") {
    CloseWorkspaceTab(owner->workspace_id, index);
    std::move(done).Run(base::Value(base::DictValue().Set("detail", "Close requested. List tabs to verify; before-unload confirmation may keep it open."))); return;
  }
  // A restored tab loads only when shown: the assistant loads it itself, and
  // waits for a loading page, before acting on it.
  bool waiting = false;
  if (auto pending = owner->deferred_urls.find(view); pending != owner->deferred_urls.end()) {
    EnsurePage(view)->GetController().LoadURL(pending->second, content::Referrer(), ui::PAGE_TRANSITION_RELOAD, std::string());
    owner->deferred_urls.erase(pending); owner->deferred_titles.erase(view);
    waiting = true;
    UpdateShell();
  }
  auto* page = EnsurePage(view);
  if (tool != "navigate" && !args.FindBool("_loaded") && (waiting || page->IsLoading())) {
    args.Set("_loaded", true);
    WhenLoaded(page->GetWeakPtr(), base::TimeTicks::Now() + base::Seconds(20), base::BindOnce(
        [](base::WeakPtr<WorkspaceWindow> window, std::string tool, base::DictValue args, AssistantPermissions permissions,
           base::OnceCallback<void(base::Value)> done, bool confirmed, std::string background, bool) {
          if (window) window->AssistantTool(tool, std::move(args), permissions, std::move(done), confirmed, background);
        }, weak_factory_.GetWeakPtr(), tool, std::move(args), permissions, std::move(done), user_confirmed, background));
    return;
  }
  if (!page || !page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() || page->IsCrashed()) { fail("Page is not ready or is protected"); return; }
  if (tool == "navigate") {
    const auto* raw = args.FindString("url"); const GURL url(raw ? *raw : "");
    if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password()) { fail("Only HTTP(S) URLs without credentials are allowed"); return; }
    page->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_TYPED, {});
    Settle(page->GetWeakPtr(), base::Milliseconds(300), std::move(done)); return;
  }
  if (debug) {
    RunAssistantDebugTool(page, tool, std::move(args), user_confirmed, std::move(done));
    return;
  }
  if (tool == "screenshot" && page->GetSize().IsEmpty()) page->Resize(gfx::Rect(shell_->content_host()->size()));
  // Saved rules change the browser for good: they always ask, whatever the
  // click setting says.
  const bool confirmed = user_confirmed || (!permissions.confirm_clicks && tool != "adblock_add_rules");
  ActOnPage(page, tool, std::move(args), confirmed, std::move(done));
}
}
