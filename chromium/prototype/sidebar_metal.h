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
#include "build/build_config.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "base/containers/span.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"
namespace gfx {class Canvas;}
namespace views { class ScrollBar; class ScrollView; class View; class Button; }
namespace zitter::sidebar { struct Item; struct Primitive; }
namespace gfx { struct VectorIcon; }
namespace zitter::prototype {
// Shared control scene with native event/focus/AX adapters. All managed text,
// actions and progress share one transparent canvas; browser services stay native.
bool SceneActionPressedForTesting(views::View* view);

class EditorControl;
class PanelScene {
 public:
  virtual ~PanelScene()=default;
  virtual std::unique_ptr<views::View> MakeText(std::u16string text,bool multiline,bool center,int fixed_width)=0;
  virtual std::unique_ptr<views::View> MakeSelect(views::View* parent,std::vector<std::u16string> labels,std::u16string name)=0;
  virtual void SetText(views::View* view,std::u16string text)=0;
  virtual std::unique_ptr<views::View> MakeActions(int spacing)=0;
  virtual views::View* AddAction(views::View* group,base::RepeatingClosure callback,std::u16string text)=0;
  virtual void SetActionText(views::View* button,std::u16string text)=0;
  virtual std::unique_ptr<views::View> MakeProgress()=0;
  virtual void SetProgress(views::View* view,double value)=0;
  // Downloads page parts (design: Downloads).
  enum class Tone { kInk, kMuted, kOk, kWarn, kDanger };
  // `style` is a design text style id (2 display-l, 5 body, 6 label, 8 url).
  virtual void SetTextStyle(views::View* text,uint32_t style,Tone tone)=0;
  virtual std::unique_ptr<views::View> MakeFileTile()=0;
  virtual void SetFileTile(views::View* tile,std::u16string extension)=0;
  // A 32 px icon button (IconButton) appended to `parent`.
  virtual views::View* AddIconAction(views::View* parent,base::RepeatingClosure callback,std::u16string name,const gfx::VectorIcon* icon)=0;
  virtual void SetIconAction(views::View* button,std::u16string name,const gfx::VectorIcon* icon)=0;
  virtual void SetProgressPaused(views::View* progress,bool paused)=0;
  // The Downloads window (design: Okno Pobrane).
  // A text action's look: 0 primary, 2 outline, 3 quiet, 4 danger text,
  // 5 danger strong, 6 filter chip, 7 chosen filter chip.
  virtual void SetActionKind(views::View* button,uint32_t kind)=0;
  // A search field: `body` text in a `bg-base` pill with a `line` edge.
  virtual EditorControl* AddSearch(views::View* parent,std::u16string placeholder,std::u16string name)=0;
  // Repaints the field in the window's current scheme (after it has one).
  virtual void RefreshSearch(EditorControl* field)=0;
  // A 28 px „⋯” IconButton for a row's menu.
  virtual views::View* AddMoreAction(views::View* parent,base::RepeatingClosure callback,std::u16string name)=0;
  // While choosing files the tile is a checkbox: `pressed` flips it.
  virtual void SetFileTileChoice(views::View* tile,bool choosing,bool checked,base::RepeatingClosure pressed)=0;
  // Site permission cards (design: Zitter dostępy stron).
  // A 28 px `accent-soft` tile with `icon` in `accent` (radius-md).
  virtual std::unique_ptr<views::View> MakeIconTile(const gfx::VectorIcon* icon)=0;
  virtual void SetSelectChanged(views::View* select,base::RepeatingClosure changed)=0;
  virtual std::optional<size_t> SelectIndex(views::View* select) const=0;
  virtual void SetSelectIndex(views::View* select,size_t index)=0;
  virtual void Schedule()=0;
  virtual base::RepeatingClosure ViewportChangedCallback()=0;
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
// Compositor raster by default; Metal with --zitter-rust-raster=metal on macOS.
// Never returns null.
std::unique_ptr<SidebarMetal> CreateSidebarMetal();
#if BUILDFLAG(IS_MAC)
// The in-process Metal overlay; null when Metal is unavailable.
std::unique_ptr<SidebarMetal> CreateMetalSidebarSurface();
#endif
// Diagnostics for memory budgeting: live/visible surfaces, cached text tile
// bytes and current drawable bytes (drawableSize x 4 x maximumDrawableCount).
struct SidebarMetalStats { size_t surfaces = 0, visible = 0, text_bytes = 0, drawable_bytes = 0, device_bytes = 0, frames = 0; double drawable_wait_ms = 0, render_ms = 0; };
SidebarMetalStats GetSidebarMetalStatsForTesting();
// Platform text services. macOS implements them with CoreText in
// sidebar_metal.mm; other platforms with gfx::RenderText in
// text_services_skia.cc.
// Paints an editor scene (untruncated shaped lines) on the compositor canvas.
void PaintEditorSceneFallback(gfx::Canvas* canvas,base::span<const sidebar::Primitive> scene);
// Typography adapter, matching the font used for GPU text tiles.
// Returns complete UTF-8 line fragments, without any native View/Label layout.
std::vector<std::string> ShapeSidebarParagraph(std::string_view text,float font_size,int width,bool bold = false);
std::vector<std::string> ShapeSidebarParagraphWeight(std::string_view text,float font_size,int width,int weight);
struct SidebarScrollBarSnapshot { gfx::Rect bounds, thumb; unsigned color = 0; bool visible = false; bool horizontal = false; };
// Native event/scroll-service/menu/AX adapter; Rust owns thumb geometry and drag.
std::unique_ptr<views::ScrollBar> CreateSidebarScrollBar(base::RepeatingCallback<bool()> scene_visible, bool horizontal = false);
SidebarScrollBarSnapshot GetSidebarScrollBarSnapshot(views::ScrollBar* bar);
}
#endif
