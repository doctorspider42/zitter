// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_METAL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_METAL_H_
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "base/functional/callback.h"
#include "base/time/time.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "base/containers/span.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"
namespace gfx {class Canvas;}
namespace views { class ScrollBar; class ScrollView; class View; class Button; }
namespace zitter::sidebar { struct Item; struct Primitive; }
namespace zitter::prototype {
// Shared control scene with native event/focus/AX adapters. All managed text,
// actions and progress share one transparent canvas; browser services stay native.
class PanelScene {
 public:
  virtual ~PanelScene()=default;
  virtual std::unique_ptr<views::View> MakeText(std::u16string text,bool multiline,bool center,int fixed_width)=0;
  virtual std::unique_ptr<views::View> MakeSelect(std::vector<std::u16string> labels,std::u16string name)=0;
  virtual void SetText(views::View* view,std::u16string text)=0;
  virtual std::unique_ptr<views::View> MakeActions(int spacing)=0;
  virtual views::Button* AddAction(views::View* group,base::RepeatingClosure callback,std::u16string text)=0;
  virtual void SetActionText(views::Button* button,std::u16string text)=0;
  virtual std::unique_ptr<views::View> MakeProgress()=0;
  virtual void SetProgress(views::View* view,double value)=0;
  virtual void Schedule()=0;
  virtual void ObserveScroll(views::ScrollView* scroll)=0;
  virtual bool visible() const=0;
  virtual bool contains_text(std::string_view text) const=0;
  virtual bool excludes(views::View* view) const=0;
  virtual bool order_correct() const=0;
  virtual bool animating() const=0;
};
std::unique_ptr<PanelScene> CreatePanelScene(views::View* root);
struct SidebarViewportNode { size_t parent; gfx::Rect bounds; bool visible; };
struct SidebarProjectedNode { size_t index; gfx::RectF bounds, clip; };
struct SidebarViewportProjection { gfx::PointF offset; std::vector<SidebarProjectedNode> nodes; };
// A visual-only overlay. Native Views retain hit testing, scrolling, menus,
// keyboard/IME and accessibility; the overlay never becomes a responder.
class SidebarMetal {
 public:
  virtual ~SidebarMetal() = default;
  virtual SidebarViewportProjection Project(gfx::Size content, gfx::Size viewport,
      gfx::PointF offset, base::span<const SidebarViewportNode> nodes) = 0;
  virtual void Update(gfx::NativeWindow window, gfx::Rect screen_bounds,
                      base::span<const sidebar::Item> items, unsigned background) = 0;
  virtual void UpdateScene(gfx::NativeWindow window,gfx::Rect screen_bounds,
      base::span<const sidebar::Primitive> primitives,unsigned background)=0;
  // Transparent chrome above the native host but below other Metal controls.
  // Interactive rectangles must already be removed from primitive clips.
  virtual void UpdateUnderlay(gfx::NativeWindow window,gfx::Rect bounds,
      base::span<const sidebar::Primitive> primitives)=0;
  virtual bool underlay_order_correct_for_testing() const=0;
  // A full page translated inside a viewport-sized native clipping adapter.
  // Immutable page scenes set redraw only on capture. Translation/settle reuse
  // the drawable; bounds/scale changes and missing drawables also force redraw.
  virtual void UpdatePreview(gfx::NativeWindow window,gfx::Rect viewport,double x,
      base::span<const sidebar::Item> items,unsigned background,bool settle,bool redraw) = 0;
  // One-shot actual Metal drawable presentation, dispatched back to the UI
  // sequence. Register before a forced UpdatePreview; no synchronous GPU wait.
  virtual void SetNextPresentationCallback(base::OnceClosure presented) = 0;
  virtual void AnimatePreview(double x,base::TimeDelta duration,base::OnceClosure done) = 0;
  virtual void Hide() = 0;
  virtual bool visible() const = 0;
  // Submitted command buffers, not presentation timestamps. UI thread only.
  virtual size_t frame_count() const = 0;
  // Last submitted display-list text, not proof of physical presentation.
  virtual bool contains_text_for_testing(std::string_view text) const = 0;
  virtual gfx::Rect screen_bounds_for_testing() const = 0;
  virtual bool contains_rect_for_testing(gfx::RectF bounds, gfx::RectF clip) const = 0;
};
std::unique_ptr<SidebarMetal> CreateSidebarMetal();
// Same untruncated CoreText editor line for a missing-device/drawable fallback.
void PaintEditorSceneFallback(gfx::Canvas* canvas,base::span<const sidebar::Primitive> scene);
// CoreText typography adapter, matching the font used for GPU text tiles.
// Returns complete UTF-8 line fragments, without any native View/Label layout.
std::vector<std::string> ShapeSidebarParagraph(std::string_view text,float font_size,int width,bool bold = false);
std::vector<std::string> ShapeSidebarParagraphWeight(std::string_view text,float font_size,int width,int weight);
struct SidebarScrollBarSnapshot { gfx::Rect bounds, thumb; unsigned color = 0; bool visible = false; bool horizontal = false; };
// Native event/scroll-service/menu/AX adapter; Rust owns thumb geometry and drag.
std::unique_ptr<views::ScrollBar> CreateSidebarScrollBar(base::RepeatingCallback<bool()> scene_visible, bool horizontal = false);
SidebarScrollBarSnapshot GetSidebarScrollBarSnapshot(views::ScrollBar* bar);
}
#endif
