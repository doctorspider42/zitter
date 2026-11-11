#ifndef ZITTER_CHROMIUM_PROTOTYPE_IMPORT_DATABASE_SNAPSHOT_H_
#define ZITTER_CHROMIUM_PROTOTYPE_IMPORT_DATABASE_SNAPSHOT_H_

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "sql/database.h"

namespace zitter::prototype {
// SQLite may write recovery/WAL bookkeeping only in this disposable snapshot.
// Check both files before/after copying so a browser writing concurrently is
// retried, rather than reading a torn database. No plaintext cookies go to disk.
inline bool SnapshotImportDatabase(const base::FilePath& path, base::ScopedTempDir& temp,
                                   sql::Database& db) {
  if (!temp.CreateUniqueTempDir()) return false;
  const auto wal = base::FilePath(path.value() + FILE_PATH_LITERAL("-wal"));
  const auto copy = temp.GetPath().AppendASCII("source.sqlite");
  for (int attempt = 0; attempt < 3; ++attempt) {
    base::File::Info before, after, wal_before, wal_after;
    if (!base::GetFileInfo(path, &before)) return false;
    const bool had_wal = base::GetFileInfo(wal, &wal_before);
    if (!base::CopyFile(path, copy)) return false;
    if (had_wal && !base::CopyFile(wal, base::FilePath(copy.value() + FILE_PATH_LITERAL("-wal")))) continue;
    if (!had_wal) base::DeleteFile(base::FilePath(copy.value() + FILE_PATH_LITERAL("-wal")));
    if (!base::GetFileInfo(path, &after)) continue;
    const bool has_wal = base::GetFileInfo(wal, &wal_after);
    if (before.size != after.size || before.last_modified != after.last_modified ||
        had_wal != has_wal || (has_wal && (wal_before.size != wal_after.size ||
          wal_before.last_modified != wal_after.last_modified))) continue;
    return db.Open(copy);
  }
  return false;
}
}  // namespace zitter::prototype

#endif
