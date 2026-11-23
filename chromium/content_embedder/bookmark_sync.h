// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CONTENT_BOOKMARK_SYNC_H_
#define ZITTER_CONTENT_BOOKMARK_SYNC_H_
#include "base/files/file_path.h"
#include "base/values.h"
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include "url/gurl.h"
namespace zitter::content_embedder::bookmark_sync {
bool CanSyncTabUrl(const GURL& url);
bool ValidTabId(std::string_view id);
std::string LegacyTabId(std::string_view workspace, std::string_view url, size_t occurrence);
bool IsTabScope(std::string_view scope);
void UpgradeLegacyTabs(base::DictValue& ledger);
bool ValidSnapshot(std::string_view scope, const base::ListValue& items);

// A catalogue lists immutable blobs by digest. A writer commits the catalogue
// only once each referenced blob is available.
inline constexpr size_t kMaxShardBytes = 5 * 1024 * 1024;
struct ShardSet {
  base::DictValue index;
  std::map<std::string, std::string> blobs;
};
std::optional<ShardSet> WriteShards(const base::DictValue& ledger);
std::optional<base::DictValue> ReadIndex(std::string_view json);
bool MatchesShardKey(std::string_view key, const base::DictValue& shard);
std::optional<base::DictValue> ReadShard(std::string_view key, std::string_view digest,
                                        std::string_view json);
base::DictValue CacheShards(base::DictValue ledger);
// A collection contains ordered pins. The ledger retains a version per pin,
// including tombstones, so a device returning from offline cannot undo deletions.
base::DictValue RecordEdits(const base::DictValue& previous,
                            const base::DictValue& current,
                            const base::DictValue& ledger,
                            const std::string& device);
bool Merge(base::DictValue& target, const base::DictValue& source);
void RecordScopeEdits(const base::DictValue& previous,
                      const base::DictValue& current,
                      base::DictValue& ledger,
                      const std::string& device);
bool ScopeDeleted(const base::DictValue& ledger, std::string_view key);
void RecordScopeDeletion(base::DictValue& ledger, std::string_view key,
                         const std::string& device);
std::optional<base::DictValue> Materialize(const base::DictValue& ledger);
struct FolderResult {
  base::DictValue ledger;
  base::DictValue cache;
};
std::optional<FolderResult> ExchangeFolder(base::FilePath folder, std::string device,
                                           base::DictValue local, bool read_only,
                                           base::DictValue cache);
}
#endif
