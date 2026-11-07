// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "base/i18n/rtl.h"
#include "chromium/prototype/sidebar_layout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <limits>
#include <vector>
#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "ui/views/layout/layout_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/background.h"
#include "ui/views/controls/separator.h"
#include "ui/color/color_id.h"
#include "ui/views/view_class_properties.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/sidebar_metal.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/scrollbar/scroll_bar.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/widget/widget.h"
namespace zitter::prototype {
namespace {
class RustViewportClip final : public views::View {
  METADATA_HEADER(RustViewportClip,views::View)
 public:
  explicit RustViewportClip(base::RepeatingCallback<void(const gfx::Rect&)> reveal):reveal_(std::move(reveal)){GetViewAccessibility().SetIsIgnored(true);SetPaintToLayer(ui::LAYER_NOT_DRAWN);layer()->SetMasksToBounds(true);layer()->SetFillsBoundsOpaquely(false);}
  void ScrollRectToVisible(const gfx::Rect& rect) override{reveal_.Run(rect);}
 private:
  base::RepeatingCallback<void(const gfx::Rect&)> reveal_;
};
BEGIN_METADATA(RustViewportClip)
END_METADATA
// macOS keeps its native overlay bar; elsewhere panels use Zitter's overlay
// bar instead of the platform's arrowed gutter (design: Overlay scrollbar).
std::unique_ptr<views::ScrollBar> DefaultPanelScrollBar(bool horizontal){
#if BUILDFLAG(IS_MAC)
  return views::PlatformStyle::CreateScrollBar(horizontal?views::ScrollBar::Orientation::kHorizontal:views::ScrollBar::Orientation::kVertical);
#else
  return CreatePanelOverlayScrollBar(horizontal);
#endif
}
class RustPanelViewport final : public views::View,public PanelViewport,public views::ScrollBarController {
  METADATA_HEADER(RustPanelViewport,views::View)
 public:
  explicit RustPanelViewport(base::RepeatingClosure changed,PanelViewportOptions options,std::unique_ptr<views::ScrollBar> primary_bar):changed_(std::move(changed)),options_(options){
    if (options_.edge_hover_scroll) SetNotifyEnterExitOnChild(true);
    clip_=AddChildView(std::make_unique<RustViewportClip>(base::BindRepeating(&RustPanelViewport::Reveal,weak_factory_.GetWeakPtr())));
    bar_=AddChildView(primary_bar?std::move(primary_bar):DefaultPanelScrollBar(options_.horizontal));bar_->set_controller(this);bar_->SetVisible(false);bar_->GetViewAccessibility().SetIsIgnored(true);bar_->GetViewAccessibility().SetIsLeaf(true);
    if(options_.dialog_style||options_.overflow_indicators){top_=AddChildView(std::make_unique<views::Separator>());bottom_=AddChildView(std::make_unique<views::Separator>());top_->SetVisible(false);bottom_->SetVisible(false);}
    auto& ax=GetViewAccessibility();ax.SetRole(ax::mojom::Role::kScrollView);ax.SetIsScrollable(true);ax.SetScrollXMin(0);ax.SetScrollXMax(0);ax.SetScrollYMin(0);ax.SetScrollYMax(0);
  }
  ~RustPanelViewport() override{bar_->set_controller(nullptr);}
  views::View* view() override{return this;}
  views::View* contents() const override{return contents_;}
  views::ScrollBar* vertical_scroll_bar() override{return options_.horizontal?nullptr:bar_;}
  views::ScrollBar* horizontal_scroll_bar() override{return options_.horizontal?bar_:nullptr;}
  void SetPanelContents(std::unique_ptr<views::View> value) override{clip_->RemoveAllChildViews();contents_background_=false;contents_=clip_->AddChildView(std::move(value));contents_->SetPaintToLayer();contents_->layer()->SetFillsBoundsOpaquely(false);ApplyBackground();InvalidateLayout();}
  void SetViewportBackground(unsigned color) override{background_=color;ApplyBackground();}
  void SetHeightRange(int minimum,int maximum) override{if(options_.minimum_height==minimum&&options_.maximum_height==maximum)return;options_.minimum_height=minimum;options_.maximum_height=maximum;PreferredSizeChanged();}
  gfx::Rect visible_rect() const override{return contents_?gfx::Rect(options_.horizontal?static_cast<int>(offset()):0,options_.horizontal?0:static_cast<int>(offset()),clip_->width(),clip_->height()):gfx::Rect();}
  double offset() const override{return state_->viewport_offset();}
  int maximum() const override{return state_->viewport_maximum();}
  bool rust_owned() const override{return true;}
  void ScrollTo(double position) override{if(state_->viewport_scroll_to(position))Apply(true);}
  // A scrolling viewport can shrink to show part of its contents: across the
  // scroll axis it needs only its configured minimum, never the whole content.
  gfx::Size GetMinimumSize() const override{const auto preferred=GetPreferredSize();return options_.horizontal?gfx::Size(0,preferred.height()):gfx::Size(preferred.width(),std::max(0,options_.minimum_height));}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& bounds) const override{const auto measured=contents_?contents_->GetPreferredSize(bounds):gfx::Size();const auto size=sidebar::viewport_preferred_size(measured.width(),measured.height(),options_.minimum_height,options_.maximum_height);return gfx::Size(size.width,size.height);}
  void Layout(PassKey) override{
    if(!contents_)return;auto preferred=contents_->GetPreferredSize(views::SizeBounds(width(),height()));
    if(!options_.use_contents_preferred_size&&!options_.horizontal){const int full_height=contents_->GetHeightForWidth(width());const int measured_width=sidebar::viewport_measure_width(width(),height(),full_height,bar_->GetThickness(),bar_->OverlapsContent());preferred=gfx::Size(measured_width,contents_->GetHeightForWidth(measured_width));}
    const double previous=offset();const auto plan=options_.horizontal?state_->viewport_configure_horizontal(width(),height(),preferred.width(),preferred.height(),bar_->GetThickness(),bar_->OverlapsContent()):state_->viewport_configure(width(),height(),preferred.width(),preferred.height(),bar_->GetThickness(),bar_->OverlapsContent());
    auto rect=[](const auto& r){return gfx::Rect(r.x,r.y,r.width,r.height);};clip_->SetBoundsRect(rect(plan.viewport));contents_->SetBoundsRect(rect(plan.content));bar_->SetBoundsRect(rect(plan.bar));bar_->SetVisible(plan.show_bar);contents_->DeprecatedLayoutImmediately();Apply(previous!=offset());
  }
  void OnThemeChanged() override{views::View::OnThemeChanged();ApplyBackground();}
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override {
    if (maximum() <= 0) return false;
    if (!options_.horizontal) return event.y_offset() != 0 && bar_->OnMouseWheel(event);
    // A regular mouse supplies only Y wheel ticks. Native horizontal input
    // keeps priority when both axes are present.
    const int delta = event.x_offset() != 0 ? event.x_offset() : event.y_offset();
    if (delta == 0) return false;
    edge_scroll_since_ = base::TimeTicks::Now();
    return bar_->ScrollByContentsOffset(delta);
  }
  void OnMouseEntered(const ui::MouseEvent&) override {
    if (options_.edge_hover_scroll && !edge_scroll_timer_.IsRunning())
      edge_scroll_timer_.Start(FROM_HERE, base::Milliseconds(16), this,
                               &RustPanelViewport::ScrollAtPointerEdge);
  }
  void OnMouseExited(const ui::MouseEvent&) override { StopEdgeScroll(); }
  void VisibilityChanged(views::View* starting_from, bool visible) override {
    views::View::VisibilityChanged(starting_from, visible);
    if (!visible) StopEdgeScroll();
  }
  bool OnKeyPressed(const ui::KeyEvent& event) override{return maximum()>0&&bar_->OnKeyPressed(event);}
  void OnScrollEvent(ui::ScrollEvent* event) override{
    if(maximum()<=0)return;bar_->ObserveScrollEvent(*event);
    // ObserveScrollEvent updates scrollbar feedback only. Cocoa supplies pixel
    // deltas, including momentum; fling-start values are velocities, not deltas.
    const float delta=options_.horizontal?event->x_offset():event->y_offset();
    if(event->type()==ui::EventType::kScroll&&std::isfinite(delta)&&delta!=0){if(state_->viewport_scroll_delta(delta))Apply(true);event->SetHandled();event->StopPropagation();}
  }
  void OnGestureEvent(ui::GestureEvent* event) override{if(maximum()>0)bar_->OnGestureEvent(event);}
  bool HandleAccessibleAction(const ui::AXActionData& action) override{
    if(action.action==(options_.horizontal?ax::mojom::Action::kScrollLeft:ax::mojom::Action::kScrollUp))return bar_->ScrollByAmount(views::ScrollBar::ScrollAmount::kPrevPage);
    if(action.action==(options_.horizontal?ax::mojom::Action::kScrollRight:ax::mojom::Action::kScrollDown))return bar_->ScrollByAmount(views::ScrollBar::ScrollAmount::kNextPage);
    if(action.action==ax::mojom::Action::kSetScrollOffset){ScrollTo(options_.horizontal?action.target_point.x():action.target_point.y());return true;}return views::View::HandleAccessibleAction(action);
  }
  void ScrollToPosition(views::ScrollBar* source,int position) override{if(source==bar_)ScrollTo(position);}
  int GetScrollIncrement(views::ScrollBar*,bool page,bool) override{const int extent=options_.horizontal?clip_->width():clip_->height();return page?extent:extent/5;}
 private:
  void StopEdgeScroll() {
    edge_scroll_timer_.Stop();
    edge_scroll_direction_ = 0;
  }
  void ScrollAtPointerEdge() {
    auto* widget = GetWidget();
    if (!widget || !widget->IsVisible() || !IsDrawn()) {
      StopEdgeScroll();
      return;
    }
    auto* screen = display::Screen::Get();
    auto point = screen->GetCursorScreenPoint();
    views::View::ConvertPointFromScreen(clip_, &point);
    if (maximum() == 0 || widget->IsMouseButtonDown() ||
        !clip_->GetLocalBounds().Contains(point) ||
        !screen->IsWindowUnderCursor(widget->GetNativeWindow())) {
      edge_scroll_direction_ = 0;
      return;
    }
    const int edge = std::min(20, clip_->width() / 3);
    const int left = point.x();
    const int right = clip_->width() - 1 - left;
    const int distance = std::min(left, right);
    if (distance >= edge) {
      edge_scroll_direction_ = 0;
      return;
    }
    const int direction = (left < right) == base::i18n::IsRTL() ? 1 : -1;
    const auto now = base::TimeTicks::Now();
    if (direction != edge_scroll_direction_) {
      edge_scroll_direction_ = direction;
      edge_scroll_since_ = now;
    }
    if (now - edge_scroll_since_ < base::Milliseconds(180)) return;
    const double proximity = 1.0 - static_cast<double>(distance) / edge;
    ScrollTo(offset() + direction * (0.75 + 4.25 * proximity));
  }

  void ApplyBackground(){std::optional<ui::ColorVariant> color;if(background_)color=*background_;else if(options_.dialog_style)color=ui::kColorDialogBackground;SetBackground(color?views::CreateSolidBackground(*color):nullptr);if(contents_){
      // Clear only a fill this viewport installed; panel contents such as
      // settings cards own their own rounded background.
      if(color||contents_background_)contents_->SetBackground(color?views::CreateSolidBackground(*color):nullptr);
      contents_background_=color.has_value();
      contents_->layer()->SetFillsBoundsOpaquely(background_?((*background_>>24)==0xff):options_.dialog_style);}}
  void Reveal(const gfx::Rect& rect){if(state_->viewport_reveal(options_.horizontal?rect.x():rect.y(),options_.horizontal?rect.width():rect.height()))Apply(true);}
  void Apply(bool moved){
    if(!contents_)return;// Transforms are physical; in RTL the logical start is on the right, so a
    // horizontal offset moves mirrored contents rightwards.
    const float horizontal=(base::i18n::IsRTL()?1.f:-1.f)*static_cast<float>(offset());
    contents_->SetTransform(gfx::Transform::MakeTranslation(options_.horizontal?horizontal:0,options_.horizontal?0:-static_cast<float>(offset())));bar_->Update(options_.horizontal?clip_->width():clip_->height(),options_.horizontal?contents_->width():contents_->height(),static_cast<int>(offset()));auto& ax=GetViewAccessibility();if(options_.horizontal){ax.SetScrollX(static_cast<int>(offset()));ax.SetScrollXMax(maximum());}else{ax.SetScrollY(static_cast<int>(offset()));ax.SetScrollYMax(maximum());}if(top_){const auto e=state_->viewport_axis_edges(options_.horizontal?height():width(),options_.horizontal);top_->SetBounds(e.top.x,e.top.y,e.top.width,e.top.height);bottom_->SetBounds(e.bottom.x,e.bottom.y,e.bottom.width,e.bottom.height);top_->SetVisible(e.show_top);bottom_->SetVisible(e.show_bottom);}if(changed_)changed_.Run();if(moved)ax.NotifyEvent(ax::mojom::Event::kScrollPositionChanged,true);
  }
  base::RepeatingClosure changed_;PanelViewportOptions options_;std::optional<unsigned> background_;bool contents_background_=false;
  rust::Box<sidebar::ScrollViewportState> state_=sidebar::new_scroll_viewport();
  raw_ptr<views::Separator> top_=nullptr,bottom_=nullptr;
  raw_ptr<RustViewportClip> clip_=nullptr;raw_ptr<views::View> contents_=nullptr;raw_ptr<views::ScrollBar> bar_=nullptr;
  base::RepeatingTimer edge_scroll_timer_;
  base::TimeTicks edge_scroll_since_;
  int edge_scroll_direction_ = 0;
  base::WeakPtrFactory<RustPanelViewport> weak_factory_{this};
};
BEGIN_METADATA(RustPanelViewport)
END_METADATA

class RustPanelLayout final : public views::LayoutManager {
 public:
  RustPanelLayout(bool vertical,gfx::Insets insets,int spacing,views::View* flex,bool center):vertical_(vertical),insets_(insets),spacing_(spacing),flex_(flex),center_(center){}
  void ViewAdded(views::View* host,views::View*) override{host->InvalidateLayout();}
  void ViewRemoved(views::View* host,views::View* view) override{if(view==flex_)flex_=nullptr;host->InvalidateLayout();}
  void Layout(views::View* host) override{const auto g=Measure(host,host->width(),host->height());size_t i=0;for(const auto& child:host->children())if(Included(child.get())){const auto& r=g.children[i++];child->SetBounds(r.x,r.y,r.width,r.height);}}
  gfx::Size GetPreferredSize(const views::View* host) const override{const auto g=Measure(host,-1,-1);return gfx::Size(g.width,g.height);}
  gfx::Size GetPreferredSize(const views::View* host,const views::SizeBounds& bounds) const override{if(!bounds.width().is_bounded())return GetPreferredSize(host);const auto g=Measure(host,bounds.width().value(),bounds.height().is_bounded()?bounds.height().value():-1);return gfx::Size(g.width,g.height);}
  int GetPreferredHeightForWidth(const views::View* host,int width) const override{return Measure(host,width,-1).height;}
  // A flex child that scrolls (a panel viewport) gives up the length it can scroll, so a
  // panel fits a short window and scrolls instead of outgrowing it.
  gfx::Size GetMinimumSize(const views::View* host) const override{const auto g=Measure(host,0,0);gfx::Size size(g.width,g.height);
    if(flex_&&Included(flex_)&&views::AsViewClass<RustPanelViewport>(flex_)){const auto preferred=flex_->GetPreferredSize();const auto minimum=flex_->GetMinimumSize();
      if(vertical_)size.set_height(std::max(0,size.height()-std::max(0,preferred.height()-minimum.height())));else size.set_width(std::max(0,size.width()-std::max(0,preferred.width()-minimum.width())));}
    // A row needs only its tallest child's own minimum height (a column keeps
    // its widest child's preferred width, as BoxLayout does).
    if(!vertical_){const auto insets=insets_+host->GetInsets();int cross=0;
      for(const auto& child:host->children())if(Included(child.get())){const auto* m=child->GetProperty(views::kMarginsKey);cross=std::max(cross,child->GetMinimumSize().height()+(m?m->height():0));}
      size.set_height(std::min(size.height(),cross+insets.height()));}
    return size;}
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
  // Children: the space rail viewport, a spacer, the Downloads ring and „+”.
  void Layout(views::View* host) override {
    if (host->children().size()!=4) return;
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
    if (host->children().size()==4) {
      auto* scroll=PanelViewportForView(host->children()[0].get());
      if (scroll&&scroll->contents()) content=scroll->contents()->GetPreferredSize().width();
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
    for (const auto& child : host->children()) {if(!child->GetVisible())continue;const auto& r=g.children[i++];child->SetBounds(r.x,r.y,r.width,r.height);}
  }
  gfx::Size GetPreferredSize(const views::View* host) const override {
    const auto g=Measure(host,-1,-1);return gfx::Size(g.width,g.height);
  }
  gfx::Size GetPreferredSize(const views::View* host,const views::SizeBounds&) const override {return GetPreferredSize(host);}
 private:
  sidebar::ColumnLayout Measure(const views::View* host,int width,int height) const {
    std::vector<sidebar::ColumnChild> children;
    // Hidden children take no room.
    for (const auto& child : host->children()) {if(!child->GetVisible())continue;const auto size=child->GetPreferredSize();children.push_back({size.width(),size.height(),0,0,0,0});}
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
}
PanelViewport* PanelViewportForView(views::View* view){
  return views::AsViewClass<RustPanelViewport>(view);
}
std::unique_ptr<views::View> CreatePanelViewport(PanelViewport** adapter,base::RepeatingClosure changed,PanelViewportOptions options){return CreatePanelViewport(adapter,std::move(changed),options,{});}
std::unique_ptr<views::View> CreatePanelViewport(PanelViewport** adapter,base::RepeatingClosure changed,PanelViewportOptions options,std::unique_ptr<views::ScrollBar> primary_bar){
  auto host=std::make_unique<RustPanelViewport>(std::move(changed),options,std::move(primary_bar));*adapter=host.get();return host;
}
void SetSidebarPanelLayout(views::View* host,bool vertical,gfx::Insets insets,int spacing,views::View* flex,bool center){
  host->SetLayoutManager(std::make_unique<RustPanelLayout>(vertical,insets,spacing,flex,center));
}
std::unique_ptr<views::LayoutManager> CreatePaletteInputLayout() {
  return std::make_unique<RustPaletteInputLayout>();
}
class ProfileFavoritesLayout final : public views::LayoutManager {
 public:
  void Layout(views::View* host) override {
    const auto grid = Measure(host->width());
    int index = 0;
    for (const auto& child : host->children()) {
      if (!child->GetVisible()) continue;
      const int column = index % grid.columns;
      const int x = column * (grid.cell_width + kGap) + std::min(column, grid.remainder);
      const int width = grid.cell_width + (column < grid.remainder ? 1 : 0);
      child->SetBounds(x, (index / grid.columns) * (grid.cell_height + kGap),
                       width, grid.cell_height);
      ++index;
    }
  }
  gfx::Size GetPreferredSize(const views::View* host) const override {
    return gfx::Size(kPreferredWidth, GetPreferredHeightForWidth(host, kPreferredWidth));
  }
  gfx::Size GetPreferredSize(const views::View* host,
                             const views::SizeBounds& available) const override {
    const int width = available.width().is_bounded() ? available.width().value() : kPreferredWidth;
    return gfx::Size(width, GetPreferredHeightForWidth(host, width));
  }
  int GetPreferredHeightForWidth(const views::View* host, int width) const override {
    const auto grid = Measure(width);
    const int count = std::ranges::count_if(host->children(), [](const auto& child) {
      return child->GetVisible();
    });
    const int rows = (count + grid.columns - 1) / grid.columns;
    return rows ? rows * (grid.cell_height + kGap) - kGap : 0;
  }
  void ViewAdded(views::View* host,views::View*) override {host->InvalidateLayout();}
  void ViewRemoved(views::View* host,views::View*) override {host->InvalidateLayout();}

 private:
  struct Grid {
    int columns;
    int cell_width;
    int cell_height;
    int remainder;
  };
  static constexpr int kGap = 8;
  static constexpr int kMinCellWidth = 48;
  static constexpr int kPreferredWidth = 200;
  static Grid Measure(int width) {
    const int columns = std::max(1, (width + kGap) / (kMinCellWidth + kGap));
    const int available = width - (columns - 1) * kGap;
    const int cell = available / columns;
    return {columns, cell, cell - cell / 6, available % columns};
  }
};
std::unique_ptr<views::LayoutManager> CreateProfileFavoritesLayout(){return std::make_unique<ProfileFavoritesLayout>();}
std::unique_ptr<views::LayoutManager> CreateSidebarColumnLayout(int spacing) {
  return std::make_unique<RustColumnLayout>(spacing);
}
std::unique_ptr<views::LayoutManager> CreateSidebarSplitLayout() {
  return std::make_unique<RustSplitLayout>();
}
gfx::Size SidebarSplitPreferredSize() {
  const auto result = sidebar::split_layout(-1, -1, 0);
  return gfx::Size(result.width, result.height);
}
std::unique_ptr<views::LayoutManager> CreateSidebarPinnedRowLayout() {
  return std::make_unique<RustColumnLayout>(0);
}
int SidebarFolderIndent(int depth) {
  return sidebar::folder_indent(depth);
}

std::unique_ptr<views::LayoutManager> CreateSidebarFooterLayout() {
  return std::make_unique<RustFooterLayout>();
}
std::unique_ptr<views::LayoutManager> CreateSidebarDotsLayout(int spacing) {
  return std::make_unique<RustDotsLayout>(spacing);
}

struct WorkspaceSwipeState::Impl {
  rust::Box<sidebar::WorkspaceSwipe> rust=sidebar::new_workspace_swipe();
};
WorkspaceSwipeState::WorkspaceSwipeState():impl_(std::make_unique<Impl>()) {}
WorkspaceSwipeState::~WorkspaceSwipeState()=default;
WorkspaceSwipeUpdate WorkspaceSwipeState::Event(float x,float y,int width,int index,int count,bool began,bool ended,bool momentum,bool phase_less) {
  const auto u=impl_->rust->event(x,y,width,index,count,began,ended,momentum,phase_less);return {u.started,u.preview,u.finish,u.handled};
}
void WorkspaceSwipeState::Finish(){impl_->rust->finish();}
void WorkspaceSwipeState::Cancel(){impl_->rust->cancel();}
bool WorkspaceSwipeState::tracking() const {return impl_->rust->tracking();}
bool WorkspaceSwipeState::phase_less() const {return impl_->rust->phase_less();}
double WorkspaceSwipeState::position() const {return impl_->rust->position();}
int WorkspaceSwipeState::target(int count) const {return impl_->rust->target(count);}
WorkspaceSwipeLayout SidebarSwipeLayout(double position,int count,int width) {
  const auto g=sidebar::swipe_layout(position,count,width);WorkspaceSwipeLayout out{g.center,{}};for(const auto& p:g.pages)out.pages.push_back({p.index,p.x});return out;
}
}
