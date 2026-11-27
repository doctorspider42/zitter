// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Installable web apps for the Content browser: a page with a web app
// manifest (a name, a start URL inside its scope and an icon) can be
// installed, and any other http(s) page can be kept as a shortcut, as
// Chrome's "Create shortcut". An installed app opens in its own window
// without the sidebar or tabs. Each profile keeps its apps in
// <profile>/WebApps (Apps.json and one PNG icon per app). On Windows an
// install also adds a Start menu shortcut with its own AppUserModelID, so
// the app's windows group on their own in the taskbar.
//
// Site settings: the category "web-app-installation" blocks or allows
// Zitter's install offers for a site (the toolbar button, the page menu and
// the Command Center). The Web Install API (navigator.install) is not
// exposed by this Content build, so the category does not gate it.

#ifndef ZITTER_CONTENT_WEB_APPS_H_
#define ZITTER_CONTENT_WEB_APPS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/blink/public/common/manifest/manifest.h"
#include "third_party/blink/public/mojom/manifest/manifest.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "url/gurl.h"

namespace base {
class CommandLine;
class SequencedTaskRunner;
}
namespace content {
class BrowserContext;
class NavigationHandle;
class Page;
class RenderFrameHost;
class WebContents;
}
namespace gfx {
class Size;
}
namespace views {
class Widget;
}
namespace zitter::prototype {
struct PageMenuItem;
struct PaletteResult;
}

namespace zitter::content_embedder {

class ProfileContext;
class WebAppWindow;

// zitter.exe --zitter-content-app=<app id> --zitter-content-app-profile=<profile id>
// opens an installed app; the Start menu shortcut runs it.
inline constexpr char kWebAppSwitch[] = "zitter-content-app";
inline constexpr char kWebAppProfileSwitch[] = "zitter-content-app-profile";
// A launch forwarded to the running Workspace: "app:<profile id>/<app id>".
inline constexpr char kWebAppCommandPrefix[] = "app:";
// The site settings category of Chrome's Web Install API permission.
inline constexpr char kWebAppSiteCategory[] = "web-app-installation";

struct WebApp {
  WebApp();
  WebApp(const WebApp&);
  WebApp& operator=(const WebApp&);
  ~WebApp();

  // 32 lowercase hex digits from the manifest id (or the start URL of a
  // shortcut); the same site installs to the same id.
  std::string id;
  GURL manifest_id;
  std::u16string name;
  GURL start_url;
  GURL scope;
  blink::mojom::DisplayMode display = blink::mojom::DisplayMode::kStandalone;
  std::optional<SkColor> theme_color;
  // A shortcut to a page without a manifest.
  bool shortcut = false;
  SkBitmap icon;
  // The Start menu shortcut on Windows, empty elsewhere.
  base::FilePath shortcut_path;

  bool InScope(const GURL& url) const;
};

bool IsWebAppId(std::string_view id);
// The id of an app from its manifest id or a shortcut's start URL.
std::string WebAppIdFor(const GURL& manifest_id);

#if BUILDFLAG(IS_WIN)
// Turns --zitter-content-app and --zitter-content-app-profile into the
// Workspace command "app:<profile>/<id>" (--zitter-command), so the ordinary
// launch path forwards it to a running Zitter or runs it after startup.
// Invalid values only drop the switches.
void TranslateWebAppLaunch(base::CommandLine& command);
// "Zitter.Browser.App.<hash>": the taskbar groups each app on its own.
std::wstring WebAppUserModelId(std::string_view profile_id, std::string_view app_id);
// zitter.exe with the launch switches.
base::CommandLine WebAppLaunchCommand(std::string_view profile_id, std::string_view app_id);
// Blocking, on a COM STA sequence: writes `icon_file` (.ico) and the Start
// menu shortcut for the app; answers the shortcut's path or nothing.
base::FilePath CreateWebAppShortcut(const std::string& profile_id,
                                    const std::string& app_id,
                                    const std::u16string& name,
                                    const SkBitmap& icon,
                                    const base::FilePath& icon_file);
// Blocking, on a COM STA sequence: removes the shortcut and the icon file.
void DeleteWebAppShortcut(const base::FilePath& shortcut, const base::FilePath& icon_file);
#endif
// <profile>/WebApps, and the app's .ico there (Windows shortcuts and windows).
base::FilePath WebAppsDirectory(const base::FilePath& profile_path);
base::FilePath WebAppIconFile(const base::FilePath& profile_path, std::string_view app_id);

// The apps of every Workspace profile: installability of the active page,
// install and uninstall, the app windows and the Command Center entries.
class WorkspaceWebApps final : public content::WebContentsObserver {
 public:
  struct Host {
    Host();
    Host(Host&&);
    Host& operator=(Host&&);
    ~Host();
    // The profile with this Workspace profile id, if it is open.
    base::RepeatingCallback<ProfileContext*(std::string_view)> find_profile;
    // The Workspace profile id of a browser context, empty for others.
    base::RepeatingCallback<std::string(const content::BrowserContext*)> profile_id;
    // Opens `url` in a tab of a space that uses the profile.
    base::RepeatingCallback<void(const std::string&, const GURL&)> open_in_browser;
    // The main Workspace window, for dialogs.
    base::RepeatingCallback<views::Widget*()> main_window;
    // The install offer for the tracked page changed.
    base::RepeatingClosure changed;
  };

  explicit WorkspaceWebApps(Host host);
  WorkspaceWebApps(const WorkspaceWebApps&) = delete;
  WorkspaceWebApps& operator=(const WorkspaceWebApps&) = delete;
  ~WorkspaceWebApps() override;

  // Follows the page of the active tab; a no-op for the same page.
  void Track(content::WebContents* page);
  // The tracked page is an installable app the site lets Zitter offer and
  // that this profile has not installed: the toolbar install button.
  bool CanOfferInstall() const;
  std::u16string InstallOfferTitle() const;
  void InstallTrackedPage();

  // "Install <app>…", "Create shortcut…" or "Open in <app>" for a page.
  void AppendPageMenuItems(content::WebContents& page,
                           std::vector<prototype::PageMenuItem>& items);
  // Command Center entries for `query` ("" or ">…"): install the page, open
  // an installed app of the profile and manage the installed apps.
  void AppendPaletteResults(std::u16string_view query,
                            const std::string& workspace_id,
                            content::WebContents* page,
                            ProfileContext& profile,
                            std::vector<prototype::PaletteResult>& results);
  // Runs an entry from AppendPaletteResults; false for other commands.
  bool RunPaletteCommand(const prototype::PaletteResult& result);
  // "app:<profile>/<id>" from another zitter.exe or the Start menu.
  bool RunExternalCommand(std::string_view command);
  bool HasOpenWindows(std::string_view profile_id) const;

  // Opens the app's window (or brings an open one forward) at `url` when it
  // is in the app's scope, at its start URL otherwise.
  bool Launch(const std::string& profile_id, const std::string& app_id,
              const GURL& url = GURL());
  // A page the app opened with window.open() as a popup.
  void OpenPopup(const std::string& profile_id, const std::string& app_id,
                 std::unique_ptr<content::WebContents> popup);
  void ShowInstalledApps(const std::string& profile_id);
  void ConfirmUninstall(const std::string& profile_id, const std::string& app_id,
                        views::Widget* parent);

  // An app window leaves the set when it closes.
  void WindowClosed(WebAppWindow* window);

 private:
  struct Candidate;
  struct ProfileApps;

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void DidStopLoading() override;
  void PrimaryMainFrameRenderProcessGone(base::TerminationStatus status) override;
  void DidUpdateWebManifestURL(content::RenderFrameHost* frame,
                               const GURL& manifest_url) override;
  void WebContentsDestroyed() override;

  void ResetCandidate();
  void CheckTrackedPage();
  void OnManifest(uint64_t generation, GURL page_url,
                  blink::mojom::ManifestRequestResult result,
                  const GURL& manifest_url,
                  std::optional<blink::Manifest> manifest);
  std::optional<WebApp> ShortcutFor(content::WebContents& page) const;
  bool SiteAllowsOffer(content::WebContents& page) const;
  bool CandidateMatches(content::WebContents& page) const;

  ProfileApps& Apps(const std::string& profile_id);
  void WhenLoaded(const std::string& profile_id, base::OnceClosure done);
  void OnLoaded(std::string profile_id, std::vector<WebApp> apps);
  const WebApp* Find(const std::string& profile_id, const std::string& app_id);
  void Save(const std::string& profile_id);

  void StartInstall(content::WebContents& page, bool shortcut);
  void OnInstallIcon(base::WeakPtr<content::WebContents> page, WebApp app,
                     int id, int status, const GURL& url,
                     const std::vector<SkBitmap>& bitmaps,
                     const std::vector<gfx::Size>& sizes);
  void ShowInstallDialog(content::WebContents& page, WebApp app);
  void Install(std::string profile_id, WebApp app);
  void Installed(std::string profile_id, std::string app_id,
                 base::FilePath shortcut_path);
  void Uninstall(std::string profile_id, std::string app_id);
  void OpenWindow(const std::string& profile_id, const std::string& app_id,
                  const GURL& url, std::unique_ptr<content::WebContents> popup);
  void EraseWindow(WebAppWindow* window);

  Host host_;
  const scoped_refptr<base::SequencedTaskRunner> files_;
#if BUILDFLAG(IS_WIN)
  const scoped_refptr<base::SequencedTaskRunner> shell_;
#endif
  std::unique_ptr<Candidate> candidate_;
  uint64_t generation_ = 0;
  std::map<std::string, std::unique_ptr<ProfileApps>> profiles_;
  std::vector<std::unique_ptr<WebAppWindow>> windows_;
  base::WeakPtr<views::Widget> installed_dialog_;
  // The profile of the last Command Center query, for its app entries.
  std::string palette_profile_id_;
  base::WeakPtrFactory<WorkspaceWebApps> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WEB_APPS_H_
