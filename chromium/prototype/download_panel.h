#ifndef ZITTER_CHROMIUM_PROTOTYPE_DOWNLOAD_PANEL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_DOWNLOAD_PANEL_H_

#include <map>
#include <set>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/download_manager.h"
#include "components/download/public/common/download_item.h"

class Profile;
namespace views { class Widget; class View; class ScrollView; class DialogDelegate; }
namespace zitter::prototype {
class PanelScene;
class PanelViewport;
// Presentation only: Chromium owns transfers, history, policy and file safety.
class DownloadPanel final : public content::DownloadManager::Observer,
                            public download::DownloadItem::Observer {
 public:
  enum class Action { kPauseResume, kCancel, kReveal };
  DownloadPanel();
  ~DownloadPanel() override;
  void SetProfile(Profile* profile);
  void Show(Profile* profile, views::Widget* parent);
  views::Widget* widget() const { return widget_.get(); }
  bool Act(std::string guid, Action action);
  // Runs when a transfer this panel saw in progress finishes safely.
  void SetCompletedCallback(base::RepeatingCallback<void(std::string guid, std::u16string name)> completed) { completed_ = std::move(completed); }
  std::vector<std::string> visible_guids() const;
  bool scene_visible_for_testing() const;
  bool scene_contains_text_for_testing(std::string_view text) const;
  bool scene_animating_for_testing() const;
  bool scene_order_correct_for_testing() const;
  PanelViewport* viewport_for_testing() const{return scroll_;}
  views::View* list_for_testing() const{return list_;}
 private:
  struct Row;
  void Disconnect();
  void CloseUI();
  views::View* MakeLabel(views::View* parent,std::u16string text);
  views::View* MakeLine(views::View* parent,uint32_t style,int tone);
  void SetText(views::View* view,std::u16string text);
  void Observe(download::DownloadItem* item);
  void ScheduleRefresh();
  void Refresh();
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
  std::unique_ptr<views::DialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<PanelScene> scene_;
  raw_ptr<PanelViewport> scroll_=nullptr;
  raw_ptr<views::View> list_ = nullptr;
  raw_ptr<views::View> count_ = nullptr;
  std::vector<Row> rows_;
  base::RepeatingTimer timer_;
  bool refresh_pending_ = false;
  base::WeakPtrFactory<DownloadPanel> weak_factory_{this};
};
}  // namespace zitter::prototype
#endif
