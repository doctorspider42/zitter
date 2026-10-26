// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/sidebar_layout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <limits>
#include <vector>
#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_manager.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/controls/scroll_view.h"
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
class RustPanelLayout final : public views::LayoutManager {
 public:
  RustPanelLayout(bool vertical,gfx::Insets insets,int spacing,views::View* flex,bool center):vertical_(vertical),insets_(insets),spacing_(spacing),flex_(flex),center_(center){}
  void ViewAdded(views::View* host,views::View*) override{host->InvalidateLayout();}
  void ViewRemoved(views::View* host,views::View* view) override{if(view==flex_)flex_=nullptr;host->InvalidateLayout();}
  void Layout(views::View* host) override{const auto g=Measure(host,host->width(),host->height());size_t i=0;for(const auto& child:host->children())if(Included(child.get())){const auto& r=g.children[i++];child->SetBounds(r.x,r.y,r.width,r.height);}}
  gfx::Size GetPreferredSize(const views::View* host) const override{const auto g=Measure(host,-1,-1);return gfx::Size(g.width,g.height);}
  gfx::Size GetPreferredSize(const views::View* host,const views::SizeBounds& bounds) const override{if(!bounds.width().is_bounded())return GetPreferredSize(host);const auto g=Measure(host,bounds.width().value(),bounds.height().is_bounded()?bounds.height().value():-1);return gfx::Size(g.width,g.height);}
  int GetPreferredHeightForWidth(const views::View* host,int width) const override{return Measure(host,width,-1).height;}
  gfx::Size GetMinimumSize(const views::View* host) const override{const auto g=Measure(host,0,0);return gfx::Size(g.width,g.height);}
 private:
  static bool Included(const views::View* child){return child->GetVisible()&&!child->GetProperty(views::kViewIgnoredByLayoutKey);}
  sidebar::ColumnLayout Measure(const views::View* host,int width,int height) const {
    const auto insets=insets_+host->GetInsets();
    std::vector<const views::View*> native_views;std::vector<sidebar::ColumnChild> initial,measured;size_t flex=std::numeric_limits<size_t>::max();
    int natural_width=0;
    if(vertical_&&!center_&&width<0){for(const auto& child:host->children())if(Included(child.get())){const auto* margins=child->GetProperty(views::kMarginsKey);natural_width=std::max(natural_width,child->GetPreferredSize().width()+(margins?margins->width():0));}}
    for(const auto& child:host->children())if(Included(child.get())){
      if(child.get()==flex_)flex=initial.size();native_views.push_back(child.get());const auto* ptr=child->GetProperty(views::kMarginsKey);const auto margin=ptr?*ptr:gfx::Insets();
      views::SizeBounds bounds;if(width>=0)bounds.set_width(std::max(0,width-insets.width()-(vertical_?margin.width():0)));else if(vertical_&&!center_)bounds.set_width(std::max(0,natural_width-margin.width()));if(height>=0)bounds.set_height(std::max(0,height-insets.height()));
      const auto size=child->GetPreferredSize(bounds);initial.push_back({size.width(),size.height(),margin.top(),margin.left(),margin.bottom(),margin.right()});
    }
    const sidebar::PanelStyle style{width,height,vertical_,spacing_,insets.top(),insets.left(),insets.bottom(),insets.right(),flex};
    auto slice=[](const auto& values){return rust::Slice<const sidebar::ColumnChild>(values.data(),values.size());};
    auto allocate=[&](){return center_?sidebar::panel_center_layout(style,slice(initial),slice(measured)):sidebar::panel_layout(style,slice(initial),slice(measured));};
    auto result=allocate();
    if((vertical_?height:width)>=0&&!(width>=0&&width<=insets.width()&&height>=0&&height<=insets.height())){
      measured=initial;bool changed=false;
      int center_cross=vertical_?width-insets.width():height-insets.height();
      if(center_&&center_cross<=0){auto natural=style;natural.width=0;natural.height=0;const auto preferred=sidebar::panel_center_layout(natural,slice(initial),slice(measured));center_cross=vertical_?preferred.width-insets.width():preferred.height-insets.height();}
      for(size_t i=0;i<initial.size();++i){const auto& rect=result.children[i];if((vertical_?rect.height:rect.width)>=(vertical_?initial[i].height:initial[i].width))continue;auto bounds=views::SizeBounds(rect.width,rect.height);if(center_){if(vertical_)bounds.set_width(std::max(0,center_cross-initial[i].left-initial[i].right));else bounds.set_height(std::max(0,center_cross-initial[i].top-initial[i].bottom));}const auto size=native_views[i]->GetPreferredSize(bounds);measured[i].width=size.width();measured[i].height=size.height();changed=true;}
      if(changed)result=allocate();
    }
    return result;
  }
  bool vertical_;gfx::Insets insets_;int spacing_;raw_ptr<views::View> flex_;bool center_;
};
class RustPaletteInputLayout final : public views::LayoutManager {
 public:
  void ViewAdded(views::View* host,views::View*) override{host->InvalidateLayout();}
  void ViewRemoved(views::View* host,views::View*) override{host->InvalidateLayout();}
  void Layout(views::View* host) override {
    if(host->children().size()!=3)return;
    const auto icon=host->children()[0]->GetPreferredSize(),field=host->children()[1]->GetPreferredSize(),hint=host->children()[2]->GetPreferredSize();
    const auto g=sidebar::palette_input_layout(host->width(),host->height(),{0,0,icon.width(),icon.height()},field.height(),{0,0,hint.width(),hint.height()});
    const std::array<sidebar::Rect,3> rects{g.icon,g.input,g.hint};
    for(size_t i=0;i<rects.size();++i){const auto& r=rects[i];host->children()[i]->SetBounds(r.x,r.y,r.width,r.height);}
  }
  gfx::Size GetPreferredSize(const views::View*) const override{return gfx::Size(560,56);}
  gfx::Size GetPreferredSize(const views::View* host,const views::SizeBounds&) const override{return GetPreferredSize(host);}
};
class RustFooterLayout final : public views::LayoutManager {
 public:
  void ViewAdded(views::View* host,views::View*) override {host->InvalidateLayout();}
  void ViewRemoved(views::View* host,views::View*) override {host->InvalidateLayout();}
  void Layout(views::View* host) override {
    if (host->children().size()!=5) return;
    const auto g=Measure(host,host->width());
    for (size_t i=0;i<g.children.size();++i) {const auto& r=g.children[i];host->children()[i]->SetBounds(r.x,r.y,r.width,r.height);}
  }
  gfx::Size GetPreferredSize(const views::View* host) const override {
    const auto g=Measure(host,host->width()>0 ? host->width() : 200);return gfx::Size(200,g.height);
  }
  gfx::Size GetPreferredSize(const views::View* host,const views::SizeBounds& available) const override {
    const auto g=Measure(host,available.width().is_bounded() ? available.width().value() : 200);return gfx::Size(200,g.height);
  }
  int GetPreferredHeightForWidth(const views::View* host,int width) const override {return Measure(host,width).height;}
 private:
  sidebar::ColumnLayout Measure(const views::View* host,int width) const {
    int content=0;
    if (host->children().size()==5) {
      auto* scroll=static_cast<const views::ScrollView*>(host->children()[2].get());
      if (scroll->contents()) content=scroll->contents()->GetPreferredSize().width();
    }
    return sidebar::footer_layout(width,content);
  }
};
class RustDotsLayout final : public views::LayoutManager {
 public:
  explicit RustDotsLayout(int spacing) : spacing_(spacing) {}
  void ViewAdded(views::View* host,views::View*) override {host->InvalidateLayout();}
  void ViewRemoved(views::View* host,views::View*) override {host->InvalidateLayout();}
  void Layout(views::View* host) override {
    const auto g=Measure(host,host->width(),host->height());size_t i=0;
    for (const auto& child : host->children()) {const auto& r=g.children[i++];child->SetBounds(r.x,r.y,r.width,r.height);}
  }
  gfx::Size GetPreferredSize(const views::View* host) const override {
    const auto g=Measure(host,-1,-1);return gfx::Size(g.width,g.height);
  }
  gfx::Size GetPreferredSize(const views::View* host,const views::SizeBounds&) const override {return GetPreferredSize(host);}
 private:
  sidebar::ColumnLayout Measure(const views::View* host,int width,int height) const {
    std::vector<sidebar::ColumnChild> children;
    for (const auto& child : host->children()) {const auto size=child->GetPreferredSize();children.push_back({size.width(),size.height(),0,0,0,0});}
    return sidebar::strip_layout(width,height,spacing_,rust::Slice<const sidebar::ColumnChild>(children.data(),children.size()));
  }
  int spacing_;
};
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
void SetSidebarPanelLayout(views::View* host,bool vertical,gfx::Insets insets,int spacing,views::View* flex,bool center){
#if BUILDFLAG(IS_MAC)
  if(RustLayoutEnabled()){host->SetLayoutManager(std::make_unique<RustPanelLayout>(vertical,insets,spacing,flex,center));return;}
#endif
  auto* layout=host->SetLayoutManager(std::make_unique<views::BoxLayout>(vertical?views::BoxLayout::Orientation::kVertical:views::BoxLayout::Orientation::kHorizontal,insets,spacing));if(flex)layout->SetFlexForView(flex,1);if(center)layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
}
std::unique_ptr<views::LayoutManager> CreatePaletteInputLayout() {
#if BUILDFLAG(IS_MAC)
  if(RustLayoutEnabled())return std::make_unique<RustPaletteInputLayout>();
#endif
  return std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,gfx::Insets::VH(0,18),12);
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

std::unique_ptr<views::LayoutManager> CreateSidebarFooterLayout() {
#if BUILDFLAG(IS_MAC)
  return std::make_unique<RustFooterLayout>();
#else
  return std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal);
#endif
}
std::unique_ptr<views::LayoutManager> CreateSidebarDotsLayout(int spacing) {
#if BUILDFLAG(IS_MAC)
  return std::make_unique<RustDotsLayout>(spacing);
#else
  return std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,gfx::Insets(),spacing);
#endif
}

struct WorkspaceSwipeState::Impl {
#if BUILDFLAG(IS_MAC)
  std::optional<rust::Box<sidebar::WorkspaceSwipe>> rust;
#endif
  float x=0,y=0;int axis=0,start=0,count=0;
  bool tracking=false,closed=false,phase_less=false;double position=0;
};
WorkspaceSwipeState::WorkspaceSwipeState():impl_(std::make_unique<Impl>()) {
#if BUILDFLAG(IS_MAC)
  if(RustLayoutEnabled())impl_->rust.emplace(sidebar::new_workspace_swipe());
#endif
}
WorkspaceSwipeState::~WorkspaceSwipeState()=default;
WorkspaceSwipeUpdate WorkspaceSwipeState::Event(float x,float y,int width,int index,int count,bool began,bool ended,bool momentum,bool phase_less) {
#if BUILDFLAG(IS_MAC)
  if(impl_->rust){const auto u=(*impl_->rust)->event(x,y,width,index,count,began,ended,momentum,phase_less);return {u.started,u.preview,u.finish,u.handled};}
#endif
  auto& s=*impl_;const bool started=count>=2&&(began||(!s.tracking&&!momentum&&!ended&&(!s.closed||phase_less)));
  if(started){s.x=s.y=0;s.axis=0;s.closed=false;s.tracking=true;s.phase_less=phase_less;s.start=index;s.count=count;s.position=index;}
  WorkspaceSwipeUpdate out{started,false,false,s.axis==1};
  if(momentum||!s.tracking){if(momentum&&s.tracking){Finish();out.finish=true;}return out;}
  if(s.axis!=-1){
    s.x+=x;s.y+=y;
    if(!s.axis){if(std::abs(s.y)>=12&&std::abs(s.y)>std::abs(s.x))s.axis=-1;else if(std::abs(s.x)>=12&&std::abs(s.x)>=std::abs(s.y)*1.5f)s.axis=1;}
    if(s.axis==1){const double p=s.start-s.x/static_cast<double>(std::max(1,width)),last=s.count-1;s.position=p<0?-std::min(0.22,-p*0.2):p>last?last+std::min(0.22,(p-last)*0.2):p;out.preview=true;}
  }
  out.handled=s.axis==1;if(ended){Finish();out.finish=true;}return out;
}
void WorkspaceSwipeState::Finish(){
#if BUILDFLAG(IS_MAC)
  if(impl_->rust){(*impl_->rust)->finish();return;}
#endif
  impl_->tracking=false;impl_->closed=true;
}
void WorkspaceSwipeState::Cancel(){
#if BUILDFLAG(IS_MAC)
  if(impl_->rust){(*impl_->rust)->cancel();return;}
#endif
  Finish();
}
bool WorkspaceSwipeState::tracking() const {
#if BUILDFLAG(IS_MAC)
  if(impl_->rust)return (*impl_->rust)->tracking();
#endif
  return impl_->tracking;
}
bool WorkspaceSwipeState::phase_less() const {
#if BUILDFLAG(IS_MAC)
  if(impl_->rust)return (*impl_->rust)->phase_less();
#endif
  return impl_->phase_less;
}
double WorkspaceSwipeState::position() const {
#if BUILDFLAG(IS_MAC)
  if(impl_->rust)return (*impl_->rust)->position();
#endif
  return impl_->position;
}
int WorkspaceSwipeState::target(int count) const {
#if BUILDFLAG(IS_MAC)
  if(impl_->rust)return (*impl_->rust)->target(count);
#endif
  return count>0?std::clamp(static_cast<int>(std::round(impl_->position)),0,count-1):-1;
}
WorkspaceSwipeLayout SidebarSwipeLayout(double position,int count,int width) {
#if BUILDFLAG(IS_MAC)
  if(RustLayoutEnabled()){const auto g=sidebar::swipe_layout(position,count,width);WorkspaceSwipeLayout out{g.center,{}};for(const auto& p:g.pages)out.pages.push_back({p.index,p.x});return out;}
#endif
  if(count<=0)return {-1,{}};
  const int center=std::clamp(static_cast<int>(std::round(position)),0,count-1);WorkspaceSwipeLayout out{center,{}};
  for(int i=std::max(0,center-1);i<=std::min(count-1,center+1);++i)out.pages.push_back({i,(i-position)*width});return out;
}

}
