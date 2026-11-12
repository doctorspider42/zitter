#ifndef ZITTER_CHROMIUM_PROTOTYPE_HISTORY_PANEL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_HISTORY_PANEL_H_

#include <array>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_service_observer.h"
#include "ui/menus/simple_menu_model.h"
#include "url/gurl.h"

namespace views { class Widget; class View; class DialogDelegate; class MenuRunner; }
namespace zitter::prototype {
class EditorControl;
class PanelScene;
class PanelViewport;
// Design: Historia — the profile's visits by day with search, a menu per
// entry, choosing entries to delete, and the tabs closed in this run.
// Chromium's HistoryService owns the data; this is presentation only.
class HistoryPanel final : public history::HistoryServiceObserver, public ui::SimpleMenuModel::Delegate {
 public:
  enum class Open { kNewTab, kSplit, kPeek };
  struct Closed { std::u16string title; GURL url; std::u16string space; base::Time when; };
  // Deleting visits asks first: title, detail, warning, the action, the answer.
  using Confirm = base::RepeatingCallback<void(views::Widget* parent, std::u16string title, std::u16string detail,
                                               std::u16string warning, std::u16string action, base::OnceCallback<void(bool)> answer)>;
  HistoryPanel();
  ~HistoryPanel() override;
  void Show(history::HistoryService* history, std::u16string profile_label,
            views::Widget* parent, bool activate = true);
  views::Widget* widget() const { return widget_.get(); }
  void SetOpenCallback(base::RepeatingCallback<void(GURL, Open)> open) { open_ = std::move(open); }
  void DisableAuxiliaryOpenModes() { auxiliary_open_modes_ = false; }
  // The tabs closed in this run, newest first, and reopening one of them.
  void SetClosedSource(base::RepeatingCallback<std::vector<Closed>()> closed) { closed_ = std::move(closed); }
  void SetReopenCallback(base::RepeatingCallback<void(size_t)> reopen) { reopen_ = std::move(reopen); }
  void SetConfirmCallback(Confirm confirm) { confirm_ = std::move(confirm); }
  void SetClearDataCallback(base::RepeatingClosure clear) { clear_ = std::move(clear); }
  void SetTimeMachineCallback(base::RepeatingClosure time_machine) { time_machine_ = std::move(time_machine); }
  void SetQuery(std::u16string query);
  void SetShowClosed(bool closed);
  void RefreshClosedTabs();
  void SetChoosing(bool choosing);
  void Choose(size_t index, bool chosen);
  // Deletes the chosen visits (after asking).
  void DeleteChosen();
  // Testing: the listed visits' urls, and whether a query is still running.
  std::vector<GURL> listed_for_testing() const;
  bool querying_for_testing() const { return querying_; }
  bool has_visible_visit_title_for_testing(std::u16string_view title) const;
  bool scene_contains_text_for_testing(std::string_view text) const;
  void DeleteForTesting(size_t index) { DeleteVisits({index}); }
  void ActivateForTesting(size_t index) { Activate(index); }
  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  // history::HistoryServiceObserver:
  void OnHistoryDeletions(history::HistoryService*, const history::DeletionInfo&) override;
  void OnURLVisited(history::HistoryService*, const history::VisitedURLInfo&) override;
  void HistoryServiceBeingDeleted(history::HistoryService*) override;
 private:
  struct Visit { GURL url; std::u16string title; base::Time time; };
  struct Row;
  void CloseUI();
  void Query();
  void OnResults(history::QueryResults results);
  void Refresh();
  void RefreshChrome();
  views::View* MakeLine(views::View* parent, uint32_t style, int tone);
  void ShowRowMenu(size_t index, views::View* anchor);
  void Activate(size_t index);
  void DeleteVisits(std::vector<size_t> indexes);
  std::vector<history::ExpireHistoryArgs> VisitsToExpire(
      const std::vector<size_t>& indexes) const;
  void ExpireVisits(const std::vector<history::ExpireHistoryArgs>& visits);
  void DeleteSite(std::string host);
  raw_ptr<history::HistoryService> history_ = nullptr;
  base::ScopedObservation<history::HistoryService, history::HistoryServiceObserver> observation_{this};
  base::CancelableTaskTracker tracker_;
  base::CancelableTaskTracker query_tracker_;
  std::vector<Visit> visits_;
  std::u16string query_;
  bool show_closed_ = false, choosing_ = false, querying_ = false;
  bool auxiliary_open_modes_ = true;
  std::set<size_t> chosen_;
  size_t menu_index_ = 0;
  base::RepeatingCallback<void(GURL, Open)> open_;
  base::RepeatingCallback<std::vector<Closed>()> closed_;
  base::RepeatingCallback<void(size_t)> reopen_;
  Confirm confirm_;
  base::RepeatingClosure clear_, time_machine_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  std::unique_ptr<views::DialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<PanelScene> scene_;
  raw_ptr<PanelViewport> scroll_ = nullptr;
  raw_ptr<views::View> list_ = nullptr, count_ = nullptr, choose_ = nullptr, choosing_actions_ = nullptr, delete_ = nullptr;
  raw_ptr<views::View> search_row_ = nullptr;
  raw_ptr<EditorControl> search_ = nullptr;
  std::array<raw_ptr<views::View>, 2> tabs_ = {};
  std::vector<Row> rows_;
  std::vector<std::string> layout_;
  base::OneShotTimer query_timer_;  // Typing settles before a query runs.
  base::WeakPtrFactory<HistoryPanel> weak_factory_{this};
};
}  // namespace zitter::prototype
#endif
