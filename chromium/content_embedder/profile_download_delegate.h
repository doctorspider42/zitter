// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PROFILE_DOWNLOAD_DELEGATE_H_
#define ZITTER_CONTENT_PROFILE_DOWNLOAD_DELEGATE_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/download_manager_delegate.h"
#include "download_file_actions.h"
#include "url/gurl.h"

namespace content {
class DownloadManager;
class RenderFrameHost;
}

namespace history {
class HistoryService;
struct DownloadRow;
}

namespace zitter::content_embedder {

class DownloadSavePicker;
class DownloadUrlReputation;
class ProfileDownloadHistory;
enum class DownloadFileVerdict;
enum class DownloadUrlVerdict;

class ProfileDownloadDelegate final : public content::DownloadManagerDelegate {
 public:
  ProfileDownloadDelegate(GURL probe_url, base::FilePath probe_directory,
                          bool prompt_for_probe = false,
                          bool allow_http_fixture = false);
  ~ProfileDownloadDelegate() override;
  void AttachManager(content::DownloadManager& manager,
                     history::HistoryService* history);
  void EnableUserDownloads(base::FilePath suggested_directory);
  bool CanSaveLink(content::RenderFrameHost& frame, const GURL& url) const;
  bool SaveLink(content::RenderFrameHost& frame, const GURL& url);

  void SetFileReputationCatalog(
      base::FilePath catalog,
      base::OnceCallback<void(const base::FilePath&)> after_allow_for_testing = {});
  void SetUrlReputation(std::unique_ptr<DownloadUrlReputation> checker);
  void GetNextId(content::DownloadIdCallback callback) override;
  bool SupportsHistoryLoading() override;
  void Shutdown() override;
  void GetSaveDir(content::BrowserContext* browser_context,
                  base::FilePath* website_save_dir,
                  base::FilePath* download_save_dir) override;
  bool DetermineDownloadTarget(download::DownloadItem* item,
                               download::DownloadTargetCallback* callback)
      override;
  bool ShouldCompleteDownload(download::DownloadItem* item,
                              base::OnceClosure complete_callback) override;
  void CheckDownloadAllowed(
      const content::WebContents::Getter& contents,
      const GURL& url,
      const std::string& method,
      std::optional<url::Origin> request_initiator,
      bool from_cross_origin_redirect,
      bool content_initiated,
      const std::string& mime_type,
      std::optional<ui::PageTransition> transition,
      content::CheckDownloadAllowedCallback callback) override;
  download::QuarantineConnectionCallback GetQuarantineConnectionCallback()
      override;
  std::string ApplicationClientIdForFileScanning() override;
  void OpenDownload(download::DownloadItem* item) override;
  void ShowDownloadInShell(download::DownloadItem* item) override;
  bool IsImportedHistoryEntry(const download::DownloadItem& item) const;
  bool CanDeleteDownloadFile(const download::DownloadItem& item) const;
  void DeleteDownloadFile(const download::DownloadItem& item,
                          base::OnceCallback<void(bool)> done);

  // Captures the result before any OS action, including a rejected path.
  using FileActionForTesting =
      base::RepeatingCallback<void(DownloadFileAction, const base::FilePath&)>;
  void SetFileActionForTesting(FileActionForTesting callback);

 private:
  void OnUrlReputationChecked(std::string guid,
                              base::FilePath name,
                              download::DownloadTargetCallback callback,
                              DownloadUrlVerdict verdict);
  void ReserveDownloadTarget(download::DownloadItem* item,
                             const base::FilePath& name,
                             download::DownloadTargetCallback callback);
  bool CanRecoverInterruptedDownload(const history::DownloadRow& row) const;
  bool HasUserDownloadCheckpoint(const download::DownloadItem& item) const;
  void RestoreUserDownloadCheckpoint(download::DownloadItem& item,
                                     const history::DownloadRow& row);
  void OnFileReputationChecked(std::string guid,
                               base::OnceClosure complete_callback,
                               DownloadFileVerdict verdict);
  void CancelUnpublishableDownload(const std::string& guid);
  bool IsUserDownload(const download::DownloadItem& item) const;
  void OnUserUrlReputationChecked(std::string guid,
                                  int64_t received_bytes,
                                  download::DownloadTargetCallback callback,
                                  DownloadUrlVerdict verdict);
  void DetermineUserDownloadTarget(download::DownloadItem* item,
                                   int64_t received_bytes,
                                   download::DownloadTargetCallback callback);
  void OnUserDownloadSelected(std::string guid, int64_t size,
                              download::DownloadTargetCallback callback,
                              download::DownloadTargetInfo selection);
  void ResumeUserDownload(download::DownloadItem& item,
                          int64_t received_bytes,
                          download::DownloadTargetCallback callback);
  void ValidateFileAction(download::DownloadItem& item, DownloadFileAction action);
  bool CanDispatchFileAction(const download::DownloadItem& item,
                             DownloadFileAction action) const;
  void OnFileActionValidated(DownloadFileAction action,
                             std::string guid,
                             int64_t size,
                             std::string hash,
                             base::FilePath path);

  const GURL probe_url_;
  const base::FilePath probe_directory_;
  const bool prompt_for_probe_;
  const bool allow_http_fixture_;
  base::FilePath user_download_directory_;
  scoped_refptr<DownloadSavePicker> save_picker_;
  raw_ptr<content::DownloadManager> manager_ = nullptr;
  raw_ptr<history::HistoryService> history_service_ = nullptr;
  std::unique_ptr<ProfileDownloadHistory> history_;
  base::FilePath file_reputation_catalog_;
  base::OnceCallback<void(const base::FilePath&)> after_file_allow_for_testing_;
  std::unique_ptr<DownloadUrlReputation> url_reputation_;
  FileActionForTesting file_action_for_testing_;
  base::WeakPtrFactory<ProfileDownloadDelegate> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PROFILE_DOWNLOAD_DELEGATE_H_
