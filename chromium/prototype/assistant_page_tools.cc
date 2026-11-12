// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/assistant_page_tools.h"

#include <algorithm>
#include <utility>

#include "assistant_page_script.h"
#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/ref_counted.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/common/isolated_world_ids.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/gfx/codec/jpeg_codec.h"

namespace zitter::prototype {
namespace {

base::Value Error(std::string message) {
  return base::Value(base::DictValue().Set("error", std::move(message)));
}

struct NamedKey { const char* name; ui::KeyboardCode code; char16_t character; };
constexpr NamedKey kKeys[] = {
    {"enter", ui::VKEY_RETURN, u'\r'}, {"tab", ui::VKEY_TAB, u'\t'}, {"backspace", ui::VKEY_BACK, 0}, {"delete", ui::VKEY_DELETE, 0},
    {"escape", ui::VKEY_ESCAPE, 0}, {"arrowup", ui::VKEY_UP, 0}, {"arrowdown", ui::VKEY_DOWN, 0}, {"arrowleft", ui::VKEY_LEFT, 0},
    {"arrowright", ui::VKEY_RIGHT, 0}, {"home", ui::VKEY_HOME, 0}, {"end", ui::VKEY_END, 0}, {"pageup", ui::VKEY_PRIOR, 0},
    {"pagedown", ui::VKEY_NEXT, 0}};
// "ctrl+shift+k", "Enter", "F5", "a": modifiers, then one key. Shortcuts that
// close or switch tabs and windows stay with the user.

}  // namespace

void RunAssistantPageOperation(content::WebContents* page, base::DictValue args,
                               base::OnceCallback<void(base::Value)> done) {
  std::string script = kAssistantPageScript;
  base::ReplaceFirstSubstringAfterOffset(&script, 0, "ZITTER_ARGS",
                                         base::WriteJson(args).value_or("{}"));
  page->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script), std::move(done),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

namespace {

// A page screenshot is at most 1280 px wide. The scale converts pixels in
// the JPEG back to page coordinates for the browser input tools.
void CopyAssistantSurface(base::WeakPtr<content::WebContents> page,
                          base::OnceCallback<void(base::Value)> done) {
  auto* view = page ? page->GetRenderWidgetHostView() : nullptr;
  if (!view) {
    std::move(done).Run(Error("Page is not ready"));
    return;
  }
  const gfx::Size size = view->GetViewBounds().size();
  if (size.IsEmpty()) {
    std::move(done).Run(Error("The page has no size"));
    return;
  }
  const double scale = std::min(1.0, 1280.0 / size.width());
  const gfx::Size output(std::max(1, static_cast<int>(size.width() * scale)),
                         std::max(1, static_cast<int>(size.height() * scale)));
  const double zoom =
      blink::ZoomLevelToZoomFactor(content::HostZoomMap::GetZoomLevel(page.get()));
  view->CopyFromSurface(gfx::Rect(), output, base::Seconds(5), base::BindOnce(
      [](base::OnceCallback<void(base::Value)> done, double css_per_pixel,
         const content::CopyFromSurfaceResult& result) {
        if (!result.has_value() || result->bitmap.drawsNothing()) {
          std::move(done).Run(Error("The page could not be captured"));
          return;
        }
        const auto jpeg = gfx::JPEGCodec::Encode(result->bitmap, 70);
        if (!jpeg) {
          std::move(done).Run(Error("The page could not be captured"));
          return;
        }
        std::move(done).Run(base::Value(base::DictValue()
            .Set("image", base::Base64Encode(*jpeg))
            .Set("width", result->bitmap.width())
            .Set("height", result->bitmap.height())
            .Set("css_per_pixel", css_per_pixel)));
      }, std::move(done), 1.0 / (scale * zoom)));
}

}  // namespace

void CaptureAssistantPage(base::WeakPtr<content::WebContents> page,
                          base::OnceCallback<void(base::Value)> done) {
  if (!page || !page->GetPrimaryMainFrame()->IsRenderFrameLive()) {
    std::move(done).Run(Error("Page is not ready"));
    return;
  }
  auto* frame = page->GetPrimaryMainFrame();
  auto capture = page->IncrementCapturerCount(
      gfx::Size(), /*stay_hidden=*/true, /*stay_awake=*/true, /*is_activity=*/false);
  const auto document = frame->GetWeakDocumentPtr();
  const auto url = page->GetLastCommittedURL();
  using CaptureCompletion = base::RefCountedData<base::OnceCallback<void(base::Value)>>;
  auto complete = base::MakeRefCounted<CaptureCompletion>(
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> page, content::WeakDocumentPtr document,
             GURL url, base::ScopedClosureRunner capture,
             base::OnceCallback<void(base::Value)> done, base::Value value) {
            capture.RunAndReset();
            if (!page || document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
                page->GetLastCommittedURL() != url) {
              std::move(done).Run(Error("The document changed during capture"));
              return;
            }
            std::move(done).Run(std::move(value));
          }, page, document, url, std::move(capture), std::move(done)));
  auto finish = base::BindRepeating(
      [](scoped_refptr<CaptureCompletion> complete, base::Value value) {
        if (complete->data)
          std::move(complete->data).Run(std::move(value));
      }, complete);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(finish, Error("The page capture timed out")), base::Seconds(8));
  frame->InsertVisualStateCallback(base::BindOnce(
      [](base::WeakPtr<content::WebContents> page, content::WeakDocumentPtr document,
         GURL url, scoped_refptr<CaptureCompletion> complete,
         base::RepeatingCallback<void(base::Value)> finish, bool ready) {
        if (!complete->data)
          return;
        if (!ready || !page || document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
            page->GetLastCommittedURL() != url) {
          finish.Run(Error("The page did not produce a frame for capture"));
          return;
        }
        CopyAssistantSurface(page, base::BindPostTaskToCurrentDefault(
            base::OnceCallback<void(base::Value)>(std::move(finish))));
      }, page, document, url, std::move(complete), std::move(finish)));
}

// Real mouse input, as a user's, at points in the page's CSS pixels.
gfx::PointF AssistantWidgetPoint(content::WebContents* page, double x, double y) {
  const double zoom = blink::ZoomLevelToZoomFactor(content::HostZoomMap::GetZoomLevel(page));
  return gfx::PointF(x * zoom, y * zoom);
}
void SendAssistantMouse(content::RenderWidgetHost* host, blink::WebInputEvent::Type type, gfx::PointF point,
               blink::WebPointerProperties::Button button, int modifiers, int clicks) {
  blink::WebMouseEvent event(type, modifiers, ui::EventTimeForNow());
  event.SetPositionInWidget(point); event.SetPositionInScreen(point);
  event.button = button; event.click_count = clicks;
  host->ForwardMouseEvent(event);
}
void ClickAssistantPageAt(content::WebContents* page, content::RenderWidgetHost* host, double x, double y,
             blink::WebPointerProperties::Button button, int clicks) {
  using Type = blink::WebInputEvent::Type;
  const auto point = AssistantWidgetPoint(page, x, y);
  const int held = button == blink::WebPointerProperties::Button::kRight ? blink::WebInputEvent::kRightButtonDown : blink::WebInputEvent::kLeftButtonDown;
  SendAssistantMouse(host, Type::kMouseMove, point, blink::WebPointerProperties::Button::kNoButton, 0, 0);
  for (int count = 1; count <= clicks; ++count) {
    SendAssistantMouse(host, Type::kMouseDown, point, button, held, count);
    SendAssistantMouse(host, Type::kMouseUp, point, button, 0, count);
  }
}

bool ParseAssistantKey(const std::string& name, ui::KeyboardCode& code, int& flags, char16_t& character) {
  const auto parts = base::SplitString(base::ToLowerASCII(name), "+", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (parts.empty() || parts.size() > 4) return false;
  flags = 0; code = ui::VKEY_UNKNOWN; character = 0;
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    const auto& modifier = parts[i];
    if (modifier == "ctrl" || modifier == "control") flags |= ui::EF_CONTROL_DOWN;
    else if (modifier == "shift") flags |= ui::EF_SHIFT_DOWN;
    else if (modifier == "alt" || modifier == "option") flags |= ui::EF_ALT_DOWN;
    else if (modifier == "meta" || modifier == "cmd" || modifier == "command" || modifier == "win") flags |= ui::EF_COMMAND_DOWN;
    else return false;
  }
  const auto& key = parts.back();
  for (const auto& entry : kKeys) if (key == entry.name) { code = entry.code; character = entry.character; }
  if (key == "return") { code = ui::VKEY_RETURN; character = u'\r'; }
  if (key == "esc") code = ui::VKEY_ESCAPE;
  if (key == "space") { code = ui::VKEY_SPACE; character = u' '; }
  if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') {
    code = static_cast<ui::KeyboardCode>(ui::VKEY_A + (key[0] - 'a'));
    character = static_cast<char16_t>((flags & ui::EF_SHIFT_DOWN) ? key[0] - 'a' + 'A' : key[0]);
  }
  if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') { code = static_cast<ui::KeyboardCode>(ui::VKEY_0 + (key[0] - '0')); character = key[0]; }
  if (int number = 0; key.size() >= 2 && key.size() <= 3 && key[0] == 'f' && base::StringToInt(key.substr(1), &number) && number >= 1 && number <= 12)
    code = static_cast<ui::KeyboardCode>(ui::VKEY_F1 + number - 1);
  if (code == ui::VKEY_UNKNOWN) return false;
  const bool primary = flags & (ui::EF_CONTROL_DOWN | ui::EF_COMMAND_DOWN);
  static constexpr ui::KeyboardCode kBrowserKeys[] = {ui::VKEY_W, ui::VKEY_Q, ui::VKEY_N, ui::VKEY_T, ui::VKEY_TAB, ui::VKEY_PRIOR, ui::VKEY_NEXT, ui::VKEY_F4};
  if ((primary && std::ranges::find(kBrowserKeys, code) != std::end(kBrowserKeys)) || ((flags & ui::EF_ALT_DOWN) && code == ui::VKEY_F4) ||
      (primary && (flags & ui::EF_SHIFT_DOWN) && code == ui::VKEY_DELETE)) return false;
  // Shortcuts type no text; Enter keeps its own.
  if ((flags & (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN)) && code != ui::VKEY_RETURN) character = 0;
  return true;
}
void TypeAssistantCharacter(content::RenderWidgetHost* host, char16_t character, int flags, bool page_only) {
  // FromCharacter is portable; the two-argument native constructor is Aura-only.
  auto event = input::NativeWebKeyboardEvent(ui::KeyEvent::FromCharacter(
      character, static_cast<ui::KeyboardCode>(character), ui::DomCode::NONE, flags));
  event.skip_if_unhandled |= page_only;
  host->ForwardKeyboardEvent(event);
}
void PressAssistantKey(content::RenderWidgetHost* host, ui::KeyboardCode code,
                       int flags, char16_t character, bool page_only) {
  auto send = [&](ui::EventType type) {
    auto event = input::NativeWebKeyboardEvent(ui::KeyEvent(type, code, flags));
    event.skip_if_unhandled |= page_only;
    host->ForwardKeyboardEvent(event);
  };
  send(ui::EventType::kKeyPressed);
  if (character)
    TypeAssistantCharacter(host, character, flags, page_only);
  send(ui::EventType::kKeyReleased);
}

void ReadAssistantPage(content::WebContents* page,
                       base::OnceCallback<void(base::Value)> done) {
  auto on_read = base::BindOnce(
      [](base::WeakPtr<content::WebContents> page,
         base::OnceCallback<void(base::Value)> done, base::Value value) {
        if (value.is_none()) {
          std::move(done).Run(Error("The page did not return readable content"));
          return;
        }
        // Canvas editors expose their rendered surface alongside the text.
        if (!page || !value.is_dict() ||
            !value.GetDict().FindBool("canvas").value_or(false)) {
          std::move(done).Run(std::move(value));
          return;
        }
        CaptureAssistantPage(page, base::BindOnce(
            [](base::Value read, base::OnceCallback<void(base::Value)> done,
               base::Value shot) {
              if (const auto* image = shot.GetIfDict(); image && image->FindString("image")) {
                auto& result = read.GetDict();
                result.Set("image", *image->FindString("image"));
                result.Set("width", image->FindInt("width").value_or(0));
                result.Set("height", image->FindInt("height").value_or(0));
                result.Set("css_per_pixel", image->FindDouble("css_per_pixel").value_or(1));
                result.Set("detail", "The page draws its content on a canvas; its screenshot is attached.");
              }
              std::move(done).Run(std::move(read));
            }, std::move(value), std::move(done)));
      }, page->GetWeakPtr(), std::move(done));
  RunAssistantPageOperation(page, base::DictValue().Set("tool", "read_page"),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(on_read), base::Value()));
}

}  // namespace zitter::prototype
