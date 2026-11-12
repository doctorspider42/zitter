// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "profile_download_delegate.h"
#include "download_file_reputation.h"
#include "download_save_picker.h"
#include "download_url_reputation.h"
#include "profile_download_history.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/scoped_observation.h"
#include "base/supports_user_data.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "components/download/public/common/download_interrupt_reasons.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/download_path_reservation_tracker.h"
#include "components/download/public/common/download_target_info.h"
#include "components/download/public/common/download_url_parameters.h"
#include "components/download/public/common/download_utils.h"
#include "components/download/public/common/resume_mode.h"
#include "components/history/content/browser/download_conversions.h"
#include "components/history/core/browser/history_service.h"
#include "components/safe_browsing/content/common/file_type_policies.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "crypto/hash.h"
#include "net/base/filename_util.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/http/http_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/origin.h"
#include "url/url_constants.h"

#if BUILDFLAG(IS_MAC)
#include "components/services/quarantine/quarantine_impl.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#elif BUILDFLAG(IS_WIN)
#include "components/services/quarantine/public/mojom/quarantine.mojom.h"
#include "content/public/browser/service_process_host.h"
#endif

namespace zitter::content_embedder {
namespace {

constexpr int64_t kMaxProbeDownloadBytes = 64 * 1024;
constexpr int64_t kMaxUserDownloadBytes = 16 * 1024 * 1024;

const void* const kFileReputationKey = &kFileReputationKey;
const void* const kApprovedDownloadKey = &kApprovedDownloadKey;

struct ApprovedDownload final : base::SupportsUserData::Data,
                                private download::DownloadItem::Observer {
  ApprovedDownload(download::DownloadItem& item,
                   const download::DownloadTargetInfo& selection, GURL url,
                   int64_t size, std::string etag)
      : target(selection.target_path), partial(selection.intermediate_path),
        url(std::move(url)), size(size), etag(std::move(etag)) {
    observation_.Observe(&item);
  }

  bool MatchesResumableDownload(const download::DownloadItem& item) const {
    return size > 0 && !etag.empty() && target == item.GetTargetFilePath() &&
           partial == item.GetFullPath() && item.GetUrlChain().size() == 1 &&
           url == item.GetURL() && size == item.GetTotalBytes() &&
           etag == item.GetETag();
  }

  const base::FilePath target;
  const base::FilePath partial;
  const GURL url;
  const int64_t size;
  const std::string etag;

 private:
  void OnDownloadUpdated(download::DownloadItem* item) override {
    if (cancel_pending_ || item->GetState() != download::DownloadItem::IN_PROGRESS ||
        item->GetReceivedBytes() <= kMaxUserDownloadBytes) {
      return;
    }
    cancel_pending_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([](base::WeakPtr<ApprovedDownload> approval) {
          auto* download = approval ? approval->observation_.GetSource() : nullptr;
          if (download && download->GetState() == download::DownloadItem::IN_PROGRESS)
            download->Cancel(true);
        }, weak_factory_.GetWeakPtr()));
  }

  void OnDownloadDestroyed(download::DownloadItem*) override {
    observation_.Reset();
  }

  bool cancel_pending_ = false;
  base::ScopedObservation<download::DownloadItem, download::DownloadItem::Observer>
      observation_{this};
  base::WeakPtrFactory<ApprovedDownload> weak_factory_{this};
};

struct FileReputationState final : base::SupportsUserData::Data {
  explicit FileReputationState(const download::DownloadItem& item)
      : staged_file(item.GetFullPath()),
        target(item.GetTargetFilePath()),
        size(item.GetReceivedBytes()),
        hash(item.GetHash()) {}

  bool Matches(const download::DownloadItem& item) const {
    return staged_file == item.GetFullPath() && MatchesTarget(item);
  }

  bool MatchesTarget(const download::DownloadItem& item) const {
    return target == item.GetTargetFilePath() &&
           size == item.GetReceivedBytes() && hash == item.GetHash();
  }

  const base::FilePath staged_file;
  const base::FilePath target;
  const int64_t size;
  const std::string hash;
  bool allowed = false;
};

bool IsHttpsDownloadUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIs(url::kHttpsScheme) && url.has_host() &&
         !url.has_username() && !url.has_password();
}

bool HasSecureInitiator(const std::optional<url::Origin>& initiator) {
  return !initiator ||
         (!initiator->opaque() && initiator->scheme() == url::kHttpsScheme);
}

bool IsLowRiskDownloadName(const base::FilePath& name) {
  if (!net::IsSafePortablePathComponent(name))
    return false;
  const auto& policies = *safe_browsing::FileTypePolicies::GetInstance();
  return policies.GetFileDangerLevel(name, GURL(), nullptr) ==
             safe_browsing::DownloadFileType::NOT_DANGEROUS &&
         !policies.IsCheckedBinaryFile(name) && !policies.IsArchiveFile(name);
}

bool FitsProbeSize(int64_t bytes) {
  return bytes > 0 && bytes <= kMaxProbeDownloadBytes;
}

bool IsStrongEtag(std::string_view etag) {
  return etag.size() >= 2 && etag.size() <= 4096 && etag.front() == '"' &&
         etag.back() == '"' && net::HttpUtil::IsValidHeaderValue(etag);
}

bool IsUserDownloadType(const base::FilePath& name, std::string_view mime) {
  constexpr struct {
    base::FilePath::StringViewType extension;
    std::string_view mime;
  } types[] = {
      {FILE_PATH_LITERAL(".txt"), "text/plain"},
      {FILE_PATH_LITERAL(".csv"), "text/plain"},
      {FILE_PATH_LITERAL(".csv"), "text/csv"},
      {FILE_PATH_LITERAL(".pdf"), "application/pdf"},
      {FILE_PATH_LITERAL(".png"), "image/png"},
      {FILE_PATH_LITERAL(".jpg"), "image/jpeg"},
      {FILE_PATH_LITERAL(".jpeg"), "image/jpeg"},
      {FILE_PATH_LITERAL(".gif"), "image/gif"},
      {FILE_PATH_LITERAL(".webp"), "image/webp"},
      {FILE_PATH_LITERAL(".mp3"), "audio/mpeg"},
      {FILE_PATH_LITERAL(".mp4"), "video/mp4"},
      {FILE_PATH_LITERAL(".m4a"), "audio/mp4"},
      {FILE_PATH_LITERAL(".mov"), "video/quicktime"},
      {FILE_PATH_LITERAL(".ogg"), "audio/ogg"},
      {FILE_PATH_LITERAL(".wav"), "audio/wav"},
      {FILE_PATH_LITERAL(".wav"), "audio/x-wav"},
      {FILE_PATH_LITERAL(".webm"), "video/webm"},
      {FILE_PATH_LITERAL(".webm"), "audio/webm"},
  };
  return std::ranges::any_of(types, [&](const auto& type) {
           return mime == type.mime && name.MatchesFinalExtension(type.extension);
         }) && IsLowRiskDownloadName(name);
}

download::DownloadTargetInfo BlockedTarget() {
  download::DownloadTargetInfo target;
  target.interrupt_reason = download::DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED;
  return target;
}

void CompleteResumedTarget(download::DownloadTargetCallback callback,
                           download::DownloadTargetInfo target,
                           download::PathValidationResult result,
                           const base::FilePath& reserved_path) {
  if (result != download::PathValidationResult::SUCCESS ||
      reserved_path != target.target_path) {
    std::move(callback).Run(BlockedTarget());
    return;
  }
  std::move(callback).Run(std::move(target));
}

void CompleteTarget(download::DownloadTargetCallback callback,
                    const base::FilePath& directory,
                    download::PathValidationResult result,
                    const base::FilePath& path) {
  download::DownloadTargetInfo target;
  if (download::IsPathValidationSuccessful(result) &&
      path.DirName() == directory) {
    target.target_path = path;
    target.intermediate_path =
        path.AddExtension(FILE_PATH_LITERAL(".crdownload"));
  } else {
    target.interrupt_reason = download::DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED;
  }
  std::move(callback).Run(std::move(target));
}

}  // namespace

ProfileDownloadDelegate::ProfileDownloadDelegate(
    GURL probe_url,
    base::FilePath probe_directory,
    bool prompt_for_probe,
    bool allow_http_fixture)
    : probe_url_(std::move(probe_url)),
      probe_directory_(std::move(probe_directory)),
      prompt_for_probe_(prompt_for_probe),
      allow_http_fixture_(allow_http_fixture) {}

ProfileDownloadDelegate::~ProfileDownloadDelegate() = default;

void ProfileDownloadDelegate::AttachManager(content::DownloadManager& manager,
                                            history::HistoryService* history) {
  manager_ = &manager;
  history_service_ = history;
  if (history)
    history_ = std::make_unique<ProfileDownloadHistory>(
        manager, *history,
        base::BindRepeating(&ProfileDownloadDelegate::CanRecoverInterruptedDownload,
                            base::Unretained(this)),
        base::BindRepeating(&ProfileDownloadDelegate::HasUserDownloadCheckpoint,
                            base::Unretained(this)),
        base::BindRepeating(&ProfileDownloadDelegate::RestoreUserDownloadCheckpoint,
                            base::Unretained(this)));
}

void ProfileDownloadDelegate::SetFileReputationCatalog(
    base::FilePath catalog,
    base::OnceCallback<void(const base::FilePath&)> after_allow_for_testing) {
  file_reputation_catalog_ = std::move(catalog);
  after_file_allow_for_testing_ = std::move(after_allow_for_testing);
}

void ProfileDownloadDelegate::SetUrlReputation(
    std::unique_ptr<DownloadUrlReputation> checker) {
  url_reputation_ = std::move(checker);
}

void ProfileDownloadDelegate::EnableUserDownloads(base::FilePath suggested_directory) {
  CHECK(!manager_ && probe_directory_.empty());
  CHECK(suggested_directory.IsAbsolute() && !suggested_directory.IsNetwork());
  user_download_directory_ = std::move(suggested_directory);
}

bool ProfileDownloadDelegate::CanSaveLink(content::RenderFrameHost& frame,
                                           const GURL& url) const {
  auto* contents = content::WebContents::FromRenderFrameHost(&frame);
  return !user_download_directory_.empty() && IsHttpsDownloadUrl(url) &&
         HasSecureInitiator(frame.GetLastCommittedOrigin()) && frame.IsActive() &&
         contents && contents->GetVisibility() != content::Visibility::HIDDEN;
}

bool ProfileDownloadDelegate::SaveLink(content::RenderFrameHost& frame,
                                        const GURL& url) {
  if (!CanSaveLink(frame, url))
    return false;
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation("zitter_save_link", R"(
    semantics {
      sender: "Zitter Save Link As"
      description: "Downloads an HTTPS link selected from the page menu."
      trigger: "The user selects Save Link As and is asked where to save the file."
      data: "The selected URL and the current profile's applicable cookies."
      destination: WEBSITE
    }
    policy {
      cookies_allowed: YES
      cookies_store: "The current browser profile."
      setting: "This request requires an explicit action in the page menu."
      policy_exception_justification: "Not implemented."
    })");
  auto parameters = frame.CreateDownloadUrlParameters(url, annotation);
  parameters->set_initiator(frame.GetLastCommittedOrigin());
  parameters->set_has_user_gesture(true);
  parameters->set_download_source(download::DownloadSource::CONTEXT_MENU);
  frame.GetBrowserContext()->GetDownloadManager()->DownloadUrl(std::move(parameters));
  return true;
}

bool ProfileDownloadDelegate::IsUserDownload(const download::DownloadItem& item) const {
  const auto& chain = item.GetUrlChain();
  const auto& initiator = item.GetRequestInitiator();
  const bool typed = !initiator && ui::PageTransitionCoreTypeIs(
      item.GetTransitionType(), ui::PAGE_TRANSITION_TYPED);
  const auto* approved = static_cast<const ApprovedDownload*>(
      item.GetUserData(kApprovedDownloadKey));
  const bool approved_resume = approved && approved->MatchesResumableDownload(item);
  return !user_download_directory_.empty() &&
         (item.HasUserGesture() || typed || approved_resume) &&
         item.GetForcedFilePath().empty() && !chain.empty() &&
         std::ranges::all_of(chain, IsHttpsDownloadUrl) &&
         HasSecureInitiator(initiator);
}

bool ProfileDownloadDelegate::CanRecoverInterruptedDownload(
    const history::DownloadRow& row) const {
  if (row.embedder_download_data == ProfileDownloadHistory::kUserCheckpoint) {
    return !user_download_directory_.empty() &&
           row.url_chain.size() == 1 && IsHttpsDownloadUrl(row.url_chain.front()) &&
           row.http_method == "GET" &&
           row.danger_type == history::DownloadDangerType::NOT_DANGEROUS &&
           !row.opened && row.received_bytes > 0 &&
           row.received_bytes < row.total_bytes &&
           row.total_bytes <= kMaxUserDownloadBytes &&
           row.hash.size() == crypto::hash::kSha256Size &&
           row.download_slice_info.empty() && IsStrongEtag(row.etag) &&
           IsUserDownloadType(row.target_path.BaseName(), row.mime_type) &&
           row.current_path.DirName() == row.target_path.DirName() &&
           row.current_path.MatchesFinalExtension(FILE_PATH_LITERAL(".crdownload")) &&
           base::Uuid::ParseLowercase(
               row.current_path.BaseName().RemoveFinalExtension().AsUTF8Unsafe()).is_valid() &&
           download::GetDownloadResumeMode(
               row.url_chain.front(),
               history::ToContentDownloadInterruptReason(row.interrupt_reason),
               /*restart_required=*/false, /*user_action_required=*/true) ==
               download::ResumeMode::USER_CONTINUE;
  }
  if (probe_directory_.empty() || row.url_chain.size() != 1 ||
      row.url_chain.front() != probe_url_ ||
      (!IsHttpsDownloadUrl(probe_url_) &&
       !(allow_http_fixture_ && probe_url_.SchemeIs(url::kHttpScheme))) ||
      row.http_method != "GET" || row.mime_type != "text/plain" ||
      row.danger_type != history::DownloadDangerType::NOT_DANGEROUS ||
      row.opened || row.received_bytes <= 0 ||
      row.received_bytes >= row.total_bytes || !FitsProbeSize(row.total_bytes) ||
      row.hash.size() != crypto::hash::kSha256Size ||
      !row.download_slice_info.empty() || !IsStrongEtag(row.etag)) {
    return false;
  }

  const auto& target = row.target_path;
  const auto& partial = row.current_path;
  if (target.DirName() != probe_directory_ ||
      partial.DirName() != probe_directory_ ||
      !IsLowRiskDownloadName(target.BaseName()) ||
      !partial.MatchesFinalExtension(FILE_PATH_LITERAL(".crdownload"))) {
    return false;
  }
  const bool owned_name =
      partial == target.AddExtension(FILE_PATH_LITERAL(".crdownload")) ||
      base::Uuid::ParseCaseInsensitive(
          partial.BaseName().RemoveFinalExtension().AsUTF8Unsafe()).is_valid();
  return owned_name &&
         download::GetDownloadResumeMode(
             probe_url_,
             history::ToContentDownloadInterruptReason(row.interrupt_reason),
             /*restart_required=*/false, /*user_action_required=*/true) ==
             download::ResumeMode::USER_CONTINUE;
}

bool ProfileDownloadDelegate::HasUserDownloadCheckpoint(
    const download::DownloadItem& item) const {
  const auto* approved = static_cast<const ApprovedDownload*>(
      item.GetUserData(kApprovedDownloadKey));
  if (!approved || !approved->MatchesResumableDownload(item))
    return false;
  const auto default_partition =
      content::StoragePartitionConfig::CreateDefault(manager_->GetBrowserContext());
  return item.GetSerializedEmbedderDownloadData() ==
         manager_->StoragePartitionConfigToSerializedEmbedderDownloadData(
             default_partition);
}

void ProfileDownloadDelegate::RestoreUserDownloadCheckpoint(
    download::DownloadItem& item,
    const history::DownloadRow& row) {
  if (item.GetGuid() != row.guid || item.GetState() != download::DownloadItem::INTERRUPTED ||
      item.GetUrlChain() != row.url_chain || item.GetTargetFilePath() != row.target_path ||
      item.GetFullPath() != row.current_path || item.GetETag() != row.etag ||
      item.GetTotalBytes() != row.total_bytes ||
      item.GetReceivedBytes() != row.received_bytes || item.GetHash() != row.hash) {
    return;
  }
  download::DownloadTargetInfo selection;
  selection.target_path = row.target_path;
  selection.intermediate_path = row.current_path;
  item.SetUserData(kApprovedDownloadKey, std::make_unique<ApprovedDownload>(
      item, selection, row.url_chain.front(), row.total_bytes, row.etag));
}

void ProfileDownloadDelegate::GetNextId(content::DownloadIdCallback callback) {
  if (history_service_)
    history_service_->GetNextDownloadId(std::move(callback));
  else
    DownloadManagerDelegate::GetNextId(std::move(callback));
}

bool ProfileDownloadDelegate::SupportsHistoryLoading() {
  return history_service_ != nullptr;
}

void ProfileDownloadDelegate::Shutdown() {
  weak_factory_.InvalidateWeakPtrs();
  file_action_for_testing_.Reset();
  after_file_allow_for_testing_.Reset();
  url_reputation_.reset();
  if (save_picker_)
    save_picker_->Cancel();
  save_picker_.reset();
  history_.reset();
  history_service_ = nullptr;
  manager_ = nullptr;
}

void ProfileDownloadDelegate::GetSaveDir(content::BrowserContext*,
                                         base::FilePath*,
                                         base::FilePath* download_save_dir) {
  *download_save_dir = user_download_directory_.empty() ? probe_directory_
                                                        : user_download_directory_;
}

bool ProfileDownloadDelegate::DetermineDownloadTarget(
    download::DownloadItem* item,
    download::DownloadTargetCallback* callback) {
  if (!user_download_directory_.empty()) {
    // The resumed response can advance the byte count during a reputation
    // lookup. Validate Content-Range against the offset at target selection.
    const int64_t received_bytes = item->GetReceivedBytes();
    if (!IsUserDownload(*item)) {
      std::move(*callback).Run(BlockedTarget());
    } else if (url_reputation_) {
      url_reputation_->Check(
          item->GetUrlChain(),
          base::BindOnce(&ProfileDownloadDelegate::OnUserUrlReputationChecked,
                         weak_factory_.GetWeakPtr(), item->GetGuid(),
                         received_bytes, std::move(*callback)));
    } else {
      DetermineUserDownloadTarget(item, received_bytes, std::move(*callback));
    }
    return true;
  }
  const auto& chain = item->GetUrlChain();
  const bool http_fixture =
      allow_http_fixture_ && chain.size() == 1 && chain.front() == probe_url_ &&
      probe_url_.SchemeIs(url::kHttpScheme);
  const bool secure_transport =
      !chain.empty() && std::ranges::all_of(chain, IsHttpsDownloadUrl) &&
      HasSecureInitiator(item->GetRequestInitiator());
  if ((!http_fixture && !secure_transport) || probe_directory_.empty() ||
      item->GetURL() != probe_url_ || !item->GetForcedFilePath().empty()) {
    std::move(*callback).Run(BlockedTarget());
    return true;
  }

  const auto& previous_target = item->GetTargetFilePath();
  const auto headers = item->GetResponseHeaders();
  const bool partial_response =
      !previous_target.empty() && headers &&
      headers->response_code() == net::HTTP_PARTIAL_CONTENT;
  const bool omitted_type =
      partial_response && !headers->HasHeader("Content-Type");
  const bool omitted_name =
      !previous_target.empty() && (partial_response || !headers) &&
      item->GetContentDisposition().empty() &&
      item->GetSuggestedFilename().empty();
  // Keep the declared extension for classification. A text MIME type must
  // not turn a named executable or an archive into a .txt candidate.
  const auto name = omitted_name ? previous_target.BaseName()
      : net::GenerateFileName(item->GetURL(), item->GetContentDisposition(),
                              /*referrer_charset=*/"", item->GetSuggestedFilename(),
                              /*mime_type=*/"", "download");
  int64_t expected_size = item->GetTotalBytes();
  if (partial_response) {
    int64_t first_byte, last_byte;
    if (!headers->GetContentRangeFor206(&first_byte, &last_byte, &expected_size)) {
      std::move(*callback).Run(BlockedTarget());
      return true;
    }
  } else if (headers) {
    expected_size =
        headers->GetInt64HeaderValue("Content-Length").value_or(expected_size);
  }
  if ((!omitted_type && item->GetMimeType() != "text/plain") ||
      !FitsProbeSize(expected_size) || !IsLowRiskDownloadName(name)) {
    std::move(*callback).Run(BlockedTarget());
    return true;
  }

  if (url_reputation_) {
    url_reputation_->Check(
        chain, base::BindOnce(&ProfileDownloadDelegate::OnUrlReputationChecked,
                              weak_factory_.GetWeakPtr(), item->GetGuid(), name,
                              std::move(*callback)));
  } else {
    ReserveDownloadTarget(item, name, std::move(*callback));
  }
  return true;
}

void ProfileDownloadDelegate::OnUserUrlReputationChecked(
    std::string guid,
    int64_t received_bytes,
    download::DownloadTargetCallback callback,
    DownloadUrlVerdict verdict) {
  auto* item = manager_ ? manager_->GetDownloadByGuid(guid) : nullptr;
  if (verdict != DownloadUrlVerdict::kNoMatch || !item ||
      item->GetState() != download::DownloadItem::IN_PROGRESS ||
      !IsUserDownload(*item)) {
    std::move(callback).Run(BlockedTarget());
    return;
  }
  DetermineUserDownloadTarget(item, received_bytes, std::move(callback));
}

void ProfileDownloadDelegate::DetermineUserDownloadTarget(
    download::DownloadItem* item,
    int64_t received_bytes,
    download::DownloadTargetCallback callback) {
  if (!item->GetTargetFilePath().empty()) {
    ResumeUserDownload(*item, received_bytes, std::move(callback));
    return;
  }
  const auto name = net::GenerateFileName(
      item->GetURL(), item->GetContentDisposition(), /*referrer_charset=*/"",
      item->GetSuggestedFilename(), /*mime_type=*/"", "download");
  const auto headers = item->GetResponseHeaders();
  const auto length = headers ? headers->GetInt64HeaderValue("Content-Length") : std::nullopt;
  const int64_t size = length.value_or(std::max<int64_t>(0, item->GetTotalBytes()));
  if (size > kMaxUserDownloadBytes ||
      (headers && headers->HasHeader("Content-Length") && (!length || *length <= 0)) ||
      !IsUserDownloadType(name, item->GetMimeType())) {
    std::move(callback).Run(BlockedTarget());
    return;
  }
  auto* contents = content::DownloadItemUtils::GetWebContents(item);
  if (!contents || (save_picker_ && save_picker_->pending())) {
    download::DownloadTargetInfo target;
    target.interrupt_reason = download::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED;
    std::move(callback).Run(std::move(target));
    return;
  }
  save_picker_ = base::MakeRefCounted<DownloadSavePicker>(
      *item, *contents, user_download_directory_.Append(name),
      base::BindRepeating([](const base::FilePath::StringType& extension,
                             const base::FilePath& path) {
        return path.MatchesFinalExtension(extension) &&
               IsLowRiskDownloadName(path.BaseName());
      }, name.FinalExtension()),
      base::BindOnce(&ProfileDownloadDelegate::OnUserDownloadSelected,
                     weak_factory_.GetWeakPtr(), item->GetGuid(), size,
                     std::move(callback)));
  save_picker_->Show();
}

void ProfileDownloadDelegate::OnUserDownloadSelected(
    std::string guid, int64_t size, download::DownloadTargetCallback callback,
    download::DownloadTargetInfo selection) {
  auto* item = manager_ ? manager_->GetDownloadByGuid(guid) : nullptr;
  if (item && !selection.target_path.empty()) {
    const std::string& etag = item->GetETag();
    item->SetUserData(kApprovedDownloadKey, std::make_unique<ApprovedDownload>(
        *item, selection, item->GetURL(), size,
        size > 0 && item->GetUrlChain().size() == 1 && IsStrongEtag(etag) ? etag : std::string()));
  }
  std::move(callback).Run(std::move(selection));
}

void ProfileDownloadDelegate::ResumeUserDownload(
    download::DownloadItem& item,
    int64_t received_bytes,
    download::DownloadTargetCallback callback) {
  const auto* approved = static_cast<const ApprovedDownload*>(item.GetUserData(kApprovedDownloadKey));
  const auto headers = item.GetResponseHeaders();
  int64_t first_byte, last_byte, total_size;
  if (!IsUserDownload(item) || !approved || approved->etag.empty() ||
      approved->target != item.GetTargetFilePath() || approved->partial != item.GetFullPath() ||
      item.GetUrlChain().size() != 1 || approved->url != item.GetURL() ||
      !headers || headers->response_code() != net::HTTP_PARTIAL_CONTENT ||
      headers->GetNormalizedHeader("ETag") != approved->etag ||
      !headers->GetContentRangeFor206(&first_byte, &last_byte, &total_size) ||
      first_byte <= 0 || first_byte != received_bytes ||
      last_byte != total_size - 1 || total_size != approved->size) {
    std::move(callback).Run(BlockedTarget());
    return;
  }
  const auto name = item.GetContentDisposition().empty() && item.GetSuggestedFilename().empty()
      ? approved->target.BaseName()
      : net::GenerateFileName(item.GetURL(), item.GetContentDisposition(), "",
                              item.GetSuggestedFilename(), "", "download");
  if (!name.MatchesFinalExtension(approved->target.FinalExtension()) ||
      !IsLowRiskDownloadName(name) ||
      (headers->HasHeader("Content-Type") && !IsUserDownloadType(name, item.GetMimeType()))) {
    std::move(callback).Run(BlockedTarget());
    return;
  }
  download::DownloadTargetInfo target;
  target.target_path = approved->target;
  target.intermediate_path = approved->partial;
  target.target_disposition = download::DownloadItem::TARGET_DISPOSITION_PROMPT;
  const auto directory = approved->target.DirName();
  download::DownloadPathReservationTracker::GetReservedPath(
      &item, approved->target, directory, base::FilePath(), false,
      download::DownloadPathReservationTracker::PROMPT,
      base::BindOnce(&CompleteResumedTarget, std::move(callback), std::move(target)), directory);
}

void ProfileDownloadDelegate::OnUrlReputationChecked(
    std::string guid,
    base::FilePath name,
    download::DownloadTargetCallback callback,
    DownloadUrlVerdict verdict) {
  auto* item = manager_ ? manager_->GetDownloadByGuid(guid) : nullptr;
  if (verdict != DownloadUrlVerdict::kNoMatch || !item ||
      item->GetState() != download::DownloadItem::IN_PROGRESS) {
    std::move(callback).Run(BlockedTarget());
    return;
  }
  ReserveDownloadTarget(item, name, std::move(callback));
}

void ProfileDownloadDelegate::ReserveDownloadTarget(
    download::DownloadItem* item,
    const base::FilePath& name,
    download::DownloadTargetCallback callback) {
  const auto& previous_target = item->GetTargetFilePath();
  if (!previous_target.empty()) {
    if (previous_target.DirName() != probe_directory_ ||
        !previous_target.MatchesFinalExtension(name.FinalExtension()) ||
        !IsLowRiskDownloadName(previous_target.BaseName())) {
      std::move(callback).Run(BlockedTarget());
      return;
    }
    download::DownloadTargetInfo target;
    target.target_path = previous_target;
    target.target_disposition = item->GetTargetDisposition();
    target.mime_type = "text/plain";
    const auto& current_path = item->GetFullPath();
    if (current_path.DirName() == probe_directory_ &&
        current_path.MatchesFinalExtension(FILE_PATH_LITERAL(".crdownload"))) {
      target.intermediate_path = current_path;
    } else {
      target.intermediate_path = probe_directory_.AppendASCII(
          base::Uuid::GenerateRandomV4().AsLowercaseString() + ".crdownload");
    }

    // Chromium releases the reservation on interruption. Reserve the same
    // path again so resumption cannot replace a file or silently pick another.
    download::DownloadPathReservationTracker::GetReservedPath(
        item, previous_target, probe_directory_, base::FilePath(),
        /*create_directory=*/false,
        download::DownloadPathReservationTracker::PROMPT,
        base::BindOnce(&CompleteResumedTarget, std::move(callback),
                       std::move(target)),
        probe_directory_);
    return;
  }

  const auto suggested = probe_directory_.Append(name);
  if (prompt_for_probe_) {
    auto* contents = content::DownloadItemUtils::GetWebContents(item);
    if (!contents || (save_picker_ && save_picker_->pending())) {
      download::DownloadTargetInfo target;
      target.interrupt_reason = download::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED;
      std::move(callback).Run(std::move(target));
      return;
    }
    save_picker_ = base::MakeRefCounted<DownloadSavePicker>(
        *item, *contents, suggested,
        base::BindRepeating(
            [](const base::FilePath& directory,
               const base::FilePath::StringType& extension,
               const base::FilePath& path) {
              return path.DirName() == directory &&
                     path.MatchesFinalExtension(extension) &&
                     IsLowRiskDownloadName(path.BaseName());
            },
            probe_directory_, name.FinalExtension()),
        std::move(callback));
    save_picker_->Show();
    return;
  }

  download::DownloadPathReservationTracker::GetReservedPath(
      item, suggested,
      probe_directory_, base::FilePath(), /*create_directory=*/false,
      download::DownloadPathReservationTracker::UNIQUIFY,
      base::BindOnce(&CompleteTarget, std::move(callback), probe_directory_),
      probe_directory_);
}

bool ProfileDownloadDelegate::ShouldCompleteDownload(
    download::DownloadItem* item,
    base::OnceClosure complete_callback) {
  const auto* approved = static_cast<const ApprovedDownload*>(item->GetUserData(kApprovedDownloadKey));
  const auto received = item->GetReceivedBytes();
  const bool user_download = IsUserDownload(*item) && approved &&
      approved->target == item->GetTargetFilePath() && approved->partial == item->GetFullPath() &&
      item->GetTargetDisposition() == download::DownloadItem::TARGET_DISPOSITION_PROMPT &&
      received > 0 && received <= kMaxUserDownloadBytes &&
      (approved->size == 0 || received == approved->size) &&
      item->GetHash().size() == crypto::hash::kSha256Size;
  const bool fixture_download = !probe_directory_.empty() && item->GetURL() == probe_url_ &&
      item->GetTargetFilePath().DirName() == probe_directory_ && FitsProbeSize(item->GetReceivedBytes());
  if (user_download || fixture_download) {
    if (fixture_download && file_reputation_catalog_.empty())
      return true;

    const auto* state = static_cast<FileReputationState*>(
        item->GetUserData(kFileReputationKey));
    if (state && state->Matches(*item))
      return state->allowed;

    if (!state && item->GetFullPath().DirName() == item->GetTargetFilePath().DirName() &&
        item->GetFullPath().MatchesFinalExtension(FILE_PATH_LITERAL(".crdownload"))) {
      auto check = std::make_unique<FileReputationState>(*item);
      auto task = base::BindOnce(
          [](const base::FilePath& catalog, const base::FilePath& staged_file,
             int64_t size, const std::string& hash,
             base::OnceCallback<void(const base::FilePath&)> after_allow) {
            const auto verdict = catalog.empty()
                ? (VerifyDownloadedFileContents(staged_file, size, hash).empty()
                       ? DownloadFileVerdict::kUnavailable : DownloadFileVerdict::kAllowed)
                : CheckDownloadFileReputation(catalog, staged_file, size, hash);
            if (verdict == DownloadFileVerdict::kAllowed && after_allow)
              std::move(after_allow).Run(staged_file);
            return verdict;
          },
          file_reputation_catalog_, check->staged_file, check->size, check->hash,
          std::move(after_file_allow_for_testing_));
      item->SetUserData(kFileReputationKey, std::move(check));
      if (base::ThreadPool::PostTaskAndReplyWithResult(
              FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
              std::move(task),
              base::BindOnce(&ProfileDownloadDelegate::OnFileReputationChecked,
                             weak_factory_.GetWeakPtr(), item->GetGuid(),
                             std::move(complete_callback)))) {
        return false;
      }
    }
  }

  // Completion is called from inside the item's state machine. Cancel after
  // that stack unwinds, without releasing the intermediate file to publish.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&ProfileDownloadDelegate::CancelUnpublishableDownload,
                     weak_factory_.GetWeakPtr(), item->GetGuid()));
  return false;
}

void ProfileDownloadDelegate::OnFileReputationChecked(
    std::string guid,
    base::OnceClosure complete_callback,
    DownloadFileVerdict verdict) {
  auto* item = manager_ ? manager_->GetDownloadByGuid(guid) : nullptr;
  if (!item || item->GetState() != download::DownloadItem::IN_PROGRESS ||
      !item->AllDataSaved()) {
    return;
  }
  auto* state = static_cast<FileReputationState*>(
      item->GetUserData(kFileReputationKey));
  if (state && state->Matches(*item) && verdict == DownloadFileVerdict::kAllowed) {
    state->allowed = true;
    std::move(complete_callback).Run();
    return;
  }

  const bool threat = verdict == DownloadFileVerdict::kThreat;
  item->OnContentCheckCompleted(
      threat ? download::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT
             : download::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS,
      threat ? download::DOWNLOAD_INTERRUPT_REASON_FILE_VIRUS_INFECTED
             : download::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED);
}

void ProfileDownloadDelegate::CancelUnpublishableDownload(
    const std::string& guid) {
  auto* item = manager_ ? manager_->GetDownloadByGuid(guid) : nullptr;
  if (item && item->GetState() == download::DownloadItem::IN_PROGRESS)
    item->Cancel(true);
}

void ProfileDownloadDelegate::CheckDownloadAllowed(
    const content::WebContents::Getter& contents,
    const GURL& url,
    const std::string& method,
    std::optional<url::Origin> request_initiator,
    bool from_cross_origin_redirect,
    bool content_initiated,
    const std::string&,
    std::optional<ui::PageTransition> transition,
    content::CheckDownloadAllowedCallback callback) {
  const bool http_fixture = allow_http_fixture_ && url == probe_url_ &&
                            url.SchemeIs(url::kHttpScheme);
  const bool secure_transport =
      IsHttpsDownloadUrl(url) && HasSecureInitiator(request_initiator);
  auto* page = contents ? contents.Run() : nullptr;
  bool user_download = false;
  if (page && !user_download_directory_.empty() && secure_transport && method == "GET") {
    user_download = transition.has_value();
    if (!user_download && content_initiated && !from_cross_origin_redirect &&
        request_initiator && request_initiator->IsSameOriginWith(url) &&
        page->GetVisibility() != content::Visibility::HIDDEN) {
      auto* frame = page->GetPrimaryMainFrame();
      user_download = frame && frame->IsActive() &&
          frame->GetLastCommittedOrigin() == *request_initiator &&
          frame->ConsumeTransientUserActivation();
    }
  }
  const bool fixture = (http_fixture || secure_transport) &&
                       !probe_directory_.empty() && url == probe_url_ &&
                       method == "GET" && !from_cross_origin_redirect &&
                       !content_initiated;
  const bool allowed = page && (user_download || fixture);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), allowed));
}

bool ProfileDownloadDelegate::IsImportedHistoryEntry(
    const download::DownloadItem& item) const {
  return history_ && history_->IsImportedEntry(item);
}

bool ProfileDownloadDelegate::CanDeleteDownloadFile(
    const download::DownloadItem& item) const {
  const auto& path = item.GetTargetFilePath();
  const auto* verified = static_cast<const FileReputationState*>(
      item.GetUserData(kFileReputationKey));
  const bool user_download = !user_download_directory_.empty() &&
      verified && verified->allowed && verified->MatchesTarget(item);
  const bool fixture_download = !probe_directory_.empty() &&
      item.GetURL() == probe_url_ && path.DirName() == probe_directory_ &&
      FitsProbeSize(item.GetReceivedBytes());
  return manager_ && manager_->GetDownloadByGuid(item.GetGuid()) == &item &&
         !IsImportedHistoryEntry(item) &&
         (user_download || fixture_download) &&
         item.GetState() == download::DownloadItem::COMPLETE &&
         !item.IsTransient() && !item.GetFileExternallyRemoved() &&
         !item.IsDangerous() && !item.IsInsecure() &&
         path == item.GetFullPath() && path.IsAbsolute() && !path.IsNetwork() &&
         IsLowRiskDownloadName(path.BaseName()) &&
         item.GetHash().size() == crypto::hash::kSha256Size;
}

void ProfileDownloadDelegate::DeleteDownloadFile(
    const download::DownloadItem& item,
    base::OnceCallback<void(bool)> done) {
  if (!CanDeleteDownloadFile(item)) {
    std::move(done).Run(false);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(
          [](const base::FilePath& path, int64_t size, const std::string& hash) {
            return DeleteVerifiedDownloadFile(path, size, hash);
          },
          item.GetFullPath(), item.GetReceivedBytes(), item.GetHash()),
      std::move(done));
}

void ProfileDownloadDelegate::OpenDownload(download::DownloadItem* item) {
  ValidateFileAction(*item, DownloadFileAction::kOpen);
}

void ProfileDownloadDelegate::ShowDownloadInShell(download::DownloadItem* item) {
  ValidateFileAction(*item, DownloadFileAction::kReveal);
}

void ProfileDownloadDelegate::SetFileActionForTesting(
    FileActionForTesting callback) {
  file_action_for_testing_ = std::move(callback);
}

bool ProfileDownloadDelegate::CanDispatchFileAction(
    const download::DownloadItem& item, DownloadFileAction action) const {
  const auto& path = item.GetTargetFilePath();
  return !IsImportedHistoryEntry(item) &&
         item.GetState() == download::DownloadItem::COMPLETE &&
         !item.IsTransient() && !item.GetFileExternallyRemoved() &&
         !item.IsDangerous() && !item.IsInsecure() &&
         path == item.GetFullPath() && path.IsAbsolute() &&
         !path.IsNetwork() && !path.ReferencesParent() &&
         !path.EndsWithSeparator() &&
         net::IsSafePortablePathComponent(path.BaseName()) &&
         (action != DownloadFileAction::kOpen ||
          IsLowRiskDownloadName(path.BaseName()));
}

void ProfileDownloadDelegate::ValidateFileAction(download::DownloadItem& item,
                                                  DownloadFileAction action) {
  if (!manager_ || manager_->GetDownloadByGuid(item.GetGuid()) != &item ||
      !CanDispatchFileAction(item, action)) {
    return;
  }
  const auto& path = item.GetTargetFilePath();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](DownloadFileAction action, const base::FilePath& path, int64_t size,
             const std::string& hash) {
            if (action == DownloadFileAction::kOpen) {
              if (hash.size() != crypto::hash::kSha256Size)
                return base::FilePath();
              return VerifyDownloadedFileContents(path, size, hash);
            }
            return VerifyDownloadFilePath(path);
          },
          action, path, item.GetReceivedBytes(), item.GetHash()),
      base::BindOnce(&ProfileDownloadDelegate::OnFileActionValidated,
                     weak_factory_.GetWeakPtr(), action, item.GetGuid(),
                     item.GetReceivedBytes(), item.GetHash()));
}

void ProfileDownloadDelegate::OnFileActionValidated(DownloadFileAction action,
                                                    std::string guid,
                                                    int64_t size,
                                                    std::string hash,
                                                    base::FilePath path) {
  auto* item = manager_ ? manager_->GetDownloadByGuid(guid) : nullptr;
  if (!item || !CanDispatchFileAction(*item, action) ||
      path != item->GetTargetFilePath() ||
      (action == DownloadFileAction::kOpen &&
       (size != item->GetReceivedBytes() || hash != item->GetHash()))) {
    path.clear();
  }
  if (file_action_for_testing_) {
    file_action_for_testing_.Run(action, path);
  } else if (!path.empty()) {
    RunDownloadFileAction(action, path);
  }
}

download::QuarantineConnectionCallback
ProfileDownloadDelegate::GetQuarantineConnectionCallback() {
#if BUILDFLAG(IS_MAC)
  return base::BindRepeating(
      [](mojo::PendingReceiver<quarantine::mojom::Quarantine> receiver) {
        mojo::MakeSelfOwnedReceiver(
            std::make_unique<quarantine::QuarantineImpl>(), std::move(receiver));
      });
#elif BUILDFLAG(IS_WIN)
  return base::BindRepeating(
      [](mojo::PendingReceiver<quarantine::mojom::Quarantine> receiver) {
        content::ServiceProcessHost::Launch(
            std::move(receiver),
            content::ServiceProcessHost::Options()
                .WithDisplayName("Zitter Download Quarantine")
                .Pass());
      });
#else
  return {};
#endif
}

std::string ProfileDownloadDelegate::ApplicationClientIdForFileScanning() {
#if BUILDFLAG(IS_WIN)
  // Stable identity for Windows Attachment Services, independent of profiles.
  return "108a3e29-e88f-4ea1-a2bb-6ae9fc4e41a2";
#else
  return {};
#endif
}

}  // namespace zitter::content_embedder
