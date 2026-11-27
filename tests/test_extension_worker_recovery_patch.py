# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
import unittest
from unittest import mock

with mock.patch.object(sys, 'path', [str(Path(__file__).resolve().parents[1] / 'tools'), *sys.path]):
    import extension_worker_recovery_patch as recovery

SOURCE = '''#include "extensions/browser/service_worker/service_worker_task_queue.h"

#include <memory>
#include <optional>

#include "base/check.h"
#include "base/metrics/histogram_functions.h"
#include "base/time/time.h"

using content::BrowserContext;

namespace extensions {

namespace {

constexpr int kUnrelated = 0;

}  // namespace

void ServiceWorkerTaskQueue::OnWorkerStart() {
  ClearRetryState(context_id.token, worker_start_retries_,
                  kStartWorkerRetryHistogramPrefix,
                  /*success=*/true);
}

void ServiceWorkerTaskQueue::OnWorkerStartFail() {
  ClearRetryState(context_id.token, worker_start_retries_,
                  kStartWorkerRetryHistogramPrefix,
                  /*success=*/false);

  RunAndClearPendingTasksWithNullContext(context_id);
}
'''


class ExtensionWorkerRecoveryPatchTests(unittest.TestCase):
    def test_re_registers_after_failed_start_and_is_idempotent(self):
        patched = recovery.patch_task_queue(SOURCE)
        self.assertEqual(patched, recovery.patch_task_queue(patched))
        self.assertIn('#include <map>\n#include <memory>\n', patched)
        self.assertIn('#include "base/no_destructor.h"\n', patched)
        failure = patched.index('/*success=*/false);')
        recovery_at = patched.index('ZitterMayRecoverMissingWorker(context_id.token)')
        self.assertLess(failure, recovery_at)
        self.assertLess(recovery_at, patched.index('RunAndClearPendingTasksWithNullContext'))
        self.assertLess(patched.index('bool ZitterMayRecoverMissingWorker('),
                        patched.index('constexpr int kUnrelated'))
        self.assertIn('RE_REGISTER_ON_STATE_MISMATCH', patched)
        self.assertIn('worker_state->ResetForStorageWipe();', patched)

    def test_rejects_unknown_source(self):
        changed = SOURCE.replace('/*success=*/false);', '/*success=*/ false);')
        with self.assertRaises(ValueError):
            recovery.patch_task_queue(changed)

    def test_matches_the_pinned_chromium_checkout(self):
        source = Path(__file__).resolve().parents[1] / '.chromium/src' / recovery.SOURCE_PATH
        if not source.is_file():
            self.skipTest('Chromium checkout not present')
        text = source.read_text(encoding='utf-8')
        self.assertEqual(recovery.patch_task_queue(recovery.patch_task_queue(text)),
                         recovery.patch_task_queue(text))


if __name__ == '__main__':
    unittest.main()
