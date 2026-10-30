// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_EXTENSION_TAB_EVENTS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_EXTENSION_TAB_EVENTS_H_
#include <set>
#include <string>
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
namespace zitter::prototype {

// chrome.tabs events for a Zitter page. Chromium's TabsEventRouter only
// watches TabStripModels, so onCreated, onUpdated (loading, url, title) and
// onRemoved come from here, to the page's own profile, scrubbed per extension
// exactly as Chromium scrubs them. onActivated comes from the window, which
// knows the page on screen.
class ExtensionTabEvents final : public content::WebContentsObserver,
                                 public content::WebContentsUserData<ExtensionTabEvents> {
 public:
  static void Attach(content::WebContents* contents);
  // The page on screen changed: chrome.tabs.onActivated.
  static void DispatchActivated(content::WebContents* contents);
  ~ExtensionTabEvents() override;

  // content::WebContentsObserver:
  void DidStartLoading() override;
  void DidStopLoading() override;
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void TitleWasSet(content::NavigationEntry* entry) override;
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<ExtensionTabEvents>;
  explicit ExtensionTabEvents(content::WebContents* contents);
  void Created();
  void Updated(std::set<std::string> changed);
  WEB_CONTENTS_USER_DATA_KEY_DECL();
  base::WeakPtrFactory<ExtensionTabEvents> weak_factory_{this};
};

}  // namespace zitter::prototype
#endif
