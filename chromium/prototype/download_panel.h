#ifndef ZITTER_CHROMIUM_PROTOTYPE_DOWNLOAD_PANEL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_DOWNLOAD_PANEL_H_

#include <array>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "url/gurl.h"
#include "content/public/browser/download_manager.h"
#include "components/download/public/common/download_item.h"

class Profile;
namespace views { class Widget; class View; class ScrollView; class DialogDelegate; class MenuRunner; }
namespace zitter::prototype {
class EditorControl;
class PanelScene;
class PanelViewport;
// Presentation only: Chromium owns transfers, history, policy and file safety.
// Design: Okno Pobrane — search, state and type filters, day groups, a menu
// per file, choosing several files, and clearing the list.
class DownloadPanel final : public content::DownloadManager::Observer,
                            public download::DownloadItem::Observer,
                            public ui::SimpleMenuModel::Delegate {
 public:
  enum class Action { kPauseResume, kCancel, kReveal, kOpen, kCopyLink, kRetry, kRemove, kDelete };
  // The state chips: all, in progress, done, interrupted (with cancelled).
  enum class StateFilter { kAll, kActive, kDone, kFailed };
  // What „Wyczyść” removes from the list (never from the disk).
  enum class Clear { kDone, kFailed, kOlderThan30Days, kMissing, kAll };
  DownloadPanel();
  ~DownloadPanel() override;
  void SetProfile(Profile* profile);
  // `anchor` (screen) is the ring or pill the list opens above; empty centres it.
  void Show(Profile* profile, views::Widget* parent, gfx::Rect anchor = {});
  views::Widget* widget() const { return widget_.get(); }
  bool Act(std::string guid, Action action);
  // Runs when a transfer this panel saw in progress finishes safely.
  void SetCompletedCallback(base::RepeatingCallback<void(std::string guid, std::u16string name)> completed) { completed_ = std::move(completed); }
  // Runs when a new transfer starts in the watched profile, with its file name.
  void SetStartedCallback(base::RepeatingCallback<void(std::u16string name)> started) { started_ = std::move(started); }
  // Runs as transfers move: how many are in progress, the share of their bytes
  // that arrived (< 0 while a size is unknown) and the newest one's name.
  void SetProgressCallback(base::RepeatingCallback<void(int active, double fraction, std::u16string newest)> progress) { progress_ = std::move(progress); }
  // Files left the list: the host shows a toast whose action runs `undo`.
  void SetRemovedCallback(base::RepeatingCallback<void(int count, base::OnceClosure undo)> removed) { removed_ = std::move(removed); }
  // Asks before deleting files: title, detail, warning, the action, the answer.
  using Confirm = base::RepeatingCallback<void(views::Widget* parent, std::u16string title, std::u16string detail,
                                               std::u16string warning, std::u16string action, base::OnceCallback<void(bool)> answer)>;
  void SetConfirmCallback(Confirm confirm) { confirm_ = std::move(confirm); }
  // Starts the file's address again (a file gone from the disk).
  void SetRedownloadCallback(base::RepeatingCallback<void(GURL)> redownload) { redownload_ = std::move(redownload); }
  // „Zmień” beside the downloads folder.
  void SetChangeFolderCallback(base::RepeatingClosure change_folder) { change_folder_ = std::move(change_folder); }
  // The files the filters show, newest first.
  std::vector<std::string> visible_guids() const;
  void SetQuery(std::u16string query);
  void SetStateFilter(StateFilter filter);
  // 0 every kind, else sidebar::download_category + 1.
  void SetCategory(uint32_t category);
  void SetChoosing(bool choosing);
  bool choosing() const { return choosing_; }
  void Choose(const std::string& guid, bool chosen);
  const std::set<std::string>& chosen() const { return chosen_; }
  // Removes the chosen files from the list, with an undo toast.
  void RemoveChosen();
  // Asks, then deletes the chosen files from the disk and the list.
  void DeleteChosen();
  void ClearList(Clear what);
  // How many files the chips and the type menu count (search and other filters applied).
  int count_for_testing(StateFilter filter) const;
  std::u16string folder_for_testing() const;
  bool scene_visible_for_testing() const;
  bool scene_contains_text_for_testing(std::string_view text) const;
  bool scene_animating_for_testing() const;
  bool scene_order_correct_for_testing() const;
  PanelViewport* viewport_for_testing() const{return scroll_;}
  views::View* list_for_testing() const{return list_;}
  EditorControl* search_for_testing() const{return search_;}
  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;
 private:
  struct Row;
  struct Facts;
  void Disconnect();
  void CloseUI();
  views::View* MakeLabel(views::View* parent,std::u16string text);
  views::View* MakeLine(views::View* parent,uint32_t style,int tone);
  void SetText(views::View* view,std::u16string text);
  void Observe(download::DownloadItem* item);
  void ScheduleRefresh();
  void ReportProgress();
  void Refresh();
  void RefreshChrome(size_t shown, size_t total);
  bool Passes(download::DownloadItem* item, bool state, bool category) const;
  std::vector<download::DownloadItem*> Listed() const;
  void ShowMenu(std::unique_ptr<ui::SimpleMenuModel> model, views::View* anchor);
  void ShowRowMenu(std::string guid, views::View* anchor);
  void ShowTypeMenu(views::View* anchor);
  void ShowClearMenu(views::View* anchor);
  void OpenFolder();
  // Hides `guids` now and removes them for good once the undo toast is gone.
  void RemoveLater(std::vector<std::string> guids);
  void CommitRemoval();
  void UndoRemoval(uint64_t batch);
  void Delete(std::vector<std::string> guids);
  void Deleted(std::string guid, bool deleted);
  void OnDownloadCreated(content::DownloadManager*, download::DownloadItem*) override;
  void OnManagerInitialized() override;
  void ManagerGoingDown(content::DownloadManager*) override;
  void OnDownloadUpdated(download::DownloadItem*) override;
  void OnDownloadRemoved(download::DownloadItem*) override;
  void OnDownloadDestroyed(download::DownloadItem*) override;
  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<content::DownloadManager> manager_ = nullptr;
  std::map<std::string, raw_ptr<download::DownloadItem>> items_;
  std::set<std::string> in_progress_;  // Seen transferring: their completion is news.
  base::RepeatingCallback<void(std::string, std::u16string)> completed_;
  base::RepeatingCallback<void(std::u16string)> started_;
  base::RepeatingCallback<void(int, double, std::u16string)> progress_;
  base::RepeatingCallback<void(int, base::OnceClosure)> removed_;
  Confirm confirm_;
  base::RepeatingCallback<void(GURL)> redownload_;
  base::RepeatingClosure change_folder_;
  bool initialized_ = false;  // History loaded: later creations are new transfers.
  // Filters and choosing.
  std::u16string query_;
  StateFilter state_ = StateFilter::kAll;
  uint32_t category_ = 0;
  bool choosing_ = false;
  std::set<std::string> chosen_;
  // Removed from the list but still undoable: hidden until `removal_timer_`.
  std::set<std::string> hidden_;
  uint64_t removal_batch_ = 0;
  base::OneShotTimer removal_timer_;
  std::string menu_guid_;  // The file whose „⋯” menu is open.
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  // The window.
  std::unique_ptr<views::DialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<PanelScene> scene_;
  raw_ptr<PanelViewport> scroll_=nullptr;
  raw_ptr<views::View> list_ = nullptr;
  raw_ptr<views::View> count_ = nullptr;
  raw_ptr<views::View> choose_ = nullptr, folder_ = nullptr, choosing_actions_ = nullptr, choose_all_ = nullptr;
  raw_ptr<EditorControl> search_ = nullptr;
  std::array<raw_ptr<views::View>, 4> chips_ = {};
  raw_ptr<views::View> type_ = nullptr;
  raw_ptr<views::View> footer_ = nullptr, clear_ = nullptr, path_ = nullptr;
  raw_ptr<views::View> bulk_ = nullptr, bulk_size_ = nullptr, bulk_delete_ = nullptr;
  std::vector<Row> rows_;
  std::vector<std::string> layout_;  // Group labels and guids the list was built for.
  base::RepeatingTimer timer_;
  bool refresh_pending_ = false;
  base::WeakPtrFactory<DownloadPanel> weak_factory_{this};  // UI callbacks; reset with the window.
  base::WeakPtrFactory<DownloadPanel> model_weak_factory_{this};
};
}  // namespace zitter::prototype
#endif
