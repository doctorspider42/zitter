#include "chromium/prototype/workspace_frame_mac.h"

#import <AppKit/AppKit.h>

#include "components/remote_cocoa/common/native_widget_ns_window.mojom.h"
#include "ui/views/widget/native_widget_mac.h"
#include "ui/views/widget/widget.h"

namespace zitter::prototype {
namespace {
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

views::NativeWidget* CreateWorkspaceNativeWidget(views::Widget* widget) {
  return new WorkspaceNativeWidgetMac(widget);
}
}  // namespace zitter::prototype
