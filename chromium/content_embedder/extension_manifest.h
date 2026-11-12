// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_EXTENSION_MANIFEST_H_
#define ZITTER_CONTENT_EXTENSION_MANIFEST_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/values.h"

class GURL;
class URLPattern;

namespace base {
class FilePath;
}

namespace extensions {
class Extension;
namespace declarative_net_request {
class FileBackedRulesetSource;
}
}

namespace network::mojom {
class URLLoaderFactory;
}

namespace zitter::content_embedder {

inline constexpr char kExtensionManifestProbe[] =
    "zitter-content-extension-manifest-probe";
inline constexpr char kExtensionRegistryProbe[] =
    "zitter-content-extension-registry-probe";
inline constexpr char kExtensionOptionalGrants[] = "optional-grants.json";
inline constexpr char kRestoreWorkspaceExtensions[] =
    "zitter-content-restore-extensions";
inline constexpr char kExtensionInstallReview[] =
    "zitter-content-extension-install-review";
inline constexpr char kExtensionInstallUiProbe[] =
    "zitter-content-extension-install-ui-probe";
inline constexpr char kExtensionInstallUiCancel[] =
    "zitter-content-extension-install-ui-cancel";
inline constexpr char kExtensionFixtureUpdate[] =
    "zitter-content-extension-update-probe";
inline constexpr char kExtensionFixtureKeyHash[] =
    "zitter-content-extension-key-sha256";
inline constexpr char kExtensionFixtureProfile[] =
    "zitter-content-extension-profile";
inline constexpr char kExtensionFixturePeer[] =
    "zitter-content-extension-peer-probe";
inline constexpr char kExtensionFixtureSibling[] =
    "zitter-content-extension-sibling-probe";
inline constexpr char kExtensionFixtureSiblingKeyHash[] =
    "zitter-content-extension-sibling-key-sha256";
inline constexpr char kExtensionFixtureState[] =
    "zitter-content-extension-state-probe";

enum class ExtensionFixtureStateAction {
  kNone,
  kDisable,
  kPauseAfterDisableCommit,
  kCheckDisabled,
  kEnable,
  kManage,
  kManageSmoke
};
std::optional<ExtensionFixtureStateAction> GetExtensionFixtureStateAction();
bool IsSupportedExtensionHostGrant(const URLPattern& pattern);
bool IsManagedExtensionSession();
bool IsWorkspaceExtensionRuntime();
bool IsWorkspaceExtensionReviewOnly();
struct WorkspaceExtensionRuleIndexes {
  base::DictValue static_rules;
  std::optional<int> dynamic_checksum;
  bool had_recovery_marker;
};
std::optional<WorkspaceExtensionRuleIndexes> PrepareVerifiedExtensionRuleIndexes(
    const extensions::Extension& extension,
    const extensions::declarative_net_request::FileBackedRulesetSource& dynamic_rules,
    bool allow_legacy_dynamic_rules);
bool PersistExtensionFixtureEnabled(const base::FilePath& version_path, bool enabled);
bool SetWorkspaceExtensionUninstallUrl(const base::FilePath& profile,
                                      const extensions::Extension& extension,
                                      std::string_view value);
GURL ReadWorkspaceExtensionUninstallUrl(
    const base::FilePath& profile,
    const extensions::Extension& extension);
std::optional<std::string> ReadPendingExtensionUpdate(
    const extensions::Extension& extension);
void ClearPendingExtensionUpdate(const extensions::Extension& extension);
bool RemoveDisabledExtensionInstallation(const base::FilePath& profile,
                                         const base::FilePath& version_path);

// Owns a verified candidate until the browser accepts or discards it.
class WorkspaceExtensionInstall {
 public:
  using Review = base::OnceCallback<void(std::optional<base::DictValue>)>;

  static std::unique_ptr<WorkspaceExtensionInstall> Start(
      const base::FilePath& source,
      const base::FilePath& profile,
      std::vector<scoped_refptr<const extensions::Extension>> installed,
      Review review);
  static std::unique_ptr<WorkspaceExtensionInstall> FromWebStore(
      network::mojom::URLLoaderFactory* factory, std::string id,
      std::string installed_version, const base::FilePath& profile,
      std::vector<scoped_refptr<const extensions::Extension>> installed,
      Review review);
  virtual ~WorkspaceExtensionInstall() = default;

  // Call only after accepting the review. One candidate can be published once.
  virtual scoped_refptr<const extensions::Extension> Publish() = 0;

 protected:
  WorkspaceExtensionInstall() = default;
};

void InitializeLabExtensionsClient();

// Runs before browser startup, after Content has registered URL schemes.
std::optional<int> ConfigureExtensionProbes();

// Called while the validated temporary Workspace root is locked.
bool ConfigureWorkspaceExtensionProbe(base::span<const base::FilePath> profiles);
// Restore verified installations before opening ordinary Workspace profiles.
void RestoreApprovedWorkspaceExtensions(base::span<const base::FilePath> profiles);
std::optional<size_t> ExtensionFixtureProfileIndex();

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_EXTENSION_MANIFEST_H_
