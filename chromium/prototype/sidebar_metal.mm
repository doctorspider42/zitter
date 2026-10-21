// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/sidebar_metal.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/sidebar_shader.h"
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include <cmath>
#include <map>
#include <sstream>
#include <vector>
#include "base/apple/scoped_cftyperef.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "ui/views/controls/scrollbar/cocoa_scroll_bar.h"

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
namespace zitter::prototype {
namespace {
class SidebarScrollBar final : public views::CocoaScrollBar {
 public:
  SidebarScrollBar() : views::CocoaScrollBar(views::ScrollBar::Orientation::kVertical) {}
  bool OverlapsContent() const override { return false; }
};
struct Quad { float x, y, width, height, radius; uint32_t color, textured, pad; };
static_assert(sizeof(Quad) == 32);
struct TextTile { id<MTLTexture> __strong texture; size_t bytes; };
class Surface final : public SidebarMetal {
 public:
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
  ~Surface() override { [view_ removeFromSuperview]; }
  void Hide() override { view_.hidden = YES; last_frame_.clear(); }
  bool visible() const override { return view_.superview && view_.window.isVisible && !view_.isHiddenOrHasHiddenAncestor; }
  void Update(gfx::NativeWindow native_window, gfx::Rect bounds,
              base::span<const sidebar::Item> items, unsigned background) override {
    NSWindow* window = native_window.GetNativeNSWindow();
    if (!window.isVisible || bounds.IsEmpty()) { Hide(); return; }
    NSView* parent = window.contentView;
    if (view_.superview != parent) { [view_ removeFromSuperview]; [parent addSubview:view_]; last_frame_.clear(); }
    // gfx screen coordinates start at the primary display's upper left.
    const double top = NSMaxY(NSScreen.screens.firstObject.frame);
    NSRect screen = NSMakeRect(bounds.x(), top - bounds.bottom(), bounds.width(), bounds.height());
    NSRect frame = [parent convertRect:[window convertRectFromScreen:screen] fromView:nil];
    const double scale = window.backingScaleFactor ?: 1;
    auto ops = sidebar::layout(rust::Slice<const sidebar::Item>(items.data(), items.size()));
    std::ostringstream signature;
    signature << bounds.ToString() << ':' << scale << ':' << background;
    for (const auto& op : ops) signature << '|' << op.x << ',' << op.y << ',' << op.width << ','
        << op.height << ',' << op.radius << ',' << op.color << ',' << op.font_size << ','
        << op.bold << ':' << op.text.size() << ':' << std::string(op.text);
    std::string fingerprint = signature.str();
    if (fingerprint == last_frame_ && !view_.hidden) return;
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
    for (const auto& op : ops) {
      const bool text = !op.text.empty();
      if (text) {
        id<MTLTexture> texture = Text(op, scale);
        if (!texture) continue;
        [encoder setFragmentTexture:texture atIndex:0];
      }
      Quad q{op.x, op.y, op.width, op.height, op.radius, op.color, text ? 1u : 0u, 0};
      [encoder setVertexBytes:&q length:sizeof(q) atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
    [encoder endEncoding];
    [command presentDrawable:drawable]; [command commit];
    view_.hidden = NO; last_frame_ = std::move(fingerprint);
  }
 private:
  id<MTLTexture> Text(const sidebar::Primitive& op, double scale) {
    std::ostringstream key;
    key << op.width << ',' << op.height << ',' << op.font_size << ',' << scale << ','
        << op.color << ',' << op.bold << ':' << std::string(op.text);
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
    NSFont* font = [NSFont systemFontOfSize:op.font_size weight:op.bold ? NSFontWeightMedium : NSFontWeightRegular];
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
      truncated.reset(CTLineCreateTruncatedLine(line.get(), op.width, kCTLineTruncationEnd, token.get()));
    CGFloat ascent = 0, descent = 0;
    CTLineRef draw = truncated ? truncated.get() : line.get();
    CTLineGetTypographicBounds(draw, &ascent, &descent, nullptr);
    CGContextSetTextPosition(context.get(), 0, (op.height - ascent - descent) / 2 + descent);
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
  std::string last_frame_;
  ZitterSidebarMetalView* __strong view_;
  CAMetalLayer* __strong layer_;
  id<MTLDevice> __strong device_;
  id<MTLCommandQueue> __strong queue_;
  id<MTLRenderPipelineState> __strong pipeline_;
};
}
std::unique_ptr<views::ScrollBar> CreateSidebarScrollBar() {
  return std::make_unique<SidebarScrollBar>();
}
std::unique_ptr<SidebarMetal> CreateSidebarMetal() {
  auto surface = std::make_unique<Surface>();
  return surface->Init() ? std::move(surface) : nullptr;
}
}
