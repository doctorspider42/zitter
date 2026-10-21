// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_METAL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_METAL_H_
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>
#include "base/functional/callback.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "base/containers/span.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"
namespace views { class ScrollBar; }
namespace zitter::sidebar { struct Item; }
namespace zitter::prototype {
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
struct SidebarScrollBarSnapshot { gfx::Rect bounds, thumb; unsigned color = 0; bool visible = false; bool horizontal = false; };
// Native event/scroll-service/menu/AX adapter; Rust owns thumb geometry and drag.
std::unique_ptr<views::ScrollBar> CreateSidebarScrollBar(base::RepeatingCallback<bool()> scene_visible, bool horizontal = false);
SidebarScrollBarSnapshot GetSidebarScrollBarSnapshot(views::ScrollBar* bar);
}
#endif
