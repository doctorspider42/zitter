#!/usr/bin/env python3
"""Install the Zitter desktop and test targets into an existing, pinned Chromium checkout.

No downloads, git staging, or broad replacement of Chromium source files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

import editor_service_patch as editor_services
import fingerprint_protection_patch as fingerprint_protection

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCY = '      "//zitter:profile_workspace_browser_tests",\n'
MARKER = ".zitter-overlay.json"


# Chromium sources are UTF-8 with LF endings; don't let Windows' locale encoding
# or newline translation touch them.
def read(path):
    return path.read_bytes().decode("utf-8")


def write(path, text):
    path.write_bytes(text.encode("utf-8"))


def patch_build(text):
    target = '  test("browser_tests") {'
    if text.count(target) != 1:
        raise ValueError("Expected exactly one desktop browser_tests target")
    start = text.index(target)
    anchor = "    deps = [\n"
    position = text.index(anchor, start) + len(anchor)
    if text.count(DEPENDENCY) == 1 and text[position:].startswith(DEPENDENCY):
        return text
    if "//zitter:profile_workspace_browser_tests" in text:
        raise ValueError("Existing Zitter dependency is in an unexpected location")
    return text[:position] + DEPENDENCY + text[position:]


def overlay_files():
    files = {"BUILD.gn": ROOT / "chromium/BUILD.gn"}
    for directory in ("zitter", "chromium/prototype", "chromium/app", "chromium/installer", "testdata", "assets/fonts"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.is_file():
                files[path.relative_to(ROOT).as_posix()] = path
    return files


def patch_keychain(text):
    # Keep OSCrypt's random key and native Keychain protection, with our own
    # identity. Reusing Chromium's item can block behind another app's ACL.
    for old, new in (("Chromium Safe Storage", "Zitter Safe Storage"),
                     ('"Chromium";', '"Zitter";')):
        if text.count(new) == 1 and old not in text:
            continue
        if text.count(old) != 1 or new in text:
            raise ValueError("Unexpected upstream Keychain identity; review the patch")
        text = text.replace(old, new)
    return text



def patch_embedded_autofill(text):
    # Standalone WebContents have no Chrome address-save bubble. Keep profile
    # preferences intact, but advertise only capabilities the host provides.
    anchor = "bool ChromeAutofillClient::IsAutofillProfileEnabled() const {\n"
    guard = ('#if !BUILDFLAG(IS_ANDROID)\n'
             '  // Zitter: standalone hosts do not provide Chrome address-save UI.\n'
             '  if (!tabs::TabInterface::MaybeGetFromContents(web_contents())) {\n'
             '    return false;\n'
             '  }\n'
             '#endif\n')
    if text.count(anchor + guard) == 1:
        return text
    if text.count(anchor) != 1 or 'Zitter: standalone hosts' in text:
        raise ValueError("Unexpected upstream Autofill capability layout; review the patch")
    return text.replace(anchor, anchor + guard, 1)

def patch_media_dispatcher(text, header=False):
    # Keep all capture policy, request-state and output-protection handling in
    # Chromium's dispatcher; expose only registration of our tab-source adapter.
    if header:
        anchor = "  void ProcessMediaAccessRequest(content::WebContents* web_contents,\n"
        addition = ("  // Zitter: register capture handlers through the policy dispatcher.\n"
                    "  void AddMediaAccessHandler(std::unique_ptr<MediaAccessHandler> handler);\n\n")
    else:
        anchor = "void MediaCaptureDevicesDispatcher::ProcessMediaAccessRequest(\n"
        addition = ("// Zitter: register capture handlers through the policy dispatcher.\n"
                    "void MediaCaptureDevicesDispatcher::AddMediaAccessHandler(\n"
                    "    std::unique_ptr<MediaAccessHandler> handler) {\n"
                    "  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);\n"
                    "  CHECK(handler);\n"
                    "  media_access_handlers_.insert(media_access_handlers_.begin(), std::move(handler));\n"
                    "}\n\n")
    if text.count(addition + anchor) == 1 and text.count(anchor) == 1:
        return text
    if text.count(anchor) != 1 or "Zitter: register capture handlers" in text:
        raise ValueError("Unexpected upstream media dispatcher layout; review the registration hook")
    return text.replace(anchor, addition + anchor, 1)


# Zitter tabs live outside Browser windows. Extensions reach them through one
# embedder hook: GetTabById for callers that need no window (tabs.sendMessage,
# scripting.executeScript, action.setIcon({tabId}), ...), tabs.get, and
# tabs.query, which lists them as one window per profile.
EXTENSION_TABS_DECLARATION = """  // Zitter: tabs an embedder hosts outside Browser windows. They have no
  // window; GetTabById finds them when no window is asked for, and the tabs
  // API lists them after the browsers' tabs.
  class EmbedderTabs {
   public:
    virtual ~EmbedderTabs() = default;
    // The profile's tabs in order.
    virtual std::vector<content::WebContents*> GetTabs(Profile* profile) = 0;
    // The tab on screen for the profile, which counts as active.
    virtual content::WebContents* GetShownTab(Profile* profile) = 0;
    // The window holding the profile's tabs (no Browser behind it), or null.
    virtual WindowController* GetWindow(Profile* profile) = 0;
    // Tab operations of the tabs API, performed by the embedder.
    virtual content::WebContents* OpenTab(Profile* profile, const GURL& url,
                                          bool active) = 0;
    virtual bool ActivateTab(content::WebContents* contents) = 0;
    virtual bool CloseTab(content::WebContents* contents) = 0;
    virtual bool MoveTab(content::WebContents* contents, int index) = 0;
    // The embedder's Split View: pairs two tabs and returns the split's id
    // (-1 when it cannot), separates a split, and names a tab's split (-1).
    virtual int SplitTabs(content::WebContents* first,
                          content::WebContents* second) = 0;
    virtual bool Unsplit(Profile* profile, int split_view_id) = 0;
    virtual int GetSplitId(content::WebContents* contents) = 0;
  };
  static void SetEmbedderTabs(EmbedderTabs* tabs);
  static EmbedderTabs* GetEmbedderTabs();

"""
EXTENSION_TABS_DEFINITIONS = """// Zitter: tabs hosted outside Browser windows.
namespace {
ExtensionTabUtil::EmbedderTabs* g_embedder_tabs = nullptr;
}  // namespace

// static
void ExtensionTabUtil::SetEmbedderTabs(EmbedderTabs* tabs) {
  g_embedder_tabs = tabs;
}

// static
ExtensionTabUtil::EmbedderTabs* ExtensionTabUtil::GetEmbedderTabs() {
  return g_embedder_tabs;
}

"""
EXTENSION_TABS_LOOKUP = """  // Zitter: tabs hosted outside Browser windows, when no window is asked for.
  if (g_embedder_tabs && !out_window) {
    for (Profile* candidate : {profile, incognito_profile}) {
      if (!candidate) {
        continue;
      }
      const std::vector<WebContents*> tabs = g_embedder_tabs->GetTabs(candidate);
      for (size_t i = 0; i < tabs.size(); ++i) {
        if (sessions::SessionTabHelper::IdForTab(tabs[i]).id() != tab_id) {
          continue;
        }
        if (out_contents) {
          *out_contents = tabs[i];
        }
        if (out_tab_index) {
          *out_tab_index = static_cast<int>(i);
        }
        return true;
      }
    }
  }

"""
TABS_GET_OLD = """  int tab_index = -1;
  std::string error;
  if (!tabs_util::GetTabById(tab_id, browser_context(),
                             include_incognito_information(), &window,
                             &contents, &tab_index, &error)) {
"""
TABS_GET_NEW = """  int tab_index = -1;
  std::string error;
  if (!tabs_util::GetTabById(tab_id, browser_context(),
                             include_incognito_information(), &window,
                             &contents, &tab_index, &error) &&
      // Zitter: a tab hosted outside Browser windows has no window.
      !ExtensionTabUtil::GetTabById(tab_id, browser_context(),
                                    include_incognito_information(), nullptr,
                                    &contents, &tab_index)) {
"""
TABS_QUERY_ANCHOR = """      result.Append(CreateTabObjectHelper(tab->GetContents(), extension(),
                                          source_context_type(), browser, i)
                        .ToValue());
    }
  }
  return result;
}
"""
TABS_QUERY_ADDITION = """      result.Append(CreateTabObjectHelper(tab->GetContents(), extension(),
                                          source_context_type(), browser, i)
                        .ToValue());
    }
  }
  // Zitter: tabs hosted outside Browser windows form one window per profile,
  // the current window when the caller has no browser window, and the last
  // focused one while no browser window is active.
  ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
  if (!embedder || !window_type.empty() || window_id >= 0 ||
      (window_id == extension_misc::kCurrentWindowId && current_browser) ||
      !MatchesBool(query_info_.current_window, !current_browser) ||
      !MatchesBool(query_info_.last_focused_window,
                   !GlobalBrowserCollection::GetInstance()->GetActiveBrowser())) {
    return result;
  }
  Profile* profile = Profile::FromBrowserContext(browser_context());
  content::WebContents* shown = embedder->GetShownTab(profile);
  const std::vector<content::WebContents*> embedded = embedder->GetTabs(profile);
  for (size_t i = 0; i < embedded.size(); ++i) {
    content::WebContents* contents = embedded[i];
    if (tab_index > -1 && static_cast<int>(i) != tab_index) {
      continue;
    }
    if (!MatchesBool(query_info_.active, contents == shown) ||
        !MatchesBool(query_info_.highlighted, contents == shown) ||
        !MatchesBool(query_info_.pinned, false) ||
        !MatchesBool(query_info_.audible, contents->IsCurrentlyAudible()) ||
        !MatchesBool(query_info_.muted, contents->IsAudioMuted()) ||
        !MatchesBool(query_info_.frozen, false) ||
        !MatchesBool(query_info_.discarded, false) ||
        (query_info_.group_id && *query_info_.group_id != -1) ||
        (query_info_.split_view_id &&
         *query_info_.split_view_id != embedder->GetSplitId(contents))) {
      continue;
    }
    ExtensionTabUtil::ScrubTabBehavior scrub =
        ExtensionTabUtil::GetScrubTabBehavior(extension(), source_context_type(),
                                              contents);
    const bool hidden = scrub.committed_info == ExtensionTabUtil::kScrubTabFully;
    if (query_info_.title && !query_info_.title->empty() &&
        (hidden || !base::MatchPattern(contents->GetTitle(),
                                       base::UTF8ToUTF16(*query_info_.title)))) {
      continue;
    }
    if (!url_patterns.is_empty() &&
        (hidden || !url_patterns.MatchesURL(contents->GetLastCommittedURL()))) {
      continue;
    }
    if (query_info_.status != tabs::TabStatus::kNone &&
        query_info_.status != ExtensionTabUtil::GetLoadingStatus(contents)) {
      continue;
    }
    result.Append(CreateTabObjectHelper(contents, extension(),
                                        source_context_type(), nullptr, i)
                      .ToValue());
  }
  return result;
}
"""
TABS_INCLUDE_ANCHOR = '#include "base/strings/pattern.h"\n'
TABS_INCLUDE = '#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"\n'


TAB_OBJECT_ACTIVE_OLD = """  bool is_active = tab_interface && tab_interface->IsActivated();
"""
TAB_OBJECT_ACTIVE_NEW = """  bool is_active = tab_interface && tab_interface->IsActivated();
  // Zitter: the embedder's tab on screen is its window's active tab.
  const bool embedder_shown =
      !tab_interface && GetEmbedderTabs() &&
      GetEmbedderTabs()->GetShownTab(
          Profile::FromBrowserContext(contents->GetBrowserContext())) == contents;
  is_active = is_active || embedder_shown;
"""
TAB_OBJECT_HIGHLIGHTED_OLD = """  tab_object.highlighted = tab_interface && tab_interface->IsSelected();
"""
TAB_OBJECT_HIGHLIGHTED_NEW = """  tab_object.highlighted =
      embedder_shown || (tab_interface && tab_interface->IsSelected());
"""
TABS_CREATE_OLD = """    browser = controller->GetBrowserWindowInterface();
  }

  // We didn't find a browser. Bail.
"""
TABS_CREATE_NEW = """    browser = controller->GetBrowserWindowInterface();
    // Zitter: the embedder's window opens the tab itself.
    ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
    if (!browser && embedder &&
        embedder->GetWindow(controller->profile()) == controller) {
      content::WebContents* contents = embedder->OpenTab(
          controller->profile(), validated_url_, active_.value_or(true));
      if (!contents) {
        return RespondNow(Error(ExtensionTabUtil::kTabStripNotEditableError));
      }
      if (!has_callback()) {
        return RespondNow(NoArguments());
      }
      return RespondNow(WithArguments(
          ExtensionTabUtil::CreateTabObject(
              contents,
              ExtensionTabUtil::GetScrubTabBehavior(
                  extension(), source_context_type(), contents),
              extension())
              .ToValue()));
    }
  }

  // We didn't find a browser. Bail.
"""
TABS_UPDATE_OLD = """                             &contents, &tab_index, &error)) {
    return RespondNow(Error(std::move(error)));
  }

  if (DevToolsWindow::IsDevToolsWindow(contents)) {
    return RespondNow(Error(tabs_constants::kNotAllowedForDevToolsError));
  }

  // GetTabById may return a null window for prerender tabs.
"""
TABS_UPDATE_NEW = """                             &contents, &tab_index, &error)) {
    // Zitter: a tab hosted outside Browser windows updates in its embedder.
    ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
    if (!embedder ||
        !ExtensionTabUtil::GetTabById(tab_id, browser_context(),
                                      include_incognito_information(), nullptr,
                                      &contents, &tab_index)) {
      return RespondNow(Error(std::move(error)));
    }
    const auto& properties = params->update_properties;
    if (properties.url) {
      base::expected<GURL, std::string> url =
          ExtensionTabUtil::PrepareURLForNavigation(*properties.url, extension(),
                                                    browser_context());
      if (!url.has_value()) {
        return RespondNow(Error(url.error()));
      }
      contents->GetController().LoadURL(*url, content::Referrer(),
                                        ui::PAGE_TRANSITION_FROM_API,
                                        std::string());
    }
    if (properties.active.value_or(false) ||
        properties.selected.value_or(false) ||
        properties.highlighted.value_or(false)) {
      embedder->ActivateTab(contents);
    }
    if (properties.muted) {
      contents->SetAudioMuted(*properties.muted);
    }
    if (!has_callback()) {
      return RespondNow(NoArguments());
    }
    return RespondNow(ArgumentList(tabs::Update::Results::Create(
        CreateTabObjectHelper(contents, extension(), source_context_type(),
                              nullptr, tab_index))));
  }

  if (DevToolsWindow::IsDevToolsWindow(contents)) {
    return RespondNow(Error(tabs_constants::kNotAllowedForDevToolsError));
  }

  // GetTabById may return a null window for prerender tabs.
"""
TABS_REMOVE_OLD = """bool TabsRemoveFunction::RemoveTab(int tab_id, std::string* error) {
  WindowController* window = nullptr;
  content::WebContents* contents = nullptr;
"""
TABS_REMOVE_NEW = """bool TabsRemoveFunction::RemoveTab(int tab_id, std::string* error) {
  WindowController* window = nullptr;
  content::WebContents* contents = nullptr;
  // Zitter: a tab hosted outside Browser windows closes in its embedder.
  if (ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
      embedder &&
      ExtensionTabUtil::GetTabById(tab_id, browser_context(),
                                   include_incognito_information(), nullptr,
                                   &contents, nullptr) &&
      !::tabs::TabInterface::MaybeGetFromContents(contents)) {
    if (!embedder->CloseTab(contents)) {
      *error = ExtensionTabUtil::kTabStripNotEditableError;
      return false;
    }
    return true;
  }
"""
TABS_MOVE_OLD = """  WindowController* source_window = nullptr;
  content::WebContents* contents = nullptr;
  int tab_index = -1;
  if (!tabs_util::GetTabById(tab_id, browser_context(),
                             include_incognito_information(), &source_window,
                             &contents, &tab_index, error)) {
    return false;
  }
"""
TABS_MOVE_NEW = """  WindowController* source_window = nullptr;
  content::WebContents* contents = nullptr;
  int tab_index = -1;
  // Zitter: a tab hosted outside Browser windows moves within its embedder.
  if (ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
      embedder &&
      ExtensionTabUtil::GetTabById(tab_id, browser_context(),
                                   include_incognito_information(), nullptr,
                                   &contents, &tab_index) &&
      !::tabs::TabInterface::MaybeGetFromContents(contents)) {
    WindowController* window = embedder->GetWindow(
        Profile::FromBrowserContext(contents->GetBrowserContext()));
    if (window_id && (!window || *window_id != window->GetWindowId())) {
      *error = tabs_constants::kInvalidWindowStateError;
      return false;
    }
    if (!embedder->MoveTab(contents, *new_index)) {
      *error = ExtensionTabUtil::kTabStripNotEditableError;
      return false;
    }
    tab_values.Append(CreateTabObjectHelper(contents, extension(),
                                            source_context_type(), nullptr,
                                            *new_index)
                          .ToValue());
    return true;
  }
  if (!tabs_util::GetTabById(tab_id, browser_context(),
                             include_incognito_information(), &source_window,
                             &contents, &tab_index, error)) {
    return false;
  }
"""
WINDOWS_GET_ALL_OLD = """  for (WindowController* controller : *WindowControllerList::GetInstance()) {
    if (!controller->GetBrowserWindowInterface() ||
"""
WINDOWS_GET_ALL_NEW = """  for (WindowController* controller : *WindowControllerList::GetInstance()) {
    // Zitter: the embedder's windows describe themselves.
    ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
    if (!controller->GetBrowserWindowInterface() && embedder &&
        embedder->GetWindow(controller->profile()) == controller) {
      if (windows_util::CanOperateOnWindow(this, controller,
                                           extractor.type_filters())) {
        window_list.Append(controller->CreateWindowValueForExtension(
            extension(), populate_tab_behavior, source_context_type()));
      }
      continue;
    }
    if (!controller->GetBrowserWindowInterface() ||
"""
WINDOWS_LAST_FOCUSED_OLD = """  if (!last_focused_browser) {
    return RespondNow(Error(tabs_constants::kNoLastFocusedWindowError));
  }
"""
WINDOWS_LAST_FOCUSED_NEW = """  if (!last_focused_browser) {
    // Zitter: the embedder's window when no browser window qualifies.
    ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
    WindowController* window =
        embedder ? embedder->GetWindow(Profile::FromBrowserContext(browser_context()))
                 : nullptr;
    if (window && windows_util::CanOperateOnWindow(this, window,
                                                   extractor.type_filters())) {
      return RespondNow(WithArguments(window->CreateWindowValueForExtension(
          extension(),
          extractor.populate_tabs() ? WindowController::kPopulateTabs
                                    : WindowController::kDontPopulateTabs,
          source_context_type())));
    }
    return RespondNow(Error(tabs_constants::kNoLastFocusedWindowError));
  }
"""
WINDOWS_REMOVE_OLD = """  TabListInterface* tab_list =
      TabListInterface::From(window_controller->GetBrowserWindowInterface());
  if (tab_list && !tab_list->IsThisTabListEditable()) {
"""
WINDOWS_REMOVE_NEW = """  // Zitter: the embedder's window holds every profile; extensions close tabs.
  if (ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
      embedder && !window_controller->GetBrowserWindowInterface() &&
      embedder->GetWindow(window_controller->profile()) == window_controller) {
    return RespondNow(Error(ExtensionTabUtil::kTabStripNotEditableError));
  }
  TabListInterface* tab_list =
      TabListInterface::From(window_controller->GetBrowserWindowInterface());
  if (tab_list && !tab_list->IsThisTabListEditable()) {
"""

CURRENT_WINDOW_ANCHOR = """  // NOTE(rafaelw): This can return NULL in some circumstances. In particular,
"""
CURRENT_WINDOW_ADDITION = """  // Zitter: the embedder's window when the profile has no browser window.
  if (!window_controller && ExtensionTabUtil::GetEmbedderTabs()) {
    window_controller = ExtensionTabUtil::GetEmbedderTabs()->GetWindow(profile);
  }

"""
FUNCTION_DETAILS_INCLUDE_ANCHOR = '#include "chrome/browser/extensions/browser_extension_window_controller.h"\n'
FUNCTION_DETAILS_INCLUDE = '#include "chrome/browser/extensions/extension_tab_util.h"\n'

WINDOWS_CREATE_ANCHOR = """  Profile* window_profile =
      incognito_result == windows_util::IncognitoResult::kIncognito
          ? calling_profile->GetPrimaryOTRProfile(/*create_if_needed=*/true)
          : calling_profile;
"""
WINDOWS_CREATE_ADDITION = """
  // Zitter: one window holds a profile's tabs. A normal window for that
  // profile opens its pages as tabs there and answers with that window.
  if (ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
      embedder && window_profile == calling_profile &&
      (!create_data_ || ((create_data_->type == windows::CreateType::kNone ||
                          create_data_->type == windows::CreateType::kNormal) &&
                         !create_data_->tab_id))) {
    if (WindowController* window = embedder->GetWindow(window_profile)) {
      const bool focused = !create_data_ || create_data_->focused.value_or(true);
      for (size_t i = 0; i < urls_.size(); ++i) {
        embedder->OpenTab(window_profile, urls_[i], focused && i == 0);
      }
      if (focused) {
        window->window()->Activate();
      }
      return RespondNow(WithArguments(window->CreateWindowValueForExtension(
          extension(), WindowController::kPopulateTabs, source_context_type())));
    }
  }
"""
WINDOWS_UPDATE_OLD = """  if (!browser) {
    return RespondNow(Error(ExtensionTabUtil::kNoCrashBrowserError));
  }
  ui::BaseWindow* browser_window = browser->GetWindow();
"""
WINDOWS_UPDATE_NEW = """  if (!browser) {
    // Zitter: the embedder's window takes focus, state and bounds.
    ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
    if (!embedder ||
        embedder->GetWindow(window_controller->profile()) != window_controller) {
      return RespondNow(Error(ExtensionTabUtil::kNoCrashBrowserError));
    }
    ui::BaseWindow* window = window_controller->window();
    gfx::Rect bounds =
        window->IsMinimized() ? window->GetRestoredBounds() : window->GetBounds();
    const bool set_bounds = UpdateWindowBoundsFromParams(*params, bounds);
    if (set_bounds && !WindowBoundsIntersectDisplays(bounds)) {
      return RespondNow(Error(tabs_constants::kInvalidWindowBoundsError));
    }
    const ui::mojom::WindowShowState state =
        ConvertToWindowShowState(params->update_info.state);
    if (state == ui::mojom::WindowShowState::kFullscreen ||
        (set_bounds && (state == ui::mojom::WindowShowState::kMinimized ||
                        state == ui::mojom::WindowShowState::kMaximized))) {
      return RespondNow(Error(tabs_constants::kInvalidWindowStateError));
    }
    if (state == ui::mojom::WindowShowState::kMinimized) {
      window->Minimize();
    } else if (state == ui::mojom::WindowShowState::kMaximized) {
      window->Maximize();
    } else if (state == ui::mojom::WindowShowState::kNormal) {
      window->Restore();
    }
    if (set_bounds) {
      window->SetBounds(bounds);
    }
    if (params->update_info.focused) {
      if (*params->update_info.focused) {
        window->Activate();
      } else {
        window->Deactivate();
      }
    }
    if (params->update_info.draw_attention) {
      window->FlashFrame(*params->update_info.draw_attention);
    }
    return RespondNow(WithArguments(window_controller->CreateWindowValueForExtension(
        extension(), WindowController::kDontPopulateTabs, source_context_type())));
  }
  ui::BaseWindow* browser_window = browser->GetWindow();
"""

TAB_OBJECT_SPLIT_OLD = """  tab_object.split_view_id = -1;
"""
TAB_OBJECT_SPLIT_NEW = """  tab_object.split_view_id = -1;
  // Zitter: the embedder's Split View.
  if (!tab_interface && GetEmbedderTabs()) {
    tab_object.split_view_id = GetEmbedderTabs()->GetSplitId(contents);
  }
"""
TABS_CREATE_SPLIT_OLD = """  std::vector<::tabs::TabHandle> tab_handles;
  tab_handles.reserve(tab_ids.size());
"""
TABS_CREATE_SPLIT_NEW = """  std::vector<::tabs::TabHandle> tab_handles;
  tab_handles.reserve(tab_ids.size());

  // Zitter: tabs hosted outside Browser windows split in their embedder.
  if (ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs()) {
    content::WebContents* first = nullptr;
    content::WebContents* second = nullptr;
    if (ExtensionTabUtil::GetTabById(tab_ids[0], browser_context(),
                                     include_incognito_information(), nullptr,
                                     &first, nullptr) &&
        ExtensionTabUtil::GetTabById(tab_ids[1], browser_context(),
                                     include_incognito_information(), nullptr,
                                     &second, nullptr) &&
        !::tabs::TabInterface::MaybeGetFromContents(first) &&
        !::tabs::TabInterface::MaybeGetFromContents(second)) {
      const int split_view_id = embedder->SplitTabs(first, second);
      if (split_view_id < 0) {
        return RespondNow(Error(tabs_constants::kSplitViewCreationFailedError));
      }
      return RespondNow(WithArguments(split_view_id));
    }
  }
"""
TABS_UNSPLIT_OLD = """  int split_view_id = params->split_view_id;
"""
TABS_UNSPLIT_NEW = """  int split_view_id = params->split_view_id;
  // Zitter: a split of the embedder's tabs.
  if (ExtensionTabUtil::EmbedderTabs* embedder = ExtensionTabUtil::GetEmbedderTabs();
      embedder && embedder->Unsplit(Profile::FromBrowserContext(browser_context()),
                                    split_view_id)) {
    return RespondNow(NoArguments());
  }
"""

def _patch_once(text, anchor, replacement, marker, what):
    if text.count(replacement) == 1:
        return text
    if text.count(anchor) != 1 or marker in text.replace(replacement, ''):
        raise ValueError(f"Unexpected upstream {what} layout; review the patch")
    return text.replace(anchor, replacement, 1)


def patch_extension_tabs(text, part):
    """part: 'header' (extension_tab_util.h), 'source' (.cc), 'tabs_api', 'windows_api' or 'function_details'."""
    if part == 'header':
        anchor = "  // Any out parameter (`window`, `contents`, & `tab_index`) may be null.\n"
        return _patch_once(text, anchor, EXTENSION_TABS_DECLARATION + anchor, "class EmbedderTabs", "ExtensionTabUtil")
    if part == 'source':
        anchor = ("// static\nbool ExtensionTabUtil::GetTabById(int tab_id,\n"
                  "                                  content::BrowserContext* browser_context,\n"
                  "                                  bool include_incognito,\n"
                  "                                  WindowController** out_window,\n")
        text = _patch_once(text, anchor, EXTENSION_TABS_DEFINITIONS + anchor, "g_embedder_tabs = nullptr", "ExtensionTabUtil")
        lookup_anchor = "  // Prerendering tab is not visible and it cannot be in `TabStripModel`, if the\n"
        text = _patch_once(text, lookup_anchor, EXTENSION_TABS_LOOKUP + lookup_anchor,
                           "tabs hosted outside Browser windows, when", "GetTabById")
        text = _patch_once(text, TAB_OBJECT_ACTIVE_OLD, TAB_OBJECT_ACTIVE_NEW, "embedder_shown =", "tab object")
        text = _patch_once(text, TAB_OBJECT_HIGHLIGHTED_OLD, TAB_OBJECT_HIGHLIGHTED_NEW,
                           "embedder_shown ||", "tab object")
        return _patch_once(text, TAB_OBJECT_SPLIT_OLD, TAB_OBJECT_SPLIT_NEW, "the embedder's Split View", "tab object")
    if part == 'function_details':
        text = _patch_once(text, FUNCTION_DETAILS_INCLUDE_ANCHOR,
                           FUNCTION_DETAILS_INCLUDE_ANCHOR + FUNCTION_DETAILS_INCLUDE,
                           FUNCTION_DETAILS_INCLUDE, "function details include")
        return _patch_once(text, CURRENT_WINDOW_ANCHOR, CURRENT_WINDOW_ADDITION + CURRENT_WINDOW_ANCHOR,
                           "window when the profile has no browser window", "current window")
    if part == 'windows_api':
        text = _patch_once(text, WINDOWS_GET_ALL_OLD, WINDOWS_GET_ALL_NEW, "windows describe themselves", "windows.getAll")
        text = _patch_once(text, WINDOWS_LAST_FOCUSED_OLD, WINDOWS_LAST_FOCUSED_NEW,
                           "window when no browser window qualifies", "windows.getLastFocused")
        text = _patch_once(text, WINDOWS_CREATE_ANCHOR, WINDOWS_CREATE_ANCHOR + WINDOWS_CREATE_ADDITION,
                           "opens its\n  // pages as tabs there", "windows.create")
        text = _patch_once(text, WINDOWS_UPDATE_OLD, WINDOWS_UPDATE_NEW, "window takes focus, state and bounds",
                           "windows.update")
        return _patch_once(text, WINDOWS_REMOVE_OLD, WINDOWS_REMOVE_NEW, "window holds every profile", "windows.remove")
    text = _patch_once(text, TABS_INCLUDE_ANCHOR, TABS_INCLUDE_ANCHOR + TABS_INCLUDE,
                       "\n" + TABS_INCLUDE, "tabs API include")
    text = _patch_once(text, TABS_GET_OLD, TABS_GET_NEW, "a tab hosted outside Browser windows", "tabs.get")
    text = _patch_once(text, TABS_QUERY_ANCHOR, TABS_QUERY_ADDITION, "form one window per", "tabs.query")
    text = _patch_once(text, TABS_CREATE_OLD, TABS_CREATE_NEW, "window opens the tab itself", "tabs.create")
    text = _patch_once(text, TABS_UPDATE_OLD, TABS_UPDATE_NEW, "updates in its embedder", "tabs.update")
    text = _patch_once(text, TABS_REMOVE_OLD, TABS_REMOVE_NEW, "closes in its embedder", "tabs.remove")
    text = _patch_once(text, TABS_MOVE_OLD, TABS_MOVE_NEW, "moves within its embedder", "tabs.move")
    text = _patch_once(text, TABS_CREATE_SPLIT_OLD, TABS_CREATE_SPLIT_NEW, "split in their embedder", "tabs.createSplit")
    return _patch_once(text, TABS_UNSPLIT_OLD, TABS_UNSPLIT_NEW, "a split of the embedder", "tabs.unsplit")


EXTENSION_TABS_FILES = {
    "header": "chrome/browser/extensions/extension_tab_util.h",
    "source": "chrome/browser/extensions/extension_tab_util.cc",
    "tabs_api": "chrome/browser/extensions/api/tabs/tabs_api.cc",
    "windows_api": "chrome/browser/extensions/api/tabs/windows_api.cc",
    "function_details": "chrome/browser/extensions/chrome_extension_function_details.cc",
}



# chrome://extensions and chrome://history are the Chromium pages Zitter shows;
# they carry the Zitter mark (assets/icon/zitter.svg, unchanged) where Chromium
# puts its logo.
EXTENSIONS_LOGO_FILES = {
    "product-logo": "chrome/browser/resources/extensions/toolbar.html.ts",
    "header-icon": "chrome/browser/resources/extensions/manager.html.ts",
}


def patch_extensions_logo(text, slot):
    import base64
    mark = base64.b64encode((ROOT / "assets/icon/zitter.svg").read_bytes()).decode()
    indent = "  " if slot == "product-logo" else "    "
    inner = indent + "  "
    original = (f'<if expr="is_android">\n'
                f'{indent}<picture slot="{slot}">\n'
                f'{inner}<source media="(prefers-color-scheme: dark)"\n'
                f'{inner}    srcset="//resources/images/chrome_logo_dark.svg">\n'
                f'{inner}<img srcset="images/product_logo.png" role="presentation">\n'
                f'{indent}</picture>\n'
                f'</if>\n')
    replacement = (f'{indent}<!-- Zitter: its own mark on every platform. -->\n'
                   f'{indent}<picture slot="{slot}">\n'
                   f'{inner}<img src="data:image/svg+xml;base64,{mark}" alt="" role="presentation">\n'
                   f'{indent}</picture>\n')
    if text.count(replacement) == 1:
        return text
    if text.count(original) != 1 or "Zitter: its own mark" in text:
        raise ValueError("Unexpected upstream Extensions page logo layout; review the patch")
    return text.replace(original, replacement, 1)

# zitter.exe links chrome.dll's resources, whose icon group comes first; Windows
# shows it for the file, taskbar and window. All icons must be in that one .rc
# (their image IDs collide across .res files), so Zitter's takes Chromium's slot.
WINDOW_ICON_FILE = "chrome/app/chrome_dll.rc"


def patch_window_icon(text):
    original = ('#else\n'
                'IDR_MAINFRAME       ICON                        "theme\\chromium\\win\\chromium.ico"\n'
                '#endif\n')
    replacement = ('#else\n'
                   '// Zitter: zitter.exe\'s icon (chromium/app/zitter.ico).\n'
                   'IDR_MAINFRAME       ICON                        "../../zitter/chromium/app/zitter.ico"\n'
                   '#endif\n')
    if text.count(replacement) == 1:
        return text
    if text.count(original) != 1 or "Zitter: zitter.exe's icon" in text:
        raise ValueError("Unexpected upstream Windows icon resource layout; review the patch")
    return text.replace(original, replacement, 1)

# zitter.exe sideloads its design fonts (Fonts\ beside it) into the browser's
# DirectWrite collection, the hook web tests use for Ahem. Variable fonts must
# bring every named instance (weights 500-700), not only the default face.
DIRECT_WRITE_FILE = "ui/gfx/win/direct_write.cc"
DIRECT_WRITE_ANCHOR = ('    hr = factory3->CreateFontFileReference(path.value().c_str(), nullptr,\n'
                       '                                           &font_file);\n'
                       '    DCHECK(SUCCEEDED(hr));\n')
DIRECT_WRITE_HOOK = ('    // Zitter: every named instance of a variable font, as installed fonts.\n'
                     '    Microsoft::WRL::ComPtr<IDWriteFontSetBuilder1> font_set_builder1;\n'
                     '    if (SUCCEEDED(font_set_builder.As(&font_set_builder1))) {\n'
                     '      hr = font_set_builder1->AddFontFile(font_file.Get());\n'
                     '      DCHECK(SUCCEEDED(hr));\n'
                     '      continue;\n'
                     '    }\n')


def patch_direct_write(text):
    if text.count(DIRECT_WRITE_ANCHOR + DIRECT_WRITE_HOOK) == 1:
        return text
    if text.count(DIRECT_WRITE_ANCHOR) != 1 or "Zitter: every named instance" in text:
        raise ValueError("Unexpected upstream DirectWrite sideload layout; review the patch")
    return text.replace(DIRECT_WRITE_ANCHOR, DIRECT_WRITE_ANCHOR + DIRECT_WRITE_HOOK, 1)

# Find in page (design: Find & Print): the current match on the design's lime
# `zing`, the other matches on its light violet, instead of Blink's orange and
# yellow. Forced colors keep the system's highlight.
SEARCH_HIGHLIGHT_FILE = "third_party/blink/renderer/core/layout/layout_theme.cc"
SEARCH_HIGHLIGHT_OLD = ('    return Color(255, 150, 50);  // Orange.\n'
                        '  }\n'
                        '  return Color(255, 255, 0);  // Yellow.\n')
SEARCH_HIGHLIGHT_NEW = ('    return Color(0xD7, 0xFF, 0x3F);  // Zitter: `zing`.\n'
                        '  }\n'
                        '  return Color(0xE4, 0xE1, 0xFF);  // Zitter: light violet.\n')


def patch_search_highlight(text):
    return _patch_once(text, SEARCH_HIGHLIGHT_OLD, SEARCH_HIGHLIGHT_NEW, "Zitter: `zing`", "find-in-page highlight")


# Chromium on Windows installs the extensions other apps register for Google
# Chrome (HKCU/HKLM Software\Google\Chrome\Extensions). They are not Zitter's:
# zitter.exe passes this switch so that provider is never created.
REGISTRY_EXTENSIONS_FILE = "chrome/browser/extensions/external_provider_impl.cc"
REGISTRY_EXTENSIONS_SWITCH = "zitter-ignore-chrome-registry-extensions"
REGISTRY_EXTENSIONS_OLD = ('#if BUILDFLAG(IS_WIN)\n'
                           '    auto registry_provider = std::make_unique<ExternalProviderImpl>(\n'
                           '        service, new ExternalRegistryLoader, profile,\n'
                           '        ManifestLocation::kExternalRegistry,\n'
                           '        ManifestLocation::kExternalPrefDownload, Extension::NO_FLAGS);\n'
                           '    registry_provider->set_allow_updates(true);\n'
                           '    provider_list->push_back(std::move(registry_provider));\n')
REGISTRY_EXTENSIONS_NEW = ('#if BUILDFLAG(IS_WIN)\n'
                           '    // Zitter: other apps\' extensions for Google Chrome are not Zitter\'s.\n'
                           '    if (!base::CommandLine::ForCurrentProcess()->HasSwitch(\n'
                           f'            "{REGISTRY_EXTENSIONS_SWITCH}")) {{\n'
                           '      auto registry_provider = std::make_unique<ExternalProviderImpl>(\n'
                           '          service, new ExternalRegistryLoader, profile,\n'
                           '          ManifestLocation::kExternalRegistry,\n'
                           '          ManifestLocation::kExternalPrefDownload, Extension::NO_FLAGS);\n'
                           '      registry_provider->set_allow_updates(true);\n'
                           '      provider_list->push_back(std::move(registry_provider));\n'
                           '    }\n')


def patch_registry_extensions(text):
    if text.count(REGISTRY_EXTENSIONS_NEW) == 1:
        return text
    if text.count(REGISTRY_EXTENSIONS_OLD) != 1 or "Zitter: other apps'" in text:
        raise ValueError("Unexpected upstream registry extension provider layout; review the patch")
    return text.replace(REGISTRY_EXTENSIONS_OLD, REGISTRY_EXTENSIONS_NEW, 1)

HISTORY_LOGO_FILES = {
    "template": "chrome/browser/resources/history/history_toolbar.html.ts",
    "style": "chrome/browser/resources/history/history_toolbar.css",
}
HISTORY_LOGO_STYLE = """
/* Zitter: its mark sits in cr-toolbar's logo slot, sized like Chromium's logo. */
cr-toolbar[narrow] picture[slot='product-logo'] {
  display: none;
}

picture[slot='product-logo'] {
  display: inline-block;
  height: 24px;
  margin-inline-end: 16px;
  width: 24px;
}

picture[slot='product-logo'] img {
  display: block;
  height: 100%;
  width: 100%;
}
"""


def patch_history_logo(text, part):
    """Fills cr-toolbar's product-logo slot, which otherwise shows Chrome's logo."""
    if part == "style":
        if text.count(HISTORY_LOGO_STYLE) == 1:
            return text
        if "#css_wrapper_metadata_end */\n" not in text or "Zitter: its mark" in text:
            raise ValueError("Unexpected upstream History toolbar style; review the patch")
        return text + HISTORY_LOGO_STYLE
    import base64
    mark = base64.b64encode((ROOT / "assets/icon/zitter.svg").read_bytes()).decode()
    original = ('    narrow-threshold="1023" @search-changed="${this.onSearchChanged_}">\n'
                '</cr-toolbar>\n')
    replacement = ('    narrow-threshold="1023" @search-changed="${this.onSearchChanged_}">\n'
                   '  <!-- Zitter: its own mark on every platform. -->\n'
                   '  <picture slot="product-logo">\n'
                   f'    <img src="data:image/svg+xml;base64,{mark}" alt="" role="presentation">\n'
                   '  </picture>\n'
                   '</cr-toolbar>\n')
    if text.count(replacement) == 1:
        return text
    if text.count(original) != 1 or "Zitter: its own mark" in text:
        raise ValueError("Unexpected upstream History page toolbar layout; review the patch")
    return text.replace(original, replacement, 1)


def patch_component_registration(text):
    """Optional embedder exclusions, leaving upstream verification untouched."""
    include_anchor = '#include "base/check.h"\n'
    includes = ('#include "base/command_line.h"\n'
                '#include "base/strings/string_split.h"\n')
    anchor = ('bool CrxUpdateService::RegisterComponent(\n'
              '    const ComponentRegistration& component) {\n'
              '  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);\n')
    guard = ('  // Zitter: exclude unused components before registration or network access.\n'
             '  const std::string disabled_value =\n'
             '      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(\n'
             '          "component-updater-disabled-ids");\n'
             '  const auto disabled = base::SplitStringPiece(\n'
             '      disabled_value, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);\n'
             '  if (std::find(disabled.begin(), disabled.end(), component.app_id) != disabled.end())\n'
             '    return false;\n')
    if text.count(anchor + guard) == 1 and text.count(include_anchor + includes) == 1:
        return text
    if (text.count(anchor) != 1 or text.count(include_anchor) != 1 or
            'Zitter: exclude unused components' in text):
        raise ValueError("Unexpected upstream component registration layout; review policy hook")
    return text.replace(include_anchor, include_anchor + includes, 1).replace(anchor, anchor + guard, 1)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_existing(destination):
    if not destination.exists():
        return
    marker = destination / MARKER
    if not marker.is_file():
        raise ValueError(f"Refusing to replace an unowned directory: {destination}")
    manifest = json.loads(read(marker))
    if manifest.get("owner") != "zitter-prototype":
        raise ValueError("Invalid overlay ownership marker")
    hashes = manifest["files"]
    actual = {p.relative_to(destination).as_posix() for p in destination.rglob("*")
              if p.is_file() and p.name != MARKER}
    if actual != set(hashes):
        raise ValueError("Overlay has added or deleted files; preserve local work first")
    for name, expected in hashes.items():
        if digest(destination / name) != expected:
            raise ValueError(f"Overlay has local modifications: {name}")


def install(checkout, dry_run=False):
    checkout = checkout.resolve()
    lock = json.loads(read(ROOT / "chromium/revision.json"))
    revision = subprocess.check_output(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
    if revision != lock["revision"]:
        raise ValueError(f"Chromium revision mismatch: {revision}; expected {lock['revision']}")
    editor_patch = editor_services.prepare(checkout)
    fingerprint_patch = fingerprint_protection.prepare(checkout)
    build = checkout / "chrome/test/BUILD.gn"
    original = read(build)
    patched = patch_build(original)
    keychain = checkout / "components/os_crypt/common/keychain_password_mac.mm"
    keychain_original = read(keychain)
    keychain_patched = patch_keychain(keychain_original)
    autofill = checkout / "chrome/browser/ui/autofill/chrome_autofill_client.cc"
    autofill_original = read(autofill)
    autofill_patched = patch_embedded_autofill(autofill_original)
    media_header = checkout / "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
    media_source = media_header.with_suffix(".cc")
    media_header_original, media_source_original = read(media_header), read(media_source)
    media_header_patched = patch_media_dispatcher(media_header_original, header=True)
    media_source_patched = patch_media_dispatcher(media_source_original)
    component_service = checkout / "components/component_updater/component_updater_service.cc"
    component_original = read(component_service)
    component_patched = patch_component_registration(component_original)
    extensions_logo = {checkout / path: read(checkout / path) for path in EXTENSIONS_LOGO_FILES.values()}
    extensions_logo_patched = {path: patch_extensions_logo(text, slot) for (slot, _), (path, text)
                               in zip(EXTENSIONS_LOGO_FILES.items(), extensions_logo.items())}
    registry_extensions = checkout / REGISTRY_EXTENSIONS_FILE
    registry_extensions_original = read(registry_extensions)
    registry_extensions_patched = patch_registry_extensions(registry_extensions_original)
    direct_write = checkout / DIRECT_WRITE_FILE
    direct_write_original = read(direct_write)
    direct_write_patched = patch_direct_write(direct_write_original)
    search_highlight = checkout / SEARCH_HIGHLIGHT_FILE
    search_highlight_original = read(search_highlight)
    search_highlight_patched = patch_search_highlight(search_highlight_original)
    window_icon = checkout / WINDOW_ICON_FILE
    window_icon_original = read(window_icon)
    window_icon_patched = patch_window_icon(window_icon_original)
    for part, path in HISTORY_LOGO_FILES.items():
        extensions_logo[checkout / path] = read(checkout / path)
        extensions_logo_patched[checkout / path] = patch_history_logo(extensions_logo[checkout / path], part)
    extension_tabs = {checkout / path: read(checkout / path) for path in EXTENSION_TABS_FILES.values()}
    extension_tabs_patched = {path: patch_extension_tabs(text, part) for (part, _), (path, text)
                              in zip(EXTENSION_TABS_FILES.items(), extension_tabs.items())}
    destination = checkout / "zitter"
    check_existing(destination)
    files = overlay_files()
    print(f"Chromium revision: {revision}")
    print(f"Overlay: {destination} ({len(files)} files)")
    print(f"Build hook: {build}")
    if dry_run:
        print("Dry run: no files changed")
        return
    # Prepare all files before touching the checkout's existing overlay.
    with tempfile.TemporaryDirectory(prefix="zitter-overlay-", dir=checkout) as staging:
        staged = Path(staging) / "zitter"
        staged.mkdir()
        hashes = {}
        for name, source in files.items():
            target = staged / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            hashes[name] = digest(target)
        write(staged / MARKER, json.dumps(
            {"owner": "zitter-prototype", "revision": revision, "files": hashes}, indent=2) + "\n")
        backup = Path(staging) / "previous"
        if destination.exists():
            destination.rename(backup)
        try:
            staged.rename(destination)
            # Preserve other edits in this file; add only our single dependency.
            if patched != original:
                write(build, patched)
            if keychain_patched != keychain_original:
                write(keychain, keychain_patched)
            if autofill_patched != autofill_original:
                write(autofill, autofill_patched)
            if media_header_patched != media_header_original:
                write(media_header, media_header_patched)
            if media_source_patched != media_source_original:
                write(media_source, media_source_patched)
            if component_patched != component_original:
                write(component_service, component_patched)
            for path, text in extension_tabs_patched.items():
                if text != extension_tabs[path]:
                    write(path, text)
            if registry_extensions_patched != registry_extensions_original:
                write(registry_extensions, registry_extensions_patched)
            if direct_write_patched != direct_write_original:
                write(direct_write, direct_write_patched)
            if window_icon_patched != window_icon_original:
                write(window_icon, window_icon_patched)
            if search_highlight_patched != search_highlight_original:
                write(search_highlight, search_highlight_patched)
            for path, text in extensions_logo_patched.items():
                if text != extensions_logo[path]:
                    write(path, text)
            editor_services.apply(editor_patch)
            fingerprint_protection.apply(fingerprint_patch)
        except Exception:
            if destination.exists():
                shutil.rmtree(destination)
            if backup.exists():
                backup.rename(destination)
            write(build, original)
            write(keychain, keychain_original)
            write(autofill, autofill_original)
            write(media_header, media_header_original)
            write(media_source, media_source_original)
            write(component_service, component_original)
            for path, text in {**extension_tabs, **extensions_logo}.items():
                write(path, text)
            raise
    print("Installed. Run gn gen/check and build zitter or zitter_prototype; see README.md.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path, help="Chromium src directory")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        install(args.checkout, args.dry_run)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Overlay installation failed: {error}\n")


if __name__ == "__main__":
    main()
