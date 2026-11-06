// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_RSS_READER_H_
#define ZITTER_CHROMIUM_PROTOTYPE_RSS_READER_H_
#include <memory>
#include <string>
#include <vector>
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/scoped_observation.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "url/gurl.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/accelerator_manager.h"
class Profile;
namespace network { class SimpleURLLoader; }
namespace views { class Widget; class View; }
namespace zitter::prototype {
class PanelScene;
class EditorControl;
class PanelViewport;
struct RssArticle {
  std::string id, title, text, date;
  GURL url;
  bool read = false, saved = false;
  base::Time published;
};
struct RssFeed {
  GURL url;
  std::string title, error;
  std::vector<RssArticle> articles;
};
// Converts the sandboxed XML parser's inert tree. No HTML is ever rendered.
std::optional<RssFeed> DecodeRss(const base::Value& tree, const GURL& final_url);
class RssReader final : public base::SupportsUserData::Data, public ProfileObserver {
 public:
  static RssReader* For(Profile* profile);
  explicit RssReader(Profile* profile);
  ~RssReader() override;
  const std::vector<RssFeed>& feeds() const { return feeds_; }
  bool ready() const { return ready_; }
  bool busy() const { return busy_; }
  const std::string& status() const { return status_; }
  base::CallbackListSubscription Observe(base::RepeatingClosure changed);
  void Add(GURL url);
  void Remove(GURL url);
  void Refresh();
  void Mark(GURL feed, std::string id, bool read, bool saved);
  void MarkAll(GURL feed);
  base::DictValue Serialize() const;
  void Restore(base::DictValue state);
 private:
  void OnProfileWillBeDestroyed(Profile* profile) override;
  base::ScopedObservation<Profile, ProfileObserver> observation_{this};
  void Next();
  void Downloaded(std::optional<std::string> body);
  void Parsed(GURL final_url, data_decoder::DataDecoder::ValueOrError result);
  void Finish(std::optional<RssFeed> feed);
  void Save();
  void Write();
  void Prune();
  void Notify();
  raw_ptr<Profile> profile_;
  std::vector<RssFeed> feeds_;
  std::vector<GURL> queue_;
  GURL fetching_;
  bool ready_ = false, busy_ = false;
  std::string status_;
  scoped_refptr<base::SequencedTaskRunner> files_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::RepeatingTimer timer_;
  base::OneShotTimer save_timer_;
  base::RepeatingClosureList changed_;
  base::WeakPtrFactory<RssReader> weak_{this};
};
inline constexpr char kRssTabUrl[] = "zitter://rss/";
inline bool IsRssTabUrl(const GURL& url) { return url == GURL(kRssTabUrl); }
inline bool IsWorkspaceTabUrl(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() || IsRssTabUrl(url);
}
class RssPanel final : public ProfileObserver, public views::WidgetObserver, public ui::AcceleratorTarget {
 public:
  RssPanel();
  ~RssPanel() override;
  void Show(Profile* profile, views::View* parent,
            base::RepeatingCallback<void(GURL)> open);
  void Discover(std::vector<GURL> urls);
  void DiscoverFromPage(base::Value value);
  void BeginDiscovery();
  size_t discovery_generation() const { return discovery_generation_; }
  base::WeakPtr<RssPanel> GetWeakPtr() { return weak_.GetWeakPtr(); }
  views::Widget* widget() const { return widget_; }
  views::View* view() const { return content_; }
  RssReader* reader() const { return reader_; }
  void Select(GURL feed, std::string id);
  void RequestFocus();
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;
  bool contains_for_testing(std::string_view text) const;
  size_t discovered_for_testing() const { return discovered_.size(); }
 private:
  void Close();
  void OnProfileWillBeDestroyed(Profile* profile) override;
  void OnWidgetThemeChanged(views::Widget* widget) override;
  void OnWidgetDestroying(views::Widget* widget) override;
  base::ScopedObservation<views::Widget, views::WidgetObserver> parent_observation_{this};
  base::ScopedObservation<Profile, ProfileObserver> observation_{this};
  void Update();
  void ScheduleUpdate();
  void Subscribe();
  void OpenSettings(bool add = false);
  void BackToReader();
  void ApplyColors();
  void RenderArticle(views::View* parent, const RssFeed& feed, const RssArticle& article, bool wide);
  views::View* Text(views::View* parent, std::string text, int style = 5);
  views::View* Action(views::View* parent, std::u16string text, base::RepeatingClosure callback, int kind = 3);
  raw_ptr<RssReader> reader_ = nullptr;
  raw_ptr<Profile> profile_ = nullptr;
  base::CallbackListSubscription subscription_;
  base::RepeatingCallback<void(GURL)> open_;
  GURL source_, article_feed_;
  std::string selected_;
  std::u16string return_focus_name_;
  bool unread_ = false, saved_ = false, settings_ = false, screen_focus_pending_ = false, update_pending_ = false, adding_ = false;
  // Two panes (list + reading pane) on a wide tab; one column otherwise.
  bool wide_ = false, focus_reader_pending_ = false;
  size_t visible_limit_ = 100;
  std::vector<GURL> discovered_;
  raw_ptr<views::Widget> widget_ = nullptr;
  raw_ptr<views::View> content_ = nullptr;
  std::unique_ptr<PanelScene> scene_;
  raw_ptr<views::View> header_ = nullptr, title_ = nullptr, scroll_ = nullptr, tools_ = nullptr;
  raw_ptr<views::View> settings_button_ = nullptr, back_button_ = nullptr, refresh_button_ = nullptr;
  raw_ptr<views::View> body_ = nullptr, add_row_ = nullptr, add_button_ = nullptr, count_ = nullptr;
  raw_ptr<views::View> panes_ = nullptr, list_pane_ = nullptr, reader_pane_ = nullptr, reader_body_ = nullptr;
  raw_ptr<EditorControl> address_ = nullptr;
  raw_ptr<PanelViewport> viewport_ = nullptr, reader_viewport_ = nullptr;
  size_t discovery_generation_ = 0;
  base::WeakPtrFactory<RssPanel> weak_{this};
};
}
#endif
