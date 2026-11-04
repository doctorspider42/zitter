#ifndef ZITTER_CHROMIUM_PROTOTYPE_PAGE_MEDIA_ACCESS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PAGE_MEDIA_ACCESS_H_

#include <memory>
#include <map>
#include <string>
#include <string_view>
#include "ui/gfx/geometry/rect.h"
#include "base/functional/callback.h"

#include "base/memory/weak_ptr.h"
#include "chrome/browser/media/webrtc/media_stream_capture_indicator.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "chrome/browser/media/webrtc/desktop_media_list.h"

namespace views { class Widget; }
class DesktopMediaPickerFactory;
class OneTimePermissionsTrackerHelper;

namespace zitter::prototype {
class MediaSceneDialog;

// Per-page permission presentation and capture controls. Policy decisions,
// device selection, OS consent and desktop picking stay in Chromium.
// A pending permission request as the address pill shows it.
struct PermissionOffer {
  bool pending = false;
  bool folded = false;  // Out of the way: the pill keeps a muted icon.
  // 0 other, 1 notifications, 2 location, 3 camera, 4 microphone, 5 both,
  // 6 local network, 7 clipboard, 8 several downloads, 9 MIDI.
  int kind = 0;
  std::u16string site;
};

class PageMediaAccess : public content::WebContentsObserver,
                        public content::WebContentsUserData<PageMediaAccess>,
                        public MediaStreamCaptureIndicator::Observer {
 public:
  static void Attach(content::WebContents* contents);
  static void Request(content::WebContents* contents,
                      const content::MediaStreamRequest& request,
                      content::MediaResponseCallback callback);
  static bool Check(content::RenderFrameHost* frame,
                    const url::Origin& origin,
                    blink::mojom::MediaStreamType type);
  static void Stop(content::WebContents* contents);
  static bool IsCapturing(content::WebContents* contents);
  static std::unique_ptr<DesktopMediaList> CreateTabSourceList(
      content::WebContents* requester, DesktopMediaList::WebContentsFilter filter);
  static void SetDesktopPickerFactoryForTesting(DesktopMediaPickerFactory* factory);
  ~PageMediaAccess() override;
  bool permission_scene_contains_for_testing(std::string_view text) const;
  bool capture_scene_contains_for_testing(std::string_view text) const;
  views::Widget* capture_prompt() const { return capture_prompt_.get(); }
  views::Widget* permission_prompt() const { return permission_prompt_.get(); }
  // Design: Prośba o dostęp — the card sits under the address pill and, for
  // requests that do not hold the page up, folds into the pill after 8 s.
  void SetPromptAnchor(base::RepeatingCallback<gfx::Rect()> anchor) { prompt_anchor_ = std::move(anchor); }
  void SetPromptCallback(base::RepeatingCallback<void(const PermissionOffer&)> report) { prompt_report_ = std::move(report); }
  gfx::Rect PromptAnchor() const { return prompt_anchor_ ? prompt_anchor_.Run() : gfx::Rect(); }
  void ReportPrompt(const PermissionOffer& offer);
  void ReopenPrompt() { if (prompt_reopen_) prompt_reopen_.Run(); }
  const PermissionOffer& prompt_offer() const { return prompt_offer_; }
  void FoldPromptForTesting() { if (prompt_fold_) prompt_fold_.Run(); }
  // 0 allow this time, 1 allow, 2 deny.
  void DecidePromptForTesting(int decision) { if (prompt_decide_) prompt_decide_.Run(decision); }
  void SetPromptHooks(base::RepeatingClosure reopen, base::RepeatingClosure fold, base::RepeatingCallback<void(int)> decide) {
    prompt_reopen_ = std::move(reopen); prompt_fold_ = std::move(fold); prompt_decide_ = std::move(decide);
  }

 private:
  friend class content::WebContentsUserData<PageMediaAccess>;
  explicit PageMediaAccess(content::WebContents* contents);
  void QueueCaptureUpdate(content::WebContents* contents);
  void UpdateCaptureIndicator();
  void CloseCaptureIndicator();
  void TrackCapture(size_t id, std::string origin, bool started);
  void CancelPendingDisplayRequests(content::RenderFrameHost* frame);
  void RenderFrameDeleted(content::RenderFrameHost* frame) override;
  void RenderFrameHostChanged(content::RenderFrameHost* old_frame,
                             content::RenderFrameHost* new_frame) override;
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void WebContentsDestroyed() override;
  void OnIsCapturingVideoChanged(content::WebContents*, bool) override;
  void OnIsCapturingAudioChanged(content::WebContents*, bool) override;
  void OnIsCapturingTabChanged(content::WebContents*, bool) override;
  void OnIsCapturingWindowChanged(content::WebContents*, bool) override;
  void OnIsCapturingDisplayChanged(content::WebContents*, bool) override;

  bool observing_capture_ = true;
  bool update_pending_ = false;
  std::shared_ptr<bool> capture_dialog_active_;
  std::unique_ptr<MediaSceneDialog> capture_scene_;
  base::RepeatingCallback<bool(std::string_view)> permission_scene_query_;
  base::WeakPtr<views::Widget> capture_prompt_;
  base::WeakPtr<views::Widget> permission_prompt_;
  base::RepeatingCallback<gfx::Rect()> prompt_anchor_;
  base::RepeatingCallback<void(const PermissionOffer&)> prompt_report_;
  base::RepeatingClosure prompt_reopen_, prompt_fold_;
  base::RepeatingCallback<void(int)> prompt_decide_;
  PermissionOffer prompt_offer_;
  size_t next_capture_id_ = 0;
  std::map<size_t, std::string> capture_origins_;
  struct DisplayRequest {
    content::MediaStreamRequest request;
    bool approved = false;
  };
  std::map<size_t, DisplayRequest> display_requests_;
  std::unique_ptr<OneTimePermissionsTrackerHelper> one_time_permissions_;
  base::WeakPtrFactory<PageMediaAccess> weak_factory_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace zitter::prototype
#endif
