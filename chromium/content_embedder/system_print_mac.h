// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_SYSTEM_PRINT_MAC_H_
#define ZITTER_CONTENT_SYSTEM_PRINT_MAC_H_

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "ui/gfx/geometry/size_f.h"
#include "ui/gfx/native_ui_types.h"

namespace base {
class RefCountedMemory;
}

namespace zitter::content_embedder {

struct SystemPrintSettings {
  SystemPrintSettings();
  SystemPrintSettings(SystemPrintSettings&&);
  ~SystemPrintSettings();

  gfx::SizeF paper_points;
  bool landscape = false;
  base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>,
                          base::OnceCallback<void(bool)>)> submit;
};

void ShowMacPrintPanel(
    gfx::NativeWindow owner,
    std::u16string title,
    bool cancel_probe,
    const base::FilePath& probe_file,
    base::OnceCallback<void(std::optional<SystemPrintSettings>)> done);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_SYSTEM_PRINT_MAC_H_
