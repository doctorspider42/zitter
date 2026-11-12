// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "workspace_media_access.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/bind_post_task.h"
#include "build/build_config.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/page_dialogs.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/desktop_capture.h"
#include "content/public/browser/media_capture_devices.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "media/mojo/mojom/display_media_information.mojom.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/text_elider.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace zitter::content_embedder {
namespace {

using Result = blink::mojom::MediaStreamRequestResult;
using Type = blink::mojom::MediaStreamType;
using Policy = network::mojom::PermissionsPolicyFeature;
using DesktopID = content::DesktopMediaID;

void CloseDesktopPicker(DesktopID source) {
  if (source.id_type == DesktopID::IdType::kNativePickerSession) {
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(
            &content::desktop_capture::CloseNativeScreenCapturePicker, source));
  }
}

std::u16string DesktopLabel(DesktopID::Type type) {
  return prototype::T(type == DesktopID::TYPE_SCREEN ? u"Ekran systemowy"
                                                     : u"Okno systemowe");
}

bool IsTabCapture(Type type) {
  return type == Type::DISPLAY_VIDEO_CAPTURE ||
         type == Type::DISPLAY_VIDEO_CAPTURE_THIS_TAB;
}

bool IsEligible(content::RenderFrameHost* frame, content::WebContents* page,
                const url::Origin& origin, bool audio, bool video,
                bool tab = false) {
  return frame && page && page->GetPrimaryMainFrame() == frame &&
         frame->IsActive() && page->GetVisibility() == content::Visibility::VISIBLE &&
         frame->GetLastCommittedOrigin() == origin &&
         network::IsOriginPotentiallyTrustworthy(origin) &&
         (!audio || frame->IsFeatureEnabled(Policy::kMicrophone)) &&
         (!video || frame->IsFeatureEnabled(Policy::kCamera)) &&
         (!tab || frame->IsFeatureEnabled(Policy::kDisplayCapture));
}

std::optional<blink::MediaStreamDevice> SelectDevice(
    const blink::MediaStreamDevices& devices,
    const std::vector<std::string>& requested) {
  for (const auto& device : devices) {
    if (requested.empty() || std::ranges::contains(requested, device.id))
      return device;
  }
  return std::nullopt;
}

}  // namespace

#if BUILDFLAG(IS_WIN)
void RequestWorkspaceCaptureAccess(bool audio,
                                   bool video,
                                   base::RepeatingCallback<bool()> still_current,
                                   base::OnceCallback<void(bool)> done) {
  std::move(done).Run(true);
}
#endif

struct WorkspaceMediaAccess::Pending {
  struct Source {
    content::WeakDocumentPtr document;
    std::u16string label;
    DesktopID::Type desktop_type = DesktopID::TYPE_NONE;
  };
  content::WeakDocumentPtr document;
  content::MediaStreamRequest request;
  content::MediaResponseCallback reply;
  std::vector<Source> sources;
  DesktopID desktop_picker;
};

class WorkspaceMediaAccess::Stream final : public content::MediaStreamUI,
                                          public content::WebContentsObserver {
 public:
  Stream(views::Widget& parent,
         content::WeakDocumentPtr document,
         const url::Origin& origin,
         Type audio,
         Type video,
         content::WebContents* source,
         std::u16string source_label,
         DesktopID desktop_source)
      : content::WebContentsObserver(source),
        parent_(parent.GetWeakPtr()),
        document_(std::move(document)),
        origin_(origin),
        audio_(audio),
        video_(video),
        source_label_(std::move(source_label)),
        desktop_source_(desktop_source) {}

  ~Stream() override {
    weak_factory_.InvalidateWeakPtrs();
    CloseDesktopPicker(desktop_source_);
    widget_.reset();
  }

  base::WeakPtr<Stream> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  bool Matches(content::RenderFrameHost* frame, const url::Origin& origin,
               Type type) const {
    return !stopped_ && frame == document_.AsRenderFrameHostIfValid() &&
           origin == origin_ &&
           ((audio_ == Type::DEVICE_AUDIO_CAPTURE && type == audio_) ||
            (video_ == Type::DEVICE_VIDEO_CAPTURE && type == video_));
  }

  bool CapturesOrBelongsTo(content::WebContents* page) const {
    auto* frame = document_.AsRenderFrameHostIfValid();
    return !stopped_ && (page == web_contents() ||
                         (frame && content::WebContents::FromRenderFrameHost(frame) == page));
  }

  gfx::NativeViewId OnStarted(base::RepeatingClosure stop, SourceCallback source,
                              const std::string& label,
                              std::vector<content::DesktopMediaID> ids,
                              StateChangeCallback state_change) override {
    if (stopped_ || !parent_ || !document_.AsRenderFrameHostIfValid()) {
      stop.Run();
      return 0;
    }
    stop_ = std::move(stop);
    const bool desktop = !desktop_source_.is_null();
    // The capture manager now owns the native picker session's lifetime.
    desktop_source_ = {};
    const auto origin = base::UTF8ToUTF16(origin_.Serialize());
    auto body = std::make_unique<views::Label>(desktop
        ? prototype::TF(u"{} udostępnia obraz: {}.", {origin, source_label_})
        : IsTabCapture(video_) ? prototype::TF(audio_ == Type::DISPLAY_AUDIO_CAPTURE
                            ? u"{} udostępnia obraz i dźwięk karty: {}."
                            : u"{} udostępnia obraz karty: {}.",
                        {origin, source_label_})
        : prototype::TF(u"{} korzysta z kamery lub mikrofonu.", {origin}));
    body->SetMultiLine(true);
    body->SetAllowCharacterBreak(true);
    delegate_ = prototype::CreateSceneModalDialog(std::move(body), false, false);
    delegate_->SetTitle(prototype::T(u"Trwa przechwytywanie"));
    delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
    delegate_->SetButtonLabel(ui::mojom::DialogButton::kCancel,
                              prototype::T(u"Zatrzymaj"));
    widget_.reset(views::DialogDelegate::CreateDialogWidget(
        delegate_.get(), parent_->GetNativeWindow(), parent_->GetNativeView()));
    widget_->SetColorModeOverride(parent_->color_mode_override());
    widget_->MakeCloseSynchronous(base::BindOnce(
        [](base::WeakPtr<Stream> stream, views::Widget::ClosedReason) {
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE, base::BindOnce(&Stream::Stop, stream));
        },
        weak_factory_.GetWeakPtr()));
    widget_->ShowInactive();
    return 0;
  }

  void OnDeviceStoppedForSourceChange(const std::string& label,
                                      const content::DesktopMediaID& old_id,
                                      const content::DesktopMediaID& new_id,
                                      bool controls) override {}
  void OnDeviceStopped(const std::string& label,
                       const content::DesktopMediaID& id) override {}

  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (navigation->IsInPrimaryMainFrame() && !navigation->IsSameDocument())
      Stop();
  }

  void PrimaryPageChanged(content::Page&) override { Stop(); }
  void WebContentsDestroyed() override { Stop(); }

  void Stop() {
    stopped_ = true;
    Observe(nullptr);
    widget_.reset();
    delegate_.reset();
    if (stop_) {
      auto stop = std::move(stop_);
      stop.Run();
    }
  }

  bool StopFromIndicatorForTesting() {
    if (!widget_ || !widget_->IsVisible() || widget_->IsClosed())
      return false;
    delegate_->CancelDialog();
    return true;
  }

 private:
  const base::WeakPtr<views::Widget> parent_;
  content::WeakDocumentPtr document_;
  const url::Origin origin_;
  const Type audio_;
  const Type video_;
  const std::u16string source_label_;
  DesktopID desktop_source_;
  bool stopped_ = false;
  base::RepeatingClosure stop_;
  std::unique_ptr<views::DialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::WeakPtrFactory<Stream> weak_factory_{this};
};

WorkspaceMediaAccess::WorkspaceMediaAccess(views::Widget& parent,
                                           SelectedPage selected,
                                           Sources sources,
                                           Prompt prompt)
    : parent_(parent), selected_(std::move(selected)),
      sources_(std::move(sources)), prompt_(std::move(prompt)) {}

WorkspaceMediaAccess::~WorkspaceMediaAccess() {
  Stop();
}

void WorkspaceMediaAccess::Request(content::WebContents* page,
                                    const content::MediaStreamRequest& request,
                                    content::MediaResponseCallback reply) {
  auto* frame = content::RenderFrameHost::FromID(request.render_process_id,
                                                  request.render_frame_id);
  const bool audio = request.audio_type == Type::DEVICE_AUDIO_CAPTURE;
  const bool video = request.video_type == Type::DEVICE_VIDEO_CAPTURE;
  const bool tab = IsTabCapture(request.video_type);
  const bool valid_types = tab
      ? (request.audio_type == Type::NO_SERVICE ||
         request.audio_type == Type::DISPLAY_AUDIO_CAPTURE)
      : (audio || video) &&
            (audio || request.audio_type == Type::NO_SERVICE) &&
            (video || request.video_type == Type::NO_SERVICE);
  VLOG(1) << "Workspace media capture requested, tab=" << tab;
  if (pending_ || stream_ || page != selected_.Run() || !request.user_gesture ||
      request.request_type != blink::MEDIA_GENERATE_STREAM ||
      request.request_pan_tilt_zoom_permission || !valid_types ||
      !IsEligible(frame, page, request.url_origin, audio, video, tab) ||
      !frame->IsFocused() || !frame->ConsumeTransientUserActivation()) {
    std::move(reply).Run({}, Result::PERMISSION_DENIED, nullptr);
    return;
  }
  pending_ = std::make_unique<Pending>(
      Pending{frame->GetWeakDocumentPtr(), request, std::move(reply), {}, {}});
  std::vector<std::u16string> choices;
  size_t tab_count = 0;
  if (tab) {
    size_t tab_number = 0;
    for (auto* source : sources_.Run()) {
      ++tab_number;
      if (!source || source->IsBeingDestroyed() ||
          source->GetBrowserContext() != page->GetBrowserContext() ||
          !source->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() ||
          source->GetController().GetPendingEntry() ||
          (source == page && request.exclude_self_browser_surface)) {
        continue;
      }
      auto* source_frame = source->GetPrimaryMainFrame();
      if (!source_frame || !source_frame->IsActive() ||
          source_frame->GetLastCommittedOrigin().opaque()) {
        continue;
      }
      const auto source_origin =
          base::UTF8ToUTF16(source_frame->GetLastCommittedOrigin().Serialize());
      std::u16string title;
      gfx::ElideString(base::CollapseWhitespace(source->GetTitle(), true),
                       48, &title);
      base::i18n::SanitizeUserSuppliedString(&title);
      auto label = prototype::TF(u"Karta {} · {}", {
          base::NumberToString16(tab_number), source_origin});
      if (!title.empty())
        label += u" · " + title;
      choices.push_back(label);
      pending_->sources.push_back({source_frame->GetWeakDocumentPtr(), std::move(label)});
    }
    tab_count = choices.size();
    bool desktop = desktop_probe_ == DesktopProbe::kSynthetic;
#if BUILDFLAG(IS_MAC)
    desktop |= (desktop_probe_ == DesktopProbe::kNone ||
                desktop_probe_ == DesktopProbe::kNativeCancel) &&
               CanUseWorkspaceDesktopPicker();
#endif
    if (desktop) {
      for (auto type : {DesktopID::TYPE_WINDOW, DesktopID::TYPE_SCREEN}) {
        if (type == DesktopID::TYPE_SCREEN && request.exclude_monitor_type_surfaces)
          continue;
        auto label = DesktopLabel(type);
        choices.push_back(label);
        pending_->sources.push_back({{}, std::move(label), type});
      }
    }
    if (choices.empty()) {
      Reject(Result::PERMISSION_DENIED);
      return;
    }
  }
  const auto origin = base::UTF8ToUTF16(request.url_origin.Serialize());
  const auto description = audio && video
      ? prototype::T(u"kamerę i mikrofon")
      : audio ? prototype::T(u"mikrofon") : prototype::T(u"kamerę");
  std::u16string audio_option;
  if (tab_count && request.audio_type == Type::DISPLAY_AUDIO_CAPTURE) {
    audio_option = prototype::T(request.disable_local_echo ||
                                    request.suppress_local_audio_playback
        ? u"Udostępnij dźwięk karty i wycisz go lokalnie"
        : u"Udostępnij także dźwięk karty");
  }
  prompt_.Run(
      tab ? prototype::TF(u"{} chce udostępnić obraz.", {origin})
          : prototype::TF(u"{} chce użyć urządzeń: {}.", {origin, description}),
      std::move(choices), tab_count, std::move(audio_option),
      base::BindOnce(&WorkspaceMediaAccess::OnConsent,
                     weak_factory_.GetWeakPtr(), ++request_id_));
}

bool WorkspaceMediaAccess::IsCurrentRequest() const {
  return pending_ && IsEligible(pending_->document.AsRenderFrameHostIfValid(),
      selected_.Run(), pending_->request.url_origin,
      pending_->request.audio_type == Type::DEVICE_AUDIO_CAPTURE,
      pending_->request.video_type == Type::DEVICE_VIDEO_CAPTURE,
      IsTabCapture(pending_->request.video_type));
}

void WorkspaceMediaAccess::OnConsent(int id, std::optional<size_t> selection,
                                     bool share_audio) {
  if (id != request_id_ || !pending_)
    return;
  if (!selection || !IsCurrentRequest()) {
    Reject(Result::PERMISSION_DENIED);
    return;
  }
  const auto& request = pending_->request;
  if (IsTabCapture(request.video_type)) {
    if (*selection >= pending_->sources.size()) {
      Reject(Result::PERMISSION_DENIED);
      return;
    }
    auto& selected_source = pending_->sources[*selection];
    if (selected_source.desktop_type != DesktopID::TYPE_NONE) {
      OpenDesktopPicker(id, selected_source.desktop_type);
      return;
    }
    auto* frame = selected_source.document.AsRenderFrameHostIfValid();
    auto* page = frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
    if (!page || page->IsBeingDestroyed() || page->GetPrimaryMainFrame() != frame ||
        !frame->IsActive() || page->GetController().GetPendingEntry() ||
        page->GetBrowserContext() != selected_.Run()->GetBrowserContext()) {
      Reject(Result::PERMISSION_DENIED);
      return;
    }
    const auto frame_id = frame->GetGlobalId();
    const content::DesktopMediaID source(
        content::DesktopMediaID::TYPE_WEB_CONTENTS,
        content::DesktopMediaID::kNullId,
        content::WebContentsMediaCaptureId(frame_id.child_id.value(),
                                          frame_id.frame_routing_id));
    auto devices = blink::mojom::StreamDevices::New();
    devices->video_device.emplace(request.video_type, source.ToString(),
                                  "Zitter tab");
    devices->video_device->display_media_info =
        media::mojom::DisplayMediaInformation::New(
            media::mojom::DisplayCaptureSurfaceType::BROWSER,
            /*logical_surface=*/true, media::mojom::CursorCaptureType::MOTION,
            /*capture_handle=*/nullptr, /*initial_zoom_level=*/100);
    if (share_audio && request.audio_type == Type::DISPLAY_AUDIO_CAPTURE) {
      auto audio_source = source.web_contents_id;
      audio_source.disable_local_echo =
          request.disable_local_echo || request.suppress_local_audio_playback;
      devices->audio_device.emplace(request.audio_type, audio_source.ToString(),
                                    "Zitter tab audio");
      devices->audio_device->display_media_info =
          devices->video_device->display_media_info.Clone();
    }
    Grant(std::move(devices), page, std::move(selected_source.label));
    return;
  }
  RequestWorkspaceCaptureAccess(
      pending_->request.audio_type == Type::DEVICE_AUDIO_CAPTURE,
      pending_->request.video_type == Type::DEVICE_VIDEO_CAPTURE,
      base::BindRepeating([](base::WeakPtr<WorkspaceMediaAccess> self, int id) {
        return self && id == self->request_id_ && self->IsCurrentRequest();
      }, weak_factory_.GetWeakPtr(), id),
      base::BindOnce(&WorkspaceMediaAccess::OnPlatformAccess,
                     weak_factory_.GetWeakPtr(), id));
}

void WorkspaceMediaAccess::OpenDesktopPicker(int id, DesktopID::Type type) {
  if (desktop_probe_ == DesktopProbe::kSynthetic) {
    OnDesktopSelected(id, DesktopID(type, DesktopID::kFakeId));
    return;
  }
  auto weak = weak_factory_.GetWeakPtr();
  content::GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(
          &content::desktop_capture::OpenNativeScreenCapturePicker, type,
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              [](base::WeakPtr<WorkspaceMediaAccess> self, int id,
                 DesktopID::Type type, DesktopID::Id session) {
                DesktopID source(type, session);
                source.id_type = DesktopID::IdType::kNativePickerSession;
                if (!self || id != self->request_id_ || !self->pending_) {
                  CloseDesktopPicker(source);
                  return;
                }
                self->pending_->desktop_picker = source;
                if (self->desktop_probe_ == DesktopProbe::kNativeCancel) {
                  std::puts("Zitter Content native desktop picker created");
                  self->OnDesktopDismissed(id, /*error=*/false);
                }
              }, weak, id, type)),
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              [](base::WeakPtr<WorkspaceMediaAccess> self, int id,
                 DesktopID::Type type, webrtc::DesktopCapturer::Source selection) {
                if (!self)
                  return;
                DesktopID source(type, selection.id);
                source.id_type = DesktopID::IdType::kNativePickerSession;
                self->OnDesktopSelected(id, source);
              }, weak, id, type)),
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              &WorkspaceMediaAccess::OnDesktopDismissed, weak, id, false)),
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              &WorkspaceMediaAccess::OnDesktopDismissed, weak, id, true))));
}

void WorkspaceMediaAccess::OnDesktopSelected(int id, DesktopID source) {
  if (id != request_id_ || !pending_)
    return;
  if (!IsCurrentRequest() ||
      (desktop_probe_ != DesktopProbe::kSynthetic &&
       source != pending_->desktop_picker)) {
    Reject(Result::PERMISSION_DENIED);
    return;
  }
  auto devices = blink::mojom::StreamDevices::New();
  devices->video_device.emplace(pending_->request.video_type, source.ToString(),
                                "Zitter desktop capture");
  devices->video_device->display_media_info =
      media::mojom::DisplayMediaInformation::New(
          source.type == DesktopID::TYPE_SCREEN
              ? media::mojom::DisplayCaptureSurfaceType::MONITOR
              : media::mojom::DisplayCaptureSurfaceType::WINDOW,
          /*logical_surface=*/true, media::mojom::CursorCaptureType::ALWAYS,
          /*capture_handle=*/nullptr, /*initial_zoom_level=*/100);
  Grant(std::move(devices), nullptr, DesktopLabel(source.type), source);
}

void WorkspaceMediaAccess::OnDesktopDismissed(int id, bool error) {
  if (id == request_id_ && pending_) {
    Reject(error ? Result::PERMISSION_DENIED_BY_SYSTEM
                 : Result::PERMISSION_DENIED);
  }
}

void WorkspaceMediaAccess::OnPlatformAccess(int id, bool allowed) {
  if (id != request_id_ || !pending_)
    return;
  if (!allowed || !IsCurrentRequest()) {
    Reject(allowed ? Result::PERMISSION_DENIED : Result::PERMISSION_DENIED_BY_SYSTEM);
    return;
  }
  auto& request = pending_->request;
  auto* capture_devices = content::MediaCaptureDevices::GetInstance();
  auto devices = blink::mojom::StreamDevices::New();
  const bool audio = request.audio_type == Type::DEVICE_AUDIO_CAPTURE;
  const bool video = request.video_type == Type::DEVICE_VIDEO_CAPTURE;
  if (audio)
    devices->audio_device = SelectDevice(capture_devices->GetAudioCaptureDevices(),
                                         request.requested_audio_device_ids);
  if (video)
    devices->video_device = SelectDevice(capture_devices->GetVideoCaptureDevices(),
                                         request.requested_video_device_ids);
  if ((audio && !devices->audio_device) || (video && !devices->video_device)) {
    Reject(Result::NO_HARDWARE);
    return;
  }
  Grant(std::move(devices));
}

void WorkspaceMediaAccess::Grant(blink::mojom::StreamDevicesPtr devices,
                                 content::WebContents* source,
                                 std::u16string source_label,
                                 DesktopID desktop_source) {
  const auto& request = pending_->request;
  auto stream = std::make_unique<Stream>(*parent_, pending_->document,
                                         request.url_origin,
                                         devices->audio_device
                                             ? devices->audio_device->type
                                             : Type::NO_SERVICE,
                                         request.video_type, source,
                                         std::move(source_label), desktop_source);
  stream_ = stream->GetWeakPtr();
  blink::mojom::StreamDevicesSet granted;
  granted.stream_devices.push_back(std::move(devices));
  auto pending = std::move(pending_);
  std::move(pending->reply).Run(granted, Result::OK, std::move(stream));
}

bool WorkspaceMediaAccess::Check(content::RenderFrameHost* frame,
                                  const url::Origin& origin, Type type) const {
  return frame && stream_ && IsEligible(frame, selected_.Run(), origin,
      type == Type::DEVICE_AUDIO_CAPTURE, type == Type::DEVICE_VIDEO_CAPTURE) &&
      stream_->Matches(frame, origin, type);
}

void WorkspaceMediaAccess::Reject(Result result) {
  auto pending = std::move(pending_);
  CloseDesktopPicker(pending->desktop_picker);
  std::move(pending->reply).Run({}, result, nullptr);
}

void WorkspaceMediaAccess::Stop() {
  if (stream_)
    stream_->Stop();
  if (pending_)
    Reject(Result::PERMISSION_DENIED);
}

bool WorkspaceMediaAccess::IsCapturing(content::WebContents* page) const {
  return stream_ && stream_->CapturesOrBelongsTo(page);
}

bool WorkspaceMediaAccess::StopFromIndicatorForTesting() {
  return stream_ && stream_->StopFromIndicatorForTesting();
}

}  // namespace zitter::content_embedder
