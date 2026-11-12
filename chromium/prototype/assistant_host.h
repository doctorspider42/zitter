// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_HOST_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_HOST_H_

#include <optional>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/values.h"
#include "chromium/prototype/assistant_settings.h"
#include "ui/gfx/geometry/rect.h"

class GURL;
namespace content { class BrowserContext; class WebContents; }

namespace zitter::prototype {

class WorkspaceShell;

// The panel owns the conversation and helper process. The browser host owns
// profile selection, persistence, and access to browser data and actions.
class AssistantHost {
 public:
  virtual WorkspaceShell* shell() const = 0;
  virtual const AssistantSettings& assistant_settings() const = 0;
  virtual void SetAssistantSettings(AssistantSettings settings) = 0;
  virtual content::WebContents* AssistantCurrentPage() = 0;
  virtual bool AssistantPageActionsAvailable() = 0;
  virtual void RunAssistantPageAction(int command) = 0;
  virtual content::BrowserContext* AssistantBrowserContext() const = 0;
  virtual std::string AssistantProfileId() const = 0;
  virtual base::DictValue AssistantContext(const AssistantPermissions&) = 0;
  virtual void AssistantTool(std::string tool, base::DictValue args,
                             AssistantPermissions permissions,
                             base::OnceCallback<void(base::Value)> done,
                             bool user_confirmed, std::string background) = 0;
  virtual void WithAssistantApiKey(base::OnceCallback<void(std::string)> done) = 0;
  virtual bool AssistantStartBackground(const std::string& key,
                                        const std::string& from_tab,
                                        const GURL& url) = 0;
  virtual bool AssistantHasBackground(const std::string& key) const = 0;
  virtual bool AssistantTakeOver(const std::string& key) = 0;
  virtual void AssistantDiscardBackground(const std::string& key) = 0;
  virtual void AssistantElementBounds(
      std::string tab, std::string element,
      base::OnceCallback<void(std::optional<gfx::Rect>)> done) = 0;
  virtual void LoadAssistantSessions(
      base::OnceCallback<void(std::optional<base::DictValue>)> done) = 0;
  virtual void SaveAssistantSessions(base::DictValue sessions) = 0;
  virtual void AssistantStateChanged() = 0;
  virtual bool NavigateFromBar(const GURL& url, bool new_tab) = 0;

 protected:
  ~AssistantHost() = default;
};

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_HOST_H_
