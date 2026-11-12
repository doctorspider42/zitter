// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PROFILE_DOWNLOAD_HISTORY_H_
#define ZITTER_CONTENT_PROFILE_DOWNLOAD_HISTORY_H_

#include <map>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "components/download/content/public/all_download_item_notifier.h"
#include "components/history/core/browser/download_row.h"

namespace history {
class HistoryService;
}

namespace zitter::content_embedder {

// Connects completed downloads and resumable checkpoints to the profile History DB.
class ProfileDownloadHistory final
    : private download::AllDownloadItemNotifier::Observer {
 public:
  using RecoveryPolicy =
      base::RepeatingCallback<bool(const history::DownloadRow&)>;
  using UserApproval = base::RepeatingCallback<bool(const download::DownloadItem&)>;
  using RestoreApproval = base::RepeatingCallback<void(
      download::DownloadItem&, const history::DownloadRow&)>;
  static constexpr char kUserCheckpoint[] = "zitter-user-download-checkpoint-v1";
  ProfileDownloadHistory(content::DownloadManager& manager,
                         history::HistoryService& history,
                         RecoveryPolicy recovery_policy,
                         UserApproval user_approval,
                         RestoreApproval restore_approval);
  ~ProfileDownloadHistory() override;
  bool IsImportedEntry(const download::DownloadItem& item) const;

 private:
  void OnQueryComplete(std::vector<history::DownloadRow> rows);
  void OnRowsValidated(std::vector<history::DownloadRow> rows);
  void Restore(std::vector<history::DownloadRow> rows);
  void OnManagerGoingDown(content::DownloadManager* manager) override;
  bool CanRestoreInterrupted(const history::DownloadRow& row) const;
  bool CanCheckpointActive(history::DownloadRow row) const;
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadOpened(content::DownloadManager* manager,
                        download::DownloadItem* item) override;
  void OnDownloadRemoved(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnInserted(history::DownloadId id, bool success);

  const raw_ref<history::HistoryService> history_;
  const RecoveryPolicy recovery_policy_;
  const UserApproval user_approval_;
  const RestoreApproval restore_approval_;
  download::AllDownloadItemNotifier notifier_;
  std::map<history::DownloadId, history::DownloadRow> records_;
  base::WeakPtrFactory<ProfileDownloadHistory> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PROFILE_DOWNLOAD_HISTORY_H_
