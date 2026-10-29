// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_LAYOUT_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_LAYOUT_H_
#include <memory>
#include <vector>
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/rect.h"
#include "base/functional/callback.h"
#include "ui/gfx/geometry/insets.h"
namespace views { class LayoutManager; class View; class ScrollBar; }
namespace zitter::prototype {
// A viewport owns scrolling state; native Views and Rust implementations share
// only measurement/event/window adapters. The returned View owns the interface.
class PanelViewport {
 public:
  virtual ~PanelViewport()=default;
  virtual views::View* view()=0;
  virtual views::View* contents() const=0;
  virtual views::ScrollBar* vertical_scroll_bar()=0;
  virtual views::ScrollBar* horizontal_scroll_bar()=0;
  virtual void SetPanelContents(std::unique_ptr<views::View> contents)=0;
  virtual void SetViewportBackground(unsigned color)=0;
  virtual void SetHeightRange(int minimum,int maximum)=0;
  virtual gfx::Rect visible_rect() const=0;
  virtual double offset() const=0;
  virtual int maximum() const=0;
  virtual void ScrollTo(double position)=0;
  virtual bool rust_owned() const=0;
};
struct PanelViewportOptions {
  int minimum_height = -1;
  int maximum_height = -1;
  bool use_contents_preferred_size = true;
  bool dialog_style = false;
  bool horizontal_scroll_enabled = false;
  bool overflow_indicators = false;
  // A single primary axis; offsets/reveal/scroll actions use X when horizontal.
  bool horizontal = false;
};
std::unique_ptr<views::View> CreatePanelViewport(PanelViewport** adapter,base::RepeatingClosure changed,PanelViewportOptions options = {});
std::unique_ptr<views::View> CreatePanelViewport(PanelViewport** adapter,base::RepeatingClosure changed,PanelViewportOptions options,std::unique_ptr<views::ScrollBar> primary_bar);
PanelViewport* PanelViewportForView(views::View* view);
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
// Rust owns list geometry. The native adapter retains View identity,
// measurement, accessibility and input delivery.
std::unique_ptr<views::LayoutManager> CreateSidebarColumnLayout(int spacing = 0);
// Install a start/stretch panel with a single optional flexible child; Rust
// owns allocation.
void SetSidebarPanelLayout(views::View* host,bool vertical,gfx::Insets insets,int spacing,views::View* flex = nullptr,bool center = false);
std::unique_ptr<views::LayoutManager> CreateSidebarSplitLayout();
gfx::Size SidebarSplitPreferredSize();
std::unique_ptr<views::LayoutManager> CreateSidebarPinnedRowLayout();
int SidebarFolderIndent(int depth);
// The space rail, a spacer and the footer's „+”.
std::unique_ptr<views::LayoutManager> CreateSidebarFooterLayout();
std::unique_ptr<views::LayoutManager> CreatePaletteInputLayout();
std::unique_ptr<views::LayoutManager> CreateSidebarDotsLayout(int spacing = 0);
}
#endif
