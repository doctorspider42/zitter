// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "file_system_access.h"

#include <array>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/observer_list.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "chromium/prototype/i18n.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/file_system_access_permission_grant.h"
#include "content/public/browser/file_system_access_write_item.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "third_party/blink/public/mojom/file_system_access/file_system_access_manager.mojom.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

#if BUILDFLAG(IS_WIN)
#include <knownfolders.h>
#include <shlobj.h>

#include "base/win/scoped_co_mem.h"
#endif

namespace zitter::content_embedder {

constexpr char kGuard[] = "file-system-write-guard";

using PermissionStatus = blink::mojom::PermissionStatus;
using Outcome = content::FileSystemAccessPermissionGrant::PermissionRequestOutcome;

class FileAccessGrant final : public content::FileSystemAccessPermissionGrant {
 public:
  FileAccessGrant(base::WeakPtr<LabFileSystemAccess> owner,
                  content::BrowserContext& context,
                  url::Origin origin,
                  content::PathInfo path,
                  bool write,
                  PermissionStatus status)
      : owner_(std::move(owner)),
        context_(context.GetWeakPtr()),
        origin_(std::move(origin)),
        path_(std::move(path)),
        write_(write),
        status_(status) {}

  PermissionStatus GetStatus() override { return status_; }
  base::FilePath GetPath() override { return path_.path; }
  std::string GetDisplayName() override { return path_.display_name; }

  void RequestPermission(content::GlobalRenderFrameHostId frame_id,
                         UserActivationState user_activation_state,
                         base::OnceCallback<void(Outcome)> callback) override {
    if (status_ == PermissionStatus::GRANTED) {
      std::move(callback).Run(Outcome::kGrantedByContentSetting);
      return;
    }
    if (!owner_ || !context_ || (write_ && owner_->WriteBlocked(origin_))) {
      std::move(callback).Run(Outcome::kBlockedByContentSetting);
      return;
    }
    auto* frame = content::RenderFrameHost::FromID(frame_id);
    if (!frame || !frame->IsActive()) {
      std::move(callback).Run(Outcome::kInvalidFrame);
      return;
    }
    if (frame->GetOutermostMainFrame()->GetLastCommittedOrigin() != origin_ &&
        frame->GetLastCommittedOrigin() != origin_) {
      std::move(callback).Run(Outcome::kThirdPartyContext);
      return;
    }
    if (user_activation_state == UserActivationState::kRequired &&
        !frame->HasTransientUserActivation()) {
      std::move(callback).Run(Outcome::kNoUserActivation);
      return;
    }
    static_cast<ProfileContext*>(context_.get())
        ->site_permissions()
        .RunSitePrompt(*frame, kGuard,
                       base::BindOnce(
                           [](scoped_refptr<FileAccessGrant> self,
                              base::OnceCallback<void(Outcome)> callback, bool allowed) {
                             self->SetStatus(allowed ? PermissionStatus::GRANTED
                                                     : PermissionStatus::DENIED);
                             std::move(callback).Run(allowed ? Outcome::kUserGranted
                                                             : Outcome::kUserDenied);
                           },
                           base::WrapRefCounted(this), std::move(callback)));
  }

  void SetStatus(PermissionStatus status) {
    if (status_ == status)
      return;
    status_ = status;
    NotifyPermissionStatusChanged();
  }

  void Grant() { SetStatus(PermissionStatus::GRANTED); }
  void Move(content::PathInfo path) { path_ = std::move(path); }
  const url::Origin& origin() const { return origin_; }
  const content::PathInfo& path() const { return path_; }
  bool write() const { return write_; }

 private:
  ~FileAccessGrant() override {
    if (owner_)
      owner_->Forget(*this);
  }

  base::WeakPtr<LabFileSystemAccess> owner_;
  base::WeakPtr<content::BrowserContext> context_;
  url::Origin origin_;
  content::PathInfo path_;
  const bool write_;
  PermissionStatus status_;
};

namespace {

// Folders that hold the system, programs and app data: a site may neither
// read nor write them, nor pick the home folder or a drive's root as a whole.
bool IsSensitive(const base::FilePath& path, bool directory) {
  if (path.empty())
    return true;
  if (directory && (path.DirName() == path))
    return true;
  base::FilePath home;
  if (base::PathService::Get(base::DIR_HOME, &home) && directory && path == home)
    return true;
  std::vector<int> recursive = {base::DIR_TEMP};
#if BUILDFLAG(IS_WIN)
  recursive.insert(recursive.end(),
                   {base::DIR_WINDOWS, base::DIR_PROGRAM_FILES, base::DIR_PROGRAM_FILESX86,
                    base::DIR_PROGRAM_FILES6432, base::DIR_COMMON_APP_DATA,
                    base::DIR_LOCAL_APP_DATA, base::DIR_ROAMING_APP_DATA});
#elif BUILDFLAG(IS_MAC)
  for (const char* system : {"/System", "/Library", "/usr", "/bin", "/sbin", "/private"}) {
    const base::FilePath blocked(system);
    if (path == blocked || blocked.IsParent(path))
      return true;
  }
  if (!home.empty()) {
    const auto library = home.AppendASCII("Library");
    if (path == library || library.IsParent(path))
      return true;
  }
#endif
  for (int key : recursive) {
    base::FilePath blocked;
    if (base::PathService::Get(key, &blocked) && (path == blocked || blocked.IsParent(path)))
      return true;
  }
  return false;
}

void ShowSensitiveNotice(content::GlobalRenderFrameHostId frame_id, base::OnceClosure done) {
  auto* frame = content::RenderFrameHost::FromID(frame_id);
  auto* contents = frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  if (!contents || !contents->GetTopLevelNativeWindow()) {
    std::move(done).Run();
    return;
  }
  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetTitle(prototype::T(u"Nie można otworzyć tego folderu"));
  dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, prototype::T(u"Wybierz inny"));
  auto finished = base::SplitOnceCallback(std::move(done));
  dialog->SetAcceptCallback(std::move(finished.first));
  dialog->SetCloseCallback(std::move(finished.second));
  auto label = std::make_unique<views::Label>(prototype::T(
      u"Zawiera pliki systemu lub programów. Wybierz folder ze swoimi dokumentami."));
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  dialog->SetContentsView(std::move(label));
  views::DialogDelegate::CreateDialogWidget(std::move(dialog), nullptr,
                                            contents->GetTopLevelNativeWindow())
      ->Show();
}

}  // namespace

LabFileSystemAccess::LabFileSystemAccess(content::BrowserContext& context)
    : context_(context) {}

LabFileSystemAccess::~LabFileSystemAccess() = default;

bool LabFileSystemAccess::WriteBlocked(const url::Origin& origin) const {
  return static_cast<ProfileContext&>(*context_).site_permissions().SiteSetting(kGuard, origin) ==
         2;
}

void LabFileSystemAccess::Forget(FileAccessGrant& grant) {
  std::erase_if(grants_, [&](const auto& entry) { return entry.second == &grant; });
}

scoped_refptr<content::FileSystemAccessPermissionGrant> LabFileSystemAccess::Grant(
    const url::Origin& origin, const content::PathInfo& path_info, HandleType handle_type,
    bool write, bool granted) {
  const Key key{origin, path_info.path, handle_type == HandleType::kDirectory, write};
  if (const auto found = grants_.find(key); found != grants_.end()) {
    if (granted)
      found->second->Grant();
    return found->second.get();
  }
  const auto status = write && WriteBlocked(origin) ? PermissionStatus::DENIED
                      : granted                     ? PermissionStatus::GRANTED
                                                    : PermissionStatus::ASK;
  auto grant = base::MakeRefCounted<FileAccessGrant>(weak_factory_.GetWeakPtr(), *context_,
                                                     origin, path_info, write, status);
  grants_[key] = grant.get();
  return grant;
}

scoped_refptr<content::FileSystemAccessPermissionGrant>
LabFileSystemAccess::GetReadPermissionGrant(const url::Origin& origin,
                                            const content::PathInfo& path_info,
                                            HandleType handle_type,
                                            AccessTrigger access_trigger) {
  // The user chose the entry in a picker or dropped it on the page.
  const bool chosen = access_trigger == AccessTrigger::kOpen ||
                      access_trigger == AccessTrigger::kSave ||
                      access_trigger == AccessTrigger::kDragAndDrop;
  return Grant(origin, path_info, handle_type, /*write=*/false, chosen);
}

scoped_refptr<content::FileSystemAccessPermissionGrant>
LabFileSystemAccess::GetWritePermissionGrant(const url::Origin& origin,
                                             const content::PathInfo& path_info,
                                             HandleType handle_type,
                                             AccessTrigger access_trigger) {
  // A save picker is the user's consent to write that file.
  return Grant(origin, path_info, handle_type, /*write=*/true,
               access_trigger == AccessTrigger::kSave);
}

void LabFileSystemAccess::ConfirmSensitiveEntryAccess(
    const url::Origin& origin,
    const content::PathInfo& path_info,
    HandleType handle_type,
    AccessTrigger access_trigger,
    content::GlobalRenderFrameHostId frame_id,
    base::OnceCallback<void(SensitiveEntryResult)> callback) {
  if (path_info.type == content::PathType::kExternal ||
      !IsSensitive(path_info.path, handle_type == HandleType::kDirectory)) {
    std::move(callback).Run(SensitiveEntryResult::kAllowed);
    return;
  }
  if (access_trigger == AccessTrigger::kDragAndDrop) {
    std::move(callback).Run(SensitiveEntryResult::kAbort);
    return;
  }
  ShowSensitiveNotice(frame_id, base::BindOnce(std::move(callback),
                                               SensitiveEntryResult::kTryAgain));
}

void LabFileSystemAccess::PerformAfterWriteChecks(
    std::unique_ptr<content::FileSystemAccessWriteItem> item,
    content::GlobalRenderFrameHostId,
    base::OnceCallback<void(AfterWriteCheckResult)> callback) {
  std::move(callback).Run(item && IsFileTypeDangerous(item->target_file_path)
                              ? AfterWriteCheckResult::kBlock
                              : AfterWriteCheckResult::kAllow);
}

bool LabFileSystemAccess::IsFileTypeDangerous(const base::FilePath& path) {
  static constexpr auto kDangerous = std::to_array<std::string_view>({
      "app",  "appref-ms", "application", "bat", "cmd",  "com",   "command", "cpl",
      "dll",  "exe",       "hta",         "inf", "jar",  "js",    "jse",     "lnk",
      "msc",  "msi",       "msp",         "pif", "ps1",  "psm1",  "reg",     "scf",
      "scr",  "sh",        "url",         "vb",  "vbe",  "vbs",   "website", "ws",
      "wsc",  "wsf",       "wsh",         "pkg", "dmg",  "terminal", "workflow",
  });
  const auto extension = base::ToLowerASCII(path.FinalExtension());
  return extension.size() > 1 &&
         std::ranges::contains(kDangerous, std::string_view(extension).substr(1));
}

base::expected<void, std::string> LabFileSystemAccess::CanShowFilePicker(
    content::RenderFrameHost*) {
  return base::ok();
}

bool LabFileSystemAccess::CanObtainReadPermission(const url::Origin&) {
  return true;
}

bool LabFileSystemAccess::CanObtainWritePermission(const url::Origin& origin) {
  return !WriteBlocked(origin);
}

void LabFileSystemAccess::SetLastPickedDirectory(const url::Origin& origin,
                                                 const std::string& id,
                                                 const content::PathInfo& path_info) {
  last_picked_[{origin, id}] = path_info;
}

content::PathInfo LabFileSystemAccess::GetLastPickedDirectory(const url::Origin& origin,
                                                              const std::string& id) {
  const auto found = last_picked_.find({origin, id});
  return found == last_picked_.end() ? content::PathInfo() : found->second;
}

base::FilePath LabFileSystemAccess::GetWellKnownDirectoryPath(
    blink::mojom::WellKnownDirectory directory,
    const url::Origin&) {
  using Directory = blink::mojom::WellKnownDirectory;
  if (directory == Directory::kDirDesktop) {
    base::FilePath desktop;
    if (base::PathService::Get(base::DIR_USER_DESKTOP, &desktop))
      return desktop;
  }
#if BUILDFLAG(IS_WIN)
  const KNOWNFOLDERID* folder = nullptr;
  switch (directory) {
    case Directory::kDirDesktop: folder = &FOLDERID_Desktop; break;
    case Directory::kDirDocuments: folder = &FOLDERID_Documents; break;
    case Directory::kDirDownloads: folder = &FOLDERID_Downloads; break;
    case Directory::kDirMusic: folder = &FOLDERID_Music; break;
    case Directory::kDirPictures: folder = &FOLDERID_Pictures; break;
    case Directory::kDirVideos: folder = &FOLDERID_Videos; break;
  }
  base::win::ScopedCoMem<wchar_t> path;
  if (folder && SUCCEEDED(::SHGetKnownFolderPath(*folder, 0, nullptr, &path)))
    return base::FilePath(path.get());
#endif
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home))
    return {};
  switch (directory) {
    case Directory::kDirDesktop: return home.AppendASCII("Desktop");
    case Directory::kDirDocuments: return home.AppendASCII("Documents");
    case Directory::kDirDownloads: return home.AppendASCII("Downloads");
    case Directory::kDirMusic: return home.AppendASCII("Music");
    case Directory::kDirPictures: return home.AppendASCII("Pictures");
    case Directory::kDirVideos: return home.AppendASCII("Movies");
  }
  return home;
}

std::u16string LabFileSystemAccess::GetPickerTitle(const blink::mojom::FilePickerOptionsPtr&) {
  return {};
}

void LabFileSystemAccess::NotifyEntryMoved(const url::Origin& origin,
                                           const content::PathInfo& old_path,
                                           const content::PathInfo& new_path) {
  std::map<Key, raw_ptr<FileAccessGrant>> moved;
  for (auto it = grants_.begin(); it != grants_.end();) {
    if (std::get<0>(it->first) == origin && std::get<1>(it->first) == old_path.path) {
      it->second->Move(new_path);
      moved[{origin, new_path.path, std::get<2>(it->first), std::get<3>(it->first)}] = it->second;
      it = grants_.erase(it);
    } else {
      ++it;
    }
  }
  grants_.merge(moved);
}

void LabFileSystemAccess::NotifyEntryModified(const url::Origin&, const content::PathInfo&) {}

void LabFileSystemAccess::NotifyEntryRemoved(const url::Origin&, const content::PathInfo&) {}

void LabFileSystemAccess::OnFileCreatedFromShowSaveFilePicker(const GURL&,
                                                              const storage::FileSystemURL&) {}

void LabFileSystemAccess::CheckPathsAgainstEnterprisePolicy(
    std::vector<content::PathInfo> entries,
    content::GlobalRenderFrameHostId,
    EntriesAllowedByEnterprisePolicyCallback callback) {
  // No enterprise data controls in Zitter.
  std::move(callback).Run(std::move(entries));
}

}  // namespace zitter::content_embedder
