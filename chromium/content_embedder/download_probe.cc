// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "download_probe.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "download_url_reputation.h"
#include "profile_context.h"
#include "profile_download_delegate.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/shell_dialogs/select_file_dialog_factory.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace zitter::content_embedder {
namespace {

constexpr char kFileReputationProbe[] =
    "zitter-content-download-file-reputation-probe";
constexpr char kPublicationChange[] =
    "zitter-content-download-publication-change";
constexpr char kExpectFileThreat[] = "zitter-content-expect-download-file-threat";
constexpr char kExpectFileUnavailable[] =
    "zitter-content-expect-download-file-unavailable";
constexpr char kReputationProbe[] = "zitter-content-download-reputation-probe";
constexpr char kSaveProbe[] = "zitter-content-download-save-probe";
constexpr char kExpectPublishCollision[] =
    "zitter-content-expect-download-publish-collision";
constexpr char kExpectResumeBlocked[] =
    "zitter-content-expect-download-resume-blocked";
constexpr char kExpectInterrupted[] =
    "zitter-content-expect-download-interrupted";

constexpr char kProbeTarget[] = "zitter-content-download-probe-target";

bool IsSaveProbeFileType(std::string_view suffix) {
  constexpr std::string_view types[] = {
      "csv", "m4a", "mov", "mp3", "mp4", "ogg", "pdf", "png", "wav", "webm"};
  return std::ranges::find(types, suffix) != std::end(types);
}

bool IsProbeUrl(const GURL& url, bool allow_https) {
  return url.is_valid() && (url.SchemeIs("http") ||
                            (allow_https && url.SchemeIs("https"))) &&
         url.host() == "127.0.0.1" && url.has_port() &&
         !url.has_username() && !url.has_password();
}

class SaveProbeDialog final : public ui::SelectFileDialog {
 public:
  SaveProbeDialog(Listener* listener,
                  std::unique_ptr<ui::SelectFilePolicy> policy,
                  std::string action,
                  base::FilePath directory,
                  base::WeakPtr<content::WebContents> contents,
                  base::RepeatingCallback<void(bool)> checked)
      : ui::SelectFileDialog(listener, std::move(policy)),
        action_(std::move(action)),
        directory_(std::move(directory)),
        contents_(std::move(contents)),
        checked_(std::move(checked)) {}

  bool IsRunning(gfx::NativeWindow) const override { return listener_ != nullptr; }
  void ListenerDestroyed() override { listener_ = nullptr; }

 private:
  ~SaveProbeDialog() override = default;
  bool HasMultipleFileTypeChoicesImpl() override { return false; }

  void SelectFileImpl(Type type,
                      const std::u16string& title,
                      const base::FilePath& default_path,
                      const FileTypeInfo* file_types,
                      int file_type_index,
                      const base::FilePath::StringType& default_extension,
                      gfx::NativeWindow owning_window,
                      const GURL*) override {
    const std::string suffix =
        IsSaveProbeFileType(action_) ? action_ : "txt";
    const auto extension = base::FilePath::FromASCII(suffix).value();
    const bool valid =
        type == SELECT_SAVEAS_FILE && !title.empty() && owning_window &&
        default_path == directory_.AppendASCII("download-probe." + suffix) &&
        file_types && file_types->allowed_paths == FileTypeInfo::NATIVE_PATH &&
        file_types->extensions.size() == 1 && file_type_index == 1 &&
        file_types->extensions.front() == std::vector{extension} &&
        default_extension == extension;
    checked_.Run(valid);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&SaveProbeDialog::Reply,
                                  base::RetainedRef(this), valid, suffix));
  }

  void Reply(bool valid, const std::string& suffix) {
    if (!listener_)
      return;
    if (!valid || action_ == "cancel") {
      listener_->FileSelectionCanceled();
      return;
    }
    if (action_ == "late" && contents_) {
      contents_->UpdateWebContentsVisibility(content::Visibility::HIDDEN);
      if (!listener_) {
        std::puts("Zitter Content save picker detached its late reply");
        return;
      }
    }
    auto path = directory_.AppendASCII("chosen-save." + suffix);
    if (action_ == "outside")
      path = directory_.DirName().AppendASCII("outside.txt");
    else if (action_ == "collision")
      path = directory_.AppendASCII("download-probe.txt");
    else if (action_ == "wrong-type")
      path = directory_.AppendASCII("chosen-save.exe");
    else if (action_ == "alias")
      path = directory_.DirName()
                 .AppendASCII("parent-alias")
                 .Append(directory_.BaseName())
                 .AppendASCII("alias-save.txt");
    listener_->FileSelected(ui::SelectedFileInfo(path), 1);
  }

  const std::string action_;
  const base::FilePath directory_;
  const base::WeakPtr<content::WebContents> contents_;
  const base::RepeatingCallback<void(bool)> checked_;
};

class SaveProbeDialogFactory final : public ui::SelectFileDialogFactory {
 public:
  SaveProbeDialogFactory(std::string action,
                         base::FilePath directory,
                         base::WeakPtr<content::WebContents> contents,
                         base::RepeatingCallback<void(bool)> checked)
      : action_(std::move(action)),
        directory_(std::move(directory)),
        contents_(std::move(contents)),
        checked_(std::move(checked)) {}
  ~SaveProbeDialogFactory() override = default;

  ui::SelectFileDialog* Create(
      ui::SelectFileDialog::Listener* listener,
      std::unique_ptr<ui::SelectFilePolicy> policy) override {
    return new SaveProbeDialog(listener, std::move(policy), action_, directory_,
                               contents_, checked_);
  }

 private:
  const std::string action_;
  const base::FilePath directory_;
  const base::WeakPtr<content::WebContents> contents_;
  const base::RepeatingCallback<void(bool)> checked_;
};

}  // namespace

// static
std::unique_ptr<DownloadProbe> DownloadProbe::Create(const base::FilePath& root) {
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  GURL url(command_line.GetSwitchValueASCII(kDownloadProbe));
  const bool transport_probe = command_line.HasSwitch(kDownloadTransportProbe);
  GURL reputation_endpoint;
  if (command_line.HasSwitch(kReputationProbe)) {
    reputation_endpoint = GURL(command_line.GetSwitchValueASCII(kReputationProbe));
    if (!transport_probe || !IsProbeUrl(reputation_endpoint, true) ||
        !reputation_endpoint.SchemeIs("https") ||
        reputation_endpoint.has_query() || reputation_endpoint.has_ref()) {
      return nullptr;
    }
  }
  GURL target_url = command_line.HasSwitch(kProbeTarget)
                        ? GURL(command_line.GetSwitchValueASCII(kProbeTarget))
                        : url;
  auto directory = root.AppendASCII("downloads");
  const auto save_action = command_line.GetSwitchValueASCII(kSaveProbe);
  const bool native_user_save = save_action == "user-native-accept" ||
                                save_action == "user-native-cancel";
  auto checkpoint_phase =
      command_line.GetSwitchValueASCII("zitter-content-download-checkpoint-probe");
#if !BUILDFLAG(IS_MAC)
  if (!checkpoint_phase.empty() || native_user_save)
    return nullptr;
#endif
  const bool user_save = native_user_save || save_action == "user-accept" ||
                         save_action == "user-cancel" || save_action == "user-wrong-type" ||
                         save_action == "user-blocked" || save_action == "user-outside" ||
                         save_action == "user-delete" || save_action == "user-delete-changed" ||
                         save_action == "user-resume" ||
                         (save_action.starts_with("user-") &&
                          IsSaveProbeFileType(std::string_view(save_action).substr(5))) ||
                         save_action == "user-anchor" || save_action == "user-anchor-passive" ||
                         save_action == "user-size-limit" || save_action == "user-save-link";
  const bool expect_blocked =
      command_line.HasSwitch("zitter-content-expect-download-blocked");
  const bool expect_publish_collision = command_line.HasSwitch(kExpectPublishCollision);
  const bool expect_resume_blocked = command_line.HasSwitch(kExpectResumeBlocked);
  const bool expect_interrupted = command_line.HasSwitch(kExpectInterrupted);
  const bool check_file = command_line.HasSwitch(kFileReputationProbe);
  auto publication_change = command_line.GetSwitchValueASCII(kPublicationChange);
  const bool change_publication = command_line.HasSwitch(kPublicationChange);
  const bool expect_file_threat = command_line.HasSwitch(kExpectFileThreat);
  const bool expect_file_unavailable = command_line.HasSwitch(kExpectFileUnavailable);
  if (!IsProbeUrl(url, transport_probe) ||
      !IsProbeUrl(target_url, transport_probe) ||
      (!checkpoint_phase.empty() &&
       (!transport_probe || url != target_url || save_action != "user-accept" ||
        (checkpoint_phase != "create" && checkpoint_phase != "restore" &&
         checkpoint_phase != "reject"))) ||
      (!transport_probe && command_line.HasSwitch(kProbeTarget)) ||
      (user_save && !transport_probe) ||
      (native_user_save && (url != target_url || url.path() != "/text" ||
                           expect_blocked != (save_action == "user-native-cancel"))) ||
      ((save_action == "user-blocked" || save_action == "user-anchor-passive") && !expect_blocked) ||
      (transport_probe && ((command_line.HasSwitch(kSaveProbe) && !user_save) ||
                           expect_publish_collision)) ||
      base::IsLink(directory) || !base::DirectoryExists(directory) ||
      (command_line.HasSwitch(kSaveProbe) && !user_save &&
       save_action != "native" && save_action != "accept" &&
       save_action != "cancel" && save_action != "outside" &&
       save_action != "collision" && save_action != "wrong-type" &&
       save_action != "late" && save_action != "alias") ||
      (expect_blocked + expect_publish_collision + expect_resume_blocked +
           expect_interrupted + expect_file_threat + expect_file_unavailable +
           change_publication > 1) ||
      ((expect_file_threat || expect_file_unavailable || change_publication) &&
       !check_file) ||
      (change_publication && publication_change != "rewrite" &&
       publication_change != "replace") ||
      (expect_publish_collision && save_action != "accept") ||
      (transport_probe && ((expect_resume_blocked && save_action != "user-resume") ||
                           expect_interrupted))) {
    return nullptr;
  }
  std::string certificate;
  if (transport_probe) {
    const auto path = root.AppendASCII("download-probe-leaf.der");
    if (base::IsLink(path) ||
        !base::ReadFileToStringWithMaxSize(path, &certificate, 16 * 1024) ||
        certificate.empty()) {
      return nullptr;
    }
  }
  ExpectedResult expected_result = ExpectedResult::kComplete;
  if (expect_blocked)
    expected_result = ExpectedResult::kBlocked;
  else if (expect_publish_collision)
    expected_result = ExpectedResult::kPublishCollision;
  else if (expect_resume_blocked)
    expected_result = ExpectedResult::kResumeBlocked;
  else if (expect_interrupted)
    expected_result = ExpectedResult::kInterrupted;
  else if (expect_file_threat)
    expected_result = ExpectedResult::kFileThreat;
  else if (expect_file_unavailable)
    expected_result = ExpectedResult::kFileUnavailable;
  else if (change_publication)
    expected_result = ExpectedResult::kPublicationChanged;
  auto file_catalog = check_file ? root.AppendASCII("download-file-reputation.txt")
                                 : base::FilePath();
  return std::unique_ptr<DownloadProbe>(new DownloadProbe(
      std::move(url), std::move(target_url), std::move(directory), expected_result,
      save_action, std::vector<uint8_t>(certificate.begin(), certificate.end()),
      std::move(reputation_endpoint), std::move(file_catalog),
      std::move(publication_change), std::move(checkpoint_phase)));
}

DownloadProbe::DownloadProbe(GURL url,
                             GURL target_url,
                             base::FilePath directory,
                             ExpectedResult expected_result,
                             std::string save_action,
                             std::vector<uint8_t> certificate,
                             GURL reputation_endpoint,
                             base::FilePath file_catalog,
                             std::string publication_change,
                             std::string checkpoint_phase)
    : url_(std::move(url)),
      target_url_(std::move(target_url)),
      directory_(std::move(directory)),
      expected_result_(expected_result),
      save_action_(std::move(save_action)),
      certificate_(std::move(certificate)),
      reputation_endpoint_(std::move(reputation_endpoint)),
      file_catalog_(std::move(file_catalog)),
      publication_change_(std::move(publication_change)),
      checkpoint_phase_(std::move(checkpoint_phase)) {}

DownloadProbe::~DownloadProbe() = default;

std::optional<bool> DownloadProbe::native_user_save() const {
  if (save_action_ == "user-native-accept")
    return true;
  if (save_action_ == "user-native-cancel")
    return false;
  return std::nullopt;
}

void DownloadProbe::Prepare(ProfileContext& profile) {
  if (!certificate_.empty())
    profile.SetDownloadProbeCertificate(certificate_);
  if (save_action_.starts_with("user-")) {
    CHECK_EQ(profile.IsOffTheRecord(), !persistent());
    CHECK_EQ(profile.GetHistory() != nullptr, persistent());
    profile.EnableUserDownloads(directory_);
  } else {
    profile.ConfigureDownloadProbe(target_url_, directory_, !save_action_.empty(),
                                   /*allow_http_fixture=*/certificate_.empty());
  }
  manager_.Observe(profile.GetDownloadManager());
  auto* delegate = static_cast<ProfileDownloadDelegate*>(
      profile.GetDownloadManagerDelegate());
  if (!file_catalog_.empty()) {
    base::OnceCallback<void(const base::FilePath&)> after_allow;
    if (!publication_change_.empty()) {
      after_allow = base::BindOnce(
          [](const std::string& change, const base::FilePath& replacement,
             const base::FilePath& staged) {
            bool changed = false;
            if (change == "replace") {
              changed = base::ReplaceFile(replacement, staged, nullptr);
            } else {
              std::string bytes;
              changed = base::ReadFileToStringWithMaxSize(replacement, &bytes,
                                                          64 * 1024) &&
                        base::WriteFile(staged, bytes);
            }
            if (changed)
              std::puts("Zitter Content changed the staged file after the allow verdict");
          },
          publication_change_,
          directory_.DirName().AppendASCII("download-publication-replacement"));
    }
    delegate->SetFileReputationCatalog(file_catalog_, std::move(after_allow));
  }
  if (!reputation_endpoint_.is_empty()) {
    delegate->SetUrlReputation(std::make_unique<DownloadUrlReputation>(
        profile.GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess(),
        reputation_endpoint_));
  }
}

void DownloadProbe::Start(content::WebContents& contents,
                          base::OnceClosure done) {
  done_ = std::move(done);
  if (checkpoint_phase_ == "reject") {
    if (manager_.GetSource()->IsManagerInitialized())
      OnManagerInitialized();
    return;
  }
  if (checkpoint_phase_ == "restore")
    return;
  if (save_action_.starts_with("user-anchor") || save_action_ == "user-save-link")
    Observe(&contents);
  if (!save_action_.empty() && !native_user_save() &&
      save_action_ != "native" && save_action_ != "user-blocked" &&
      save_action_ != "user-anchor-passive") {
    const auto action = (save_action_.starts_with("user-delete") || save_action_ == "user-resume" ||
                         save_action_ == "user-anchor" || save_action_ == "user-size-limit" ||
                         save_action_ == "user-save-link") ? "accept"
        : save_action_.starts_with("user-") ? save_action_.substr(5) : save_action_;
    dialog_factory_ = std::make_unique<ui::ScopedSetSelectFileDialogFactory>(
        std::make_unique<SaveProbeDialogFactory>(
            action, directory_, contents.GetWeakPtr(),
            base::BindRepeating([](DownloadProbe* probe, bool valid) {
              ++probe->picker_checks_;
              probe->picker_checked_ = valid;
            }, base::Unretained(this))));
  }
  content::NavigationController::LoadURLParams params(url_);
  if (save_action_.starts_with("user-"))
    params.transition_type = ui::PAGE_TRANSITION_TYPED;
  contents.GetController().LoadURLWithParams(params);
}

void DownloadProbe::DidFinishLoad(content::RenderFrameHost* frame, const GURL& url) {
  if (frame != web_contents()->GetPrimaryMainFrame() || url != url_)
    return;
  Observe(nullptr);
  if (save_action_ == "user-save-link") {
    auto* delegate = static_cast<ProfileDownloadDelegate*>(
        frame->GetBrowserContext()->GetDownloadManagerDelegate());
    if (!delegate || !delegate->SaveLink(*frame, target_url_))
      Finish(false);
    return;
  }
  constexpr char16_t script[] = u"document.querySelector('a[download]').click()";
  if (save_action_ == "user-anchor") {
    frame->ExecuteJavaScriptWithUserGestureForTests(
        script, base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
  } else {
    frame->ExecuteJavaScriptForTests(script, base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
  }
}

void DownloadProbe::OnDownloadCreated(content::DownloadManager*,
                                      download::DownloadItem* item) {
  if (checkpoint_phase_ == "reject") {
    Finish(false);
    return;
  }
  if (item->GetURL() != target_url_ || item_.IsObserving()) {
    Finish(false);
    return;
  }
  item_.Observe(item);
  if (checkpoint_phase_ == "restore") {
    if (item->GetState() != download::DownloadItem::INTERRUPTED ||
        !item->CanResume()) {
      Finish(false);
      return;
    }
    // History attaches the validated approval after Chromium publishes the item.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([](base::WeakPtr<DownloadProbe> probe) {
          if (probe && probe->item_.IsObserving())
            probe->item_.GetSource()->Resume(/*user_resume=*/true);
        }, weak_factory_.GetWeakPtr()));
    return;
  }
  OnDownloadUpdated(item);
}

void DownloadProbe::OnManagerInitialized() {
  if (checkpoint_phase_ != "reject")
    return;
  content::DownloadManager::DownloadVector items;
  manager_.GetSource()->GetAllDownloads(&items);
  Finish(items.empty());
}

void DownloadProbe::OnDownloadDropped(content::DownloadManager*) {
  const bool blocked = !certificate_.empty() &&
                       expected_result_ == ExpectedResult::kBlocked;
  if (blocked)
    std::puts("Zitter Content rejected the download request");
  Finish(blocked);
}

void DownloadProbe::OnDownloadUpdated(download::DownloadItem* item) {
  if (expected_result_ == ExpectedResult::kInterrupted &&
      item->GetState() == download::DownloadItem::INTERRUPTED &&
      item->CanResume()) {
    Finish(item->GetReceivedBytes() > 0 &&
           item->GetFullPath().DirName() == directory_);
    return;
  }
  if (expected_result_ == ExpectedResult::kPublishCollision &&
      item->GetState() == download::DownloadItem::INTERRUPTED) {
    Finish(item->GetTargetFilePath() == directory_.AppendASCII("chosen-save.txt") &&
           item->GetLastReason() == download::DOWNLOAD_INTERRUPT_REASON_FILE_FAILED);
    return;
  }
  if (!item->IsDone())
    return;
  if (save_action_ == "user-size-limit") {
    Finish(item->GetState() == download::DownloadItem::CANCELLED &&
           item->GetTargetFilePath() == directory_.AppendASCII("chosen-save.txt"));
    return;
  }
  if (save_action_.starts_with("user-delete")) {
    if (std::exchange(delete_started_, true))
      return;
    if (item->GetState() != download::DownloadItem::COMPLETE ||
        item->GetTargetFilePath() != directory_.AppendASCII("chosen-save.txt")) {
      Finish(false);
      return;
    }
    const bool change = save_action_ == "user-delete-changed";
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce([](const base::FilePath& path, bool change) {
          return !change || base::WriteFile(path, "Changed after download\n");
        }, item->GetTargetFilePath(), change),
        base::BindOnce([](base::WeakPtr<DownloadProbe> probe, bool change, bool ready) {
          if (!probe)
            return;
          auto* item = probe->item_.GetSource();
          if (!ready || !item) {
            probe->Finish(false);
            return;
          }
          auto* context = content::DownloadItemUtils::GetBrowserContext(item);
          auto* delegate = context ? static_cast<ProfileDownloadDelegate*>(
              context->GetDownloadManagerDelegate()) : nullptr;
          if (!delegate || !delegate->CanDeleteDownloadFile(*item)) {
            probe->Finish(false);
            return;
          }
          delegate->DeleteDownloadFile(*item, base::BindOnce(
              [](base::WeakPtr<DownloadProbe> probe, bool change, bool deleted) {
                if (probe)
                  probe->Finish(deleted != change);
              }, probe, change));
        }, weak_factory_.GetWeakPtr(), change));
    return;
  }
  if (expected_result_ == ExpectedResult::kResumeBlocked) {
    const bool rejected_file =
        item->GetState() == download::DownloadItem::INTERRUPTED &&
        item->GetLastReason() ==
            download::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED;
    Finish((item->GetState() == download::DownloadItem::CANCELLED ||
            rejected_file) &&
           item->GetTargetFilePath().DirName() == directory_);
    return;
  }
  if (expected_result_ == ExpectedResult::kFileThreat ||
      expected_result_ == ExpectedResult::kFileUnavailable) {
    const bool threat = expected_result_ == ExpectedResult::kFileThreat;
    const auto reason = threat
        ? download::DOWNLOAD_INTERRUPT_REASON_FILE_VIRUS_INFECTED
        : download::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED;
    const bool blocked = item->GetState() == download::DownloadItem::INTERRUPTED &&
                         item->GetLastReason() == reason && !item->CanResume() &&
                         item->GetTargetFilePath().DirName() == directory_;
    if (blocked)
      std::puts("Zitter Content rejected the completed download artifact");
    Finish(blocked);
    return;
  }
  if (expected_result_ == ExpectedResult::kPublicationChanged) {
    const bool blocked =
        item->GetState() == download::DownloadItem::INTERRUPTED &&
        item->GetLastReason() ==
            download::DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED &&
        !item->CanResume() && item->GetTargetFilePath().DirName() == directory_;
    if (blocked)
      std::puts("Zitter Content blocked publication of a changed file");
    Finish(blocked);
    return;
  }
  // Chromium maps an empty target path to a cancelled item.
  if (expected_result_ == ExpectedResult::kBlocked) {
    const bool blocked = item->GetState() == download::DownloadItem::CANCELLED &&
                         item->GetTargetFilePath().empty();
    if (blocked && !certificate_.empty())
      std::puts("Zitter Content rejected the download target");
    Finish(blocked);
    return;
  }
  const bool saved_at_destination = save_action_ == "user-outside"
      ? item->GetTargetFilePath() == directory_.DirName().AppendASCII("outside.txt")
      : item->GetTargetFilePath().DirName() == directory_;
  Finish(expected_result_ == ExpectedResult::kComplete &&
         item->GetState() == download::DownloadItem::COMPLETE && saved_at_destination);
}

void DownloadProbe::OnDownloadDestroyed(download::DownloadItem*) {
  item_.Reset();
  Finish(false);
}

void DownloadProbe::Finish(bool success) {
  if (!done_)
    return;
  if (success && (!dialog_factory_ || (picker_checked_ && picker_checks_ == 1)))
    std::puts("Zitter Content download probe passed");
  else
    LOG(ERROR) << "Unexpected Content download result";
  std::move(done_).Run();
}

}  // namespace zitter::content_embedder
