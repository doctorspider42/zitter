// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "ui_automation.h"

#import <AppKit/AppKit.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace zitter::content_embedder {
namespace {

using Reply = base::OnceCallback<void(base::DictValue)>;

base::DictValue Error(std::string message) {
  base::DictValue result;
  result.Set("ok", false);
  result.Set("error", std::move(message));
  return result;
}

base::DictValue Ok() {
  base::DictValue result;
  result.Set("ok", true);
  return result;
}

base::DictValue RectValue(const gfx::Rect& rect) {
  base::DictValue value;
  value.Set("x", rect.x());
  value.Set("y", rect.y());
  value.Set("width", rect.width());
  value.Set("height", rect.height());
  return value;
}

// ---------------------------------------------------------------- windows

views::Widget* WidgetFor(NSWindow* window) {
  return window ? views::Widget::GetWidgetForNativeWindow(gfx::NativeWindow(window))
                : nullptr;
}

NSWindow* WindowFor(views::Widget* widget) {
  return widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
}

// Front to back, only windows that belong to a Views widget.
std::vector<views::Widget*> OrderedWidgets(bool visible_only) {
  std::vector<views::Widget*> widgets;
  NSArray<NSWindow*>* windows = visible_only ? NSApp.orderedWindows : NSApp.windows;
  for (NSWindow* window in windows) {
    views::Widget* widget = WidgetFor(window);
    if (widget && (!visible_only || (widget->IsVisible() && window.isVisible)))
      widgets.push_back(widget);
  }
  return widgets;
}

bool IsAlive(views::Widget* widget) {
  for (views::Widget* candidate : OrderedWidgets(false))
    if (candidate == widget)
      return true;
  return false;
}

std::string Title(views::Widget* widget) {
  NSWindow* window = WindowFor(widget);
  return window ? base::SysNSStringToUTF8(window.title) : std::string();
}

float ScaleOf(views::Widget* widget) {
  const ui::Compositor* compositor = widget->GetCompositor();
  return compositor ? compositor->device_scale_factor() : 1.0f;
}

// "window" is a window number, an exact title, or a part of one. Without it,
// the main "Zitter" window (or the frontmost visible one).
views::Widget* FindWidget(const base::DictValue& args, std::string* error) {
  const auto visible = OrderedWidgets(true);
  if (const auto number = args.FindInt("window")) {
    for (views::Widget* widget : OrderedWidgets(false))
      if (WindowFor(widget).windowNumber == *number)
        return widget;
    *error = base::StringPrintf("no window %d", *number);
    return nullptr;
  }
  if (const std::string* title = args.FindString("window")) {
    for (views::Widget* widget : visible)
      if (Title(widget) == *title)
        return widget;
    const std::string wanted = base::ToLowerASCII(*title);
    for (views::Widget* widget : visible)
      if (base::ToLowerASCII(Title(widget)).find(wanted) != std::string::npos)
        return widget;
    *error = "no visible window titled \"" + *title + "\"";
    return nullptr;
  }
  for (views::Widget* widget : visible)
    if (Title(widget) == "Zitter")
      return widget;
  if (!visible.empty())
    return visible.front();
  *error = "no visible window";
  return nullptr;
}

// ------------------------------------------------------------------ views

std::string NameOf(const views::View* view) {
  return base::UTF16ToUTF8(view->GetViewAccessibility().GetCachedName());
}

gfx::Rect BoundsInWidget(const views::View* view) {
  return view->ConvertRectToWidget(view->GetLocalBounds());
}

template <typename Visit>
void ForEachDrawnView(views::View* view, const Visit& visit) {
  if (!view->GetVisible())
    return;
  visit(view);
  for (views::View* child : view->children())
    ForEachDrawnView(child, visit);
}

views::WebView* WebViewAncestor(views::View* view) {
  for (; view; view = view->parent())
    if (auto* web = views::AsViewClass<views::WebView>(view))
      return web;
  return nullptr;
}

// "name" matches the accessible name: exactly (ignoring case) first, then as a
// part. Several matches fail unless "index" picks one.
views::View* FindView(const base::DictValue& args, std::string* error) {
  const std::string* name = args.FindString("name");
  std::vector<views::Widget*> widgets;
  if (args.Find("window")) {
    views::Widget* widget = FindWidget(args, error);
    if (!widget)
      return nullptr;
    widgets.push_back(widget);
  } else {
    widgets = OrderedWidgets(true);
  }
  const std::string wanted = base::ToLowerASCII(*name);
  std::vector<views::View*> exact, partial;
  for (views::Widget* widget : widgets) {
    ForEachDrawnView(widget->GetRootView(), [&](views::View* view) {
      const std::string own = base::ToLowerASCII(NameOf(view));
      if (own.empty() || !view->IsDrawn())
        return;
      if (own == wanted)
        exact.push_back(view);
      else if (own.find(wanted) != std::string::npos)
        partial.push_back(view);
    });
  }
  const auto& matches = exact.empty() ? partial : exact;
  const int index = args.FindInt("index").value_or(-1);
  if (matches.empty()) {
    *error = "no view named \"" + *name + "\"";
    return nullptr;
  }
  if (index >= 0) {
    if (static_cast<size_t>(index) < matches.size())
      return matches[index];
    *error = base::StringPrintf("only %zu views match", matches.size());
    return nullptr;
  }
  if (matches.size() > 1) {
    std::string list;
    for (size_t i = 0; i < matches.size() && i < 8; ++i) {
      list += base::StringPrintf("\n  [%zu] \"%s\" in \"%s\" at %s", i,
                                 NameOf(matches[i]).c_str(),
                                 Title(matches[i]->GetWidget()).c_str(),
                                 BoundsInWidget(matches[i]).ToString().c_str());
    }
    *error = base::StringPrintf("%zu views match \"%s\"; pass index:", matches.size(),
                                name->c_str()) + list;
    return nullptr;
  }
  return matches.front();
}

// A point in a widget's own coordinates (its root view: the screenshot's
// coordinates at scale 1).
struct Target {
  raw_ptr<views::Widget> widget = nullptr;
  gfx::Point point;
};

// {name} → that view's center; {x, y, window} → window coordinates;
// {x, y} alone → screen points, hit-tested front to back.
std::optional<Target> ResolveTarget(const base::DictValue& args, std::string* error,
                                    const char* x_key = "x", const char* y_key = "y") {
  if (args.FindString("name") && x_key == std::string("x")) {
    views::View* view = FindView(args, error);
    if (!view)
      return std::nullopt;
    return Target{view->GetWidget(), BoundsInWidget(view).CenterPoint()};
  }
  const auto x = args.FindDouble(x_key);
  const auto y = args.FindDouble(y_key);
  if (!x || !y) {
    *error = std::string("needs name, or ") + x_key + " and " + y_key;
    return std::nullopt;
  }
  const gfx::Point point(std::lround(*x), std::lround(*y));
  if (args.Find("window")) {
    views::Widget* widget = FindWidget(args, error);
    if (!widget)
      return std::nullopt;
    return Target{widget, point};
  }
  for (views::Widget* widget : OrderedWidgets(true)) {
    if (WindowFor(widget).ignoresMouseEvents ||
        !widget->GetWindowBoundsInScreen().Contains(point)) {
      continue;
    }
    return Target{widget, point - widget->GetClientAreaBoundsInScreen().OffsetFromOrigin()};
  }
  *error = "no window at " + point.ToString();
  return std::nullopt;
}

int Modifiers(const base::DictValue& args, int* ns_flags = nullptr) {
  int flags = 0;
  NSUInteger ns = 0;
  if (const base::ListValue* list = args.FindList("modifiers")) {
    for (const base::Value& item : *list) {
      const std::string name = item.is_string() ? base::ToLowerASCII(item.GetString()) : "";
      if (name == "cmd" || name == "command" || name == "meta") {
        flags |= ui::EF_COMMAND_DOWN;
        ns |= NSEventModifierFlagCommand;
      } else if (name == "shift") {
        flags |= ui::EF_SHIFT_DOWN;
        ns |= NSEventModifierFlagShift;
      } else if (name == "alt" || name == "option") {
        flags |= ui::EF_ALT_DOWN;
        ns |= NSEventModifierFlagOption;
      } else if (name == "ctrl" || name == "control") {
        flags |= ui::EF_CONTROL_DOWN;
        ns |= NSEventModifierFlagControl;
      }
    }
  }
  if (ns_flags)
    *ns_flags = static_cast<int>(ns);
  return flags;
}

int BlinkModifiers(int flags) {
  int modifiers = 0;
  if (flags & ui::EF_SHIFT_DOWN) modifiers |= blink::WebInputEvent::kShiftKey;
  if (flags & ui::EF_CONTROL_DOWN) modifiers |= blink::WebInputEvent::kControlKey;
  if (flags & ui::EF_ALT_DOWN) modifiers |= blink::WebInputEvent::kAltKey;
  if (flags & ui::EF_COMMAND_DOWN) modifiers |= blink::WebInputEvent::kMetaKey;
  if (flags & ui::EF_LEFT_MOUSE_BUTTON) modifiers |= blink::WebInputEvent::kLeftButtonDown;
  if (flags & ui::EF_RIGHT_MOUSE_BUTTON) modifiers |= blink::WebInputEvent::kRightButtonDown;
  return modifiers;
}

// ---------------------------------------------------------------- capture

// Copies a widget's composited frame (web pages included) at `scale` pixels
// per window point; empty on failure or after `timeout`.
void CaptureWidget(views::Widget* widget, float scale,
                   base::OnceCallback<void(SkBitmap)> done) {
  ui::Compositor* compositor = widget->GetCompositor();
  ui::Layer* layer = widget->GetLayer();
  if (!compositor || !layer) {
    std::move(done).Run(SkBitmap());
    return;
  }
  struct Pending : base::RefCounted<Pending> {
    base::OnceCallback<void(SkBitmap)> done;
    void Finish(SkBitmap bitmap) {
      if (done)
        std::move(done).Run(std::move(bitmap));
    }
   private:
    friend class base::RefCounted<Pending>;
    ~Pending() = default;
  };
  auto pending = base::MakeRefCounted<Pending>();
  pending->done = std::move(done);
  auto request = std::make_unique<viz::CopyOutputRequest>(
      viz::CopyOutputRequest::ResultFormat::RGBA,
      viz::CopyOutputRequest::ResultDestination::kSystemMemory,
      base::BindOnce(
          [](scoped_refptr<Pending> pending, std::unique_ptr<viz::CopyOutputResult> result) {
            pending->Finish(result->IsEmpty()
                                ? SkBitmap()
                                : result->ScopedAccessSkBitmap().GetOutScopedBitmap());
          },
          pending));
  const int from = std::lround(compositor->device_scale_factor() * 100);
  const int to = std::max(1L, std::lround(scale * 100));
  if (from != to)
    request->SetUniformScaleRatio(from, to);
  layer->RequestCopyOfOutput(std::move(request));
  compositor->ScheduleFullRedraw();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&Pending::Finish, pending, SkBitmap()), base::Seconds(5));
}

SkBitmap Crop(const SkBitmap& bitmap, const base::DictValue& args, float scale) {
  const base::DictValue* crop = args.FindDict("crop");
  if (!crop || bitmap.drawsNothing())
    return bitmap;
  const auto value = [&](const char* key) {
    return static_cast<int>(std::lround(crop->FindDouble(key).value_or(0) * scale));
  };
  SkIRect area = SkIRect::MakeXYWH(value("x"), value("y"), value("width"), value("height"));
  if (!area.intersect(SkIRect::MakeWH(bitmap.width(), bitmap.height())))
    return SkBitmap();
  SkBitmap subset, copy;
  if (!bitmap.extractSubset(&subset, area) || !copy.tryAllocPixels(subset.info()) ||
      !subset.readPixels(copy.pixmap())) {
    return SkBitmap();
  }
  return copy;
}

void WritePng(SkBitmap bitmap, base::FilePath path, Reply reply, base::DictValue result) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(
          [](SkBitmap bitmap, base::FilePath path) {
            const auto png = gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false);
            return png && base::WriteFile(path, *png);
          },
          bitmap, path),
      base::BindOnce(
          [](Reply reply, base::DictValue result, base::FilePath path, gfx::Size size,
             bool written) {
            if (!written) {
              std::move(reply).Run(Error("could not write " + path.value()));
              return;
            }
            result.Set("ok", true);
            result.Set("path", path.value());
            result.Set("width", size.width());
            result.Set("height", size.height());
            std::move(reply).Run(std::move(result));
          },
          std::move(reply), std::move(result), path,
          gfx::Size(bitmap.width(), bitmap.height())));
}

// Lays frames out in a grid with their index and time since the first one, so
// an animation reads from one image.
SkBitmap ContactSheet(const std::vector<std::pair<SkBitmap, base::TimeDelta>>& frames,
                      int columns) {
  constexpr int kLabel = 18, kGap = 6;
  int width = 0, height = 0;
  for (const auto& [bitmap, time] : frames) {
    width = std::max(width, bitmap.width());
    height = std::max(height, bitmap.height());
  }
  columns = std::clamp(columns, 1, static_cast<int>(frames.size()));
  const int rows = (static_cast<int>(frames.size()) + columns - 1) / columns;
  const gfx::Size size(columns * width + (columns + 1) * kGap,
                       rows * (height + kLabel) + (rows + 1) * kGap);
  gfx::Canvas canvas(size, 1.0f, true);
  canvas.DrawColor(SkColorSetRGB(0x2a, 0x2a, 0x2e));
  for (size_t i = 0; i < frames.size(); ++i) {
    const int x = kGap + static_cast<int>(i % columns) * (width + kGap);
    const int y = kGap + static_cast<int>(i / columns) * (height + kLabel + kGap);
    canvas.DrawStringRect(
        base::UTF8ToUTF16(base::StringPrintf(
            "#%zu  +%lld ms", i + 1, frames[i].second.InMilliseconds())),
        gfx::FontList(), SK_ColorWHITE, gfx::Rect(x + 2, y, width, kLabel));
    if (!frames[i].first.drawsNothing())
      canvas.DrawImageInt(gfx::ImageSkia::CreateFrom1xBitmap(frames[i].first), x, y + kLabel);
  }
  return canvas.GetBitmap();
}

// ----------------------------------------------------------------- keys

std::optional<ui::KeyboardCode> KeyCode(std::string name) {
  name = base::ToLowerASCII(name);
  static constexpr std::pair<const char*, ui::KeyboardCode> kNamed[] = {
      {"return", ui::VKEY_RETURN},   {"enter", ui::VKEY_RETURN},
      {"escape", ui::VKEY_ESCAPE},   {"esc", ui::VKEY_ESCAPE},
      {"tab", ui::VKEY_TAB},         {"space", ui::VKEY_SPACE},
      {"backspace", ui::VKEY_BACK},  {"delete", ui::VKEY_DELETE},
      {"up", ui::VKEY_UP},           {"down", ui::VKEY_DOWN},
      {"left", ui::VKEY_LEFT},       {"right", ui::VKEY_RIGHT},
      {"home", ui::VKEY_HOME},       {"end", ui::VKEY_END},
      {"pageup", ui::VKEY_PRIOR},    {"pagedown", ui::VKEY_NEXT},
      {"comma", ui::VKEY_OEM_COMMA}, {"period", ui::VKEY_OEM_PERIOD},
      {"slash", ui::VKEY_OEM_2},     {"minus", ui::VKEY_OEM_MINUS},
      {"equal", ui::VKEY_OEM_PLUS},  {"bracketleft", ui::VKEY_OEM_4},
      {"bracketright", ui::VKEY_OEM_6},
  };
  for (const auto& [key, code] : kNamed)
    if (name == key)
      return code;
  if (name.size() == 1 && base::IsAsciiAlpha(name[0]))
    return static_cast<ui::KeyboardCode>(ui::VKEY_A + (name[0] - 'a'));
  if (name.size() == 1 && base::IsAsciiDigit(name[0]))
    return static_cast<ui::KeyboardCode>(ui::VKEY_0 + (name[0] - '0'));
  if (name.size() >= 2 && name[0] == 'f') {
    int number = 0;
    for (size_t i = 1; i < name.size() && base::IsAsciiDigit(name[i]); ++i)
      number = number * 10 + (name[i] - '0');
    if (number >= 1 && number <= 12)
      return static_cast<ui::KeyboardCode>(ui::VKEY_F1 + number - 1);
  }
  return std::nullopt;
}

// ---------------------------------------------------------------- driver

// Runs commands on the UI thread.
class Driver final : public views::WidgetObserver {
 public:
  void Handle(const std::string& line, Reply reply) {
    std::optional<base::Value> parsed = base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      std::move(reply).Run(Error("expected a JSON object"));
      return;
    }
    Run(std::move(parsed->GetDict()), std::move(reply));
  }

  void Run(base::DictValue args, Reply reply) {
    const std::string* command = args.FindString("cmd");
    if (!command) {
      std::move(reply).Run(Error("missing cmd"));
      return;
    }
    if (*command == "ping") return std::move(reply).Run(Ok());
    if (*command == "windows") return std::move(reply).Run(Windows());
    if (*command == "tree") return std::move(reply).Run(Tree(args));
    if (*command == "click") return Click(args, std::move(reply));
    if (*command == "move") return Move(args, std::move(reply));
    if (*command == "drag") return Drag(args, std::move(reply));
    if (*command == "scroll") return Scroll(args, std::move(reply));
    if (*command == "key") return Key(args, std::move(reply));
    if (*command == "type") return Type(args, std::move(reply));
    if (*command == "shot") return Shot(args, std::move(reply));
    if (*command == "frames") return Frames(std::move(args), std::move(reply));
    std::move(reply).Run(Error("unknown cmd " + *command));
  }

  base::WeakPtr<Driver> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  // ---- queries

  base::DictValue Windows() {
    base::ListValue list;
    for (views::Widget* widget : OrderedWidgets(false)) {
      NSWindow* window = WindowFor(widget);
      base::DictValue item = RectValue(widget->GetWindowBoundsInScreen());
      item.Set("id", static_cast<int>(window.windowNumber));
      item.Set("title", Title(widget));
      item.Set("visible", widget->IsVisible() && window.isVisible);
      item.Set("active", widget->IsActive());
      item.Set("key", static_cast<bool>(window.isKeyWindow));
      item.Set("scale", ScaleOf(widget));
      item.Set("client", RectValue(widget->GetClientAreaBoundsInScreen()));
      list.Append(std::move(item));
    }
    base::DictValue result = Ok();
    result.Set("windows", std::move(list));
    return result;
  }

  base::DictValue Tree(const base::DictValue& args) {
    std::string error;
    views::Widget* widget = FindWidget(args, &error);
    if (!widget)
      return Error(error);
    const bool all = args.FindBool("all").value_or(false);
    views::View* focused = widget->GetFocusManager()
                               ? widget->GetFocusManager()->GetFocusedView()
                               : nullptr;
    base::ListValue views;
    ForEachDrawnView(widget->GetRootView(), [&](views::View* view) {
      const std::string name = NameOf(view);
      if ((!all && name.empty()) || views.size() >= 3000)
        return;
      const gfx::Rect bounds = BoundsInWidget(view);
      if (bounds.IsEmpty())
        return;
      base::DictValue item = RectValue(bounds);
      item.Set("name", name);
      item.Set("role", ui::ToString(view->GetViewAccessibility().GetCachedRole()));
      item.Set("class", view->GetClassName());
      if (!view->GetEnabled()) item.Set("disabled", true);
      if (view == focused) item.Set("focused", true);
      if (views::AsViewClass<views::WebView>(view)) item.Set("web", true);
      views.Append(std::move(item));
    });
    base::DictValue result = Ok();
    result.Set("window", Title(widget));
    result.Set("views", std::move(views));
    return result;
  }

  // ---- pointer

  // Routes to the page when the point is over a web view, else to Views.
  void Pointer(views::Widget* widget, ui::EventType type, const gfx::Point& point, int flags,
               int changed, int clicks = 0) {
    views::View* hit = widget->GetRootView()->GetEventHandlerForPoint(point);
    views::WebView* web = WebViewAncestor(hit);
    if (web && web->web_contents() && web->web_contents()->GetRenderWidgetHostView()) {
      PointerToWeb(web, type, point, flags, changed, clicks);
      return;
    }
    if (hover_widget_ && hover_widget_ != widget && IsAlive(hover_widget_)) {
      ui::MouseEvent exit(ui::EventType::kMouseExited, gfx::PointF(), gfx::PointF(),
                          ui::EventTimeForNow(), 0, 0);
      hover_widget_->OnMouseEvent(&exit);
    }
    SetHover(widget);
    ui::MouseEvent event(type, gfx::PointF(point), gfx::PointF(point), ui::EventTimeForNow(),
                         flags, changed);
    if (clicks)
      event.SetClickCount(clicks);
    widget->OnMouseEvent(&event);
  }

  void PointerToWeb(views::WebView* web, ui::EventType type, gfx::Point point, int flags,
                    int changed, int clicks) {
    views::View::ConvertPointFromWidget(web, &point);
    using Type = blink::WebInputEvent::Type;
    Type web_type = Type::kMouseMove;
    if (type == ui::EventType::kMousePressed) web_type = Type::kMouseDown;
    if (type == ui::EventType::kMouseReleased) web_type = Type::kMouseUp;
    if (type == ui::EventType::kMouseExited) web_type = Type::kMouseLeave;
    const auto button = (changed | flags) & ui::EF_RIGHT_MOUSE_BUTTON
                            ? blink::WebMouseEvent::Button::kRight
                        : (changed | flags) & ui::EF_LEFT_MOUSE_BUTTON
                            ? blink::WebMouseEvent::Button::kLeft
                            : blink::WebMouseEvent::Button::kNoButton;
    const gfx::Point screen = point + web->GetBoundsInScreen().OffsetFromOrigin();
    blink::WebMouseEvent event(web_type, gfx::PointF(point), gfx::PointF(screen), button,
                               clicks, BlinkModifiers(flags), ui::EventTimeForNow());
    if (web_type == Type::kMouseDown)
      web->web_contents()->Focus();
    web->web_contents()->GetRenderWidgetHostView()->GetRenderWidgetHost()->ForwardMouseEvent(
        event);
  }

  static base::DictValue Described(const Target& target) {
    base::DictValue result = Ok();
    result.Set("window", Title(target.widget));
    result.Set("x", target.point.x());
    result.Set("y", target.point.y());
    views::View* hit = target.widget->GetRootView()->GetEventHandlerForPoint(target.point);
    if (hit) {
      result.Set("hit", std::string(hit->GetClassName()));
      if (const std::string name = NameOf(hit); !name.empty())
        result.Set("hit_name", name);
      if (WebViewAncestor(hit))
        result.Set("web", true);
    }
    return result;
  }

  // Replies first, then dispatches in a task of its own: a click that opens a
  // native menu or a modal loop must not hold the reply, and later commands
  // still run inside that loop.
  void ReplyThenDispatch(const Target& target, Reply reply, base::OnceClosure dispatch) {
    std::move(reply).Run(Described(target));
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, std::move(dispatch));
  }

  void Click(const base::DictValue& args, Reply reply) {
    std::string error;
    const auto target = ResolveTarget(args, &error);
    if (!target)
      return std::move(reply).Run(Error(error));
    const std::string* button_name = args.FindString("button");
    const int button = button_name && *button_name == "right" ? ui::EF_RIGHT_MOUSE_BUTTON
                                                              : ui::EF_LEFT_MOUSE_BUTTON;
    const int count = std::clamp(args.FindInt("count").value_or(1), 1, 3);
    const int modifiers = Modifiers(args);
    const int hover_ms = std::clamp(args.FindInt("hover_ms").value_or(0), 0, 5000);
    views::Widget* widget = target->widget;
    const gfx::Point point = target->point;
    ReplyThenDispatch(*target, std::move(reply),
        base::BindOnce(
            [](base::WeakPtr<Driver> self, views::Widget* widget, gfx::Point point, int button,
               int count, int modifiers, int hover_ms) {
              if (!self || !IsAlive(widget))
                return;
              self->Pointer(widget, ui::EventType::kMouseMoved, point, modifiers, 0);
              auto press = base::BindOnce(
                  [](base::WeakPtr<Driver> self, views::Widget* widget, gfx::Point point,
                     int button, int count, int modifiers) {
                    if (!self || !IsAlive(widget))
                      return;
                    for (int click = 1; click <= count; ++click) {
                      self->Pointer(widget, ui::EventType::kMousePressed, point,
                                    modifiers | button, button, click);
                      if (!IsAlive(widget))
                        return;
                      self->Pointer(widget, ui::EventType::kMouseReleased, point,
                                    modifiers | button, button, click);
                      if (!IsAlive(widget))
                        return;
                    }
                  },
                  self, base::Unretained(widget), point, button, count, modifiers);
              if (hover_ms) {
                base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                    FROM_HERE, std::move(press), base::Milliseconds(hover_ms));
              } else {
                std::move(press).Run();
              }
            },
            GetWeakPtr(), base::Unretained(widget), point, button, count, modifiers, hover_ms));
  }

  void Move(const base::DictValue& args, Reply reply) {
    std::string error;
    const auto target = ResolveTarget(args, &error);
    if (!target)
      return std::move(reply).Run(Error(error));
    Pointer(target->widget, ui::EventType::kMouseMoved, target->point, Modifiers(args), 0);
    std::move(reply).Run(Described(*target));
  }

  void Drag(const base::DictValue& args, Reply reply) {
    std::string error;
    const auto from = ResolveTarget(args, &error);
    if (!from)
      return std::move(reply).Run(Error(error));
    std::optional<Target> to;
    if (args.FindDouble("to_x")) {
      to = ResolveTarget(args, &error, "to_x", "to_y");
    } else if (const std::string* name = args.FindString("to_name")) {
      base::DictValue lookup = args.Clone();
      lookup.Set("name", *name);
      lookup.Remove("index");
      to = ResolveTarget(lookup, &error);
    } else {
      error = "drag needs to_x/to_y or to_name";
    }
    if (!to)
      return std::move(reply).Run(Error(error));
    if (to->widget != from->widget) {
      // Views drags stay within the widget that took the press.
      to->point = to->point + to->widget->GetClientAreaBoundsInScreen().OffsetFromOrigin() -
                  from->widget->GetClientAreaBoundsInScreen().OffsetFromOrigin();
    }
    const int steps = std::clamp(args.FindInt("steps").value_or(12), 1, 200);
    const int step_ms = std::clamp(args.FindInt("step_ms").value_or(16), 0, 1000);
    const int modifiers = Modifiers(args);
    base::DictValue described = Described(*from);
    described.Set("to_x", to->point.x());
    described.Set("to_y", to->point.y());
    Pointer(from->widget, ui::EventType::kMouseMoved, from->point, modifiers, 0);
    Pointer(from->widget, ui::EventType::kMousePressed, from->point,
            modifiers | ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON, 1);
    DragStep(from->widget, from->point, to->point, 1, steps, step_ms, modifiers,
             base::BindOnce([](Reply reply, base::DictValue described) {
               std::move(reply).Run(std::move(described));
             }, std::move(reply), std::move(described)));
  }

  void DragStep(views::Widget* widget, gfx::Point from, gfx::Point to, int step, int steps,
                int step_ms, int modifiers, base::OnceClosure done) {
    if (!IsAlive(widget))
      return std::move(done).Run();
    const gfx::Point point(from.x() + (to.x() - from.x()) * step / steps,
                           from.y() + (to.y() - from.y()) * step / steps);
    Pointer(widget, ui::EventType::kMouseDragged, point, modifiers | ui::EF_LEFT_MOUSE_BUTTON,
            0);
    if (step < steps) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Driver::DragStep, GetWeakPtr(), base::Unretained(widget), from, to,
                         step + 1, steps,
                         step_ms, modifiers, std::move(done)),
          base::Milliseconds(step_ms));
      return;
    }
    Pointer(widget, ui::EventType::kMouseReleased, to, modifiers | ui::EF_LEFT_MOUSE_BUTTON,
            ui::EF_LEFT_MOUSE_BUTTON, 1);
    std::move(done).Run();
  }

  void Scroll(const base::DictValue& args, Reply reply) {
    std::string error;
    const auto target = ResolveTarget(args, &error);
    if (!target)
      return std::move(reply).Run(Error(error));
    // Positive dy scrolls the content down (towards its end), like a page.
    const int dx = std::lround(args.FindDouble("dx").value_or(0));
    const int dy = std::lround(args.FindDouble("dy").value_or(0));
    views::Widget* widget = target->widget;
    views::View* hit = widget->GetRootView()->GetEventHandlerForPoint(target->point);
    Pointer(widget, ui::EventType::kMouseMoved, target->point, 0, 0);
    if (views::WebView* web = WebViewAncestor(hit);
        web && web->web_contents() && web->web_contents()->GetRenderWidgetHostView()) {
      gfx::Point point = target->point;
      views::View::ConvertPointFromWidget(web, &point);
      blink::WebMouseWheelEvent event(blink::WebInputEvent::Type::kMouseWheel,
                                      blink::WebInputEvent::kNoModifiers, ui::EventTimeForNow());
      event.SetPositionInWidget(gfx::PointF(point));
      event.SetPositionInScreen(
          gfx::PointF(point + web->GetBoundsInScreen().OffsetFromOrigin()));
      event.delta_x = -dx;
      event.delta_y = -dy;
      event.delta_units = ui::ScrollGranularity::kScrollByPrecisePixel;
      web->web_contents()->GetRenderWidgetHostView()->GetRenderWidgetHost()->ForwardWheelEvent(
          event);
    } else {
      ui::MouseWheelEvent event(gfx::Vector2d(-dx, -dy), gfx::PointF(target->point),
                                gfx::PointF(target->point), ui::EventTimeForNow(), 0, 0);
      widget->OnMouseEvent(&event);
    }
    std::move(reply).Run(Described(*target));
  }

  // ---- keyboard

  // Keys go through NSApp like real ones, so menu equivalents and the app's
  // shortcut monitors see them.
  void Key(const base::DictValue& args, Reply reply) {
    const std::string* name = args.FindString("key");
    const auto code = name ? KeyCode(*name) : std::nullopt;
    if (!code)
      return std::move(reply).Run(Error("unknown key"));
    std::string error;
    views::Widget* widget = args.Find("window") ? FindWidget(args, &error) : KeyWidget();
    if (!widget)
      return std::move(reply).Run(Error(error.empty() ? "no window for keys" : error));
    int ns_flags = 0;
    Modifiers(args, &ns_flags);
    unichar shifted = 0, character = 0;
    const int mac_code =
        ui::MacKeyCodeForWindowsKeyCode(*code, ns_flags, &shifted, &character);
    if (mac_code < 0)
      return std::move(reply).Run(Error("key has no macOS code"));
    NSWindow* window = WindowFor(widget);
    if (!window.isKeyWindow)
      [window makeKeyWindow];
    NSString* characters = character ? [NSString stringWithCharacters:&character length:1] : @"";
    NSString* unmodified = shifted ? [NSString stringWithCharacters:&shifted length:1] : @"";
    for (NSEventType type : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
      NSEvent* event = [NSEvent keyEventWithType:type
                                        location:NSZeroPoint
                                   modifierFlags:static_cast<NSEventModifierFlags>(ns_flags)
                                       timestamp:NSProcessInfo.processInfo.systemUptime
                                    windowNumber:window.windowNumber
                                         context:nil
                                      characters:characters
                     charactersIgnoringModifiers:unmodified
                                       isARepeat:NO
                                         keyCode:static_cast<unsigned short>(mac_code)];
      [NSApp postEvent:event atStart:NO];
    }
    base::DictValue result = Ok();
    result.Set("window", Title(widget));
    std::move(reply).Run(std::move(result));
  }

  void Type(const base::DictValue& args, Reply reply) {
    const std::string* text = args.FindString("text");
    if (!text)
      return std::move(reply).Run(Error("type needs text"));
    std::string error;
    views::Widget* widget = args.Find("window") ? FindWidget(args, &error) : KeyWidget();
    if (!widget)
      return std::move(reply).Run(Error(error.empty() ? "no window for text" : error));
    views::View* focused =
        widget->GetFocusManager() ? widget->GetFocusManager()->GetFocusedView() : nullptr;
    base::DictValue result = Ok();
    result.Set("window", Title(widget));
    if (views::WebView* web = WebViewAncestor(focused); web && web->web_contents()) {
      // One character event per UTF-16 unit, like typing on a keyboard.
      content::RenderWidgetHost* host =
          web->web_contents()->GetRenderWidgetHostView()
              ? web->web_contents()->GetRenderWidgetHostView()->GetRenderWidgetHost()
              : nullptr;
      if (!host)
        return std::move(reply).Run(Error("the page has no widget"));
      for (const char16_t character : base::UTF8ToUTF16(*text)) {
        input::NativeWebKeyboardEvent event(blink::WebInputEvent::Type::kChar,
                                            blink::WebInputEvent::kNoModifiers,
                                            ui::EventTimeForNow());
        event.text[0] = character;
        event.unmodified_text[0] = character;
        host->ForwardKeyboardEvent(event);
      }
      result.Set("target", "web");
      return std::move(reply).Run(std::move(result));
    }
    ui::InputMethod* input = widget->GetInputMethod();
    ui::TextInputClient* client = input ? input->GetTextInputClient() : nullptr;
    if (!client || client->GetTextInputType() == ui::TEXT_INPUT_TYPE_NONE)
      return std::move(reply).Run(Error("no focused text field in " + Title(widget)));
    client->InsertText(base::UTF8ToUTF16(*text),
                       ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
    if (focused)
      result.Set("target", std::string(focused->GetClassName()));
    std::move(reply).Run(std::move(result));
  }

  // The window keys would go to: the key window, else the main one.
  views::Widget* KeyWidget() {
    if (views::Widget* widget = WidgetFor(NSApp.keyWindow))
      return widget;
    std::string ignored;
    return FindWidget(base::DictValue(), &ignored);
  }

  // ---- screenshots

  void Shot(const base::DictValue& args, Reply reply) {
    std::string error;
    views::Widget* widget = FindWidget(args, &error);
    const std::string* path = args.FindString("path");
    if (!widget)
      return std::move(reply).Run(Error(error));
    if (!path || !base::FilePath(*path).IsAbsolute())
      return std::move(reply).Run(Error("shot needs an absolute path"));
    const float scale = std::clamp(args.FindDouble("scale").value_or(1.0), 0.1, 4.0);
    base::DictValue result;
    result.Set("window", Title(widget));
    result.Set("scale", scale);
    CaptureWidget(widget, scale,
        base::BindOnce(
            [](base::DictValue args, float scale, base::FilePath path, Reply reply,
               base::DictValue result, SkBitmap bitmap) {
              bitmap = Crop(bitmap, args, scale);
              if (bitmap.drawsNothing())
                return std::move(reply).Run(Error("no frame (window hidden or crop empty)"));
              WritePng(std::move(bitmap), path, std::move(reply), std::move(result));
            },
            args.Clone(), scale, base::FilePath(*path), std::move(reply), std::move(result)));
  }

  struct FrameRun {
    base::DictValue args;
    raw_ptr<views::Widget> widget = nullptr;
    base::FilePath path;
    float scale = 1;
    int count = 0;
    int interval_ms = 0;
    int pending = 0;
    base::TimeTicks start;
    std::vector<std::pair<SkBitmap, base::TimeDelta>> frames;
    Reply reply;
  };

  // Captures `count` frames `interval_ms` apart into one contact sheet,
  // optionally after running `start_with` (a click that starts an animation).
  void Frames(base::DictValue args, Reply reply) {
    std::string error;
    auto run = std::make_unique<FrameRun>();
    run->widget = FindWidget(args, &error);
    const std::string* path = args.FindString("path");
    if (!run->widget)
      return std::move(reply).Run(Error(error));
    if (!path || !base::FilePath(*path).IsAbsolute())
      return std::move(reply).Run(Error("frames needs an absolute path"));
    run->path = base::FilePath(*path);
    run->scale = std::clamp(args.FindDouble("scale").value_or(0.5), 0.1, 4.0);
    run->count = std::clamp(args.FindInt("count").value_or(6), 1, 48);
    run->interval_ms = std::clamp(args.FindInt("interval_ms").value_or(50), 0, 10000);
    run->frames.resize(run->count);
    run->pending = run->count;
    run->reply = std::move(reply);
    const int delay_ms = std::clamp(args.FindInt("delay_ms").value_or(0), 0, 10000);
    base::DictValue* start_with = args.FindDict("start_with");
    std::optional<base::DictValue> first;
    if (start_with)
      first = start_with->Clone();
    run->args = std::move(args);
    auto begin = base::BindOnce(
        [](base::WeakPtr<Driver> self, std::unique_ptr<FrameRun> run, int delay_ms,
           base::DictValue started) {
          if (!self)
            return;
          if (const auto ok = started.FindBool("ok"); ok && !*ok) {
            std::move(run->reply).Run(std::move(started));
            return;
          }
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
              FROM_HERE,
              base::BindOnce(&Driver::StartFrames, self, std::move(run)),
              base::Milliseconds(delay_ms));
        },
        GetWeakPtr(), std::move(run), delay_ms);
    if (first)
      Run(std::move(*first), std::move(begin));
    else
      std::move(begin).Run(Ok());
  }

  void StartFrames(std::unique_ptr<FrameRun> owned) {
    FrameRun* run = owned.release();  // Freed by the last capture.
    run->start = base::TimeTicks::Now();
    for (int i = 0; i < run->count; ++i) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Driver::CaptureFrame, GetWeakPtr(), base::Unretained(run), i),
          base::Milliseconds(i * run->interval_ms));
    }
  }

  void CaptureFrame(FrameRun* run, int index) {
    if (!IsAlive(run->widget)) {
      FrameDone(run, index, SkBitmap());
      return;
    }
    CaptureWidget(run->widget, run->scale,
                  base::BindOnce(&Driver::FrameDone, GetWeakPtr(), base::Unretained(run), index));
  }

  void FrameDone(FrameRun* run, int index, SkBitmap bitmap) {
    run->frames[index] = {Crop(bitmap, run->args, run->scale),
                          base::TimeTicks::Now() - run->start};
    if (--run->pending)
      return;
    std::unique_ptr<FrameRun> owned(run);
    const bool any = std::any_of(owned->frames.begin(), owned->frames.end(),
                                 [](const auto& frame) { return !frame.first.drawsNothing(); });
    if (!any)
      return std::move(owned->reply).Run(Error("no frames (window hidden?)"));
    const bool sheet = owned->args.FindBool("sheet").value_or(true);
    base::DictValue result;
    result.Set("window", Title(owned->widget));
    result.Set("frames", owned->count);
    base::ListValue times;
    for (const auto& frame : owned->frames)
      times.Append(static_cast<int>(frame.second.InMilliseconds()));
    result.Set("times_ms", std::move(times));
    if (sheet) {
      const int columns = owned->args.FindInt("columns").value_or(std::min(owned->count, 4));
      WritePng(ContactSheet(owned->frames, columns), owned->path, std::move(owned->reply),
               std::move(result));
      return;
    }
    // One file per frame: <path stem>-01.png, -02.png, ...
    base::ListValue paths;
    for (size_t i = 0; i < owned->frames.size(); ++i) {
      const base::FilePath frame_path = owned->path.InsertBeforeExtensionASCII(
          base::StringPrintf("-%02zu", i + 1));
      paths.Append(frame_path.value());
      WritePng(owned->frames[i].first, frame_path, base::DoNothing(), base::DictValue());
    }
    result.Set("ok", true);
    result.Set("paths", std::move(paths));
    std::move(owned->reply).Run(std::move(result));
  }

  // ---- hover bookkeeping

  void SetHover(views::Widget* widget) {
    if (hover_widget_ == widget)
      return;
    hover_observation_.Reset();
    hover_widget_ = widget;
    if (widget)
      hover_observation_.Observe(widget);
  }

  void OnWidgetDestroying(views::Widget* widget) override {
    if (widget == hover_widget_) {
      hover_observation_.Reset();
      hover_widget_ = nullptr;
    }
  }

  raw_ptr<views::Widget> hover_widget_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver> hover_observation_{this};
  base::WeakPtrFactory<Driver> weak_factory_{this};
};

// --------------------------------------------------------------- listener

// Serves one connection at a time on a thread of its own; each line is one
// command, answered by one line.
class Listener final : public base::RefCountedThreadSafe<Listener>,
                       public base::PlatformThread::Delegate {
 public:
  Listener(int fd, scoped_refptr<base::SingleThreadTaskRunner> ui,
           base::WeakPtr<Driver> driver)
      : fd_(fd), ui_(std::move(ui)), driver_(std::move(driver)) {}

  void Stop() { stop_ = true; }

  void ThreadMain() override {
    base::PlatformThread::SetName("ZitterAutomation");
    while (!stop_) {
      pollfd ready{fd_, POLLIN, 0};
      if (poll(&ready, 1, 200) <= 0)
        continue;
      const int client = accept(fd_, nullptr, nullptr);
      if (client < 0)
        continue;
      Serve(client);
      close(client);
    }
    close(fd_);
    Release();  // The thread's own reference.
  }

 private:
  friend class base::RefCountedThreadSafe<Listener>;
  ~Listener() override = default;

  struct Answer : base::RefCountedThreadSafe<Answer> {
    base::WaitableEvent ready;
    base::Lock lock;
    std::string text;
    void Set(base::DictValue value) {
      {
        base::AutoLock hold(lock);
        text = base::WriteJson(value).value_or("{\"ok\":false}");
      }
      ready.Signal();
    }
   private:
    friend class base::RefCountedThreadSafe<Answer>;
    ~Answer() = default;
  };

  void Serve(int client) {
    std::string buffer;
    char chunk[4096];
    while (!stop_) {
      pollfd ready{client, POLLIN, 0};
      const int polled = poll(&ready, 1, 200);
      if (polled == 0)
        continue;
      if (polled < 0)
        return;
      const ssize_t read_size = read(client, chunk, sizeof(chunk));
      if (read_size <= 0)
        return;
      buffer.append(chunk, static_cast<size_t>(read_size));
      for (size_t end; (end = buffer.find('\n')) != std::string::npos;) {
        const std::string line = buffer.substr(0, end);
        buffer.erase(0, end + 1);
        if (base::TrimWhitespaceASCII(line, base::TRIM_ALL).empty())
          continue;
        std::string answer = Call(line) + "\n";
        for (size_t sent = 0; sent < answer.size();) {
          const std::string_view rest = std::string_view(answer).substr(sent);
          const ssize_t wrote = write(client, rest.data(), rest.size());
          if (wrote <= 0)
            return;
          sent += static_cast<size_t>(wrote);
        }
      }
    }
  }

  std::string Call(const std::string& line) {
    auto answer = base::MakeRefCounted<Answer>();
    ui_->PostTask(FROM_HERE,
                  base::BindOnce(&Driver::Handle, driver_, line,
                                 base::BindOnce(&Answer::Set, answer)));
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(120);
    while (!answer->ready.TimedWait(base::Milliseconds(200))) {
      if (stop_ || base::TimeTicks::Now() > deadline)
        return "{\"ok\":false,\"error\":\"timed out\"}";
    }
    base::AutoLock hold(answer->lock);
    return answer->text;
  }

  const int fd_;
  std::atomic<bool> stop_{false};
  const scoped_refptr<base::SingleThreadTaskRunner> ui_;
  const base::WeakPtr<Driver> driver_;
};

class UiAutomationImpl final : public UiAutomation {
 public:
  UiAutomationImpl(base::FilePath socket, int fd) : socket_(std::move(socket)) {
    listener_ = base::MakeRefCounted<Listener>(
        fd, base::SingleThreadTaskRunner::GetCurrentDefault(), driver_.GetWeakPtr());
    listener_->AddRef();  // Released when the thread ends.
    if (!base::PlatformThread::CreateNonJoinable(0, listener_.get())) {
      listener_->Release();
      close(fd);
    }
  }

  ~UiAutomationImpl() override {
    listener_->Stop();
    unlink(socket_.value().c_str());
  }

 private:
  const base::FilePath socket_;
  Driver driver_;
  scoped_refptr<Listener> listener_;
};

}  // namespace

// static
std::unique_ptr<UiAutomation> UiAutomation::Start(const base::FilePath& socket) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (socket.value().size() >= sizeof(address.sun_path)) {
    LOG(ERROR) << "Automation socket path is too long: " << socket.value();
    return nullptr;
  }
  std::copy(socket.value().begin(), socket.value().end(), address.sun_path);
  unlink(socket.value().c_str());
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return nullptr;
  const mode_t previous = umask(077);
  const bool bound =
      bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
  umask(previous);
  if (!bound || chmod(socket.value().c_str(), 0600) != 0 || listen(fd, 4) != 0) {
    PLOG(ERROR) << "Automation socket failed";
    close(fd);
    return nullptr;
  }
  LOG(WARNING) << "UI automation listening on " << socket.value();
  return std::make_unique<UiAutomationImpl>(socket, fd);
}

}  // namespace zitter::content_embedder
