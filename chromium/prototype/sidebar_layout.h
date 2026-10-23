// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_LAYOUT_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_LAYOUT_H_
#include <memory>
#include <vector>
#include "ui/gfx/geometry/size.h"
namespace views { class LayoutManager; }
namespace zitter::prototype {
struct WorkspaceSwipeUpdate { bool started, preview, finish, handled; };
struct WorkspaceSwipePage { int index; double x; };
struct WorkspaceSwipeLayout { int center; std::vector<WorkspaceSwipePage> pages; };
// Platform event/timer/window adapters consume this controller. The Metal backend
// owns the state in Rust; Views retains its existing policy for comparison.
class WorkspaceSwipeState {
 public:
  WorkspaceSwipeState();
  ~WorkspaceSwipeState();
  WorkspaceSwipeUpdate Event(float x,float y,int width,int index,int count,
      bool began,bool ended,bool momentum,bool phase_less);
  void Finish();
  void Cancel();
  bool tracking() const;
  bool phase_less() const;
  double position() const;
  int target(int count) const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
WorkspaceSwipeLayout SidebarSwipeLayout(double position,int count,int width);
// Rust owns list geometry in the Metal backend. The native adapter retains
// View identity, measurement, accessibility and input delivery during migration.
std::unique_ptr<views::LayoutManager> CreateSidebarColumnLayout(int spacing = 0);
std::unique_ptr<views::LayoutManager> CreateSidebarSplitLayout();
gfx::Size SidebarSplitPreferredSize();
std::unique_ptr<views::LayoutManager> CreateSidebarPinnedRowLayout();
int SidebarFolderIndent(int depth);
std::unique_ptr<views::LayoutManager> CreateSidebarFooterLayout();
std::unique_ptr<views::LayoutManager> CreatePaletteInputLayout();
std::unique_ptr<views::LayoutManager> CreateSidebarDotsLayout(int spacing = 0);
}
#endif
