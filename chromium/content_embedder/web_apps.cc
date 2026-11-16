// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "web_apps.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/rtl.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/toolbar_menu.h"
#include "chromium/prototype/workspace_shell.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "crypto/sha2.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "third_party/blink/public/common/manifest/manifest.h"
#include "third_party/blink/public/common/manifest/manifest_util.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/origin.h"
#include "web_app_window.h"

namespace zitter::content_embedder {

namespace {

constexpr char kAppsFile[] = "Apps.json";
constexpr size_t kMaxAppsFileBytes = 1024 * 1024;
constexpr size_t kMaxIconFileBytes = 4 * 1024 * 1024;
constexpr size_t kMaxApps = 200;
constexpr size_t kMaxNameLength = 200;
constexpr size_t kMaxUrlLength = 8192;
constexpr int kIconSize = 256;

constexpr char kInstallCommand[] = "web_app_install";
constexpr char kShortcutCommand[] = "web_app_shortcut";
constexpr char kOpenCommand[] = "web_app_open";
constexpr char kManageCommand[] = "web_apps_manage";

bool IsAppUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() &&
         !url.has_password() && url.spec().size() <= kMaxUrlLength;
}

std::u16string CleanName(std::u16string name) {
  base::i18n::SanitizeUserSuppliedString(&name);
  name = std::u16string(base::TrimWhitespace(name, base::TRIM_ALL));
  std::ranges::replace_if(name, [](char16_t c) { return c < 0x20; }, u' ');
  return name.substr(0, kMaxNameLength);
}

std::u16string OriginText(const GURL& url) {
  return base::UTF8ToUTF16(url::Origin::Create(url).Serialize());
}

base::DictValue ToValue(const WebApp& app) {
  base::DictValue value;
  value.Set("id", app.id);
  value.Set("manifest_id", app.manifest_id.spec());
  value.Set("name", app.name);
  value.Set("start_url", app.start_url.spec());
  value.Set("scope", app.scope.spec());
  value.Set("display", blink::DisplayModeToString(app.display));
  if (app.theme_color)
    value.Set("theme_color", static_cast<int>(*app.theme_color));
  value.Set("shortcut", app.shortcut);
  if (!app.shortcut_path.empty())
    value.Set("shortcut_path", app.shortcut_path.AsUTF8Unsafe());
  return value;
}

std::optional<WebApp> FromValue(const base::DictValue& value) {
  const auto* id = value.FindString("id");
  const auto* manifest_id = value.FindString("manifest_id");
  const auto* name = value.FindString("name");
  const auto* start_url = value.FindString("start_url");
  const auto* scope = value.FindString("scope");
  if (!id || !IsWebAppId(*id) || !manifest_id || !name || !start_url || !scope)
    return std::nullopt;
  WebApp app;
  app.id = *id;
  app.manifest_id = GURL(*manifest_id);
  app.name = CleanName(base::UTF8ToUTF16(*name));
  app.start_url = GURL(*start_url);
  app.scope = GURL(*scope);
  if (app.name.empty() || !IsAppUrl(app.manifest_id) || !IsAppUrl(app.start_url) ||
      !IsAppUrl(app.scope) || WebAppIdFor(app.manifest_id) != app.id) {
    return std::nullopt;
  }
  if (const auto* display = value.FindString("display")) {
    app.display = blink::DisplayModeFromString(*display);
    if (app.display == blink::mojom::DisplayMode::kUndefined)
      app.display = blink::mojom::DisplayMode::kStandalone;
  }
  if (const auto theme = value.FindInt("theme_color"))
    app.theme_color = static_cast<SkColor>(*theme);
  app.shortcut = value.FindBool("shortcut").value_or(false);
  if (const auto* path = value.FindString("shortcut_path"))
    app.shortcut_path = base::FilePath::FromUTF8Unsafe(*path);
  return app;
}

// Blocking: the apps of a profile and their icons.
std::vector<WebApp> ReadApps(const base::FilePath& directory) {
  std::vector<WebApp> apps;
  std::string json;
  if (!base::ReadFileToStringWithMaxSize(directory.AppendASCII(kAppsFile), &json,
                                         kMaxAppsFileBytes)) {
    return apps;
  }
  auto root = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const auto* list = root ? root->FindList("apps") : nullptr;
  if (!list)
    return apps;
  for (const auto& entry : *list) {
    if (!entry.is_dict() || apps.size() >= kMaxApps)
      continue;
    auto app = FromValue(entry.GetDict());
    if (!app || std::ranges::any_of(apps, [&](const WebApp& other) {
          return other.id == app->id;
        })) {
      continue;
    }
    std::string png;
    if (base::ReadFileToStringWithMaxSize(directory.AppendASCII(app->id + ".png"), &png,
                                          kMaxIconFileBytes)) {
      app->icon = gfx::PNGCodec::Decode(base::as_byte_span(png));
    }
    apps.push_back(std::move(*app));
  }
  return apps;
}

void WriteAppsFile(const base::FilePath& directory, std::string json) {
  if (!base::CreateDirectory(directory) ||
      !base::ImportantFileWriter::WriteFileAtomically(directory.AppendASCII(kAppsFile),
                                                      json)) {
    LOG(ERROR) << "Could not save the installed web apps";
  }
}

void WriteIconFile(const base::FilePath& directory, const std::string& id,
                   std::vector<uint8_t> png) {
  if (!base::CreateDirectory(directory) ||
      !base::WriteFile(directory.AppendASCII(id + ".png"), png)) {
    LOG(ERROR) << "Could not save a web app icon";
  }
}

void DeleteIconFile(const base::FilePath& directory, const std::string& id) {
  base::DeleteFile(directory.AppendASCII(id + ".png"));
}

// A rounded tile with the name's first letter, for sites without an icon.
SkBitmap Monogram(const std::u16string& name, std::optional<SkColor> theme) {
  constexpr int kSize = 128;
  gfx::Canvas canvas(gfx::Size(kSize, kSize), 1.0f, /*is_opaque=*/false);
  const SkColor background = theme ? SkColorSetA(*theme, SK_AlphaOPAQUE)
                                   : SkColorSetRGB(0x5B, 0x5F, 0xC7);
  prototype::FillRound(&canvas, gfx::RectF(0, 0, kSize, kSize), 28, background);
  std::u16string letter = name.empty() ? u"?" : name.substr(0, 1);
  letter = base::i18n::ToUpper(letter);
  canvas.DrawStringRectWithFlags(letter, prototype::DesignFont(2, 64, 700),
                                 color_utils::GetColorWithMaxContrast(background),
                                 gfx::Rect(0, 0, kSize, kSize),
                                 gfx::Canvas::TEXT_ALIGN_CENTER);
  return canvas.GetBitmap();
}

SkBitmap FallbackIcon(content::WebContents& page, const WebApp& app) {
  if (auto* entry = page.GetController().GetLastCommittedEntry()) {
    const auto& favicon = entry->GetFavicon();
    if (favicon.valid && !favicon.image.IsEmpty())
      return favicon.image.AsBitmap();
  }
  return Monogram(app.name, app.theme_color);
}

// Lowercase without Polish diacritics, as the Command Center matches.
std::u16string Fold(std::u16string_view text) {
  std::u16string folded = base::i18n::ToLower(text);
  for (auto& c : folded) {
    switch (c) {
      case u'ą': c = u'a'; break;
      case u'ć': c = u'c'; break;
      case u'ę': c = u'e'; break;
      case u'ł': c = u'l'; break;
      case u'ń': c = u'n'; break;
      case u'ó': c = u'o'; break;
      case u'ś': c = u's'; break;
      case u'ź':
      case u'ż': c = u'z'; break;
      default: break;
    }
  }
  return folded;
}

std::unique_ptr<views::ImageView> AppIconView(const SkBitmap& icon, int size) {
  auto view = std::make_unique<views::ImageView>();
  if (!icon.drawsNothing()) {
    view->SetImage(ui::ImageModel::FromImageSkia(gfx::ImageSkia::CreateFrom1xBitmap(icon)));
  }
  view->SetImageSize(gfx::Size(size, size));
  view->SetPreferredSize(gfx::Size(size, size));
  return view;
}

std::unique_ptr<views::Label> TextLabel(std::u16string text, int style = views::style::STYLE_PRIMARY) {
  auto label = std::make_unique<views::Label>(std::move(text), views::style::CONTEXT_LABEL, style);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  return label;
}

void ShowDialog(std::unique_ptr<views::DialogDelegate> dialog, views::Widget* parent) {
  dialog->SetModalType(parent ? ui::mojom::ModalType::kWindow : ui::mojom::ModalType::kNone);
  auto* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog), /*context=*/gfx::NativeWindow(), parent ? parent->GetNativeView() : gfx::NativeView());
  widget->Show();
}

}  // namespace

WebApp::WebApp() = default;
WebApp::WebApp(const WebApp&) = default;
WebApp& WebApp::operator=(const WebApp&) = default;
WebApp::~WebApp() = default;

bool WebApp::InScope(const GURL& url) const {
  return url.is_valid() && url::Origin::Create(url).IsSameOriginWith(scope) &&
         url.path().starts_with(scope.path());
}

bool IsWebAppId(std::string_view id) {
  return id.size() == 32 && std::ranges::all_of(id, [](char c) {
           return base::IsAsciiDigit(c) || (c >= 'a' && c <= 'f');
         });
}

std::string WebAppIdFor(const GURL& manifest_id) {
  return base::HexEncodeLower(
             crypto::SHA256HashString(manifest_id.GetWithoutRef().spec()))
      .substr(0, 32);
}

base::FilePath WebAppsDirectory(const base::FilePath& profile_path) {
  return profile_path.AppendASCII("WebApps");
}

base::FilePath WebAppIconFile(const base::FilePath& profile_path, std::string_view app_id) {
  return WebAppsDirectory(profile_path).AppendASCII(std::string(app_id) + ".ico");
}

struct WorkspaceWebApps::Candidate {
  GURL page_url;
  bool ready = false;
  // An installable app from the page's manifest.
  std::optional<WebApp> app;
  GURL icon_url;
};

struct WorkspaceWebApps::ProfileApps {
  bool loading = false;
  bool loaded = false;
  std::vector<WebApp> apps;
  std::vector<base::OnceClosure> waiting;
};

WorkspaceWebApps::Host::Host() = default;
WorkspaceWebApps::Host::Host(Host&&) = default;
WorkspaceWebApps::Host& WorkspaceWebApps::Host::operator=(Host&&) = default;
WorkspaceWebApps::Host::~Host() = default;

WorkspaceWebApps::WorkspaceWebApps(Host host)
    : host_(std::move(host)),
      files_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}))
#if BUILDFLAG(IS_WIN)
      ,
      shell_(base::ThreadPool::CreateCOMSTATaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}))
#endif
{
}

WorkspaceWebApps::~WorkspaceWebApps() {
  weak_factory_.InvalidateWeakPtrs();
  if (installed_dialog_)
    installed_dialog_->CloseNow();
  windows_.clear();
  Observe(nullptr);
}

// Installability of the active page.

void WorkspaceWebApps::Track(content::WebContents* page) {
  if (page == web_contents())
    return;
  Observe(page);
  ResetCandidate();
  if (!page)
    return;
  if (const auto profile = host_.profile_id.Run(page->GetBrowserContext()); !profile.empty())
    WhenLoaded(profile, base::DoNothing());
  if (!page->IsLoading())
    CheckTrackedPage();
}

void WorkspaceWebApps::PrimaryPageChanged(content::Page&) {
  ResetCandidate();
}

// The same document at another URL (history.pushState) keeps its manifest.
void WorkspaceWebApps::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (!navigation->IsInPrimaryMainFrame() || !navigation->HasCommitted() ||
      !navigation->IsSameDocument() || !candidate_ || !candidate_->ready) {
    return;
  }
  candidate_->page_url = web_contents()->GetLastCommittedURL();
  host_.changed.Run();
}

void WorkspaceWebApps::DidStopLoading() {
  CheckTrackedPage();
}

void WorkspaceWebApps::DidUpdateWebManifestURL(content::RenderFrameHost* frame,
                                               const GURL&) {
  if (frame && frame->IsInPrimaryMainFrame() && !web_contents()->IsLoading()) {
    candidate_.reset();
    CheckTrackedPage();
  }
}

void WorkspaceWebApps::WebContentsDestroyed() {
  Observe(nullptr);
  ResetCandidate();
}

void WorkspaceWebApps::ResetCandidate() {
  ++generation_;
  const bool had = candidate_ && candidate_->ready;
  candidate_.reset();
  if (had)
    host_.changed.Run();
}

void WorkspaceWebApps::CheckTrackedPage() {
  auto* page = web_contents();
  if (!page)
    return;
  const GURL url = page->GetLastCommittedURL();
  if (candidate_ && candidate_->page_url == url)
    return;
  ++generation_;
  candidate_ = std::make_unique<Candidate>();
  candidate_->page_url = url;
  if (!IsAppUrl(url) || page->GetPrimaryMainFrame()->IsErrorDocument()) {
    candidate_->ready = true;
    host_.changed.Run();
    return;
  }
  page->GetPrimaryPage().GetManifest(base::BindOnce(
      &WorkspaceWebApps::OnManifest, weak_factory_.GetWeakPtr(), generation_, url));
}

void WorkspaceWebApps::OnManifest(uint64_t generation, GURL page_url,
                                  blink::mojom::ManifestRequestResult result,
                                  const GURL& manifest_url,
                                  blink::mojom::ManifestPtr manifest) {
  auto* page = web_contents();
  if (generation != generation_ || !candidate_ || !page ||
      page->GetLastCommittedURL() != page_url) {
    return;
  }
  candidate_->ready = true;
  if (result == blink::mojom::ManifestRequestResult::kSuccess && manifest &&
      !manifest_url.is_empty()) {
    WebApp app;
    app.name = CleanName(manifest->name ? *manifest->name
                         : manifest->short_name ? *manifest->short_name
                                                : std::u16string());
    app.start_url = manifest->start_url;
    app.scope = manifest->scope;
    app.manifest_id = manifest->id.is_valid() ? manifest->id : manifest->start_url;
    app.display = manifest->display == blink::mojom::DisplayMode::kUndefined
                      ? blink::mojom::DisplayMode::kStandalone
                      : manifest->display;
    if (manifest->theme_color)
      app.theme_color = static_cast<SkColor>(*manifest->theme_color);
    // The largest icon for any purpose, preferring sizes from 144 px and
    // "any" (SVG) icons, which the download rasterizes.
    const blink::Manifest::ImageResource* best = nullptr;
    int best_score = -1;
    for (const auto& icon : manifest->icons) {
      if (!icon.src.is_valid() || !icon.src.SchemeIsHTTPOrHTTPS() ||
          (!icon.purpose.empty() &&
           !std::ranges::contains(icon.purpose,
                                  blink::mojom::ManifestImageResource_Purpose::ANY))) {
        continue;
      }
      int score = 1;
      for (const auto& size : icon.sizes) {
        if (size.IsEmpty())
          score = std::max(score, 10000);
        else
          score = std::max(score, std::min(size.width(), size.height()));
      }
      if (score > best_score) {
        best = &icon;
        best_score = score;
      }
    }
    const url::Origin origin = url::Origin::Create(page_url);
    if (!app.name.empty() && best && IsAppUrl(app.start_url) && IsAppUrl(app.scope) &&
        IsAppUrl(app.manifest_id) && app.InScope(app.start_url) &&
        origin.IsSameOriginWith(app.start_url) &&
        network::IsUrlPotentiallyTrustworthy(app.start_url)) {
      app.id = WebAppIdFor(app.manifest_id);
      candidate_->icon_url = best->src;
      candidate_->app = std::move(app);
    }
  }
  host_.changed.Run();
}

bool WorkspaceWebApps::CandidateMatches(content::WebContents& page) const {
  return &page == web_contents() && candidate_ && candidate_->ready &&
         candidate_->page_url == page.GetLastCommittedURL();
}

bool WorkspaceWebApps::SiteAllowsOffer(content::WebContents& page) const {
  const GURL url = page.GetLastCommittedURL();
  if (!IsAppUrl(url))
    return false;
  auto* profile = host_.find_profile.Run(host_.profile_id.Run(page.GetBrowserContext()));
  return profile && profile->site_permissions().SiteSetting(
                        kWebAppSiteCategory, url::Origin::Create(url)) != 2;
}

std::optional<WebApp> WorkspaceWebApps::ShortcutFor(content::WebContents& page) const {
  const GURL url = page.GetLastCommittedURL().GetWithoutRef();
  if (!IsAppUrl(url) || page.GetPrimaryMainFrame()->IsErrorDocument())
    return std::nullopt;
  WebApp app;
  app.shortcut = true;
  app.manifest_id = url;
  app.id = WebAppIdFor(url);
  app.start_url = url;
  app.scope = url.GetWithoutFilename();
  app.name = CleanName(page.GetTitle());
  if (app.name.empty() || app.name == base::UTF8ToUTF16(url.spec()))
    app.name = CleanName(base::UTF8ToUTF16(url.host()));
  return app;
}

bool WorkspaceWebApps::CanOfferInstall() const {
  auto* page = web_contents();
  if (!page || !CandidateMatches(*page) || !candidate_->app || !SiteAllowsOffer(*page))
    return false;
  const auto profile = host_.profile_id.Run(page->GetBrowserContext());
  const auto found = profiles_.find(profile);
  return found != profiles_.end() && found->second->loaded &&
         std::ranges::none_of(found->second->apps, [&](const WebApp& app) {
           return app.id == candidate_->app->id;
         });
}

std::u16string WorkspaceWebApps::InstallOfferTitle() const {
  return candidate_ && candidate_->app
             ? prototype::TF(u"Zainstaluj {}…", {candidate_->app->name})
             : prototype::T(u"Zainstaluj aplikację");
}

void WorkspaceWebApps::InstallTrackedPage() {
  if (auto* page = web_contents(); page && CanOfferInstall())
    StartInstall(*page, /*shortcut=*/false);
}

// Menus and the Command Center.

void WorkspaceWebApps::AppendPageMenuItems(content::WebContents& page,
                                           std::vector<prototype::PageMenuItem>& items) {
  if (!CandidateMatches(page) || !SiteAllowsOffer(page))
    return;
  const auto profile_id = host_.profile_id.Run(page.GetBrowserContext());
  const auto found = profiles_.find(profile_id);
  if (profile_id.empty() || found == profiles_.end() || !found->second->loaded)
    return;
  const auto weak = weak_factory_.GetWeakPtr();
  const auto page_weak = page.GetWeakPtr();
  const std::string candidate_id =
      candidate_->app ? candidate_->app->id : WebAppIdFor(page.GetLastCommittedURL().GetWithoutRef());
  for (const auto& app : found->second->apps) {
    if (app.id != candidate_id && !(app.InScope(page.GetLastCommittedURL()) && !app.shortcut))
      continue;
    items.push_back(
        {.title = prototype::TF(u"Otwórz w aplikacji {}", {app.name}),
         .run = base::BindRepeating(
             [](base::WeakPtr<WorkspaceWebApps> self, std::string profile, std::string id,
                base::WeakPtr<content::WebContents> source) {
               if (self && source)
                 self->Launch(profile, id, source->GetLastCommittedURL());
             },
             weak, profile_id, app.id, page_weak)});
    return;
  }
  const bool shortcut = !candidate_->app;
  if (shortcut && !ShortcutFor(page))
    return;
  items.push_back(
      {.title = shortcut ? prototype::T(u"Utwórz skrót do strony…")
                         : prototype::TF(u"Zainstaluj {}…", {candidate_->app->name}),
       .run = base::BindRepeating(
           [](base::WeakPtr<WorkspaceWebApps> self, base::WeakPtr<content::WebContents> source,
              bool as_shortcut) {
             if (self && source)
               self->StartInstall(*source, as_shortcut);
           },
           weak, page_weak, shortcut)});
}

void WorkspaceWebApps::AppendPaletteResults(std::u16string_view query,
                                            const std::string& workspace_id,
                                            content::WebContents* page,
                                            ProfileContext& profile,
                                            std::vector<prototype::PaletteResult>& results) {
  if (query.starts_with(u">"))
    query.remove_prefix(1);
  const auto words = base::SplitString(Fold(query), u" \t", base::TRIM_WHITESPACE,
                                       base::SPLIT_WANT_NONEMPTY);
  palette_profile_id_ = host_.profile_id.Run(&profile);
  if (palette_profile_id_.empty())
    return;
  const auto add = [&](std::string_view id, std::u16string title, std::u16string detail,
                       std::u16string_view aliases, std::string target) {
    const auto haystack = Fold(title + u" " + detail + u" " + std::u16string(aliases));
    if (!std::ranges::all_of(words, [&](const std::u16string& word) {
          return haystack.find(word) != std::u16string::npos;
        })) {
      return;
    }
    prototype::PaletteResult result;
    result.title = std::move(title);
    result.detail = prototype::TF(u"Polecenie · {}", {detail});
    result.workspace_id = workspace_id;
    result.command_id = std::string(id);
    result.target_workspace_id = std::move(target);
    results.push_back(std::move(result));
  };
  const auto found = profiles_.find(palette_profile_id_);
  if (found == profiles_.end() || !found->second->loaded) {
    WhenLoaded(palette_profile_id_, base::DoNothing());
    return;
  }
  const auto& apps = found->second->apps;
  if (page && CandidateMatches(*page) && SiteAllowsOffer(*page) &&
      host_.profile_id.Run(page->GetBrowserContext()) == palette_profile_id_) {
    const std::string candidate_id =
        candidate_->app ? candidate_->app->id
                        : WebAppIdFor(page->GetLastCommittedURL().GetWithoutRef());
    const bool installed = std::ranges::any_of(
        apps, [&](const WebApp& app) { return app.id == candidate_id; });
    if (!installed && candidate_->app) {
      add(kInstallCommand, prototype::TF(u"Zainstaluj {}…", {candidate_->app->name}),
          prototype::T(u"Aplikacja internetowa we własnym oknie"),
          u"install app pwa zainstaluj aplikacja", "");
    } else if (!installed && ShortcutFor(*page)) {
      add(kShortcutCommand, prototype::T(u"Utwórz skrót do strony…"),
          prototype::T(u"Strona we własnym oknie, bez kart"),
          u"create shortcut app install skrot aplikacja", "");
    }
  }
  for (const auto& app : apps) {
    add(kOpenCommand, prototype::TF(u"Otwórz aplikację: {}", {app.name}),
        OriginText(app.start_url), u"open app launch aplikacja uruchom", app.id);
  }
  if (!apps.empty()) {
    add(kManageCommand, prototype::T(u"Zainstalowane aplikacje"),
        prototype::T(u"Otwórz lub odinstaluj aplikacje tego profilu"),
        u"installed apps uninstall remove odinstaluj aplikacje pwa", "");
  }
}

bool WorkspaceWebApps::RunPaletteCommand(const prototype::PaletteResult& result) {
  const std::string_view id(result.command_id);
  if (id == kInstallCommand || id == kShortcutCommand) {
    if (auto* page = web_contents())
      StartInstall(*page, id == kShortcutCommand);
    return true;
  }
  if (id == kOpenCommand) {
    Launch(palette_profile_id_, result.target_workspace_id);
    return true;
  }
  if (id == kManageCommand) {
    ShowInstalledApps(palette_profile_id_);
    return true;
  }
  return false;
}

bool WorkspaceWebApps::RunExternalCommand(std::string_view command) {
  if (!command.starts_with(kWebAppCommandPrefix))
    return false;
  command.remove_prefix(std::size(kWebAppCommandPrefix) - 1);
  const auto slash = command.find('/');
  if (slash == std::string_view::npos)
    return false;
  const std::string profile(command.substr(0, slash));
  const std::string app(command.substr(slash + 1));
  if (profile.empty() || profile.size() > 64 || !IsWebAppId(app) ||
      !host_.find_profile.Run(profile)) {
    return false;
  }
  return Launch(profile, app);
}

// Storage.

WorkspaceWebApps::ProfileApps& WorkspaceWebApps::Apps(const std::string& profile_id) {
  auto& entry = profiles_[profile_id];
  if (!entry)
    entry = std::make_unique<ProfileApps>();
  return *entry;
}

void WorkspaceWebApps::WhenLoaded(const std::string& profile_id, base::OnceClosure done) {
  auto& entry = Apps(profile_id);
  if (entry.loaded) {
    std::move(done).Run();
    return;
  }
  entry.waiting.push_back(std::move(done));
  if (entry.loading)
    return;
  auto* profile = host_.find_profile.Run(profile_id);
  if (!profile || profile->IsOffTheRecord()) {
    entry.waiting.clear();
    return;
  }
  entry.loading = true;
  files_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ReadApps, WebAppsDirectory(profile->GetPath())),
      base::BindOnce(&WorkspaceWebApps::OnLoaded, weak_factory_.GetWeakPtr(), profile_id));
}

void WorkspaceWebApps::OnLoaded(std::string profile_id, std::vector<WebApp> apps) {
  auto& entry = Apps(profile_id);
  entry.loading = false;
  entry.loaded = true;
  entry.apps = std::move(apps);
  auto waiting = std::move(entry.waiting);
  entry.waiting.clear();
  for (auto& done : waiting)
    std::move(done).Run();
  host_.changed.Run();
}

const WebApp* WorkspaceWebApps::Find(const std::string& profile_id,
                                     const std::string& app_id) {
  const auto found = profiles_.find(profile_id);
  if (found == profiles_.end())
    return nullptr;
  const auto app = std::ranges::find(found->second->apps, app_id, &WebApp::id);
  return app == found->second->apps.end() ? nullptr : &*app;
}

void WorkspaceWebApps::Save(const std::string& profile_id) {
  auto* profile = host_.find_profile.Run(profile_id);
  const auto found = profiles_.find(profile_id);
  if (!profile || found == profiles_.end() || !found->second->loaded)
    return;
  base::ListValue list;
  for (const auto& app : found->second->apps)
    list.Append(ToValue(app));
  base::DictValue root;
  root.Set("version", 1);
  root.Set("apps", std::move(list));
  auto json = base::WriteJson(root);
  if (!json)
    return;
  files_->PostTask(FROM_HERE, base::BindOnce(&WriteAppsFile,
                                             WebAppsDirectory(profile->GetPath()),
                                             std::move(*json)));
}

// Install and uninstall.

void WorkspaceWebApps::StartInstall(content::WebContents& page, bool shortcut) {
  if (!CandidateMatches(page) || !SiteAllowsOffer(page) ||
      host_.profile_id.Run(page.GetBrowserContext()).empty()) {
    return;
  }
  if (!shortcut && candidate_->app) {
    page.DownloadImage(candidate_->icon_url, /*is_favicon=*/false,
                       gfx::Size(kIconSize, kIconSize), /*max_bitmap_size=*/kIconSize,
                       /*bypass_cache=*/false,
                       base::BindOnce(&WorkspaceWebApps::OnInstallIcon,
                                      weak_factory_.GetWeakPtr(), page.GetWeakPtr(),
                                      *candidate_->app));
    return;
  }
  auto app = ShortcutFor(page);
  if (!app)
    return;
  app->icon = FallbackIcon(page, *app);
  ShowInstallDialog(page, std::move(*app));
}

void WorkspaceWebApps::OnInstallIcon(base::WeakPtr<content::WebContents> page, WebApp app,
                                     int, int, const GURL&,
                                     const std::vector<SkBitmap>& bitmaps,
                                     const std::vector<gfx::Size>&) {
  if (!page)
    return;
  const SkBitmap* best = nullptr;
  for (const auto& bitmap : bitmaps) {
    if (!bitmap.drawsNothing() && (!best || bitmap.width() > best->width()))
      best = &bitmap;
  }
  app.icon = best ? *best : FallbackIcon(*page, app);
  ShowInstallDialog(*page, std::move(app));
}

void WorkspaceWebApps::ShowInstallDialog(content::WebContents& page, WebApp app) {
  const auto profile_id = host_.profile_id.Run(page.GetBrowserContext());
  auto* parent = host_.main_window.Run();
  if (profile_id.empty() || !parent)
    return;
  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetTitle(app.shortcut ? prototype::T(u"Utworzyć skrót?")
                                : prototype::T(u"Zainstalować aplikację?"));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk,
                         app.shortcut ? prototype::T(u"Utwórz") : prototype::T(u"Zainstaluj"));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel, prototype::T(u"Anuluj"));

  auto contents = std::make_unique<views::View>();
  auto* row = contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(16, 20), 14));
  row->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStart);
  contents->AddChildView(AppIconView(app.icon, 48));
  auto* text = contents->AddChildView(std::make_unique<views::View>());
  text->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  row->SetFlexForView(text, 1);
  auto* name = text->AddChildView(TextLabel(app.name, views::style::STYLE_EMPHASIZED));
  name->SetMaximumWidthSingleLine(320);
  auto* origin = text->AddChildView(TextLabel(OriginText(app.start_url),
                                              views::style::STYLE_SECONDARY));
  origin->SetMaximumWidthSingleLine(320);
#if BUILDFLAG(IS_WIN)
  auto* about = text->AddChildView(TextLabel(prototype::T(
      u"Otwiera się we własnym oknie, bez kart. Skrót trafi do menu Start.")));
#else
  auto* about = text->AddChildView(
      TextLabel(prototype::T(u"Otwiera się we własnym oknie, bez kart.")));
#endif
  about->SetMultiLine(true);
  about->SetMaximumWidth(320);
  dialog->SetContentsView(std::move(contents));
  dialog->SetAcceptCallback(base::BindOnce(&WorkspaceWebApps::Install,
                                           weak_factory_.GetWeakPtr(), profile_id,
                                           std::move(app)));
  ShowDialog(std::move(dialog), parent);
}

void WorkspaceWebApps::Install(std::string profile_id, WebApp app) {
  auto& entry = Apps(profile_id);
  if (!entry.loaded) {
    WhenLoaded(profile_id, base::BindOnce(&WorkspaceWebApps::Install,
                                          weak_factory_.GetWeakPtr(), profile_id,
                                          std::move(app)));
    return;
  }
  if (Find(profile_id, app.id)) {
    Launch(profile_id, app.id);
    return;
  }
  auto* profile = host_.find_profile.Run(profile_id);
  if (!profile || entry.apps.size() >= kMaxApps)
    return;
  const auto directory = WebAppsDirectory(profile->GetPath());
  if (auto png = gfx::PNGCodec::EncodeBGRASkBitmap(app.icon, /*discard_transparency=*/false)) {
    files_->PostTask(FROM_HERE,
                     base::BindOnce(&WriteIconFile, directory, app.id, std::move(*png)));
  }
  const std::string id = app.id;
#if BUILDFLAG(IS_WIN)
  shell_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&CreateWebAppShortcut, profile_id, id, app.name, app.icon,
                     WebAppIconFile(profile->GetPath(), id)),
      base::BindOnce(&WorkspaceWebApps::Installed, weak_factory_.GetWeakPtr(), profile_id,
                     id));
#endif
  entry.apps.push_back(std::move(app));
  Save(profile_id);
  host_.changed.Run();
  Launch(profile_id, id);
}

void WorkspaceWebApps::Installed(std::string profile_id, std::string app_id,
                                 base::FilePath shortcut_path) {
  const auto found = profiles_.find(profile_id);
  if (shortcut_path.empty() || found == profiles_.end())
    return;
  const auto app = std::ranges::find(found->second->apps, app_id, &WebApp::id);
  if (app == found->second->apps.end()) {
    // Uninstalled while the shortcut was being written.
#if BUILDFLAG(IS_WIN)
    shell_->PostTask(FROM_HERE, base::BindOnce(&DeleteWebAppShortcut, shortcut_path,
                                               base::FilePath()));
#endif
    return;
  }
  app->shortcut_path = std::move(shortcut_path);
  Save(profile_id);
}

void WorkspaceWebApps::ConfirmUninstall(const std::string& profile_id,
                                        const std::string& app_id,
                                        views::Widget* parent) {
  const auto* app = Find(profile_id, app_id);
  if (!app)
    return;
  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetTitle(prototype::TF(u"Odinstalować {}?", {app->name}));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, prototype::T(u"Odinstaluj"));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel, prototype::T(u"Anuluj"));
  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(16, 20), 0));
  auto* text = contents->AddChildView(TextLabel(prototype::T(
      u"Zitter usunie aplikację i jej skrót. Dane witryny zostaną w profilu.")));
  text->SetMultiLine(true);
  text->SetMaximumWidth(360);
  dialog->SetContentsView(std::move(contents));
  dialog->SetAcceptCallback(base::BindOnce(&WorkspaceWebApps::Uninstall,
                                           weak_factory_.GetWeakPtr(), profile_id, app_id));
  ShowDialog(std::move(dialog), parent ? parent : host_.main_window.Run());
}

void WorkspaceWebApps::Uninstall(std::string profile_id, std::string app_id) {
  const auto found = profiles_.find(profile_id);
  auto* profile = host_.find_profile.Run(profile_id);
  if (found == profiles_.end() || !profile)
    return;
  auto& apps = found->second->apps;
  const auto app = std::ranges::find(apps, app_id, &WebApp::id);
  if (app == apps.end())
    return;
#if BUILDFLAG(IS_WIN)
  shell_->PostTask(FROM_HERE,
                   base::BindOnce(&DeleteWebAppShortcut, app->shortcut_path,
                                  WebAppIconFile(profile->GetPath(), app_id)));
#endif
  apps.erase(app);
  files_->PostTask(FROM_HERE, base::BindOnce(&DeleteIconFile,
                                             WebAppsDirectory(profile->GetPath()), app_id));
  Save(profile_id);
  for (const auto& window : windows_) {
    if (window->profile_id() == profile_id && window->app_id() == app_id)
      window->Close();
  }
  host_.changed.Run();
  if (installed_dialog_) {
    installed_dialog_->Close();
    installed_dialog_.reset();
    ShowInstalledApps(profile_id);
  }
}

// Windows.

bool WorkspaceWebApps::Launch(const std::string& profile_id, const std::string& app_id,
                              const GURL& url) {
  if (!IsWebAppId(app_id) || !host_.find_profile.Run(profile_id))
    return false;
  WhenLoaded(profile_id, base::BindOnce(
                             [](base::WeakPtr<WorkspaceWebApps> self, std::string profile,
                                std::string id, GURL target) {
                               if (self)
                                 self->OpenWindow(profile, id, target, nullptr);
                             },
                             weak_factory_.GetWeakPtr(), profile_id, app_id, url));
  return true;
}

void WorkspaceWebApps::OpenPopup(const std::string& profile_id, const std::string& app_id,
                                 std::unique_ptr<content::WebContents> popup) {
  if (popup)
    OpenWindow(profile_id, app_id, GURL(), std::move(popup));
}

void WorkspaceWebApps::OpenWindow(const std::string& profile_id, const std::string& app_id,
                                  const GURL& url,
                                  std::unique_ptr<content::WebContents> popup) {
  const auto* app = Find(profile_id, app_id);
  auto* profile = host_.find_profile.Run(profile_id);
  if (!app || !profile)
    return;
  if (!popup) {
    for (const auto& window : windows_) {
      if (!window->popup() && window->profile_id() == profile_id &&
          window->app_id() == app_id && !url.is_valid()) {
        window->Activate();
        return;
      }
    }
  }
  const GURL target = url.is_valid() && app->InScope(url) ? url : app->start_url;
  const auto weak = weak_factory_.GetWeakPtr();
  WebAppWindow::Callbacks callbacks;
  callbacks.open_in_browser = base::BindRepeating(
      [](base::WeakPtr<WorkspaceWebApps> self, std::string profile_key, const GURL& link) {
        if (self)
          self->host_.open_in_browser.Run(profile_key, link);
      },
      weak, profile_id);
  callbacks.open_popup = base::BindRepeating(&WorkspaceWebApps::OpenPopup, weak,
                                             profile_id, app_id);
  callbacks.closed = base::BindRepeating(&WorkspaceWebApps::WindowClosed, weak);
  windows_.push_back(std::make_unique<WebAppWindow>(
      *app, profile_id, *profile, WebAppIconFile(profile->GetPath(), app_id),
      std::move(callbacks), target, std::move(popup)));
}

void WorkspaceWebApps::WindowClosed(WebAppWindow* window) {
  // The widget is still closing; delete the window afterwards.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WorkspaceWebApps::EraseWindow,
                                weak_factory_.GetWeakPtr(), base::Unretained(window)));
}

void WorkspaceWebApps::EraseWindow(WebAppWindow* window) {
  std::erase_if(windows_, [window](const auto& open) { return open.get() == window; });
}

void WorkspaceWebApps::ShowInstalledApps(const std::string& profile_id) {
  const auto found = profiles_.find(profile_id);
  if (found == profiles_.end() || !found->second->loaded) {
    WhenLoaded(profile_id, base::BindOnce(
                               [](base::WeakPtr<WorkspaceWebApps> self, std::string profile) {
                                 if (self)
                                   self->ShowInstalledApps(profile);
                               },
                               weak_factory_.GetWeakPtr(), profile_id));
    return;
  }
  if (installed_dialog_)
    installed_dialog_->Close();
  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetTitle(prototype::T(u"Zainstalowane aplikacje"));
  dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, prototype::T(u"Zamknij"));

  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(12, 20), 0));
  const auto& apps = found->second->apps;
  if (apps.empty()) {
    contents->AddChildView(TextLabel(prototype::T(u"Nie ma zainstalowanych aplikacji.")));
  } else {
    auto* scroll = contents->AddChildView(std::make_unique<views::ScrollView>());
    scroll->ClipHeightTo(0, 360);
    auto* list = scroll->SetContents(std::make_unique<views::View>());
    list->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
    const auto weak = weak_factory_.GetWeakPtr();
    for (const auto& app : apps) {
      auto* row = list->AddChildView(std::make_unique<views::View>());
      auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(2, 0), 10));
      layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
      row->AddChildView(AppIconView(app.icon, 28));
      auto* text = row->AddChildView(std::make_unique<views::View>());
      text->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
      layout->SetFlexForView(text, 1);
      text->AddChildView(TextLabel(app.name))->SetMaximumWidthSingleLine(260);
      text->AddChildView(TextLabel(OriginText(app.start_url), views::style::STYLE_SECONDARY))
          ->SetMaximumWidthSingleLine(260);
      row->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(
              [](base::WeakPtr<WorkspaceWebApps> self, std::string profile, std::string id) {
                if (self)
                  self->Launch(profile, id);
              },
              weak, profile_id, app.id),
          prototype::T(u"Otwórz")));
      row->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(
              [](base::WeakPtr<WorkspaceWebApps> self, std::string profile, std::string id) {
                if (self)
                  self->ConfirmUninstall(profile, id, self->installed_dialog_.get());
              },
              weak, profile_id, app.id),
          prototype::T(u"Odinstaluj")));
    }
  }
  contents->SetPreferredSize(gfx::Size(480, contents->GetPreferredSize().height()));
  dialog->SetContentsView(std::move(contents));
  auto* parent = host_.main_window.Run();
  dialog->SetModalType(ui::mojom::ModalType::kNone);
  auto* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog), /*context=*/gfx::NativeWindow(), parent ? parent->GetNativeView() : gfx::NativeView());
  installed_dialog_ = widget->GetWeakPtr();
  widget->Show();
}

}  // namespace zitter::content_embedder
