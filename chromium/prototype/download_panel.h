#ifndef ZITTER_CHROMIUM_PROTOTYPE_DOWNLOAD_PANEL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_DOWNLOAD_PANEL_H_

#include <map>
#include <string>
#include <vector>
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/download_manager.h"
#include "components/download/public/common/download_item.h"

class Profile;
namespace views { class Widget; class View; }
namespace zitter::prototype {
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
  std::vector<std::string> visible_guids() const;
 private:
  struct Row;
  void Disconnect();
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
  base::WeakPtr<views::Widget> widget_;
  raw_ptr<views::View> list_ = nullptr;
  std::vector<Row> rows_;
  base::RepeatingTimer timer_;
  bool refresh_pending_ = false;
  base::WeakPtrFactory<DownloadPanel> weak_factory_{this};
};
}  // namespace zitter::prototype
#endif
