// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/night_shift.h"

#include "base/files/file_path.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/profiles/profile.h"
#include "night_shift_manifest.h"

namespace zitter::prototype {

void EnsureNightShift(Profile* profile) {
  if (!profile || !profile->IsRegularProfile()) {
    return;
  }
  auto* loader = extensions::ComponentLoader::Get(profile);
  if (!loader || loader->Exists(kNightShiftId)) {
    return;
  }
  loader->Add(kNightShiftManifest, base::FilePath(FILE_PATH_LITERAL("night_shift")));
}

}  // namespace zitter::prototype
