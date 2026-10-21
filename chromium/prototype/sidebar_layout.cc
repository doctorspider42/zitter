// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/sidebar_layout.h"
#include <algorithm>
#include <optional>
#include <vector>
#include "base/command_line.h"
#include "build/build_config.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_manager.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/sidebar_scene.rs.h"
#endif
namespace zitter::prototype {
namespace {
#if BUILDFLAG(IS_MAC)
bool RustLayoutEnabled() {
  return base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar") == "rust-metal";
}
class RustSplitLayout final : public views::LayoutManager {
 public:
  void ViewAdded(views::View* host, views::View*) override { host->InvalidateLayout(); }
  void ViewRemoved(views::View* host, views::View*) override { host->InvalidateLayout(); }
  void Layout(views::View* host) override {
    size_t count = 0;
    for (const auto& child : host->children()) if (Included(child.get())) ++count;
    const auto result = sidebar::split_layout(host->width(), host->height(), count);
    size_t index = 0;
    for (const auto& child : host->children()) {
      if (!Included(child.get())) continue;
      const auto& r = result.children[index++];
      child->SetBounds(r.x, r.y, r.width, r.height);
    }
  }
  gfx::Size GetPreferredSize(const views::View*) const override { return SidebarSplitPreferredSize(); }
  gfx::Size GetPreferredSize(const views::View*, const views::SizeBounds&) const override {
    return SidebarSplitPreferredSize();
  }
 private:
  static bool Included(const views::View* view) {
    return view->GetVisible() && !view->GetProperty(views::kViewIgnoredByLayoutKey);
  }
};
class RustColumnLayout final : public views::LayoutManager {
 public:
  explicit RustColumnLayout(int spacing) : spacing_(spacing) {}
  void ViewAdded(views::View* host, views::View*) override { host->InvalidateLayout(); }
  void ViewRemoved(views::View* host, views::View*) override { host->InvalidateLayout(); }
  void Layout(views::View* host) override {
    const auto result = Measure(host, host->width());
    size_t index = 0;
    for (const auto& child : host->children()) {
      if (!Included(child.get())) continue;
      const auto& r = result.children[index++];
      child->SetBounds(r.x, r.y, r.width, r.height);
    }
  }
  gfx::Size GetPreferredSize(const views::View* host) const override {
    const auto result = Measure(host, std::nullopt);
    return gfx::Size(result.width, result.height);
  }
  gfx::Size GetPreferredSize(const views::View* host,
                            const views::SizeBounds& available) const override {
    const auto result = Measure(host, available.width().is_bounded()
        ? std::optional<int>(available.width().value()) : std::nullopt);
    return gfx::Size(result.width, result.height);
  }
  int GetPreferredHeightForWidth(const views::View* host, int width) const override {
    return Measure(host, width).height;
  }
 private:
  static bool Included(const views::View* view) {
    return view->GetVisible() && !view->GetProperty(views::kViewIgnoredByLayoutKey);
  }
  sidebar::ColumnLayout Measure(const views::View* host, std::optional<int> width) const {
    const auto insets = host->GetInsets();
    std::vector<sidebar::ColumnChild> children;
    for (const auto& child : host->children()) {
      if (!Included(child.get())) continue;
      const auto* margins = child->GetProperty(views::kMarginsKey);
      const auto margin = margins ? *margins : gfx::Insets();
      views::SizeBounds available;
      if (width) available.set_width(std::max(0, *width - insets.width() - margin.width()));
      const auto preferred = child->GetPreferredSize(available);
      children.push_back({preferred.width(), preferred.height(), margin.top(),
                          margin.left(), margin.bottom(), margin.right()});
    }
    return sidebar::column_layout(width.value_or(-1), spacing_, insets.top(),
        insets.left(), insets.bottom(), insets.right(),
        rust::Slice<const sidebar::ColumnChild>(children.data(), children.size()));
  }
  const int spacing_;
};
#endif
}
std::unique_ptr<views::LayoutManager> CreateSidebarColumnLayout(int spacing) {
#if BUILDFLAG(IS_MAC)
  if (RustLayoutEnabled())
    return std::make_unique<RustColumnLayout>(spacing);
#endif
  return std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                          gfx::Insets(), spacing);
}
std::unique_ptr<views::LayoutManager> CreateSidebarSplitLayout() {
#if BUILDFLAG(IS_MAC)
  if (RustLayoutEnabled()) return std::make_unique<RustSplitLayout>();
#endif
  auto layout = std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(4), 4);
  layout->SetDefaultFlex(1);
  return layout;
}
gfx::Size SidebarSplitPreferredSize() {
#if BUILDFLAG(IS_MAC)
  if (RustLayoutEnabled()) {
    const auto result = sidebar::split_layout(-1, -1, 0);
    return gfx::Size(result.width, result.height);
  }
#endif
  return gfx::Size(200, 46);
}
std::unique_ptr<views::LayoutManager> CreateSidebarPinnedRowLayout() {
#if BUILDFLAG(IS_MAC)
  if (RustLayoutEnabled()) return std::make_unique<RustColumnLayout>(0);
#endif
  return std::make_unique<views::FillLayout>();
}
int SidebarFolderIndent(int depth) {
#if BUILDFLAG(IS_MAC)
  if (RustLayoutEnabled()) return sidebar::folder_indent(depth);
#endif
  return std::clamp(depth, 0, 8) * 12;
}

}
