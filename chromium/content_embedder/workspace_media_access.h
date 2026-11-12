// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_WORKSPACE_MEDIA_ACCESS_H_
#define ZITTER_CONTENT_WORKSPACE_MEDIA_ACCESS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/media_stream_request.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom-forward.h"

namespace content {
class RenderFrameHost;
class WebContents;
}

namespace views {
class Widget;
}

namespace zitter::content_embedder {

// macOS checks the application's system permission. Windows enforces
// device access when the capture service opens the selected device.
void RequestWorkspaceCaptureAccess(bool audio,
                                   bool video,
                                   base::RepeatingCallback<bool()> still_current,
                                   base::OnceCallback<void(bool)> done);

#if BUILDFLAG(IS_MAC)
void ConfigureWorkspaceDesktopPicker();
bool CanUseWorkspaceDesktopPicker();
#endif

class WorkspaceMediaAccess {
 public:
  using SelectedPage = base::RepeatingCallback<content::WebContents*()>;
  using Sources = base::RepeatingCallback<std::vector<content::WebContents*>()>;
  using Prompt = base::RepeatingCallback<void(
      std::u16string, std::vector<std::u16string>, size_t, std::u16string,
      base::OnceCallback<void(std::optional<size_t>, bool)>)>;

  WorkspaceMediaAccess(views::Widget& parent, SelectedPage selected,
                       Sources sources, Prompt prompt);
  ~WorkspaceMediaAccess();
  base::WeakPtr<WorkspaceMediaAccess> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  void Request(content::WebContents* page,
               const content::MediaStreamRequest& request,
               content::MediaResponseCallback reply);
  bool Check(content::RenderFrameHost* frame,
             const url::Origin& origin,
             blink::mojom::MediaStreamType type) const;
  enum class DesktopProbe { kNone, kTabsOnly, kSynthetic, kNativeCancel };
  void SetDesktopProbe(DesktopProbe probe) { desktop_probe_ = probe; }
  void Stop();
  bool IsCapturing(content::WebContents* page) const;
  bool StopFromIndicatorForTesting();

 private:
  class Stream;
  struct Pending;

  void OnConsent(int id, std::optional<size_t> selection, bool share_audio);
  void OnPlatformAccess(int id, bool allowed);
  void OpenDesktopPicker(int id, content::DesktopMediaID::Type type);
  void OnDesktopSelected(int id, content::DesktopMediaID source);
  void OnDesktopDismissed(int id, bool error);
  void Grant(blink::mojom::StreamDevicesPtr devices,
             content::WebContents* source = nullptr,
             std::u16string source_label = {},
             content::DesktopMediaID desktop_source = {});
  bool IsCurrentRequest() const;
  void Reject(blink::mojom::MediaStreamRequestResult result);

  const raw_ref<views::Widget> parent_;
  SelectedPage selected_;
  Sources sources_;
  Prompt prompt_;
  std::unique_ptr<Pending> pending_;
  base::WeakPtr<Stream> stream_;
  int request_id_ = 0;
  DesktopProbe desktop_probe_ = DesktopProbe::kNone;
  base::WeakPtrFactory<WorkspaceMediaAccess> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WORKSPACE_MEDIA_ACCESS_H_
