// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_DOWNLOAD_PROBE_H_
#define ZITTER_CONTENT_DOWNLOAD_PROBE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents_observer.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}

namespace ui {
class ScopedSetSelectFileDialogFactory;
}

namespace zitter::content_embedder {

class ProfileContext;

inline constexpr char kDownloadProbe[] = "zitter-content-download-probe";
inline constexpr char kDownloadTransportProbe[] =
    "zitter-content-download-transport-probe";
inline constexpr char kProbeRoot[] = "zitter-content-probe-root";

class DownloadProbe final : private content::DownloadManager::Observer,
                            private download::DownloadItem::Observer,
                            private content::WebContentsObserver {
 public:
  // The caller validates that root is a disposable platform fixture.
  static std::unique_ptr<DownloadProbe> Create(const base::FilePath& root);
  ~DownloadProbe() override;
  bool persistent() const { return !checkpoint_phase_.empty(); }
  std::optional<bool> native_user_save() const;

  void Prepare(ProfileContext& profile);
  void Start(content::WebContents& contents, base::OnceClosure done);

 private:
  enum class ExpectedResult {
    kComplete, kBlocked, kPublishCollision, kResumeBlocked, kInterrupted,
    kFileThreat, kFileUnavailable, kPublicationChanged
  };
  DownloadProbe(GURL url, GURL target_url, base::FilePath directory,
                ExpectedResult expected_result, std::string save_action,
                std::vector<uint8_t> certificate, GURL reputation_endpoint,
                base::FilePath file_catalog, std::string publication_change,
                std::string checkpoint_phase);

  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadDropped(content::DownloadManager* manager) override;
  void OnManagerInitialized() override;
  void OnDownloadUpdated(download::DownloadItem* item) override;
  void OnDownloadDestroyed(download::DownloadItem* item) override;
  void DidFinishLoad(content::RenderFrameHost* frame, const GURL& url) override;
  void Finish(bool success);

  const GURL url_;
  const GURL target_url_;
  const base::FilePath directory_;
  const ExpectedResult expected_result_;
  const std::string save_action_;
  const std::vector<uint8_t> certificate_;
  const GURL reputation_endpoint_;
  const base::FilePath file_catalog_;
  const std::string publication_change_;
  const std::string checkpoint_phase_;
  std::unique_ptr<ui::ScopedSetSelectFileDialogFactory> dialog_factory_;
  int picker_checks_ = 0;
  bool picker_checked_ = false;
  bool delete_started_ = false;
  base::OnceClosure done_;
  base::ScopedObservation<content::DownloadManager,
                          content::DownloadManager::Observer>
      manager_{this};
  base::ScopedObservation<download::DownloadItem, download::DownloadItem::Observer>
      item_{this};
  base::WeakPtrFactory<DownloadProbe> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_DOWNLOAD_PROBE_H_
