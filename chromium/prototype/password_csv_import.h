// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_PASSWORD_CSV_IMPORT_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PASSWORD_CSV_IMPORT_H_

#include <cstddef>

#include "base/functional/callback.h"
#include "ui/gfx/native_ui_types.h"

namespace content {
class WebContents;
}

namespace zitter::prototype {
// Passwords from a CSV that Chrome, Edge or another browser exported, into the
// password store of |contents|' profile. Chrome on Windows encrypts its
// passwords for itself (App-Bound Encryption), so its export is the way over
// (docs/windows-port.md, Decisions).
struct PasswordCsvOutcome {
  bool cancelled = false;  // The file picker was closed.
  bool imported = false;
  size_t count = 0;
};
// Asks for the file, imports it and reports once; owns itself until then.
void ImportPasswordsFromCsv(content::WebContents* contents, gfx::NativeWindow owner,
                            base::OnceCallback<void(PasswordCsvOutcome)> done);
}  // namespace zitter::prototype

#endif
