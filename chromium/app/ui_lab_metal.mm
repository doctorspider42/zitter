#include "chromium/app/ui_lab_metal.h"
#include "chromium/app/ui_lab_shader.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"

@interface ZitterLabMetalView : NSView
@property(nonatomic, copy) void (^clicked)(float);
@end
@implementation ZitterLabMetalView
@synthesize clicked = _clicked;
- (BOOL)isFlipped { return YES; }
- (void)mouseDown:(NSEvent*)event {
  if (self.clicked) self.clicked([self convertPoint:event.locationInWindow fromView:nil].y);
}
@end

namespace zitter::ui_lab {
namespace {
// Identical opaque rectangles to gfx::Canvas. No page texture is imported,
// read back, or uploaded: WebContents remains a sibling native surface.

class Surface final : public MetalSurface {
 public:
  bool Init(base::RepeatingCallback<void(float)> clicked) {
    device_ = MTLCreateSystemDefaultDevice();
    if (!device_) return false;
    queue_ = [device_ newCommandQueue];
    NSError* error = nil;
    dispatch_data_t data = dispatch_data_create(ui_lab::kShaderLibrary, sizeof(ui_lab::kShaderLibrary),
        dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    id<MTLLibrary> library = [device_ newLibraryWithData:data error:&error];
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library newFunctionWithName:@"ui_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"ui_fragment"];
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipeline_ = library ? [device_ newRenderPipelineStateWithDescriptor:descriptor error:&error] : nil;
    if (!pipeline_ || !queue_) {
      LOG(ERROR) << "UI lab Metal initialization: " << error.localizedDescription.UTF8String;
      return false;
    }
    view_ = [[ZitterLabMetalView alloc] initWithFrame:NSMakeRect(0, 0, 280, 780)];
    view_.clicked = ^(float y) { clicked.Run(y); };
    layer_ = [CAMetalLayer layer];
    layer_.device = device_;
    layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer_.framebufferOnly = YES;
    layer_.opaque = YES;
    layer_.displaySyncEnabled = YES;
    layer_.maximumDrawableCount = 3;
    view_.wantsLayer = YES;
    view_.layer = layer_;
    return true;
  }
  ~Surface() override { view_.clicked = nil; [view_ removeFromSuperview]; }
  gfx::NativeView native_view() override { return gfx::NativeView(view_); }
  void Draw(const rust::Vec<Rect>& scene, Presented presented) override {
    static_assert(sizeof(Rect) == 20);
    const double started = CACurrentMediaTime();
    const auto size = view_.bounds.size;
    const double scale = view_.window.backingScaleFactor ?: 1;
    layer_.contentsScale = scale;
    layer_.drawableSize = CGSizeMake(size.width * scale, size.height * scale);
    id<CAMetalDrawable> drawable = [layer_ nextDrawable];
    if (!drawable || scene.empty() || size.width <= 0 || size.height <= 0) {
      LOG(ERROR) << "UI lab Metal surface unavailable: drawable=" << !!drawable
                 << " scene=" << scene.size() << " width=" << size.width << " height=" << size.height;
      std::move(presented).Run(false, 0, 0); return;
    }
    // A small immutable UI display list, not a copy of the web page pixels.
    id<MTLBuffer> buffer = [device_ newBufferWithBytes:scene.data()
        length:scene.size() * sizeof(Rect) options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> command = [queue_ commandBuffer];
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:pipeline_];
    [encoder setVertexBuffer:buffer offset:0 atIndex:0];
    const float viewport[2] = {static_cast<float>(size.width), static_cast<float>(size.height)};
    [encoder setVertexBytes:viewport length:sizeof(viewport) atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6 instanceCount:scene.size()];
    [encoder endEncoding];
    auto callback = std::make_shared<Presented>(std::move(presented));
    auto runner = base::SingleThreadTaskRunner::GetCurrentDefault();
    [drawable addPresentedHandler:^(id<MTLDrawable> shown) {
      const double latency = (shown.presentedTime - started) * 1000;
      const double gpu = (command.GPUEndTime - command.GPUStartTime) * 1000;
      runner->PostTask(FROM_HERE, base::BindOnce(
          [](std::shared_ptr<Presented> cb, bool ok, double ms, double gpu_ms) {
            if (*cb) std::move(*cb).Run(ok, ms, gpu_ms);
          }, callback, shown.presentedTime >= started, latency, gpu));
    }];
    [command presentDrawable:drawable];
    [command commit];
  }
 private:
  ZitterLabMetalView* __strong view_;
  CAMetalLayer* __strong layer_;
  id<MTLDevice> __strong device_;
  id<MTLCommandQueue> __strong queue_;
  id<MTLRenderPipelineState> __strong pipeline_;
};
}  // namespace
std::unique_ptr<MetalSurface> CreateMetalSurface(base::RepeatingCallback<void(float)> clicked) {
  auto result = std::make_unique<Surface>();
  if (!result->Init(std::move(clicked))) return nullptr;
  return result;
}
}  // namespace zitter::ui_lab
