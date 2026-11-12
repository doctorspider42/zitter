// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "profile_download_history.h"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "components/download/public/common/download_utils.h"
#include "components/history/content/browser/download_conversions.h"
#include "components/history/core/browser/history_service.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/storage_partition_config.h"
#include "download_file_actions.h"

namespace zitter::content_embedder {
namespace {

bool IsAbsoluteFile(const base::FilePath& path) {
  return path.IsAbsolute() && !path.IsNetwork() && !path.ReferencesParent() &&
         !path.EndsWithSeparator() && path.DirName() != path;
}

bool HasDownloadIdentity(const history::DownloadRow& row) {
  return !row.transient &&
         row.id != history::kInvalidDownloadId &&
         base::Uuid::ParseCaseInsensitive(row.guid).is_valid() &&
         !row.url_chain.empty() && row.url_chain.back().is_valid() &&
         IsAbsoluteFile(row.target_path);
}

bool IsImportedHistory(const history::DownloadRow& row) {
  return row.embedder_download_data == "zitter-imported-download-v1";
}

bool CanRestoreImportedFailure(const history::DownloadRow& row) {
  return IsImportedHistory(row) && HasDownloadIdentity(row) &&
         (row.state == history::DownloadState::INTERRUPTED ||
          row.state == history::DownloadState::CANCELLED) &&
         row.current_path.empty() && row.hash.empty() &&
         row.download_slice_info.empty() && row.http_method.empty() &&
         row.etag.empty() && row.last_modified.empty();
}

bool CanRestoreCompleted(const history::DownloadRow& row) {
  return row.state == history::DownloadState::COMPLETE &&
         HasDownloadIdentity(row) &&
         (row.current_path.empty() || row.current_path == row.target_path);
}

std::vector<history::DownloadRow> FilterMissingPartialFiles(
    std::vector<history::DownloadRow> rows) {
  std::erase_if(rows, [](const auto& row) {
    if (row.state != history::DownloadState::INTERRUPTED ||
        IsImportedHistory(row)) {
      return false;
    }
    return !VerifyDownloadCheckpoint(row.current_path, row.received_bytes, row.hash);
  });
  return rows;
}

history::DownloadRow MakeDownloadRow(const download::DownloadItem& item) {
  history::DownloadRow row;
  row.id = history::ToHistoryDownloadId(item.GetId());
  row.guid = item.GetGuid();
  row.current_path = item.GetFullPath();
  row.target_path = item.GetTargetFilePath();
  row.url_chain = item.GetUrlChain();
  download::TruncateDataUrlAtTheEndIfNeeded(&row.url_chain);
  row.referrer_url = item.GetReferrerUrl();
  row.tab_url = item.GetTabUrl();
  row.tab_referrer_url = item.GetTabReferrerUrl();
  row.embedder_download_data = item.GetSerializedEmbedderDownloadData();
  row.mime_type = item.GetMimeType();
  row.original_mime_type = item.GetOriginalMimeType();
  row.start_time = item.GetStartTime();
  row.end_time = item.GetEndTime();
  row.etag = item.GetETag();
  row.last_modified = item.GetLastModifiedTime();
  row.received_bytes = item.GetReceivedBytes();
  row.total_bytes = item.GetTotalBytes();
  row.state = history::ToHistoryDownloadState(item.GetState());
  row.hash = item.GetHash();
  if (row.state == history::DownloadState::IN_PROGRESS ||
      row.state == history::DownloadState::INTERRUPTED) {
    row.http_method = "GET";
  }
  row.danger_type = history::ToHistoryDownloadDangerType(item.GetDangerType());
  row.interrupt_reason =
      history::ToHistoryDownloadInterruptReason(item.GetLastReason());
  row.opened = item.GetOpened();
  row.last_access_time = item.GetLastAccessTime();
  row.download_slice_info = history::GetHistoryDownloadSliceInfos(item);
  return row;
}

}  // namespace

ProfileDownloadHistory::ProfileDownloadHistory(
    content::DownloadManager& manager,
    history::HistoryService& history,
    RecoveryPolicy recovery_policy,
    UserApproval user_approval,
    RestoreApproval restore_approval)
    : history_(history),
      recovery_policy_(std::move(recovery_policy)),
      user_approval_(std::move(user_approval)),
      restore_approval_(std::move(restore_approval)),
      notifier_(&manager, this) {
  history_->QueryDownloads(base::BindOnce(&ProfileDownloadHistory::OnQueryComplete,
                                          weak_factory_.GetWeakPtr()));
}

ProfileDownloadHistory::~ProfileDownloadHistory() = default;

bool ProfileDownloadHistory::IsImportedEntry(
    const download::DownloadItem& item) const {
  const auto row = records_.find(history::ToHistoryDownloadId(item.GetId()));
  return row != records_.end() && row->second.guid == item.GetGuid() &&
         IsImportedHistory(row->second);
}

bool ProfileDownloadHistory::CanRestoreInterrupted(
    const history::DownloadRow& row) const {
  const auto now = base::Time::Now();
  return row.state == history::DownloadState::INTERRUPTED &&
         !IsImportedHistory(row) && HasDownloadIdentity(row) && IsAbsoluteFile(row.current_path) &&
         !row.start_time.is_null() && row.start_time <= now &&
         now - row.start_time < download::GetExpiredDownloadDeleteTime() &&
         recovery_policy_.Run(row);
}

bool ProfileDownloadHistory::CanCheckpointActive(history::DownloadRow row) const {
  // History converts a surviving active row to this state on the next startup.
  row.state = history::DownloadState::INTERRUPTED;
  row.interrupt_reason = history::ToHistoryDownloadInterruptReason(
      download::DOWNLOAD_INTERRUPT_REASON_CRASH);
  return CanRestoreInterrupted(row);
}

void ProfileDownloadHistory::OnQueryComplete(
    std::vector<history::DownloadRow> rows) {
  std::erase_if(rows, [this](const auto& row) {
    return !CanRestoreCompleted(row) && !CanRestoreImportedFailure(row) &&
           !CanRestoreInterrupted(row);
  });
  if (std::ranges::none_of(rows, [](const auto& row) {
        return row.state == history::DownloadState::INTERRUPTED &&
               !IsImportedHistory(row);
      })) {
    OnRowsValidated(std::move(rows));
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&FilterMissingPartialFiles, std::move(rows)),
      base::BindOnce(&ProfileDownloadHistory::OnRowsValidated,
                     weak_factory_.GetWeakPtr()));
}

void ProfileDownloadHistory::OnRowsValidated(
    std::vector<history::DownloadRow> rows) {
  if (auto* manager = notifier_.GetManager()) {
    manager->OnHistoryQueryComplete(
        base::BindOnce(&ProfileDownloadHistory::Restore,
                       weak_factory_.GetWeakPtr(), std::move(rows)));
  }
}

void ProfileDownloadHistory::Restore(std::vector<history::DownloadRow> rows) {
  auto* manager = notifier_.GetManager();
  if (!manager)
    return;
  // Use this profile's default partition for admitted records. Chromium
  // restores history downloads with their automatic retry budget exhausted.
  const auto partition =
      content::StoragePartitionConfig::CreateDefault(manager->GetBrowserContext());
  for (const auto& row : rows) {
    if ((!CanRestoreCompleted(row) && !CanRestoreImportedFailure(row) &&
         !CanRestoreInterrupted(row)) ||
        manager->GetDownloadByGuid(row.guid) ||
        !records_.emplace(row.id, row).second) {
      continue;
    }
    auto url_chain = row.url_chain;
    download::TruncateDataUrlAtTheEndIfNeeded(&url_chain);
    const bool interrupted = row.state == history::DownloadState::INTERRUPTED;
    const bool hide_transfer_context = interrupted || CanRestoreImportedFailure(row);
    auto reason = download::DOWNLOAD_INTERRUPT_REASON_NONE;
    if (IsImportedHistory(row) && interrupted) {
      // Preserve the original failure in SQLite. The displayed item cannot
      // resume, even if its original error would have allowed a retry.
      reason = download::DOWNLOAD_INTERRUPT_REASON_USER_SHUTDOWN;
    } else if (interrupted) {
      reason = history::ToContentDownloadInterruptReason(row.interrupt_reason);
    }
    auto* item = manager->CreateDownloadItem(
        row.guid, history::ToContentDownloadId(row.id), row.current_path,
        row.target_path, url_chain, hide_transfer_context ? GURL() : row.referrer_url,
        partition, hide_transfer_context ? GURL() : row.tab_url,
        hide_transfer_context ? GURL() : row.tab_referrer_url, std::nullopt, row.mime_type,
        row.original_mime_type, row.start_time, row.end_time, row.etag,
        row.last_modified, row.received_bytes, row.total_bytes, row.hash,
        history::ToContentDownloadState(row.state),
        history::ToContentDownloadDangerType(row.danger_type),
        reason,
        row.opened,
        row.last_access_time, /*transient=*/false,
        history::ToContentReceivedSlices(row.download_slice_info), {});
    if (!item)
      records_.erase(row.id);
    else if (interrupted && row.embedder_download_data == kUserCheckpoint)
      restore_approval_.Run(*item, row);
  }
  manager->PostInitialization(
      content::DownloadManager::DOWNLOAD_INITIALIZATION_DEPENDENCY_HISTORY_DB);
}

void ProfileDownloadHistory::OnManagerGoingDown(content::DownloadManager* manager) {
  // Shutdown cancels active items after detaching observers. Remove any
  // checkpoint for those transfers, including one whose insert is pending.
  std::set<history::DownloadId> obsolete;
  for (const auto& [id, row] : records_) {
    const auto* item = manager->GetDownloadByGuid(row.guid);
    if (row.state == history::DownloadState::IN_PROGRESS ||
        (row.state == history::DownloadState::INTERRUPTED && item &&
         item->GetState() == download::DownloadItem::IN_PROGRESS)) {
      obsolete.insert(id);
    }
  }
  for (auto id : obsolete)
    records_.erase(id);
  if (!obsolete.empty())
    history_->RemoveDownloads(obsolete);
}

void ProfileDownloadHistory::OnDownloadCreated(content::DownloadManager* manager,
                                               download::DownloadItem* item) {
  if (!records_.contains(history::ToHistoryDownloadId(item->GetId())))
    OnDownloadUpdated(manager, item);
}

void ProfileDownloadHistory::OnDownloadUpdated(content::DownloadManager*,
                                               download::DownloadItem* item) {
  using Item = download::DownloadItem;
  const auto state = item->GetState();
  if (state == Item::IN_PROGRESS && item->GetHash().empty())
    return;
  const auto id = history::ToHistoryDownloadId(item->GetId());
  auto found = records_.find(id);
  if (found != records_.end() && IsImportedHistory(found->second))
    return;
  auto row = MakeDownloadRow(*item);
  if ((state == Item::IN_PROGRESS || state == Item::INTERRUPTED) &&
      user_approval_.Run(*item)) {
    row.embedder_download_data = kUserCheckpoint;
  }
  const bool allowed =
      !item->IsTransient() && !item->IsTemporary() && !item->IsDangerous() &&
      !item->IsInsecure() &&
      (CanRestoreCompleted(row) ||
       (state == Item::IN_PROGRESS && CanCheckpointActive(row)) ||
       (item->CanResume() && CanRestoreInterrupted(row)));
  if (!allowed) {
    if (state != Item::IN_PROGRESS && found != records_.end() &&
        found->second.state != history::DownloadState::COMPLETE) {
      records_.erase(found);
      history_->RemoveDownloads({id});
    }
    return;
  }

  if (found != records_.end()) {
    auto& stored = found->second;
    if (stored.state == history::DownloadState::COMPLETE) {
      // Keep legacy extension and site metadata when updating mutable facts.
      if (stored.current_path == item->GetFullPath() &&
          stored.opened == item->GetOpened() &&
          stored.last_access_time == item->GetLastAccessTime()) {
        return;
      }
      stored.current_path = item->GetFullPath();
      stored.opened = item->GetOpened();
      stored.last_access_time = item->GetLastAccessTime();
    } else {
      if (stored == row)
        return;
      stored = std::move(row);
    }
    history_->UpdateDownload(stored, /*should_commit_immediately=*/true);
    return;
  }

  const auto& stored = records_.emplace(id, std::move(row)).first->second;
  history_->CreateDownload(
      stored, base::BindOnce(&ProfileDownloadHistory::OnInserted,
                             weak_factory_.GetWeakPtr(), id));
}

void ProfileDownloadHistory::OnDownloadOpened(content::DownloadManager* manager,
                                              download::DownloadItem* item) {
  OnDownloadUpdated(manager, item);
}

void ProfileDownloadHistory::OnDownloadRemoved(content::DownloadManager*,
                                               download::DownloadItem* item) {
  const auto id = history::ToHistoryDownloadId(item->GetId());
  // History operations use the same sequenced backend, including a pending
  // insert. Posting the removal here also handles closing before its reply.
  if (records_.erase(id))
    history_->RemoveDownloads({id});
}

void ProfileDownloadHistory::OnInserted(history::DownloadId id, bool success) {
  auto found = records_.find(id);
  if (found == records_.end())
    return;
  if (!success) {
    records_.erase(found);
    LOG(ERROR) << "Could not persist a Content download";
    return;
  }
  if (found->second.state != history::DownloadState::COMPLETE) {
    // Desktop History batches inserts. Flush a recoverable checkpoint before
    // it is needed by another process.
    history_->UpdateDownload(found->second, /*should_commit_immediately=*/true);
  }
}

}  // namespace zitter::content_embedder
