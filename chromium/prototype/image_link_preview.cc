#include "chromium/prototype/image_link_preview.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/display/screen.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"

namespace zitter::prototype {
ImageLinkPreview::ImageLinkPreview(content::WebContents* page, views::WebView* owner,
                                 base::RepeatingCallback<bool()> enabled)
    : WebContentsObserver(page), owner_(owner), enabled_(std::move(enabled)) {}
ImageLinkPreview::~ImageLinkPreview() = default;

bool ImageLinkPreview::IsImageURL(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password()) return false;
  const auto path = base::ToLowerASCII(url.path());
  for (const auto* extension : {".png", ".jpg", ".jpeg", ".gif", ".webp", ".avif", ".bmp", ".ico", ".svg"})
    if (base::EndsWith(path, extension)) return true;
  return false;
}
bool ImageLinkPreview::CanShow() const {
  return web_contents() && enabled_.Run() && owner_->IsDrawn() && owner_->GetWidget() &&
         owner_->GetWidget()->IsActive() && web_contents()->GetVisibility() == content::Visibility::VISIBLE;
}
void ImageLinkPreview::Hover(const GURL& url) {
  if (hovered_url_ == url && CanShow()) return;
  Reset();
  if (!IsImageURL(url) || !CanShow()) return;
  if (observation_.GetSource() != owner_->GetWidget()) {
    observation_.Reset();
    observation_.Observe(owner_->GetWidget());
  }
  hovered_url_ = url;
  timer_.Start(FROM_HERE, base::Milliseconds(400), this, &ImageLinkPreview::Download);
}
void ImageLinkPreview::Reset() {
  timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  hovered_url_ = GURL();
  widget_.reset();
}
void ImageLinkPreview::Download() {
  if (!CanShow()) { Reset(); return; }
  // The source renderer supplies the profile's cookies/cache and image decoder.
  // Only the bounded decoded bitmap crosses into the browser UI.
  web_contents()->DownloadImage(hovered_url_, false, gfx::Size(360, 260), 720, false,
      base::BindOnce([](base::WeakPtr<ImageLinkPreview> preview, int, int status,
                        const GURL&, const std::vector<SkBitmap>& bitmaps,
                        const std::vector<gfx::Size>&) {
        if (!preview || !preview->CanShow() || status >= 400 || bitmaps.empty() || bitmaps.front().drawsNothing()) return;
        preview->Show(bitmaps.front());
      }, weak_factory_.GetWeakPtr()));
}
void ImageLinkPreview::Show(const SkBitmap& bitmap) {
  const auto area = owner_->GetBoundsInScreen();
  const int width = std::min(360, area.width() - 32);
  const int height = std::min(260, area.height() - 32);
  if (width <= 0 || height <= 0) return;
  const double scale = std::min({1.0, double(width) / bitmap.width(), double(height) / bitmap.height()});
  const gfx::Size size(std::max(1, int(std::round(bitmap.width() * scale))),
                       std::max(1, int(std::round(bitmap.height() * scale))));
  auto image = std::make_unique<views::ImageView>(ui::ImageModel::FromImageSkia(gfx::ImageSkia::CreateFrom1xBitmap(bitmap)));
  image->SetImageSize(size);
  auto card = std::make_unique<views::View>();
  card->SetLayoutManager(std::make_unique<views::FillLayout>());
  card->SetBorder(views::CreateEmptyBorder(6));
  card->SetBackground(views::CreateRoundedRectBackground(SkColorSetRGB(32, 33, 38), 10));
  card->AddChildView(std::move(image));
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET, views::Widget::InitParams::TYPE_TOOLTIP);
  params.parent = owner_->GetWidget()->GetNativeView();
  params.accept_events = false;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  widget_->SetContentsView(std::move(card));
  const auto cursor = display::Screen::Get()->GetCursorScreenPoint();
  gfx::Rect bounds(cursor.x() + 18, cursor.y() + 20, size.width() + 12, size.height() + 12);
  if (bounds.right() > area.right()) bounds.set_x(cursor.x() - bounds.width() - 18);
  if (bounds.bottom() > area.bottom()) bounds.set_y(cursor.y() - bounds.height() - 20);
  bounds.AdjustToFit(area);
  widget_->SetBounds(bounds);
  widget_->ShowInactive();
}
bool ImageLinkPreview::visible_for_testing() const { return widget_ && widget_->IsVisible(); }
void ImageLinkPreview::DidStartNavigation(content::NavigationHandle* navigation) {
  if (navigation->IsInPrimaryMainFrame()) Reset();
}
void ImageLinkPreview::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE) Reset();
}
void ImageLinkPreview::WebContentsDestroyed() { Reset(); }
void ImageLinkPreview::OnWidgetActivationChanged(views::Widget*, bool active) { if (!active) Reset(); }
void ImageLinkPreview::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) { Reset(); }
void ImageLinkPreview::OnWidgetDestroying(views::Widget*) { observation_.Reset(); Reset(); }
}  // namespace zitter::prototype
