// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/sidebar_metal.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/sidebar_shader.h"
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import <QuartzCore/CAAnimation.h>
#import <QuartzCore/CAMediaTimingFunction.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <map>
#include <sstream>
#include <vector>
#include "base/apple/scoped_cftyperef.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/task/bind_post_task.h"
#include "base/strings/sys_string_conversions.h"
#include "ui/views/controls/scrollbar/scroll_bar.h"
#include "ui/views/controls/scrollbar/base_scroll_bar_thumb.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "cc/paint/paint_flags.h"

#include "chromium/prototype/editor_text_layout_mac.inc"

// Returning nil delegates EVERY event to Chromium's existing native view:
// mouse/hover/drag, context menus, trackpad scrolling, key routing and VoiceOver.
@interface ZitterSidebarMetalView : NSView
@end
@implementation ZitterSidebarMetalView
- (BOOL)isFlipped { return YES; }
- (NSView*)hitTest:(NSPoint)point { return nil; }
- (BOOL)acceptsFirstResponder { return NO; }
- (BOOL)isAccessibilityElement { return NO; }
@end
// Both presentation and animation completion may originate outside the UI
// sequence. Their callbacks are posted and guarded by the shell generation.
@interface ZitterMetalAnimationCompletion : NSObject <CAAnimationDelegate> {
 @public
  base::OnceClosure callback;
}
@end
@implementation ZitterMetalAnimationCompletion
- (void)animationDidStop:(CAAnimation*)animation finished:(BOOL)finished {
  if(callback)std::move(callback).Run();
}
@end
namespace zitter::prototype {
namespace {
class SidebarScrollBar;
class SidebarThumb final : public views::BaseScrollBarThumb {
 public:
  SidebarThumb(views::ScrollBar* bar, base::RepeatingCallback<bool()> scene_visible)
      : BaseScrollBarThumb(bar), scene_visible_(std::move(scene_visible)) {}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override { return IsHorizontal() ? gfx::Size(20,15) : gfx::Size(15,20); }
  unsigned color() const {
    const auto state = GetState();
    return GetColorProvider()->GetColor(state == views::Button::STATE_PRESSED
        ? ui::kColorWebNativeControlScrollbarThumbPressed : state == views::Button::STATE_HOVERED
        ? ui::kColorWebNativeControlScrollbarThumbHovered : ui::kColorWebNativeControlScrollbarThumb);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    if (scene_visible_ && scene_visible_.Run()) return;
    sidebar::Item item{}; item.kind=6; item.tile=IsHorizontal(); item.width=width(); item.height=height(); item.fill=color();
    item.clip={0,0,float(width()),float(height())};
    const auto ops=sidebar::layout(rust::Slice<const sidebar::Item>(&item,1));
    for (const auto& op : ops) {
      cc::PaintFlags flags; flags.setAntiAlias(true); flags.setColor(op.color);
      canvas->DrawRoundRect(gfx::RectF(op.x,op.y,op.width,op.height),op.radius,flags);
    }
  }
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override { drag_->end(); BaseScrollBarThumb::OnMouseReleased(event); }
  void OnMouseCaptureLost() override { drag_->end(); BaseScrollBarThumb::OnMouseCaptureLost(); }
 private:
  rust::Box<sidebar::ScrollThumb> drag_=sidebar::new_scroll_thumb();
  base::RepeatingCallback<bool()> scene_visible_;
};
class SidebarScrollBar final : public views::ScrollBar {
 public:
  SidebarScrollBar(base::RepeatingCallback<bool()> scene_visible, bool horizontal)
      : ScrollBar(horizontal ? Orientation::kHorizontal : Orientation::kVertical) {
    SetThumb(new SidebarThumb(this,std::move(scene_visible)));
    GetViewAccessibility().SetName(horizontal ? u"Przewijanie przestrzeni" : u"Przewijanie kart");
    GetViewAccessibility().AddAction(ax::mojom::Action::kIncrement);
    GetViewAccessibility().AddAction(ax::mojom::Action::kDecrement);
  }
  int GetThickness() const override { return 15; }
  gfx::Rect GetTrackBounds() const override { return GetLocalBounds(); }
  void Update(int viewport, int content, int offset) override {
    viewport_=viewport; content_=content; offset_=offset;
    ScrollBar::Update(viewport,content,offset); SyncThumb();
  }
  void Layout(PassKey) override { SyncThumb(); }
  bool HandleAccessibleAction(const ui::AXActionData& action) override {
    if (action.action==ax::mojom::Action::kIncrement) return ScrollByAmount(ScrollAmount::kNextLine);
    if (action.action==ax::mojom::Action::kDecrement) return ScrollByAmount(ScrollAmount::kPrevLine);
    return ScrollBar::HandleAccessibleAction(action);
  }
  void Begin(sidebar::ScrollThumb& drag, float pointer) {
    drag.begin(Length(),viewport_,content_,offset_,pointer);
  }
  void Drag(const sidebar::ScrollThumb& drag, float pointer, float cross, bool snap) {
    ScrollToPosition(drag.drag(Length(),viewport_,content_,offset_,pointer,cross,GetThickness(),snap));
  }
  SidebarScrollBarSnapshot Snapshot() const {
    return {GetBoundsInScreen(),GetThumb()->GetBoundsInScreen(),
        static_cast<SidebarThumb*>(GetThumb())->color(),IsDrawn(),GetOrientation()==Orientation::kHorizontal};
  }
 protected:
  void ScrollToPosition(int position) override {
    offset_=sidebar::scroll_metrics(Length(),viewport_,content_,position).offset;
    ScrollBar::ScrollToPosition(offset_);
  }
 private:
  void SyncThumb() {
    const auto g=sidebar::scroll_metrics(Length(),viewport_,content_,offset_);
    offset_=g.offset;
    if (GetOrientation()==Orientation::kHorizontal) GetThumb()->SetBounds(g.position,0,g.length,height());
    else GetThumb()->SetBounds(0,g.position,width(),g.length);
    auto& ax=GetViewAccessibility(); ax.SetMinValueForRange(0); ax.SetMaxValueForRange(g.max_offset); ax.SetValueForRange(g.offset);
  }
  int Length() const { return GetOrientation()==Orientation::kHorizontal ? width() : height(); }
  int viewport_=0,content_=0,offset_=0;
};
bool SidebarThumb::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton() || !GetEnabledInViewsSubtree()) return false;
  static_cast<SidebarScrollBar*>(scroll_bar())->Begin(*drag_,IsHorizontal() ? x()+event.x() : y()+event.y());
  SetState(views::Button::STATE_PRESSED); return true;
}
bool SidebarThumb::OnMouseDragged(const ui::MouseEvent& event) {
  if (!GetEnabledInViewsSubtree()) { drag_->end(); return false; }
  static_cast<SidebarScrollBar*>(scroll_bar())->Drag(*drag_,IsHorizontal() ? x()+event.x() : y()+event.y(),IsHorizontal() ? event.y() : event.x(),GetSnapBackOnDragOutside());
  return true;
}
struct Quad { float x, y, width, height, radius; uint32_t color, textured; float stroke; };
static_assert(sizeof(Quad) == 32);
struct TextTile { id<MTLTexture> __strong texture; size_t bytes; };
struct SharedMetalContext {
  id<MTLDevice> __strong device_;
  id<MTLCommandQueue> __strong queue_;
  id<MTLRenderPipelineState> __strong pipeline_;
  bool Init() {
    device_ = MTLCreateSystemDefaultDevice();
    if (!device_) return false;
    queue_ = [device_ newCommandQueue];
    NSError* error = nil;
    dispatch_data_t data = dispatch_data_create(sidebar::kShaderLibrary, sizeof(sidebar::kShaderLibrary),
        dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    id<MTLLibrary> library = [device_ newLibraryWithData:data error:&error];
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library newFunctionWithName:@"ui_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"ui_fragment"];
    auto* color = descriptor.colorAttachments[0];
    color.pixelFormat = MTLPixelFormatBGRA8Unorm;
    color.blendingEnabled = YES;
    color.sourceRGBBlendFactor = MTLBlendFactorOne;
    color.sourceAlphaBlendFactor = MTLBlendFactorOne;
    color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeline_ = library ? [device_ newRenderPipelineStateWithDescriptor:descriptor error:&error] : nil;
    if (!queue_ || !pipeline_) { LOG(ERROR) << "Sidebar Metal initialization failed"; return false; }
    return true;
  }
};
std::shared_ptr<SharedMetalContext> AcquireMetalContext() {
  // Surface creation and updates run on the UI thread. Resources live until
  // the last native-window canvas is detached; no process-lifetime GPU cache.
  static base::NoDestructor<std::weak_ptr<SharedMetalContext>> cached;
  if (auto existing=cached->lock()) return existing;
  auto context=std::make_shared<SharedMetalContext>();
  if (!context->Init()) return nullptr;
  *cached=context; return context;
}
class Surface final : public SidebarMetal {
 public:
  bool Init() {
    context_=AcquireMetalContext();
    if (!context_) return false;
    device_=context_->device_; queue_=context_->queue_; pipeline_=context_->pipeline_;
    view_ = [[ZitterSidebarMetalView alloc] initWithFrame:NSZeroRect];
    layer_ = [CAMetalLayer layer];
    layer_.device = device_; layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer_.framebufferOnly = YES; layer_.opaque = YES;
    layer_.presentsWithTransaction = NO; layer_.displaySyncEnabled = YES;
    layer_.maximumDrawableCount = 3;
    view_.layer = layer_; view_.wantsLayer = YES;
    view_.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
    view_.hidden = YES;
    return true;
  }
  ~Surface() override { [view_.layer removeAnimationForKey:@"zitter-preview"]; [view_ removeFromSuperview]; [preview_clip_ removeFromSuperview]; }
  void Hide() override { next_presentation_.Reset(); view_.hidden = YES; [view_.layer removeAnimationForKey:@"zitter-preview"]; last_frame_.clear(); last_bounds_ = {}; }
  bool visible() const override { return view_.superview && view_.window.isVisible && !view_.isHiddenOrHasHiddenAncestor; }
  size_t frame_count() const override { return frame_count_; }
  bool contains_text_for_testing(std::string_view text) const override {
    // Reuse the existing frame signature; no per-frame diagnostic text copies.
    return last_frame_.find(":" + std::to_string(text.size()) + ":" + std::string(text)) != std::string::npos;
  }
  gfx::Rect screen_bounds_for_testing() const override { return last_bounds_; }
  bool contains_rect_for_testing(gfx::RectF bounds, gfx::RectF clip) const override {
    std::ostringstream prefix;
    prefix << '|' << bounds.x() << ',' << bounds.y() << ',' << bounds.width() << ',' << bounds.height()
           << ',' << clip.x() << ',' << clip.y() << ',' << clip.width() << ',' << clip.height() << ',';
    return last_frame_.find(prefix.str()) != std::string::npos;
  }
  SidebarViewportProjection Project(gfx::Size content, gfx::Size size, gfx::PointF offset,
      base::span<const SidebarViewportNode> nodes) override {
    std::vector<sidebar::ViewportNode> input;
    input.reserve(nodes.size());
    for (const auto& node : nodes) input.push_back({node.parent,
        {float(node.bounds.x()), float(node.bounds.y()), float(node.bounds.width()), float(node.bounds.height())}, node.visible});
    const auto result = viewport_->project(content.width(), content.height(), size.width(), size.height(),
        offset.x(), offset.y(), rust::Slice<const sidebar::ViewportNode>(input.data(), input.size()));
    SidebarViewportProjection out;
    out.offset = gfx::PointF(result.x, result.y); out.nodes.reserve(result.nodes.size());
    for (const auto& node : result.nodes) out.nodes.push_back({node.index,
        gfx::RectF(node.bounds.x, node.bounds.y, node.bounds.width, node.bounds.height),
        gfx::RectF(node.clip.x, node.clip.y, node.clip.width, node.clip.height)});
    return out;
  }
  void Update(gfx::NativeWindow native_window, gfx::Rect bounds,
              base::span<const sidebar::Item> items, unsigned background) override {
    NSWindow* window = native_window.GetNativeNSWindow();
    if (!window.isVisible || bounds.IsEmpty()) { Hide(); return; }
    if(preview_clip_) {
      [view_ removeFromSuperview];[preview_clip_ removeFromSuperview];preview_clip_=nil;
      [view_.layer removeAnimationForKey:@"zitter-preview"];view_.layer.transform=CATransform3DIdentity;
    }
    NSView* parent = window.contentView;
    if (view_.superview != parent) { [view_ removeFromSuperview]; [parent addSubview:view_]; last_frame_.clear(); }
    // gfx screen coordinates start at the primary display's upper left.
    const double top = NSMaxY(NSScreen.screens.firstObject.frame);
    NSRect screen = NSMakeRect(bounds.x(), top - bounds.bottom(), bounds.width(), bounds.height());
    NSRect frame = [parent convertRect:[window convertRectFromScreen:screen] fromView:nil];
    Render(window,bounds,frame,items,background);
  }
  void UpdatePreview(gfx::NativeWindow native_window,gfx::Rect bounds,double x,
      base::span<const sidebar::Item> items,unsigned background,bool settle,bool redraw) override {
    NSWindow* window=native_window.GetNativeNSWindow();
    if(!window.isVisible||bounds.IsEmpty()||!std::isfinite(x)){Hide();return;}
    NSView* parent=window.contentView;
    if(!preview_clip_){preview_clip_=[[ZitterSidebarMetalView alloc] initWithFrame:NSZeroRect];preview_clip_.wantsLayer=YES;preview_clip_.layer.masksToBounds=YES;}
    if(preview_clip_.superview!=parent){[preview_clip_ removeFromSuperview];[parent addSubview:preview_clip_];}
    if(view_.superview!=preview_clip_){[view_ removeFromSuperview];[preview_clip_ addSubview:view_];last_frame_.clear();}
    const double top=NSMaxY(NSScreen.screens.firstObject.frame);
    const NSRect screen=NSMakeRect(bounds.x(),top-bounds.bottom(),bounds.width(),bounds.height());
    [CATransaction begin];[CATransaction setDisableActions:YES];
    preview_clip_.frame=[parent convertRect:[window convertRectFromScreen:screen] fromView:nil];
    [CATransaction commit];
    // Page pixels never move in the display list; Core Animation translates the
    // drawable layer inside the clipping adapter, also during the final settle.
    const bool was=visible();
    if(redraw||!was||next_presentation_||bounds!=last_bounds_||layer_.contentsScale!=(window.backingScaleFactor?:1))
      Render(window,bounds,NSMakeRect(0,0,bounds.width(),bounds.height()),items,background);
    if(!visible())return;
    AnimatePreview(x,settle&&was?base::Milliseconds(160):base::TimeDelta(),{});
  }
  void SetNextPresentationCallback(base::OnceClosure presented) override {next_presentation_=std::move(presented);}
  void AnimatePreview(double x,base::TimeDelta duration,base::OnceClosure done) override {
    // Post completion even for cancellation/zero duration. Reentrant teardown
    // from a CAAnimationDelegate call must not destroy its active receiver.
    auto posted=done?base::BindPostTaskToCurrentDefault(std::move(done)):base::OnceClosure();
    if(!visible()||!std::isfinite(x)){if(posted)std::move(posted).Run();return;}
    CALayer* presentation=(CALayer*)view_.layer.presentationLayer;
    const double previous=presentation?presentation.transform.m41:view_.layer.transform.m41;
    [CATransaction begin];[CATransaction setDisableActions:YES];
    [view_.layer removeAnimationForKey:@"zitter-preview"];
    view_.layer.transform=CATransform3DMakeTranslation(x,0,0);
    if(duration.is_positive()&&previous!=x){
      CABasicAnimation* animation=[CABasicAnimation animationWithKeyPath:@"transform.translation.x"];
      animation.fromValue=@(previous);animation.toValue=@(x);animation.duration=duration.InSecondsF();
      animation.timingFunction=[CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
      auto* completion=[[ZitterMetalAnimationCompletion alloc] init];completion->callback=std::move(posted);animation.delegate=completion;
      [view_.layer addAnimation:animation forKey:@"zitter-preview"];
    } else if(posted)std::move(posted).Run();
    [CATransaction commit];
  }
 private:
  void Render(NSWindow* window,gfx::Rect bounds,NSRect frame,
      base::span<const sidebar::Item> items,unsigned background) {
    const double scale = window.backingScaleFactor ?: 1;
    auto ops = sidebar::layout(rust::Slice<const sidebar::Item>(items.data(), items.size()));
    std::ostringstream signature;
    signature << bounds.ToString() << ':' << scale << ':' << background;
    for (const auto& op : ops) signature << '|' << op.x << ',' << op.y << ',' << op.width << ','
        << op.height << ',' << op.clip.x << ',' << op.clip.y << ',' << op.clip.width << ',' << op.clip.height
        << ',' << op.radius << ',' << op.color << ',' << op.font_size << ','
        << op.shape << ',' << op.stroke << ',' << op.bold << ',' << op.middle << ',' << op.center << ':' << op.text.size() << ':' << std::string(op.text);
    std::string fingerprint = signature.str();
    if (fingerprint == last_frame_ && !view_.hidden && !next_presentation_) return;
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    view_.frame = frame;
    const CGSize pixels = CGSizeMake(bounds.width() * scale, bounds.height() * scale);
    if (layer_.contentsScale != scale || !CGSizeEqualToSize(layer_.drawableSize, pixels)) {
      layer_.contentsScale = scale; layer_.drawableSize = pixels;
    }
    [CATransaction commit];
    id<CAMetalDrawable> drawable = [layer_ nextDrawable];
    if (!drawable) { Hide(); return; }  // The live Views tree is a safe fallback.
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(((background>>16)&255)/255.,
        ((background>>8)&255)/255., (background&255)/255., 1);
    id<MTLCommandBuffer> command = [queue_ commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:pipeline_];
    const float viewport[2] = {float(bounds.width()), float(bounds.height())};
    [encoder setVertexBytes:viewport length:sizeof(viewport) atIndex:1];
    std::optional<MTLScissorRect> last_clip;
    for (const auto& op : ops) {
      const double x = std::clamp(std::floor(op.clip.x * scale), 0.0, double(pixels.width));
      const double y = std::clamp(std::floor(op.clip.y * scale), 0.0, double(pixels.height));
      const double right = std::clamp(std::ceil((op.clip.x + op.clip.width) * scale), x, double(pixels.width));
      const double bottom = std::clamp(std::ceil((op.clip.y + op.clip.height) * scale), y, double(pixels.height));
      if (right <= x || bottom <= y) continue;
      const MTLScissorRect clip{NSUInteger(x), NSUInteger(y), NSUInteger(right - x), NSUInteger(bottom - y)};
      if (!last_clip || last_clip->x != clip.x || last_clip->y != clip.y ||
          last_clip->width != clip.width || last_clip->height != clip.height) {
        [encoder setScissorRect:clip]; last_clip = clip;
      }
      const bool text = !op.text.empty();
      if (text) {
        id<MTLTexture> texture = Text(op, scale);
        if (!texture) continue;
        [encoder setFragmentTexture:texture atIndex:0];
      }
      Quad q{op.x, op.y, op.width, op.height, op.radius, op.color, text ? 1u : op.shape, op.stroke};
      [encoder setVertexBytes:&q length:sizeof(q) atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
    [encoder endEncoding];
    if(next_presentation_) {
      auto callback=std::make_shared<base::OnceClosure>(base::BindPostTaskToCurrentDefault(std::move(next_presentation_)));
      [drawable addPresentedHandler:^(id<MTLDrawable> presented) {if(*callback)std::move(*callback).Run();}];
    }
    [command presentDrawable:drawable]; [command commit];
    ++frame_count_;
    view_.hidden = NO; last_frame_ = std::move(fingerprint); last_bounds_ = bounds;
  }
 private:
  id<MTLTexture> Text(const sidebar::Primitive& op, double scale) {
    std::ostringstream key;
    key << op.width << ',' << op.height << ',' << op.font_size << ',' << scale << ','
        << op.color << ',' << op.shape << ',' << op.stroke << ',' << op.bold << ',' << op.middle << ',' << op.center << ':' << std::string(op.text);
    auto found = text_.find(key.str());
    if (found != text_.end()) return found->second.texture;
    const size_t width = std::min(size_t{4096}, size_t(std::ceil(op.width * scale)));
    const size_t height = std::min(size_t{512}, size_t(std::ceil(op.height * scale)));
    if (!width || !height) return nil;
    const size_t bytes = width * height * 4;
    if (text_.size() >= 256 || text_bytes_ + bytes > 16 * 1024 * 1024) {
      text_.clear(); text_bytes_ = 0;
    }
    std::vector<uint8_t> pixels(bytes, 0);
    base::apple::ScopedCFTypeRef<CGColorSpaceRef> space(CGColorSpaceCreateDeviceRGB());
    base::apple::ScopedCFTypeRef<CGContextRef> context(CGBitmapContextCreate(pixels.data(),
        width, height, 8, width * 4, space.get(), static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst) | static_cast<uint32_t>(kCGBitmapByteOrder32Little)));
    if (!context) return nil;
    CGContextScaleCTM(context.get(), scale, scale);
    NSFont* font = editor_text_detail::Font(op.font_size,op.bold);
    NSColor* color = [NSColor colorWithSRGBRed:((op.color>>16)&255)/255.
        green:((op.color>>8)&255)/255. blue:(op.color&255)/255. alpha:(op.color>>24)/255.];
    NSString* string = base::SysUTF8ToNSString(std::string(op.text));
    NSDictionary* attributes = @{NSFontAttributeName:font, NSForegroundColorAttributeName:color};
    NSAttributedString* attributed = [[NSAttributedString alloc] initWithString:string attributes:attributes];
    base::apple::ScopedCFTypeRef<CTLineRef> line(CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attributed));
    NSAttributedString* ellipsis = [[NSAttributedString alloc] initWithString:@"…" attributes:attributes];
    base::apple::ScopedCFTypeRef<CTLineRef> token(CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)ellipsis));
    base::apple::ScopedCFTypeRef<CTLineRef> truncated;
    if (CTLineGetTypographicBounds(line.get(), nullptr, nullptr, nullptr) > op.width)
      truncated.reset(CTLineCreateTruncatedLine(line.get(), op.width, op.middle ? kCTLineTruncationMiddle : kCTLineTruncationEnd, token.get()));
    CGFloat ascent = 0, descent = 0;
    CTLineRef draw = truncated ? truncated.get() : line.get();
    CTLineGetTypographicBounds(draw, &ascent, &descent, nullptr);
    CGContextSetTextPosition(context.get(), op.center ? std::max(0.0, (op.width-CTLineGetTypographicBounds(draw,nullptr,nullptr,nullptr))/2.0) : 0, (op.height - ascent - descent) / 2 + descent);
    CTLineDraw(draw, context.get());
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:width height:height mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
    if (!texture) return nil;
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:width * 4];
    text_.emplace(key.str(), TextTile{texture, bytes}); text_bytes_ += bytes;
    return texture;
  }
  std::map<std::string, TextTile> text_;
  size_t text_bytes_ = 0;
  rust::Box<sidebar::Viewport> viewport_ = sidebar::new_viewport();
  std::shared_ptr<SharedMetalContext> context_;
  base::OnceClosure next_presentation_;
  size_t frame_count_ = 0;
  std::string last_frame_;
  gfx::Rect last_bounds_;
  ZitterSidebarMetalView* __strong preview_clip_;
  ZitterSidebarMetalView* __strong view_;
  CAMetalLayer* __strong layer_;
  id<MTLDevice> __strong device_;
  id<MTLCommandQueue> __strong queue_;
  id<MTLRenderPipelineState> __strong pipeline_;
};
}
std::unique_ptr<views::ScrollBar> CreateSidebarScrollBar(base::RepeatingCallback<bool()> scene_visible, bool horizontal) {
  return std::make_unique<SidebarScrollBar>(std::move(scene_visible),horizontal);
}
SidebarScrollBarSnapshot GetSidebarScrollBarSnapshot(views::ScrollBar* bar) {
  return static_cast<SidebarScrollBar*>(bar)->Snapshot();
}
std::vector<std::string> ShapeSidebarParagraph(std::string_view text,float font_size,int width,bool bold) {
  std::vector<std::string> lines;if(text.empty()||width<=0||!std::isfinite(font_size)||font_size<=0)return lines;
  NSString* string=base::SysUTF8ToNSString(std::string(text));
  NSFont* font=[NSFont systemFontOfSize:font_size weight:bold?NSFontWeightMedium:NSFontWeightRegular];
  NSAttributedString* attributed=[[NSAttributedString alloc] initWithString:string attributes:@{NSFontAttributeName:font}];
  base::apple::ScopedCFTypeRef<CTTypesetterRef> typesetter(CTTypesetterCreateWithAttributedString((__bridge CFAttributedStringRef)attributed));
  if(!typesetter)return lines;
  for(NSUInteger offset=0;offset<string.length;) {
    CFIndex count=CTTypesetterSuggestLineBreak(typesetter.get(),offset,width);
    if(count<=0)count=[string rangeOfComposedCharacterSequenceAtIndex:offset].length;
    const NSUInteger length=std::min(static_cast<NSUInteger>(count),string.length-offset);
    lines.push_back(base::SysNSStringToUTF8([string substringWithRange:NSMakeRange(offset,length)]));offset+=length;
  }
  return lines;
}
std::unique_ptr<SidebarMetal> CreateSidebarMetal() {
  auto surface = std::make_unique<Surface>();
  return surface->Init() ? std::move(surface) : nullptr;
}
}
