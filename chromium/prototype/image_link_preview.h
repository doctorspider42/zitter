#ifndef ZITTER_CHROMIUM_PROTOTYPE_IMAGE_LINK_PREVIEW_H_
#define ZITTER_CHROMIUM_PROTOTYPE_IMAGE_LINK_PREVIEW_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

class SkBitmap;
namespace views { class WebView; }
namespace zitter::prototype {

// A passive, decoded image preview: it never navigates a page or takes focus.
class ImageLinkPreview final : public content::WebContentsObserver,
                               public views::WidgetObserver {
 public:
  ImageLinkPreview(content::WebContents* page, views::WebView* owner,
                   base::RepeatingCallback<bool()> enabled);
  ~ImageLinkPreview() override;
  static bool IsImageURL(const GURL& url);
  void Hover(const GURL& url);
  void Reset();
  bool visible_for_testing() const;

 private:
  bool CanShow() const;
  void Download();
  void Show(const SkBitmap& bitmap);
  void DidStartNavigation(content::NavigationHandle* navigation) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void WebContentsDestroyed() override;
  void OnWidgetActivationChanged(views::Widget*, bool active) override;
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetDestroying(views::Widget*) override;

  raw_ptr<views::WebView> owner_;
  base::RepeatingCallback<bool()> enabled_;
  GURL hovered_url_;
  base::OneShotTimer timer_;
  std::unique_ptr<views::Widget> widget_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{this};
  base::WeakPtrFactory<ImageLinkPreview> weak_factory_{this};
};
}  // namespace zitter::prototype
#endif
