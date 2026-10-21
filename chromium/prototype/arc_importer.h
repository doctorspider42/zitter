#ifndef ZITTER_CHROMIUM_PROTOTYPE_ARC_IMPORTER_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ARC_IMPORTER_H_

#include <string_view>

#include "base/files/file_path.h"
#include "chromium/prototype/browser_data.h"

namespace zitter::prototype {
// Reads sidebar metadata and profile display names only. No source writes.
ArcImport ReadArcImport(const base::FilePath& arc_directory);
ArcImport ParseArcImport(std::string_view sidebar, std::string_view local_state);
base::FilePath DefaultArcDirectory();
}  // namespace zitter::prototype
#endif
