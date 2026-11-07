#!/usr/bin/env python3
"""Guarded Chromium support for DevTools docking in an embedder WebView."""

PATCHES = [
    (
        'chrome/browser/devtools/devtools_window.h',
        '#include "base/callback_list.h"',
        '''#include "base/callback_list.h"
#include "base/supports_user_data.h"''',
    ),
    (
        'chrome/browser/devtools/devtools_window.h',
        '  static const char kDevToolsApp[];',
        '''  // Optional docking host for pages outside a Browser tab strip. The page owns
  // this data; embedders must detach borrowed frontend views before teardown.
  struct EmbedderDockingHost : base::SupportsUserData::Data {
    explicit EmbedderDockingHost(base::RepeatingClosure update);
    ~EmbedderDockingHost() override;
    base::RepeatingClosure update;
  };
  static constexpr char kEmbedderDockingHostKey[] = "devtools-embedder-dock";
  static EmbedderDockingHost* GetEmbedderDockingHost(content::WebContents* page);

  static const char kDevToolsApp[];''',
    ),
    (
        'chrome/browser/devtools/devtools_window.cc',
        'const char DevToolsWindow::kDevToolsApp[] = "DevToolsApp";',
        '''DevToolsWindow::EmbedderDockingHost::EmbedderDockingHost(
    base::RepeatingClosure update) : update(std::move(update)) {}
DevToolsWindow::EmbedderDockingHost::~EmbedderDockingHost() = default;

DevToolsWindow::EmbedderDockingHost* DevToolsWindow::GetEmbedderDockingHost(
    content::WebContents* page) {
  return page ? static_cast<EmbedderDockingHost*>(
                    page->GetUserData(kEmbedderDockingHostKey)) : nullptr;
}

const char DevToolsWindow::kDevToolsApp[] = "DevToolsApp";''',
    ),
    (
        'chrome/browser/devtools/devtools_window.cc',
        '    if (!devtools_ui_controller || !devtools_ui_controller->CanDockDevtools()) {',
        '''    if ((!devtools_ui_controller || !devtools_ui_controller->CanDockDevtools()) &&
        !GetEmbedderDockingHost(inspected_web_contents)) {''',
    ),
    (
        'chrome/browser/devtools/devtools_window.cc',
        '  // Create WebContents with devtools.',
        '''  // Migrate Zitter's previously forced undocked state once. Later explicit
  // choices in the standard frontend menu are retained by Chromium.
  if (can_dock && GetEmbedderDockingHost(inspected_web_contents) &&
      !profile->GetPrefs()->GetDict(prefs::kDevToolsPreferences)
           .FindString("zitterDockingInitialized")) {
    for (const char* pref : {prefs::kDevToolsPreferences,
                            prefs::kDevToolsSyncedPreferencesSyncEnabled,
                            prefs::kDevToolsSyncedPreferencesSyncDisabled}) {
      ScopedDictPrefUpdate update(profile->GetPrefs(), pref);
      update->Remove("currentDockState");
    }
    ScopedDictPrefUpdate update(profile->GetPrefs(), prefs::kDevToolsPreferences);
    update->Set("currentDockState", "\\"right\\"");
    update->Set("zitterDockingInitialized", "true");
  }

  // Create WebContents with devtools.''',
    ),
    (
        'chrome/browser/devtools/devtools_window.cc',
        '    CHECK(inspected_browser, base::NotFatalUntil::M161);',
        '''    if (!inspected_browser) {
      if (auto* host = GetEmbedderDockingHost(inspected_web_contents)) {
        main_web_contents_->SetDelegate(this);
        main_web_contents_->SetIgnoreZoomGestures(true);
        host->update.Run();
        main_web_contents_->SetInitialFocus();
        PrefsTabHelper::CreateForWebContents(main_web_contents_);
        OverrideAndSyncDevToolsRendererPrefs();
        DoAction(action);
        return;
      }
    }
    CHECK(inspected_browser, base::NotFatalUntil::M161);''',
    ),
    (
        'chrome/browser/devtools/devtools_window_views.cc',
        '''  BrowserWindowInterface* inspected_browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
          inspected_web_contents);''',
        '''  if (auto* host = GetEmbedderDockingHost(inspected_web_contents)) {
    host->update.Run();
    return;
  }
  BrowserWindowInterface* inspected_browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
          inspected_web_contents);''',
    ),
    (
        'chrome/browser/devtools/devtools_window_views.cc',
        '  return content::KeyboardEventProcessingResult::NOT_HANDLED;',
        '''  auto* page = GetInspectedWebContents();
  if (GetEmbedderDockingHost(page) && page->GetDelegate()) {
    return page->GetDelegate()->PreHandleKeyboardEvent(page, event);
  }
  return content::KeyboardEventProcessingResult::NOT_HANDLED;''',
    ),
]


def prepare(checkout):
    result = {}
    for name, old, new in PATCHES:
        path = checkout / name
        original, text = result.get(path, (path.read_text(), path.read_text()))
        if new not in text:
            if text.count(old) != 1:
                raise ValueError(f"Unexpected DevTools docking anchor in {name}")
            text = text.replace(old, new, 1)
        result[path] = (original, text)
    return result


def apply(plan):
    for path, (old, new) in plan.items():
        if old != new:
            path.write_bytes(new.encode("utf-8"))
