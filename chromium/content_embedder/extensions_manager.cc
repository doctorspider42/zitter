// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "zitter_content/extensions_manager.h"

#include "chromium/prototype/workspace_urls.h"

#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "content/public/browser/webui_config_map.h"
#include "content/public/common/url_constants.h"

namespace zitter::content_embedder {
namespace {

constexpr char kHost[] = "extensions";
constexpr char kHtml[] =
#include "zitter_content/extensions_manager.html.inc"
    ;
constexpr char kCss[] =
#include "zitter_content/extensions_manager.css.inc"
    ;
constexpr char kScript[] =
#include "zitter_content/extensions_manager.js.inc"
    ;

using Dispatch = base::RepeatingCallback<base::DictValue(
    content::WebContents&, const base::DictValue&)>;

class ManagerPage final : public content::WebContentsUserData<ManagerPage> {
 public:
  const Dispatch dispatch;

 private:
  friend class content::WebContentsUserData<ManagerPage>;
  ManagerPage(content::WebContents* page, Dispatch callback)
      : content::WebContentsUserData<ManagerPage>(*page),
        dispatch(std::move(callback)) {}
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
WEB_CONTENTS_USER_DATA_KEY_IMPL(ManagerPage);

class ManagerSource final : public content::URLDataSource {
  std::string GetSource() override { return kHost; }
  std::string GetMimeType(const GURL& url) override {
    if (url.path() == "/manager.css")
      return "text/css";
    if (url.path() == "/manager.js")
      return "application/javascript";
    return "text/html";
  }
  bool ShouldServeMimeTypeAsContentTypeHeader() override { return true; }
  void StartDataRequest(const GURL& url,
                        const content::WebContents::Getter&,
                        GotDataCallback callback) override {
    std::string_view resource;
    if (url.path() == "/")
      resource = kHtml;
    else if (url.path() == "/manager.css")
      resource = kCss;
    else if (url.path() == "/manager.js")
      resource = kScript;
    if (resource.empty()) {
      std::move(callback).Run(nullptr);
      return;
    }
    std::move(callback).Run(
        base::MakeRefCounted<base::RefCountedString>(std::string(resource)));
  }
};

class ManagerHandler final : public content::WebUIMessageHandler {
  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "zitterExtensionsRequest",
        base::BindRepeating(&ManagerHandler::Request, base::Unretained(this)));
  }

  void Request(const base::ListValue& args) {
    if (args.size() != 2 || !args[0].is_int() || !args[1].is_dict())
      return;
    auto* page = web_ui()->GetWebContents();
    if (page->GetLastCommittedURL().GetWithoutRef() != GURL(prototype::kExtensionsTabUrl))
      return;
    AllowJavascript();
    const auto* binding = ManagerPage::FromWebContents(page);
    const base::Value reply(binding ? binding->dispatch.Run(*page, args[1].GetDict())
                                    : base::DictValue());
    if (IsJavascriptAllowed())
      CallJavascriptFunction("window.zitterExtensionsReply", args[0], reply);
  }
};

class ManagerController final : public content::WebUIController {
 public:
  explicit ManagerController(content::WebUI* ui)
      : content::WebUIController(ui) {
    content::URLDataSource::Add(ui->GetWebContents()->GetBrowserContext(),
                                std::make_unique<ManagerSource>());
    ui->AddMessageHandler(std::make_unique<ManagerHandler>());
  }
};

class ManagerConfig final : public content::DefaultWebUIConfig<ManagerController> {
 public:
  ManagerConfig() : DefaultWebUIConfig(content::kChromeUIScheme, kHost) {}
  bool IsWebUIEnabled(content::BrowserContext* context) override {
    return !context->IsOffTheRecord();
  }
};

}  // namespace

void RegisterExtensionsManagerWebUI() {
  static const bool registered = [] {
    content::WebUIConfigMap::GetInstance().AddWebUIConfig(
        std::make_unique<ManagerConfig>());
    return true;
  }();
  static_cast<void>(registered);
}

void AttachExtensionsManager(content::WebContents& page, Dispatch dispatch) {
  ManagerPage::CreateForWebContents(&page, std::move(dispatch));
}

}  // namespace zitter::content_embedder
