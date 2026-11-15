// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// The standalone window of an installed web app: the native title bar with
// the app's name and icon, a slim toolbar (back, reload, the page title and
// "Open in Zitter") and the page. Links the user follows out of the app's
// scope and new tabs open in the main Workspace; window.open() popups get a
// window of their own. On Windows the window carries the app's
// AppUserModelID, so the taskbar groups it apart from Zitter.

#ifndef ZITTER_CONTENT_WEB_APP_WINDOW_H_
#define ZITTER_CONTENT_WEB_APP_WINDOW_H_

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"
#include "web_apps.h"

namespace content {
class WebContents;
}
namespace views {
class Label;
class View;
class Widget;
class WidgetDelegate;
}

namespace zitter::content_embedder {

class AppWebView;
class ProfileContext;

class WebAppWindow final : public views::WidgetObserver {
 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();
    // Opens a URL in a tab of the main Workspace for the app's profile.
    base::RepeatingCallback<void(const GURL&)> open_in_browser;
    // window.open() popups of the app.
    base::RepeatingCallback<void(std::unique_ptr<content::WebContents>)> open_popup;
    // The window closed; the owner deletes it later.
    base::RepeatingCallback<void(WebAppWindow*)> closed;
  };

  // Opens `url`, or adopts `popup` when given (a popup window that keeps the
  // opener and does not move links out of scope to the Workspace).
  WebAppWindow(const WebApp& app,
               std::string profile_id,
               ProfileContext& profile,
               base::FilePath icon_file,
               Callbacks callbacks,
               const GURL& url,
               std::unique_ptr<content::WebContents> popup = nullptr);
  WebAppWindow(const WebAppWindow&) = delete;
  WebAppWindow& operator=(const WebAppWindow&) = delete;
  ~WebAppWindow() override;

  const std::string& app_id() const { return app_.id; }
  const std::string& profile_id() const { return profile_id_; }
  bool popup() const { return popup_; }
  void Activate();
  void Close();

  // From the page and the toolbar.
  void GoBack();
  void Reload();
  void OpenInBrowser();
  void OpenOutside(const GURL& url);
  // window.open() from the page: a popup window of the app.
  void AdoptPopup(std::unique_ptr<content::WebContents> page);
  void PageChanged();
  void SetFullscreen(bool fullscreen);
  bool fullscreen() const { return fullscreen_; }
  const WebApp& app() const { return app_; }

 private:
  // views::WidgetObserver:
  void OnWidgetClosing(views::Widget* widget) override;

  const WebApp app_;
  const std::string profile_id_;
  const bool popup_;
  Callbacks callbacks_;
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<AppWebView> web_view_ = nullptr;
  raw_ptr<views::View> back_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  bool fullscreen_ = false;
  bool closed_ = false;
  base::WeakPtrFactory<WebAppWindow> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WEB_APP_WINDOW_H_
