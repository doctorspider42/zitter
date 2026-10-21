#ifndef ZITTER_CHROMIUM_APP_UI_LAB_METAL_H_
#define ZITTER_CHROMIUM_APP_UI_LAB_METAL_H_

#include <memory>
#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "ui/gfx/native_ui_types.h"
#include "third_party/rust/cxx/v1/cxx.h"
#include "chromium/app/ui_lab_scene.rs.h"

namespace zitter::ui_lab {
// Native-view transport only: Rust owns scene generation, Chromium owns pages.
class MetalSurface {
 public:
  using Presented = base::OnceCallback<void(bool, double, double)>;
  virtual ~MetalSurface() = default;
  virtual gfx::NativeView native_view() = 0;
  // Latency is drawable.presentedTime minus the update's CACurrentMediaTime.
  // GPU time is command-buffer execution, not system composition time.
  virtual void Draw(base::span<const Rect> scene, Presented presented) = 0;
};
std::unique_ptr<MetalSurface> CreateMetalSurface(
    base::RepeatingCallback<void(float)> clicked, bool optimized);
// All backends must have an actually visible window for presentation timings.
void ShowLabWindow(gfx::NativeWindow window);
}  // namespace zitter::ui_lab
#endif
