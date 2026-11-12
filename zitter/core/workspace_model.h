#ifndef ZITTER_CORE_WORKSPACE_MODEL_H_
#define ZITTER_CORE_WORKSPACE_MODEL_H_

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace zitter {

enum class LiveFolderProvider { kGitHub, kGitLab, kJira };
inline constexpr auto kLiveFolderProviders = std::to_array({
    LiveFolderProvider::kGitHub, LiveFolderProvider::kGitLab,
    LiveFolderProvider::kJira});

struct Workspace {
  std::string id;
  std::string profile_id;
  std::string name;
  std::array<bool, kLiveFolderProviders.size()> live_folders_enabled{true, true, true};

  bool LiveFoldersEnabled(LiveFolderProvider provider) const {
    return live_folders_enabled[static_cast<std::size_t>(provider)];
  }
};

// Product metadata only. Chromium owns profiles and live page state.
class WorkspaceModel {
 public:
  WorkspaceModel();
  ~WorkspaceModel();
  bool Add(Workspace workspace);
  bool Remove(std::string_view id);
  bool Update(std::string_view id, std::string name, std::string profile_id);
  bool SetLiveFoldersEnabled(std::string_view id, LiveFolderProvider provider, bool enabled);
  const Workspace* Find(std::string_view id) const;
  const Workspace* active() const;
  bool SetActive(std::string_view id);
  const std::vector<Workspace>& workspaces() const { return workspaces_; }

 private:
  std::vector<Workspace> workspaces_;
  std::string active_id_;
};

}  // namespace zitter
#endif
