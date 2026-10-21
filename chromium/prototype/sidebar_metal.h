// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_METAL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_METAL_H_
#include <memory>
#include "base/containers/span.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"
namespace views { class ScrollBar; }
namespace zitter::sidebar { struct Item; }
namespace zitter::prototype {
// A visual-only overlay. Native Views retain hit testing, scrolling, menus,
// keyboard/IME and accessibility; the overlay never becomes a responder.
class SidebarMetal {
 public:
  virtual ~SidebarMetal() = default;
  virtual void Update(gfx::NativeWindow window, gfx::Rect screen_bounds,
                      base::span<const sidebar::Item> items, unsigned background) = 0;
  virtual void Hide() = 0;
  virtual bool visible() const = 0;
};
std::unique_ptr<SidebarMetal> CreateSidebarMetal();
// Keep the native fading scroller outside the Metal canvas.
std::unique_ptr<views::ScrollBar> CreateSidebarScrollBar();
}
#endif
