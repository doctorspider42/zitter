// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chrome/renderer/zitter_page_scripts.h"

#include <string>
#include <string_view>

#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "third_party/blink/public/web/web_view.h"
#include "url/gurl.h"

namespace zitter {

void RunPageScripts(content::RenderFrame* render_frame) {
  auto* frame = render_frame ? render_frame->GetWebFrame() : nullptr;
  if (!frame || !render_frame->GetWebView()) return;
  const auto& scripts = render_frame->GetWebView()->GetRendererPreferences().zitter_page_scripts;
  if (scripts.empty()) return;
  // about:blank and srcdoc frames run their creator's scripts, so they get the
  // scripts of the origin they inherit: ad code uses them for clean natives.
  const GURL url = frame->GetDocument().Url();
  std::string host = url.SchemeIsHTTPOrHTTPS() ? std::string(url.host()) : frame->GetSecurityOrigin().Host().Utf8();
  if (host.empty()) return;
  for (const auto& entry : scripts) {
    const auto newline = entry.find('\n');
    if (newline == std::string::npos) continue;
    const std::string_view site(entry.data(), newline);
    const bool match = site == "*" || host == site ||
        (host.size() > site.size() && host.ends_with(site) && host[host.size() - site.size() - 1] == '.');
    if (!match) continue;
    // The page's own world, so the scriptlets see and wrap what its code uses.
    frame->ExecuteScript(blink::WebScriptSource(blink::WebString::FromUtf8(std::string_view(entry).substr(newline + 1))));
  }
}

}  // namespace zitter
