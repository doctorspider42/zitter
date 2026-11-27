// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_CRASHED_PAGE_VIEW_H_
#define ZITTER_CHROMIUM_PROTOTYPE_CRASHED_PAGE_VIEW_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace zitter::sidebar {
struct DesignPalette;
}

namespace zitter::prototype {

// Why the page's renderer stopped. The host maps base::TerminationStatus.
enum class PageCrashReason { kCrashed, kKilled, kOutOfMemory };

// Shown in place of a page whose renderer process died (views::WebView's
// crashed overlay). It matches the network error page: a card with a symbol,
// a title, a hint, a Reload pill and the reason's short code.
class CrashedPageView final : public views::View {
  METADATA_HEADER(CrashedPageView, views::View)

 public:
  CrashedPageView(base::RepeatingCallback<sidebar::DesignPalette()> palette,
                  base::RepeatingClosure reload);
  ~CrashedPageView() override;

  void SetReason(PageCrashReason reason);
  PageCrashReason reason() const { return reason_; }
  views::View* reload_button_for_testing() const;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;

 private:
  class ReloadButton;

  gfx::Rect CardBounds() const;
  std::u16string Title() const;
  std::u16string Hint() const;
  std::u16string Code() const;

  base::RepeatingCallback<sidebar::DesignPalette()> palette_;
  PageCrashReason reason_ = PageCrashReason::kCrashed;
  raw_ptr<ReloadButton> reload_ = nullptr;
};

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_CRASHED_PAGE_VIEW_H_
