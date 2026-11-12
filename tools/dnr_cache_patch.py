"""Scope Chromium's DNR history-cache invalidation to the owning profile."""


def patch_rules_monitor(source):
    replacements = (
        ("#include <algorithm>\n", "#include <algorithm>\n#include <map>\n"),
        (
            '#include "base/memory/ptr_util.h"\n',
            '#include "base/memory/ptr_util.h"\n#include "base/memory/weak_ptr.h"\n',
        ),
        (
            '#include "content/public/browser/browser_thread.h"\n',
            '#include "content/public/browser/browser_thread.h"\n'
            '#include "content/public/browser/back_forward_cache.h"\n'
            '#include "content/public/browser/navigation_controller.h"\n'
            '#include "content/public/browser/render_process_host.h"\n'
            '#include "content/public/browser/web_contents.h"\n',
        ),
        (
            'namespace dnr_api = api::declarative_net_request;\n',
            '''namespace dnr_api = api::declarative_net_request;

// A rule update affects history entries only in this BrowserContext.
void FlushDnrBackForwardCache(content::BrowserContext* context) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::map<content::WebContents*, base::WeakPtr<content::WebContents>> pages;
  for (auto hosts = content::RenderProcessHost::AllHostsIterator();
       !hosts.IsAtEnd(); hosts.Advance()) {
    auto* process = hosts.GetCurrentValue();
    if (process->GetBrowserContext() != context) {
      continue;
    }
    process->ForEachRenderFrameHost([&pages](content::RenderFrameHost* frame) {
      auto* page = content::WebContents::FromRenderFrameHost(frame);
      if (page && !page->IsBeingDestroyed()) {
        pages.try_emplace(page, page->GetWeakPtr());
      }
    });
  }
  for (const auto& entry : pages) {
    if (entry.second) {
      entry.second->GetController().GetBackForwardCache().Flush();
    }
  }
}
''',
        ),
        (
            '          std::move(result.disabled_rule_ids_after_update));\n      break;',
            '          std::move(result.disabled_rule_ids_after_update));\n'
            '      ClearRendererCacheOnNavigation();\n'
            '      FlushDnrBackForwardCache(context_);\n      break;',
        ),
        (
            '    matcher->AddOrUpdateRulesets(std::move(new_matchers));\n',
            '    matcher->AddOrUpdateRulesets(std::move(new_matchers));\n'
            '    FlushDnrBackForwardCache(context_);\n',
        ),
        (
            '  ruleset_manager_.RemoveRuleset(extension_id);\n',
            '  ruleset_manager_.RemoveRuleset(extension_id);\n'
            '  FlushDnrBackForwardCache(context_);\n',
        ),
        (
            '  ruleset_manager_.AddRuleset(extension.id(), std::move(matcher));\n',
            '  ruleset_manager_.AddRuleset(extension.id(), std::move(matcher));\n'
            '  FlushDnrBackForwardCache(context_);\n',
        ),
        (
            '  matcher->AddOrUpdateRuleset(std::move(ruleset_matcher));\n',
            '  matcher->AddOrUpdateRuleset(std::move(ruleset_matcher));\n'
            '  FlushDnrBackForwardCache(context_);\n',
        ),
    )
    if all(source.count(updated) == 1 for _, updated in replacements):
        return source
    if any(source.count(updated) or source.count(original) != 1
           for original, updated in replacements):
        raise ValueError("Unexpected Chromium DNR rules monitor")
    for original, updated in replacements:
        source = source.replace(original, updated)
    return source
