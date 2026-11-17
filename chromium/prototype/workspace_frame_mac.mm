#include "chromium/prototype/workspace_frame_mac.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

#include <algorithm>

#include "components/remote_cocoa/common/native_widget_ns_window.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/native_widget_mac.h"
#include "ui/views/widget/widget.h"

// AppKit re-lays the titlebar on resize, key and appearance changes and puts
// the buttons back at its default line; every button frame change re-applies.
@interface ZitterWindowButtonsAligner : NSObject
@property(nonatomic) CGFloat centerY;
- (instancetype)initWithWindow:(NSWindow*)window;
- (void)apply;
@end

@implementation ZitterWindowButtonsAligner {
  __weak NSWindow* _window;
  BOOL _applying;
}
@synthesize centerY = _centerY;
- (instancetype)initWithWindow:(NSWindow*)window {
  if ((self = [super init])) {
    _window = window;
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    for (NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
      NSButton* button = [window standardWindowButton:kind];
      button.postsFrameChangedNotifications = YES;
      [center addObserver:self selector:@selector(changed:) name:NSViewFrameDidChangeNotification object:button];
    }
    for (NSNotificationName name : {NSWindowDidResizeNotification, NSWindowDidExitFullScreenNotification})
      [center addObserver:self selector:@selector(changed:) name:name object:window];
  }
  return self;
}
- (void)dealloc {
  [NSNotificationCenter.defaultCenter removeObserver:self];
}
- (void)changed:(NSNotification*)notification {
  [self apply];
}
- (void)apply {
  NSWindow* window = _window;
  if (!window || _applying || (window.styleMask & NSWindowStyleMaskFullScreen)) return;
  _applying = YES;
  const CGFloat top = NSHeight(window.contentView.frame);
  for (NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    NSButton* button = [window standardWindowButton:kind];
    if (!button.superview) continue;
    NSRect frame = button.frame;
    NSRect in_window = [button.superview convertRect:frame toView:nil];
    in_window.origin.y = round(top - _centerY - NSHeight(frame) / 2);
    frame.origin.y = [button.superview convertRect:in_window fromView:nil].origin.y;
    if (!NSEqualRects(frame, button.frame)) button.frame = frame;
  }
  _applying = NO;
}
@end

@interface ZitterPanelMaterialView : NSVisualEffectView
@end

@implementation ZitterPanelMaterialView
- (NSView*)hitTest:(NSPoint)point {
  return nil;
}
@end

namespace zitter::prototype {
namespace {
const void* const kWindowButtonsAlignerKey = &kWindowButtonsAlignerKey;
const void* const kPanelMaterialKey = &kPanelMaterialKey;

class WorkspaceNativeWidgetMac final : public views::NativeWidgetMac {
 public:
  explicit WorkspaceNativeWidgetMac(views::Widget* widget)
      : views::NativeWidgetMac(widget) {}

 protected:
  void PopulateCreateWindowParams(
      const views::Widget::InitParams& widget_params,
      remote_cocoa::mojom::CreateWindowParams* params) override {
    views::NativeWidgetMac::PopulateCreateWindowParams(widget_params, params);
    // Keep the default NSWindow class: Chrome's BrowserNativeWidgetWindow
    // expects Chrome's command dispatcher and breaks page key routing here.
    params->style_mask |= NSWindowStyleMaskFullSizeContentView;
    params->titlebar_appears_transparent = true;
    params->window_title_hidden = true;
  }
};
}  // namespace

void SetWorkspaceNativeAppearance(views::Widget* widget, std::optional<bool> dark) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  if (!dark) { if (window.appearance) window.appearance = nil; return; }
  NSString* name = *dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua;
  if (![window.appearance.name isEqualToString:name]) window.appearance = [NSAppearance appearanceNamed:name];
}

namespace {
bool SetWorkspaceMaterial(views::Widget* widget, bool enabled,
                          NSVisualEffectMaterial effect,
                          const gfx::Insets& insets, int corner_radius) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  NSView* content = window.contentView;
  if (!content)
    return false;
  ZitterPanelMaterialView* material =
      objc_getAssociatedObject(content, kPanelMaterialKey);
  if (!enabled || !WorkspaceAllowsTransparency()) {
    [material removeFromSuperview];
    objc_setAssociatedObject(content, kPanelMaterialKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return false;
  }
  if (!material) {
    material = [[ZitterPanelMaterialView alloc] initWithFrame:content.bounds];
    material.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    material.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    material.state = NSVisualEffectStateActive;
    [content addSubview:material positioned:NSWindowBelow relativeTo:nil];
    objc_setAssociatedObject(content, kPanelMaterialKey, material, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  material.material = effect;
  const NSRect bounds = content.bounds;
  material.frame = NSMakeRect(NSMinX(bounds) + insets.left(),
                             NSMinY(bounds) + insets.bottom(),
                             std::max<CGFloat>(0, NSWidth(bounds) - insets.width()),
                             std::max<CGFloat>(0, NSHeight(bounds) - insets.height()));
  if (corner_radius) {
    material.wantsLayer = YES;
    material.layer.cornerRadius = corner_radius;
    material.layer.masksToBounds = YES;
  }
  return true;
}
}  // namespace

bool SetWorkspacePanelMaterial(views::Widget* widget, bool enabled) {
  return SetWorkspaceMaterial(widget, enabled, NSVisualEffectMaterialHUDWindow,
                              {}, 0);
}

bool SetWorkspaceMenuMaterial(views::Widget* widget, const gfx::Insets& insets,
                              int corner_radius) {
  return SetWorkspaceMaterial(widget, true, NSVisualEffectMaterialMenu, insets,
                              corner_radius);
}

bool WorkspaceAllowsTransparency() {
  return !NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceTransparency;
}

bool SetWorkspaceSidebarMaterial(views::Widget* widget) {
  return SetWorkspaceMaterial(widget, true, NSVisualEffectMaterialSidebar,
                              {}, 12);
}

void SetWorkspaceSidebarMaterialOffset(views::Widget* widget, double offset) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  NSView* material = objc_getAssociatedObject(window.contentView, kPanelMaterialKey);
  if (material) {
    NSPoint origin = material.frame.origin;
    origin.x = NSMinX(material.superview.bounds) + offset;
    [material setFrameOrigin:origin];
  }
}

bool SetWorkspaceAssistantMaterial(views::Widget* widget, bool enabled,
                                   const gfx::Insets& insets, int corner_radius) {
  return SetWorkspaceMaterial(widget, enabled, NSVisualEffectMaterialSidebar,
                              insets, corner_radius);
}

views::NativeWidget* CreateWorkspaceNativeWidget(views::Widget* widget) {
  return new WorkspaceNativeWidgetMac(widget);
}

gfx::Rect AlignWorkspaceWindowButtons(views::Widget* widget, int center_y) {
  NSWindow* window = widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  NSButton* close = [window standardWindowButton:NSWindowCloseButton];
  if (!close || close.hidden || (window.styleMask & NSWindowStyleMaskFullScreen)) return gfx::Rect();
  ZitterWindowButtonsAligner* aligner = objc_getAssociatedObject(window, kWindowButtonsAlignerKey);
  if (!aligner) {
    aligner = [[ZitterWindowButtonsAligner alloc] initWithWindow:window];
    objc_setAssociatedObject(window, kWindowButtonsAlignerKey, aligner, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }
  aligner.centerY = center_y;
  [aligner apply];
  NSRect lights = NSZeroRect;
  for (NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    NSButton* button = [window standardWindowButton:kind];
    if (button && !button.hidden) lights = NSUnionRect(lights, [button convertRect:button.bounds toView:nil]);
  }
  const CGFloat top = NSHeight(window.contentView.frame);
  return gfx::Rect(NSMinX(lights), top - NSMaxY(lights), NSWidth(lights), NSHeight(lights));
}
}  // namespace zitter::prototype
