// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// File System Access permissions for a Content profile. Without a context
// Content allows every write and skips the sensitive-folder checks; this one
// grants what the user picked, asks before other access (Settings > Site
// permissions > File editing), keeps sites out of system folders and refuses
// executable file types.

#ifndef ZITTER_CONTENT_FILE_SYSTEM_ACCESS_H_
#define ZITTER_CONTENT_FILE_SYSTEM_ACCESS_H_

#include <map>
#include <string>
#include <tuple>

#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/file_system_access_permission_context.h"

namespace content {
class BrowserContext;
}

namespace zitter::content_embedder {

class FileAccessGrant;

class LabFileSystemAccess final : public content::FileSystemAccessPermissionContext {
 public:
  explicit LabFileSystemAccess(content::BrowserContext& context);
  ~LabFileSystemAccess() override;

  scoped_refptr<content::FileSystemAccessPermissionGrant> GetReadPermissionGrant(
      const url::Origin& origin,
      const content::PathInfo& path_info,
      HandleType handle_type,
      AccessTrigger access_trigger) override;
  scoped_refptr<content::FileSystemAccessPermissionGrant> GetWritePermissionGrant(
      const url::Origin& origin,
      const content::PathInfo& path_info,
      HandleType handle_type,
      AccessTrigger access_trigger) override;
  void ConfirmSensitiveEntryAccess(const url::Origin& origin,
                                   const content::PathInfo& path_info,
                                   HandleType handle_type,
                                   AccessTrigger access_trigger,
                                   content::GlobalRenderFrameHostId frame_id,
                                   base::OnceCallback<void(SensitiveEntryResult)> callback) override;
  void PerformAfterWriteChecks(std::unique_ptr<content::FileSystemAccessWriteItem> item,
                               content::GlobalRenderFrameHostId frame_id,
                               base::OnceCallback<void(AfterWriteCheckResult)> callback) override;
  bool IsFileTypeDangerous(const base::FilePath& path) override;
  base::expected<void, std::string> CanShowFilePicker(content::RenderFrameHost* rfh) override;
  bool CanObtainReadPermission(const url::Origin& origin) override;
  bool CanObtainWritePermission(const url::Origin& origin) override;
  void SetLastPickedDirectory(const url::Origin& origin,
                              const std::string& id,
                              const content::PathInfo& path_info) override;
  content::PathInfo GetLastPickedDirectory(const url::Origin& origin,
                                           const std::string& id) override;
  base::FilePath GetWellKnownDirectoryPath(blink::mojom::WellKnownDirectory directory,
                                           const url::Origin& origin) override;
  std::u16string GetPickerTitle(const blink::mojom::FilePickerOptionsPtr& options) override;
  void NotifyEntryMoved(const url::Origin& origin,
                        const content::PathInfo& old_path,
                        const content::PathInfo& new_path) override;
  void NotifyEntryModified(const url::Origin& origin, const content::PathInfo& path) override;
  void NotifyEntryRemoved(const url::Origin& origin, const content::PathInfo& path) override;
  void OnFileCreatedFromShowSaveFilePicker(const GURL& file_picker_binding_context,
                                           const storage::FileSystemURL& url) override;
  void CheckPathsAgainstEnterprisePolicy(
      std::vector<content::PathInfo> entries,
      content::GlobalRenderFrameHostId frame_id,
      EntriesAllowedByEnterprisePolicyCallback callback) override;

  bool WriteBlocked(const url::Origin& origin) const;
  void Forget(FileAccessGrant& grant);

 private:
  // Origin, path, directory?, write?
  using Key = std::tuple<url::Origin, base::FilePath, bool, bool>;
  scoped_refptr<content::FileSystemAccessPermissionGrant> Grant(
      const url::Origin& origin, const content::PathInfo& path_info, HandleType handle_type,
      bool write, bool granted);

  const raw_ref<content::BrowserContext> context_;
  std::map<Key, raw_ptr<FileAccessGrant>> grants_;
  std::map<std::pair<url::Origin, std::string>, content::PathInfo> last_picked_;
  base::WeakPtrFactory<LabFileSystemAccess> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_FILE_SYSTEM_ACCESS_H_
