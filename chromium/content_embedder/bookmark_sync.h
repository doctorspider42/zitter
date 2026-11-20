// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CONTENT_BOOKMARK_SYNC_H_
#define ZITTER_CONTENT_BOOKMARK_SYNC_H_
#include "base/files/file_path.h"
#include "base/values.h"
#include <optional>
namespace zitter::content_embedder::bookmark_sync {
// A collection contains ordered pins. The ledger retains a version per pin,
// including tombstones, so a device returning from offline cannot undo deletions.
base::DictValue RecordEdits(const base::DictValue& previous,
                            const base::DictValue& current,
                            const base::DictValue& ledger,
                            const std::string& device);
bool Merge(base::DictValue& target, const base::DictValue& source);
std::optional<base::DictValue> Materialize(const base::DictValue& ledger);
std::optional<base::DictValue> ExchangeFolder(base::FilePath folder,
                                             std::string device,
                                             base::DictValue local);
}
#endif
