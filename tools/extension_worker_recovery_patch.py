"""Re-register an extension worker whose registration vanished while it ran.

Chromium checks an extension's worker registration only when it activates the
extension. If the registration disappears later in the session, every start
fails with kErrorNotFound, queued tasks fail, and the extension stays broken
until the browser restarts (LastPass's popup spun forever this way). The patch
resets the worker state and registers the worker again, at most once a minute
per activation, and keeps the queued tasks for the new registration.

Only service_worker_task_queue.cc changes, so a build recompiles one file.
"""

SOURCE_PATH = "extensions/browser/service_worker/service_worker_task_queue.cc"

_HELPER = '''
// Zitter: allows one missing-registration recovery a minute per activation, so
// a registration that keeps disappearing can't make the queue loop.
bool ZitterMayRecoverMissingWorker(const base::UnguessableToken& activation) {
  static base::NoDestructor<std::map<base::UnguessableToken, base::TimeTicks>>
      recoveries;
  const base::TimeTicks now = base::TimeTicks::Now();
  std::erase_if(*recoveries, [now](const auto& entry) {
    return now - entry.second >= base::Minutes(1);
  });
  return recoveries->try_emplace(activation, now).second;
}
'''

_RECOVERY = '''
  // Zitter: the registration disappeared after activation. Register the worker
  // again and keep the queued tasks for it instead of failing them.
  if (status.status_code == blink::ServiceWorkerStatusCode::kErrorNotFound &&
      !browser_context_shutting_down_ &&
      ZitterMayRecoverMissingWorker(context_id.token)) {
    const Extension* extension =
        ExtensionRegistry::Get(browser_context_)
            ->enabled_extensions()
            .GetByID(context_id.extension_id);
    if (extension && BackgroundInfo::IsServiceWorkerBased(extension)) {
      LOG(WARNING) << "Registering the missing worker of "
                   << context_id.extension_id << " again";
      pending_storage_registrations_.erase(context_id.extension_id);
      RemoveRegisteredServiceWorkerInfo(context_id.extension_id);
      worker_registration_retries_.erase(context_id.token);
      worker_unregistration_wait_retries_.erase(context_id.token);
      worker_state->ResetForStorageWipe();
      RegisterServiceWorker(RegistrationReason::RE_REGISTER_ON_STATE_MISMATCH,
                            context_id, *extension);
      return;
    }
  }
'''

_FAILURE_ANCHOR = '''  ClearRetryState(context_id.token, worker_start_retries_,
                  kStartWorkerRetryHistogramPrefix,
                  /*success=*/false);
'''

REPLACEMENTS = (
    ("#include <memory>\n", "#include <map>\n#include <memory>\n"),
    (
        '#include "base/metrics/histogram_functions.h"\n',
        '#include "base/metrics/histogram_functions.h"\n#include "base/no_destructor.h"\n',
    ),
    ("namespace extensions {\n\nnamespace {\n",
     "namespace extensions {\n\nnamespace {\n" + _HELPER),
    (_FAILURE_ANCHOR, _FAILURE_ANCHOR + _RECOVERY),
)


def patch_task_queue(source):
    if all(source.count(updated) == 1 for _, updated in REPLACEMENTS):
        return source
    if any(source.count(updated) or source.count(original) != 1
           for original, updated in REPLACEMENTS):
        raise ValueError("Unexpected Chromium extension service worker task queue")
    for original, updated in REPLACEMENTS:
        source = source.replace(original, updated)
    return source
