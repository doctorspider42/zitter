// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "cookie_store_verifier.h"

#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#else
#include <sys/stat.h>
#include <cerrno>
#endif

#include <algorithm>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_util.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "crypto/sha2.h"
#include "sql/database.h"
#include "sql/statement.h"

namespace zitter::content_embedder {
namespace {

// The version that binds each encrypted cookie to its host. The network
// service can discard data during schema migration or on decryption failure.
constexpr int kVerifiedCookieSchema = 24;

bool CopyDatabaseFiles(const base::FilePath& directory,
                       std::string_view database_name,
                       base::ScopedTempDir& scratch) {
  for (const char* suffix : {"", "-journal", "-wal", "-shm"}) {
    const auto name = std::string(database_name).append(suffix);
    const auto source = directory.AppendASCII(name);
#if BUILDFLAG(IS_WIN)
    const DWORD attributes = ::GetFileAttributesW(source.value().c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const DWORD error = ::GetLastError();
      if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        return false;
      continue;
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
      return false;
#else
    struct stat info;
    if (lstat(source.value().c_str(), &info) != 0) {
      if (errno != ENOENT)
        return false;
      continue;
    }
    if (!S_ISREG(info.st_mode))
      return false;
#endif
    if (!scratch.IsValid() && !scratch.CreateUniqueTempDir())
      return false;
    if (!base::CopyFile(source, scratch.GetPath().AppendASCII(name)))
      return false;
  }
  return true;
}

bool CheckCookieStore(const base::FilePath& network_path,
                      const os_crypt_async::Encryptor& encryptor) {
  base::ScopedTempDir scratch;
  if (!CopyDatabaseFiles(network_path, "Cookies", scratch))
    return false;
  if (!scratch.IsValid())
    return true;

  // SQLite may create WAL bookkeeping even for a read-only connection. Open
  // a disposable copy so no database or sidecar in the profile can change.
  sql::Database database(sql::DatabaseOptions().set_read_only(true), "Cookie");
  if (!database.Open(scratch.GetPath().AppendASCII("Cookies")))
    return false;
  sql::Statement schema(database.GetReadonlyStatement(
      "SELECT value FROM meta WHERE key = 'version'"));
  if (!schema.Step() || schema.ColumnInt(0) != kVerifiedCookieSchema ||
      schema.Step() || !schema.Succeeded()) {
    return false;
  }
  sql::Statement rows(database.GetReadonlyStatement(
      "SELECT host_key, value, encrypted_value FROM cookies"));
  while (rows.Step()) {
    const auto ciphertext = rows.ColumnBlob(2);
    if (ciphertext.empty() || !rows.ColumnStringView(1).empty())
      return false;
    const auto plaintext = encryptor.DecryptData(ciphertext);
    if (!plaintext || !plaintext->starts_with(
                          crypto::SHA256HashString(rows.ColumnStringView(0)))) {
      return false;
    }
#if BUILDFLAG(IS_MAC)
    // Chromium's v10 CBC decryption can fall back to a public empty key.
    // The fixed IV makes re-encryption an exact check against the active key.
    if (base::as_string_view(ciphertext).starts_with("v10")) {
      const auto encrypted = encryptor.EncryptString(*plaintext);
      if (!encrypted || !std::ranges::equal(*encrypted, ciphertext))
        return false;
    }
#endif
  }
  return rows.Succeeded();
}

bool CheckLoginStore(const base::FilePath& profile_path,
                     const os_crypt_async::Encryptor& encryptor) {
  base::ScopedTempDir scratch;
  if (!CopyDatabaseFiles(profile_path, "Login Data", scratch))
    return false;
  if (!scratch.IsValid())
    return true;
  sql::Database database(sql::DatabaseOptions().set_read_only(true), "Passwords");
  if (!database.Open(scratch.GetPath().AppendASCII("Login Data")))
    return false;
  sql::Statement rows(database.GetReadonlyStatement("SELECT password_value FROM logins"));
  while (rows.Step()) {
    const auto ciphertext = rows.ColumnBlob(0);
    if (ciphertext.empty())
      continue;
    const auto plaintext = encryptor.DecryptData(ciphertext);
    if (!plaintext || !base::IsStringUTF8AllowingNoncharacters(*plaintext))
      return false;
#if BUILDFLAG(IS_MAC)
    // Legacy CBC can fall back to the empty key. Unlike Windows AES-GCM,
    // CBC does not authenticate the key, so check the fixed-IV ciphertext.
    const auto encrypted = encryptor.EncryptString(*plaintext);
    if (!encrypted || !std::ranges::equal(*encrypted, ciphertext))
      return false;
#endif
  }
  return rows.Succeeded();
}

}  // namespace

bool CheckCookieStores(const std::vector<base::FilePath>& network_paths,
                       const os_crypt_async::Encryptor& encryptor) {
  for (const auto& path : network_paths) {
    if (!CheckCookieStore(path, encryptor))
      return false;
  }
  return true;
}

bool CheckLoginStores(const std::vector<base::FilePath>& profile_paths,
                      const os_crypt_async::Encryptor& encryptor) {
  for (const auto& path : profile_paths) {
    if (!CheckLoginStore(path, encryptor))
      return false;
  }
  return true;
}

}  // namespace zitter::content_embedder
