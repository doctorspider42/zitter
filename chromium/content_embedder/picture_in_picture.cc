// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "picture_in_picture.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/numerics/safe_conversions.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/layers/deadline_policy.h"
#include "chromium/prototype/i18n.h"
#include "components/vector_icons/vector_icons.h"
#include "components/viz/common/surfaces/frame_sink_id.h"
#include "components/viz/common/surfaces/surface_id.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/document_picture_in_picture_window_controller.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/overlay_window.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "lab_web_view.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "third_party/blink/public/common/frame/user_activation_state.h"
#include "third_party/blink/public/mojom/picture_in_picture_window_options/picture_in_picture_window_options.mojom.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/hit_test.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_surface.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/size_f.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/frame_view.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace zitter::content_embedder {

namespace {

// Settings > Site permissions key, shared with ProfilePermissionController.
constexpr char kAutomaticSetting[] = "auto-picture-in-picture";

constexpr SkColor kWindowColor = SkColorSetRGB(0x20, 0x21, 0x24);
constexpr SkColor kButtonColor = SkColorSetARGB(0xa0, 0x00, 0x00, 0x00);
constexpr SkColor kScrimColor = SkColorSetARGB(0x50, 0x00, 0x00, 0x00);
constexpr SkColor kTextColor = SK_ColorWHITE;

// Chrome's minimum picture-in-picture window size.
constexpr gfx::Size kMinimumSize(240, 52);
constexpr gfx::Size kDefaultDocumentSize(480, 270);
constexpr double kMaximumScreenRatio = 0.8;
constexpr int kResizeBorder = 6;
constexpr int kMargin = 8;
constexpr int kButtonSize = 32;
constexpr int kIconSize = 18;
constexpr int kPlayButtonSize = 48;
constexpr int kPlayIconSize = 28;
constexpr int kHeaderHeight = 32;
constexpr int kHeaderButtonSize = 26;
constexpr int kPromptHeight = 36;

// The work area of the display that shows `window`, else the primary one.
gfx::Rect WorkAreaFor(gfx::NativeWindow window) {
  auto* screen = display::Screen::Get();
  if (!screen)
    return gfx::Rect(0, 0, 1280, 800);
  return (window ? screen->GetDisplayNearestWindow(window) : screen->GetPrimaryDisplay())
      .work_area();
}

gfx::Size MaximumSizeFor(const gfx::Rect& work_area) {
  return gfx::Size(
      std::max(kMinimumSize.width(),
               static_cast<int>(work_area.width() * kMaximumScreenRatio)),
      std::max(kMinimumSize.height(),
               static_cast<int>(work_area.height() * kMaximumScreenRatio)));
}

// Scales `size` down to fit `maximum` and up to reach `minimum`, keeping its
// aspect ratio while both bounds allow it.
gfx::Size ClampKeepingAspect(gfx::Size size, const gfx::Size& minimum,
                             const gfx::Size& maximum) {
  if (size.IsEmpty())
    return minimum;
  double scale = 1;
  scale = std::min(scale, static_cast<double>(maximum.width()) / size.width());
  scale = std::min(scale, static_cast<double>(maximum.height()) / size.height());
  if (scale >= 1) {
    scale = std::max(scale, static_cast<double>(minimum.width()) / size.width());
    scale = std::max(scale, static_cast<double>(minimum.height()) / size.height());
  }
  gfx::Size scaled(base::ClampRound(size.width() * scale),
                   base::ClampRound(size.height() * scale));
  scaled.SetToMax(minimum);
  scaled.SetToMin(maximum);
  return scaled;
}

// The largest rect with `natural`'s aspect ratio centred in `area`.
gfx::Rect Letterbox(const gfx::Rect& area, const gfx::Size& natural) {
  if (natural.IsEmpty() || area.IsEmpty())
    return area;
  const double aspect = static_cast<double>(natural.width()) / natural.height();
  int width = area.width();
  int height = base::ClampRound(width / aspect);
  if (height > area.height()) {
    height = area.height();
    width = std::min(area.width(), base::ClampRound(height * aspect));
  }
  return gfx::Rect(area.x() + (area.width() - width) / 2,
                   area.y() + (area.height() - height) / 2, width, height);
}

// A new window sits in the bottom-right corner of the work area; a moved or
// resized one stays where it is, kept on screen.
gfx::Rect PlaceWindow(const gfx::Rect& work_area, const gfx::Size& size,
                      std::optional<gfx::Point> origin) {
  const int margin = std::max(kMargin, (work_area.width() + work_area.height()) / 100);
  const gfx::Point corner(work_area.right() - size.width() - margin,
                          work_area.bottom() - size.height() - margin);
  gfx::Rect bounds(origin.value_or(corner), size);
  bounds.AdjustToFit(work_area);
  return bounds;
}

void SetButtonLabel(views::Button& button, const std::u16string& label) {
  button.SetTooltipText(label);
  button.GetViewAccessibility().SetName(label);
}

void SetButtonIcon(views::ImageButton& button, const gfx::VectorIcon& icon,
                   int icon_size) {
  button.SetImageModel(views::Button::STATE_NORMAL,
                       ui::ImageModel::FromVectorIcon(icon, ui::ColorVariant(kTextColor),
                                                      icon_size));
}

std::unique_ptr<views::ImageButton> MakeIconButton(views::Button::PressedCallback callback,
                                                   const gfx::VectorIcon& icon,
                                                   int icon_size, int size,
                                                   const std::u16string& label) {
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  SetButtonIcon(*button, icon, icon_size);
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetBackground(views::CreateRoundedRectBackground(ui::ColorVariant(kButtonColor),
                                                           size / 2.0f));
  button->SetSize(gfx::Size(size, size));
  SetButtonLabel(*button, label);
  return button;
}

std::unique_ptr<views::LabelButton> MakeTextButton(views::Button::PressedCallback callback,
                                                   const std::u16string& text) {
  auto button = std::make_unique<views::LabelButton>(std::move(callback), text);
  button->SetEnabledTextColors(ui::ColorVariant(kTextColor));
  button->SetBackground(views::CreateRoundedRectBackground(ui::ColorVariant(kButtonColor), 6.0f));
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 10)));
  return button;
}

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text) {
  auto label = std::make_unique<views::Label>(text);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(ui::ColorVariant(kTextColor));
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  return label;
}

bool ContainsVisible(const views::View* view, const gfx::Point& point) {
  return view && view->GetVisible() && view->bounds().Contains(point);
}

// The frame of a frameless picture-in-picture window: the client area fills
// it, its edges resize the window and `client_hit_test` says which client
// points are controls (HTCLIENT) and which drag the window (HTCAPTION).
class FramelessFrameView final : public views::FrameView {
 public:
  using HitTest = base::RepeatingCallback<int(const gfx::Point&)>;

  explicit FramelessFrameView(HitTest client_hit_test)
      : client_hit_test_(std::move(client_hit_test)) {}
  FramelessFrameView(const FramelessFrameView&) = delete;
  FramelessFrameView& operator=(const FramelessFrameView&) = delete;
  ~FramelessFrameView() override = default;

  gfx::Rect GetBoundsForClientView() const override { return bounds(); }

  gfx::Rect GetWindowBoundsForClientBounds(const gfx::Rect& client_bounds) const override {
    return client_bounds;
  }

  int NonClientHitTest(const gfx::Point& point) override {
    if (!bounds().Contains(point))
      return HTNOWHERE;
    const bool resizable = GetWidget() && GetWidget()->widget_delegate() &&
                           GetWidget()->widget_delegate()->CanResize();
    const int edge = GetHTComponentForFrame(point, gfx::Insets(kResizeBorder), 16, 16,
                                            resizable);
    if (edge != HTNOWHERE && edge != HTCLIENT)
      return edge;
    return client_hit_test_.Run(point);
  }

  // Events reach the client view; the frame only answers hit tests.
  bool DoesIntersectRect(const views::View* target, const gfx::Rect& rect) const override {
    return false;
  }

 private:
  HitTest client_hit_test_;
};

class FramelessDelegate final : public views::WidgetDelegate {
 public:
  explicit FramelessDelegate(FramelessFrameView::HitTest hit_test)
      : hit_test_(std::move(hit_test)) {
    SetCanResize(true);
    SetCanMaximize(false);
    SetCanMinimize(false);
    SetModalType(ui::mojom::ModalType::kNone);
    SetShowTitle(false);
    SetTitle(prototype::T(u"Obraz w obrazie"));
  }
  FramelessDelegate(const FramelessDelegate&) = delete;
  FramelessDelegate& operator=(const FramelessDelegate&) = delete;
  ~FramelessDelegate() override = default;

  std::unique_ptr<views::FrameView> CreateFrameView(views::Widget* widget) override {
    return std::make_unique<FramelessFrameView>(hit_test_);
  }

 private:
  FramelessFrameView::HitTest hit_test_;
};

std::unique_ptr<views::Widget> CreateFramelessWidget(views::WidgetDelegate* delegate,
                                                     const gfx::Rect& bounds) {
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                   views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate;
  params.bounds = bounds;
  params.z_order = ui::ZOrderLevel::kFloatingWindow;
  params.visible_on_all_workspaces = true;
  params.remove_standard_frame = true;
  params.name = "PictureInPictureWindow";
  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  return widget;
}

// The root view of a picture-in-picture window: its owner lays it out and
// learns when the pointer enters or leaves it.
class OverlayContentsView final : public views::View {
 public:
  OverlayContentsView(base::RepeatingClosure layout,
                      base::RepeatingCallback<void(bool)> hover)
      : layout_(std::move(layout)), hover_(std::move(hover)) {
    SetNotifyEnterExitOnChild(true);
  }
  OverlayContentsView(const OverlayContentsView&) = delete;
  OverlayContentsView& operator=(const OverlayContentsView&) = delete;
  ~OverlayContentsView() override = default;

  void SetSizeLimits(const gfx::Size& minimum, const gfx::Size& maximum) {
    minimum_ = minimum;
    maximum_ = maximum;
  }

  gfx::Size GetMinimumSize() const override { return minimum_; }
  gfx::Size GetMaximumSize() const override { return maximum_; }
  void Layout(PassKey) override { layout_.Run(); }
  void OnMouseEntered(const ui::MouseEvent& event) override { hover_.Run(true); }
  void OnMouseMoved(const ui::MouseEvent& event) override { hover_.Run(true); }
  void OnMouseExited(const ui::MouseEvent& event) override { hover_.Run(false); }

 private:
  base::RepeatingClosure layout_;
  base::RepeatingCallback<void(bool)> hover_;
  gfx::Size minimum_ = kMinimumSize;
  gfx::Size maximum_;
};

// Offered by a window that opened automatically while the site's setting is
// "ask": allowing or blocking stores a site rule, like Chrome's AutoPipSetting.
class AutomaticSettingPrompt final : public views::View {
 public:
  explicit AutomaticSettingPrompt(base::RepeatingCallback<void(bool)> answered)
      : answered_(std::move(answered)) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(true);
    SetBackground(views::CreateSolidBackground(ui::ColorVariant(kWindowColor)));
    label_ = AddChildView(MakeLabel(prototype::T(u"Automatycznie otwierać obraz w obrazie?")));
    allow_ = AddChildView(MakeTextButton(
        base::BindRepeating(&AutomaticSettingPrompt::Answer, base::Unretained(this), true),
        prototype::T(u"Zezwalaj")));
    block_ = AddChildView(MakeTextButton(
        base::BindRepeating(&AutomaticSettingPrompt::Answer, base::Unretained(this), false),
        prototype::T(u"Blokuj")));
  }
  AutomaticSettingPrompt(const AutomaticSettingPrompt&) = delete;
  AutomaticSettingPrompt& operator=(const AutomaticSettingPrompt&) = delete;
  ~AutomaticSettingPrompt() override = default;

  void Layout(PassKey) override {
    const gfx::Rect area = GetLocalBounds();
    block_->SizeToPreferredSize();
    allow_->SizeToPreferredSize();
    block_->SetPosition(gfx::Point(area.right() - kMargin - block_->width(),
                                   (area.height() - block_->height()) / 2));
    allow_->SetPosition(gfx::Point(block_->x() - kMargin / 2 - allow_->width(),
                                   (area.height() - allow_->height()) / 2));
    label_->SetBounds(kMargin, 0, std::max(0, allow_->x() - 2 * kMargin), area.height());
  }

 private:
  void Answer(bool allow) {
    SetVisible(false);
    // Copied: answering may close the window that owns this view.
    auto answered = answered_;
    answered.Run(allow);
  }

  base::RepeatingCallback<void(bool)> answered_;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::LabelButton> allow_ = nullptr;
  raw_ptr<views::LabelButton> block_ = nullptr;
};

// A floating, frameless window that shows a video's surface with Chrome-like
// controls: play/pause, previous and next track, skip ad, mute, back to tab
// and close. Content owns it through the video's
// VideoPictureInPictureWindowController.
class VideoPictureInPictureWindow final : public content::VideoOverlayWindow,
                                          public views::WidgetObserver {
 public:
  explicit VideoPictureInPictureWindow(
      content::VideoPictureInPictureWindowController* controller)
      : controller_(controller),
        delegate_(base::BindRepeating(&VideoPictureInPictureWindow::HitTest,
                                      base::Unretained(this))) {
    auto contents = std::make_unique<OverlayContentsView>(
        base::BindRepeating(&VideoPictureInPictureWindow::LayoutViews,
                            base::Unretained(this)),
        base::BindRepeating(&VideoPictureInPictureWindow::SetControlsShown,
                            base::Unretained(this)));
    contents->SetBackground(views::CreateSolidBackground(ui::ColorVariant(SK_ColorBLACK)));
    contents_ = contents.get();

    video_view_ = contents->AddChildView(std::make_unique<views::View>());
    video_view_->SetPaintToLayer(ui::LAYER_SURFACE);
    video_view_->layer()->SetMasksToBounds(true);
    video_view_->layer()->SetFillsBoundsOpaquely(false);
    video_view_->layer()->SetName("PictureInPictureVideo");

    controls_ = contents->AddChildView(std::make_unique<views::View>());
    controls_->SetPaintToLayer();
    controls_->layer()->SetFillsBoundsOpaquely(false);
    controls_->SetBackground(views::CreateSolidBackground(ui::ColorVariant(kScrimColor)));
    controls_->SetVisible(false);

    const auto bind = [this](void (VideoPictureInPictureWindow::*method)()) {
      return base::BindRepeating(method, base::Unretained(this));
    };
    mute_ = controls_->AddChildView(MakeIconButton(
        bind(&VideoPictureInPictureWindow::ToggleMute), vector_icons::kVolumeUpIcon,
        kIconSize, kButtonSize, prototype::T(u"Wycisz")));
    back_to_tab_ = controls_->AddChildView(MakeIconButton(
        bind(&VideoPictureInPictureWindow::BackToTab), vector_icons::kBackToTabIcon,
        kIconSize, kButtonSize, prototype::T(u"Wróć do karty")));
    close_ = controls_->AddChildView(MakeIconButton(
        bind(&VideoPictureInPictureWindow::CloseWindow), vector_icons::kCloseIcon,
        kIconSize, kButtonSize, prototype::T(u"Zamknij")));
    previous_track_ = controls_->AddChildView(MakeIconButton(
        bind(&VideoPictureInPictureWindow::PreviousTrack), vector_icons::kSkipPreviousIcon,
        kIconSize, kButtonSize, prototype::T(u"Poprzedni utwór")));
    play_pause_ = controls_->AddChildView(MakeIconButton(
        bind(&VideoPictureInPictureWindow::TogglePlayPause), vector_icons::kPauseIcon,
        kPlayIconSize, kPlayButtonSize, prototype::T(u"Wstrzymaj")));
    next_track_ = controls_->AddChildView(MakeIconButton(
        bind(&VideoPictureInPictureWindow::NextTrack), vector_icons::kSkipNextIcon,
        kIconSize, kButtonSize, prototype::T(u"Następny utwór")));
    skip_ad_ = controls_->AddChildView(MakeTextButton(
        bind(&VideoPictureInPictureWindow::SkipAd), prototype::T(u"Pomiń reklamę")));

    prompt_ = contents->AddChildView(std::make_unique<AutomaticSettingPrompt>(
        base::BindRepeating([](bool allow) {
          PictureInPicture::Get().AnswerAutomaticSetting(allow);
        })));
    prompt_->SetVisible(false);
    UpdateButtons();

    auto* page = controller_->GetWebContents();
    const gfx::Rect work_area =
        WorkAreaFor(page ? page->GetTopLevelNativeWindow() : gfx::NativeWindow());
    contents->SetSizeLimits(kMinimumSize, MaximumSizeFor(work_area));
    delegate_.SetContentsView(std::move(contents));
    widget_ = CreateFramelessWidget(&delegate_, PlaceWindow(work_area, kMinimumSize, {}));
    widget_observation_.Observe(widget_.get());
  }

  VideoPictureInPictureWindow(const VideoPictureInPictureWindow&) = delete;
  VideoPictureInPictureWindow& operator=(const VideoPictureInPictureWindow&) = delete;

  ~VideoPictureInPictureWindow() override {
    UnregisterFrameSink();
    widget_observation_.Reset();
  }

  // content::VideoOverlayWindow:
  bool IsActive() const override { return widget_->IsActive(); }

  void Close() override {
    UnregisterFrameSink();
    widget_->Close();
  }

  void ShowInactive() override {
    prompt_->SetVisible(PictureInPicture::Get().ShouldOfferAutomaticSetting(
        controller_->GetWebContents()));
    SetMediaMuted(controller_->GetMuteStatus());
    SetControlsShown(false);
    widget_->SetBounds(CalculateBounds());
    widget_->ShowInactive();
    widget_->SetVisibleOnAllWorkspaces(true);
    has_been_shown_ = true;
    contents_->InvalidateLayout();
  }

  void Hide() override {
    prompt_->SetVisible(false);
    widget_->Hide();
    UnregisterFrameSink();
  }

  bool IsVisible() const override { return widget_->IsVisible(); }

  gfx::Rect GetBounds() override { return widget_->GetWindowBoundsInScreen(); }

  void UpdateNaturalSize(const gfx::Size& natural_size) override {
    if (natural_size.IsEmpty())
      return;
    natural_size_ = natural_size;
    widget_->SetAspectRatio(gfx::SizeF(natural_size_));
    widget_->SetBounds(CalculateBounds());
    contents_->InvalidateLayout();
  }

  void SetPlaybackState(PlaybackState playback_state) override {
    playing_ = playback_state == kPlaying;
    SetButtonIcon(*play_pause_, playing_ ? vector_icons::kPauseIcon
                                         : vector_icons::kPlayArrowIcon,
                  kPlayIconSize);
    SetButtonLabel(*play_pause_,
                   playing_ ? prototype::T(u"Wstrzymaj") : prototype::T(u"Odtwórz"));
  }

  void SetPlayPauseButtonVisibility(bool is_visible) override {
    show_play_pause_ = is_visible;
    UpdateButtons();
  }

  void SetSkipAdButtonVisibility(bool is_visible) override {
    show_skip_ad_ = is_visible;
    UpdateButtons();
  }

  void SetNextTrackButtonVisibility(bool is_visible) override {
    show_next_track_ = is_visible;
    UpdateButtons();
  }

  void SetPreviousTrackButtonVisibility(bool is_visible) override {
    show_previous_track_ = is_visible;
    UpdateButtons();
  }

  void SetHidePictureInPictureButtonVisibility(bool is_visible) override {}
  void SetMicrophoneMuted(bool muted) override {}
  void SetCameraState(bool turned_on) override {}

  void SetMediaMuted(bool muted) override {
    muted_ = muted;
    SetButtonIcon(*mute_, muted_ ? vector_icons::kVolumeOffIcon : vector_icons::kVolumeUpIcon,
                  kIconSize);
    SetButtonLabel(*mute_, muted_ ? prototype::T(u"Włącz dźwięk") : prototype::T(u"Wycisz"));
  }

  void SetToggleMicrophoneButtonVisibility(bool is_visible) override {}
  void SetToggleCameraButtonVisibility(bool is_visible) override {}
  void SetHangUpButtonVisibility(bool is_visible) override {}
  void SetNextSlideButtonVisibility(bool is_visible) override {}
  void SetPreviousSlideButtonVisibility(bool is_visible) override {}
  void SetMediaPosition(const media_session::MediaPosition& position) override {}

  void SetSourceTitle(const std::u16string& source_title) override {
    delegate_.SetTitle(source_title.empty() ? prototype::T(u"Obraz w obrazie") : source_title);
  }

  void SetFaviconImages(const std::vector<media_session::MediaImage>& images) override {}

  void SetSurfaceId(const viz::SurfaceId& surface_id) override {
    if (!show_playback_controls_) {
      show_playback_controls_ = true;
      UpdateButtons();
    }
    UnregisterFrameSink();
    if (auto* compositor = widget_->GetCompositor()) {
      compositor->AddChildFrameSink(surface_id.frame_sink_id());
      frame_sink_id_ = surface_id.frame_sink_id();
    }
    auto* surface = video_view_->layer()->AsSurface();
    surface->SetFallbackBackgroundColor(SkColors::kBlack);
    // Layout sizes the surface to the letterboxed video afterwards.
    surface->SetShowSurface(surface_id, GetBounds().size(),
                            cc::DeadlinePolicy::UseDefaultDeadline(),
                            /*stretch_content_to_fill_bounds=*/true);
    contents_->InvalidateLayout();
  }

  void SetPlaybackControlsVisibility(bool is_visible) override {
    show_playback_controls_ = is_visible;
    UpdateButtons();
  }

  void SetImmersiveVideoOptions(const content::ImmersiveOptions& options) override {}

 private:
  // views::WidgetObserver:
  void OnWidgetBoundsChanged(views::Widget* widget, const gfx::Rect& new_bounds) override {
    controller_->UpdateLayerBounds();
  }

  void OnWidgetDestroying(views::Widget* widget) override { UnregisterFrameSink(); }

  // The system closed the window (e.g. Alt+F4). The controller owns this
  // object and destroys it when told, so tell it outside this notification.
  void OnWidgetDestroyed(views::Widget* widget) override {
    widget_observation_.Reset();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&VideoPictureInPictureWindow::NotifyDestroyed,
                                  weak_factory_.GetWeakPtr()));
  }

  void NotifyDestroyed() {
    controller_->OnWindowDestroyed(/*should_pause_video=*/show_play_pause_);
  }

  int HitTest(const gfx::Point& point) {
    if (ContainsVisible(prompt_, point))
      return HTCLIENT;
    if (controls_->GetVisible()) {
      for (const views::View* child : controls_->children()) {
        if (ContainsVisible(child, point))
          return HTCLIENT;
      }
    }
    return HTCAPTION;
  }

  gfx::Rect CalculateBounds() {
    auto* page = controller_->GetWebContents();
    const gfx::Rect work_area = WorkAreaFor(
        has_been_shown_ ? widget_->GetNativeWindow()
                        : page ? page->GetTopLevelNativeWindow() : gfx::NativeWindow());
    const gfx::Size maximum = MaximumSizeFor(work_area);
    contents_->SetSizeLimits(kMinimumSize, maximum);
    const gfx::Rect current = widget_->GetWindowBoundsInScreen();
    gfx::Size size = has_been_shown_
        ? current.size()
        : gfx::Size(work_area.width() / 5, work_area.height() / 5);
    if (!natural_size_.IsEmpty()) {
      const double aspect =
          static_cast<double>(natural_size_.width()) / natural_size_.height();
      if (has_been_shown_) {
        // Keep the area the user chose for the new aspect ratio.
        const double area = static_cast<double>(size.width()) * size.height();
        const int width = base::ClampRound(std::sqrt(area * aspect));
        size = gfx::Size(width, base::ClampRound(width / aspect));
      } else {
        size = Letterbox(gfx::Rect(size), natural_size_).size();
      }
    }
    size = ClampKeepingAspect(size, kMinimumSize, maximum);
    return PlaceWindow(work_area, size,
                       has_been_shown_ ? std::make_optional(current.origin()) : std::nullopt);
  }

  void LayoutViews() {
    const gfx::Rect area = contents_->GetLocalBounds();
    video_view_->SetBoundsRect(Letterbox(area, natural_size_));
    video_view_->layer()->AsSurface()->SetSurfaceSize(video_view_->size());
    controls_->SetBoundsRect(area);

    mute_->SetPosition(gfx::Point(kMargin, kMargin));
    close_->SetPosition(gfx::Point(area.width() - kMargin - kButtonSize, kMargin));
    back_to_tab_->SetPosition(gfx::Point(close_->x() - kMargin / 2 - kButtonSize, kMargin));

    const int middle = area.height() / 2;
    play_pause_->SetPosition(gfx::Point((area.width() - kPlayButtonSize) / 2,
                                        middle - kPlayButtonSize / 2));
    previous_track_->SetPosition(gfx::Point(play_pause_->x() - 2 * kMargin - kButtonSize,
                                            middle - kButtonSize / 2));
    next_track_->SetPosition(gfx::Point(play_pause_->bounds().right() + 2 * kMargin,
                                        middle - kButtonSize / 2));

    int bottom = area.height();
    if (prompt_->GetVisible()) {
      prompt_->SetBounds(0, bottom - kPromptHeight, area.width(), kPromptHeight);
      bottom -= kPromptHeight;
    }
    skip_ad_->SizeToPreferredSize();
    skip_ad_->SetPosition(gfx::Point(area.width() - kMargin - skip_ad_->width(),
                                     bottom - kMargin - skip_ad_->height()));
  }

  void UpdateButtons() {
    play_pause_->SetVisible(show_play_pause_ && show_playback_controls_);
    previous_track_->SetVisible(show_previous_track_ && show_playback_controls_);
    next_track_->SetVisible(show_next_track_ && show_playback_controls_);
    skip_ad_->SetVisible(show_skip_ad_);
    contents_->InvalidateLayout();
  }

  void SetControlsShown(bool shown) {
    if (controls_->GetVisible() != shown)
      controls_->SetVisible(shown);
  }

  void UnregisterFrameSink() {
    if (!frame_sink_id_)
      return;
    const viz::FrameSinkId id = *std::exchange(frame_sink_id_, std::nullopt);
    if (auto* compositor = widget_ ? widget_->GetCompositor() : nullptr)
      compositor->RemoveChildFrameSink(id);
  }

  void TogglePlayPause() {
    // Like Chrome: assume the command took effect until the player reports.
    SetPlaybackState(controller_->TogglePlayPause() ? kPlaying : kPaused);
  }

  void ToggleMute() { controller_->RequestMute(!controller_->GetMuteStatus()); }
  void NextTrack() { controller_->NextTrack(); }
  void PreviousTrack() { controller_->PreviousTrack(); }
  void SkipAd() { controller_->SkipAd(); }

  void BackToTab() {
    PictureInPicture::Get().CloseFromWindow(controller_,
                                            PictureInPicture::CloseReason::kBackToTab);
  }

  void CloseWindow() {
    PictureInPicture::Get().CloseFromWindow(controller_,
                                            PictureInPicture::CloseReason::kCloseAndPause);
  }

  const raw_ptr<content::VideoPictureInPictureWindowController> controller_;
  FramelessDelegate delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> widget_observation_{this};
  raw_ptr<OverlayContentsView> contents_ = nullptr;
  raw_ptr<views::View> video_view_ = nullptr;
  raw_ptr<views::View> controls_ = nullptr;
  raw_ptr<views::ImageButton> mute_ = nullptr;
  raw_ptr<views::ImageButton> back_to_tab_ = nullptr;
  raw_ptr<views::ImageButton> close_ = nullptr;
  raw_ptr<views::ImageButton> previous_track_ = nullptr;
  raw_ptr<views::ImageButton> play_pause_ = nullptr;
  raw_ptr<views::ImageButton> next_track_ = nullptr;
  raw_ptr<views::LabelButton> skip_ad_ = nullptr;
  raw_ptr<AutomaticSettingPrompt> prompt_ = nullptr;
  gfx::Size natural_size_;
  std::optional<viz::FrameSinkId> frame_sink_id_;
  bool has_been_shown_ = false;
  bool playing_ = true;
  bool muted_ = false;
  bool show_play_pause_ = true;
  bool show_playback_controls_ = true;
  bool show_previous_track_ = false;
  bool show_next_track_ = false;
  bool show_skip_ad_ = false;
  base::WeakPtrFactory<VideoPictureInPictureWindow> weak_factory_{this};
};

// Hosts a document picture-in-picture page. Its delegate closes the window
// when the page closes itself (window.close() or the opener's controller).
class DocumentWebView final : public LabWebView {
 public:
  DocumentWebView(content::BrowserContext* context, base::RepeatingClosure closed)
      : LabWebView(context), closed_(std::move(closed)) {}
  DocumentWebView(const DocumentWebView&) = delete;
  DocumentWebView& operator=(const DocumentWebView&) = delete;
  ~DocumentWebView() override = default;

 private:
  void CloseContents(content::WebContents* source) override {
    if (closed_)
      closed_.Run();
  }

  void ActivateContents(content::WebContents* contents) override {
    if (GetWidget())
      GetWidget()->Activate();
  }

  std::optional<gfx::Rect> GetWindowBoundsInScreen() override {
    if (!GetWidget())
      return std::nullopt;
    return GetWidget()->GetWindowBoundsInScreen();
  }

  base::RepeatingClosure closed_;
};

}  // namespace

// The floating window of documentPictureInPicture.requestWindow(): a header
// with the opener's site, back to tab and close, above the PiP page.
class DocumentPictureInPictureWindow final : public views::WidgetObserver {
 public:
  DocumentPictureInPictureWindow(content::WebContents& opener,
                                 std::unique_ptr<content::WebContents> child,
                                 bool offer_setting)
      : delegate_(base::BindRepeating(&DocumentPictureInPictureWindow::HitTest,
                                      base::Unretained(this))) {
    const auto& options = child->GetPictureInPictureOptions();
    auto contents = std::make_unique<OverlayContentsView>(
        base::BindRepeating(&DocumentPictureInPictureWindow::LayoutViews,
                            base::Unretained(this)),
        base::DoNothing());
    contents->SetBackground(views::CreateSolidBackground(ui::ColorVariant(kWindowColor)));
    contents_ = contents.get();

    const std::string host = opener.GetPrimaryMainFrame()->GetLastCommittedOrigin().host();
    const std::u16string site =
        host.empty() ? prototype::T(u"Obraz w obrazie") : base::UTF8ToUTF16(host);
    title_ = contents->AddChildView(MakeLabel(site));
    back_to_tab_ = contents->AddChildView(MakeIconButton(
        base::BindRepeating(&DocumentPictureInPictureWindow::Leave, base::Unretained(this),
                            PictureInPicture::CloseReason::kBackToTab),
        vector_icons::kBackToTabIcon, kIconSize, kHeaderButtonSize,
        prototype::T(u"Wróć do karty")));
    back_to_tab_->SetVisible(!(options && options->disallow_return_to_opener));
    close_ = contents->AddChildView(MakeIconButton(
        base::BindRepeating(&DocumentPictureInPictureWindow::Leave, base::Unretained(this),
                            PictureInPicture::CloseReason::kCloseAndPause),
        vector_icons::kCloseIcon, kIconSize, kHeaderButtonSize, prototype::T(u"Zamknij")));
    if (offer_setting) {
      prompt_ = contents->AddChildView(std::make_unique<AutomaticSettingPrompt>(
          base::BindRepeating(&DocumentPictureInPictureWindow::OnPromptAnswered,
                              base::Unretained(this))));
    }

    auto web_view = std::make_unique<DocumentWebView>(
        child->GetBrowserContext(),
        base::BindRepeating(&DocumentPictureInPictureWindow::OnContentsClosed,
                            base::Unretained(this)));
    web_view_ = contents->AddChildView(std::move(web_view));
    web_view_->SetOwnedWebContents(std::move(child));

    const gfx::Rect work_area = WorkAreaFor(opener.GetTopLevelNativeWindow());
    gfx::Size inner(options ? base::saturated_cast<int>(options->width) : 0,
                    options ? base::saturated_cast<int>(options->height) : 0);
    if (inner.IsEmpty())
      inner = kDefaultDocumentSize;
    // Like Chrome, a site asks for at most about a quarter of the screen.
    inner = ClampKeepingAspect(inner, kMinimumSize,
                               gfx::Size(std::max(kMinimumSize.width(), work_area.width() / 2),
                                         std::max(kMinimumSize.height(),
                                                  work_area.height() / 2)));
    const int chrome_height = kHeaderHeight + (prompt_ ? kPromptHeight : 0);
    const gfx::Size size(inner.width(), inner.height() + chrome_height);
    contents->SetSizeLimits(
        gfx::Size(kMinimumSize.width(), kMinimumSize.height() + chrome_height),
        MaximumSizeFor(work_area));
    delegate_.SetContentsView(std::move(contents));
    widget_ = CreateFramelessWidget(&delegate_, PlaceWindow(work_area, size, {}));
    widget_observation_.Observe(widget_.get());
  }

  DocumentPictureInPictureWindow(const DocumentPictureInPictureWindow&) = delete;
  DocumentPictureInPictureWindow& operator=(const DocumentPictureInPictureWindow&) = delete;

  ~DocumentPictureInPictureWindow() override { widget_observation_.Reset(); }

  content::WebContents* child() const { return web_view_->web_contents(); }

  void Show(bool activate) {
    if (activate) {
      widget_->Show();
      web_view_->RequestFocus();
    } else {
      widget_->ShowInactive();
    }
    widget_->SetVisibleOnAllWorkspaces(true);
  }

  void Hide() { widget_->Hide(); }

 private:
  // views::WidgetObserver: the system closed the window (e.g. Alt+F4).
  void OnWidgetDestroyed(views::Widget* widget) override {
    widget_observation_.Reset();
    PictureInPicture::Get().OnDocumentWindowClosed(this);
  }

  void OnContentsClosed() { PictureInPicture::Get().OnDocumentWindowClosed(this); }

  void Leave(PictureInPicture::CloseReason reason) {
    auto& manager = PictureInPicture::Get();
    if (manager.controller_ && manager.document_.get() == this)
      manager.CloseFromWindow(manager.controller_, reason);
    else
      manager.OnDocumentWindowClosed(this);
  }

  void OnPromptAnswered(bool allow) {
    contents_->InvalidateLayout();
    PictureInPicture::Get().AnswerAutomaticSetting(allow);
  }

  int HitTest(const gfx::Point& point) {
    if (ContainsVisible(back_to_tab_, point) || ContainsVisible(close_, point) ||
        ContainsVisible(prompt_, point)) {
      return HTCLIENT;
    }
    return point.y() < kHeaderHeight ? HTCAPTION : HTCLIENT;
  }

  void LayoutViews() {
    const gfx::Rect area = contents_->GetLocalBounds();
    const int button_y = (kHeaderHeight - kHeaderButtonSize) / 2;
    close_->SetPosition(gfx::Point(area.width() - button_y - kHeaderButtonSize, button_y));
    back_to_tab_->SetPosition(
        gfx::Point(close_->x() - kMargin / 2 - kHeaderButtonSize, button_y));
    const int title_end = back_to_tab_->GetVisible() ? back_to_tab_->x() : close_->x();
    title_->SetBounds(kMargin, 0, std::max(0, title_end - 2 * kMargin), kHeaderHeight);
    int top = kHeaderHeight;
    if (prompt_ && prompt_->GetVisible()) {
      prompt_->SetBounds(0, top, area.width(), kPromptHeight);
      top += kPromptHeight;
    }
    web_view_->SetBounds(0, top, area.width(), std::max(0, area.height() - top));
  }

  FramelessDelegate delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> widget_observation_{this};
  raw_ptr<OverlayContentsView> contents_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageButton> back_to_tab_ = nullptr;
  raw_ptr<views::ImageButton> close_ = nullptr;
  raw_ptr<AutomaticSettingPrompt> prompt_ = nullptr;
  raw_ptr<DocumentWebView> web_view_ = nullptr;
};

// Closes the video window when its page navigates away or goes away.
class PictureInPicture::VideoObserver final : public content::WebContentsObserver {
 public:
  explicit VideoObserver(content::WebContents* page) : content::WebContentsObserver(page) {}

  void PrimaryPageChanged(content::Page& page) override {
    PictureInPicture::Get().CloseWindowInternal();
  }

  void WebContentsDestroyed() override { PictureInPicture::Get().CloseWindowInternal(); }
};

// Forgets the document window's controller when its opener goes away; the
// controller itself closes the PiP page.
class PictureInPicture::OpenerObserver final : public content::WebContentsObserver {
 public:
  explicit OpenerObserver(content::WebContents* opener)
      : content::WebContentsObserver(opener) {}

  void WebContentsDestroyed() override { PictureInPicture::Get().OnOpenerDestroyed(); }
};

PictureInPicture& PictureInPicture::Get() {
  static base::NoDestructor<PictureInPicture> instance;
  return *instance;
}

PictureInPicture::PictureInPicture() = default;
PictureInPicture::~PictureInPicture() = default;

content::PictureInPictureResult PictureInPicture::EnterVideo(content::WebContents* page) {
  // A document picture-in-picture page cannot put a video in its own window.
  if (!page || (document_ && document_->child() == page))
    return content::PictureInPictureResult::kNotSupported;
  if (!controller_ || controller_->GetWebContents() != page ||
      !page->HasPictureInPictureVideo()) {
    if (controller_ || document_) {
      const auto weak_page = page->GetWeakPtr();
      CloseWindowInternal();
      if (!weak_page)
        return content::PictureInPictureResult::kNotSupported;
    }
    video_observer_ = std::make_unique<VideoObserver>(page);
    controller_ = content::PictureInPictureWindowController::
        GetOrCreateVideoPictureInPictureController(page);
    NoteWindowOpened(page);
  }
  return content::PictureInPictureResult::kSuccess;
}

void PictureInPicture::Exit() {
  CloseWindowInternal();
}

content::WebContents* PictureInPicture::OpenDocument(
    content::WebContents* opener, std::unique_ptr<content::WebContents> child) {
  // Nested document windows are not supported.
  if (!opener || !child || (document_ && document_->child() == opener))
    return nullptr;
  const auto weak_opener = opener->GetWeakPtr();
  if (controller_ || document_)
    CloseWindowInternal();
  if (!weak_opener)
    return nullptr;

  NoteWindowOpened(opener);
  const bool automatic = automatic_page_.get() == opener;
  auto* controller = content::PictureInPictureWindowController::
      GetOrCreateDocumentPictureInPictureController(opener);
  auto* page = child.get();
  controller->SetChildWebContents(page);
  opener_observer_ = std::make_unique<OpenerObserver>(opener);
  document_ = std::make_unique<DocumentPictureInPictureWindow>(
      *opener, std::move(child), automatic && automatic_ask_);
  controller_ = controller;
  controller->Show();
  // An automatic window does not take focus from what the user switched to.
  document_->Show(/*activate=*/!automatic);
  page->ResumeLoadingCreatedWebContents();
  return page;
}

void PictureInPicture::CloseFromWindow(content::PictureInPictureWindowController* controller,
                                       CloseReason reason) {
  if (!controller)
    return;
  if (controller == controller_) {
    // Forget the window first: closing reenters through Exit().
    controller_ = nullptr;
    video_observer_.reset();
    opener_observer_.reset();
    automatic_page_ = nullptr;
    automatic_ask_ = false;
  }
  switch (reason) {
    case CloseReason::kBackToTab:
      controller->CloseAndFocusInitiator();
      break;
    case CloseReason::kCloseAndPause:
      controller->Close(/*should_pause_video=*/true);
      break;
    case CloseReason::kClose:
      controller->Close(/*should_pause_video=*/false);
      break;
  }
  if (!controller_)
    DestroyDocumentWindowSoon();
}

void PictureInPicture::OnTabVisibilityChanged(content::WebContents& page,
                                              content::Visibility visibility,
                                              bool capturing) {
  if (visibility == content::Visibility::VISIBLE) {
    if (automatic_page_.get() == &page || pending_page_.get() == &page) {
      // Exit asynchronously, like Chrome, so a window that is still opening
      // is not closed from inside its own setup.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&PictureInPicture::MaybeExitAutomatically,
                                    weak_factory_.GetWeakPtr(), page.GetWeakPtr()));
    }
    return;
  }
  // Let the switch finish first: a tab that closes is gone by then.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&PictureInPicture::MaybeEnterAutomatically,
                                weak_factory_.GetWeakPtr(), page.GetWeakPtr(), capturing));
}

bool PictureInPicture::AllowsAutomaticWindow(content::WebContents* opener) const {
  return opener && pending_page_.get() == opener && base::TimeTicks::Now() < pending_deadline_;
}

bool PictureInPicture::ShouldOfferAutomaticSetting(content::WebContents* page) const {
  return page && automatic_ask_ && automatic_page_.get() == page;
}

void PictureInPicture::AnswerAutomaticSetting(bool allow) {
  automatic_ask_ = false;
  auto* page = automatic_page_.get();
  if (!page || page->IsBeingDestroyed())
    return;
  const url::Origin origin = page->GetPrimaryMainFrame()->GetLastCommittedOrigin();
  static_cast<ProfileContext*>(page->GetBrowserContext())
      ->site_permissions()
      .SetSiteRule(kAutomaticSetting, origin, allow ? 1 : 2);
  if (!allow && controller_)
    CloseFromWindow(controller_, CloseReason::kClose);
}

void PictureInPicture::CloseWindowInternal() {
  video_observer_.reset();
  opener_observer_.reset();
  automatic_page_ = nullptr;
  automatic_ask_ = false;
  // Forget the controller before closing: closing reenters through Exit().
  if (auto* controller = controller_.get()) {
    controller_ = nullptr;
    controller->Close(/*should_pause_video=*/false);
  }
  DestroyDocumentWindowSoon();
}

void PictureInPicture::DestroyDocumentWindowSoon() {
  if (!document_)
    return;
  document_->Hide();
  // The window may be in the middle of a callback from its page or widget.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, std::move(document_));
}

void PictureInPicture::OnDocumentWindowClosed(DocumentPictureInPictureWindow* window) {
  if (document_.get() != window)
    return;
  // Destroying the page tells its controller, which then calls Exit().
  DestroyDocumentWindowSoon();
}

void PictureInPicture::OnOpenerDestroyed() {
  opener_observer_.reset();
  controller_ = nullptr;
  automatic_page_ = nullptr;
  automatic_ask_ = false;
  DestroyDocumentWindowSoon();
}

void PictureInPicture::NoteWindowOpened(content::WebContents* page) {
  const bool automatic =
      pending_page_.get() == page && base::TimeTicks::Now() < pending_deadline_;
  automatic_page_ = automatic ? page->GetWeakPtr() : nullptr;
  automatic_ask_ = automatic && pending_ask_;
  if (!automatic)
    return;
  pending_page_ = nullptr;
  // The user came back before the window opened: close it again.
  if (page->GetVisibility() == content::Visibility::VISIBLE) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&PictureInPicture::MaybeExitAutomatically,
                                  weak_factory_.GetWeakPtr(), page->GetWeakPtr()));
  }
}

void PictureInPicture::MaybeEnterAutomatically(base::WeakPtr<content::WebContents> page,
                                               bool capturing) {
  auto* contents = page.get();
  if (!contents || contents->IsBeingDestroyed() ||
      contents->GetVisibility() == content::Visibility::VISIBLE) {
    return;
  }
  // Like Chrome, automatic picture-in-picture never replaces an open window.
  if (controller_ || document_)
    return;
  const GURL& url = contents->GetLastCommittedURL();
  if (!url.SchemeIs(url::kHttpsScheme) && !url.SchemeIsFile())
    return;
  const url::Origin origin = contents->GetPrimaryMainFrame()->GetLastCommittedOrigin();
  if (origin.opaque())
    return;
  auto* session = content::MediaSession::GetIfExists(contents);
  if (!session)
    return;
  // The site registered an "enterpictureinpicture" action handler, or
  // Content offers browser-initiated automatic picture-in-picture.
  if (!std::ranges::contains(
          session->GetMediaSessionActionsSync(),
          media_session::mojom::MediaSessionAction::kEnterAutoPictureInPicture)) {
    return;
  }
  const auto info = session->GetMediaSessionInfoSync();
  const bool playing = info &&
      info->playback_state == media_session::mojom::MediaPlaybackState::kPlaying &&
      contents->IsCurrentlyAudible();
  if (!playing && !capturing)
    return;
  const int setting = static_cast<ProfileContext*>(contents->GetBrowserContext())
                          ->site_permissions()
                          .SiteSetting(kAutomaticSetting, origin);
  // Block, or ask in a private window (Chrome treats it as block there).
  if (setting == 2 || (setting == 3 && contents->GetBrowserContext()->IsOffTheRecord()))
    return;
  pending_page_ = page;
  pending_deadline_ = base::TimeTicks::Now() + blink::kActivationLifespan;
  pending_ask_ = setting == 3;
  session->EnterAutoPictureInPicture();
}

void PictureInPicture::MaybeExitAutomatically(base::WeakPtr<content::WebContents> page) {
  auto* contents = page.get();
  if (!contents || contents->GetVisibility() != content::Visibility::VISIBLE)
    return;
  if (pending_page_.get() == contents)
    pending_page_ = nullptr;
  if (automatic_page_.get() == contents)
    Exit();
}

std::unique_ptr<content::VideoOverlayWindow> CreateVideoOverlayWindow(
    content::VideoPictureInPictureWindowController* controller) {
  return std::make_unique<VideoPictureInPictureWindow>(controller);
}

}  // namespace zitter::content_embedder
