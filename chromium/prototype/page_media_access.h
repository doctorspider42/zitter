#ifndef ZITTER_CHROMIUM_PROTOTYPE_PAGE_MEDIA_ACCESS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PAGE_MEDIA_ACCESS_H_

#include <memory>
#include <map>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/media/webrtc/media_stream_capture_indicator.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "chrome/browser/media/webrtc/desktop_media_list.h"

namespace views { class Widget; }
class DesktopMediaPickerFactory;
class OneTimePermissionsTrackerHelper;

namespace zitter::prototype {

// Per-page permission presentation and capture controls. Policy decisions,
// device selection, OS consent and desktop picking stay in Chromium.
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
  views::Widget* capture_prompt() const { return capture_prompt_.get(); }
  views::Widget* permission_prompt() const { return permission_prompt_.get(); }

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
  base::WeakPtr<views::Widget> capture_prompt_;
  base::WeakPtr<views::Widget> permission_prompt_;
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
