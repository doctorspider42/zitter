// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "download_file_actions.h"

#include <windows.h>
#include <shlobj.h>

#include <array>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/win/scoped_co_mem.h"
#include "ui/base/win/shell.h"

namespace zitter::content_embedder {
namespace {

void RunShellAction(DownloadFileAction action, const base::FilePath& path) {
  if (action == DownloadFileAction::kOpen) {
    ui::win::OpenFileViaShell(path);
    return;
  }
  base::win::ScopedCoMem<ITEMIDLIST> item;
  if (SUCCEEDED(SHParseDisplayName(path.value().c_str(), nullptr, &item, 0,
                                   nullptr))) {
    SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
  }
}

}  // namespace

base::FilePath VerifyDownloadFilePath(const base::FilePath& path) {
  const UINT drive = GetDriveTypeW(path.value().substr(0, 3).c_str());
  if (drive == DRIVE_UNKNOWN || drive == DRIVE_NO_ROOT_DIR ||
      drive == DRIVE_REMOTE) {
    return {};
  }
  const DWORD attributes = GetFileAttributesW(path.value().c_str());
  base::FilePath canonical;
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
                     FILE_ATTRIBUTE_DEVICE)) ||
      !base::NormalizeFilePath(path, &canonical) ||
      !base::FilePath::CompareEqualIgnoreCase(canonical.value(), path.value())) {
    return {};
  }
  return canonical;
}

namespace {

base::File OpenVerifiedDownloadFile(const base::FilePath& path,
                                    DWORD access,
                                    int64_t size,
                                    std::string_view hash,
                                    bool checkpoint = false) {
  if (VerifyDownloadFilePath(path).empty())
    return {};
  base::File file(::CreateFileW(
      path.value().c_str(), access, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info;
  if (!file.IsValid() || ::GetFileType(file.GetPlatformFile()) != FILE_TYPE_DISK ||
      !::GetFileInformationByHandle(file.GetPlatformFile(), &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT |
                                FILE_ATTRIBUTE_DIRECTORY)) ||
      info.nNumberOfLinks != 1) {
    return {};
  }
  std::array<wchar_t, MAX_PATH + 5> resolved;
  const DWORD length = ::GetFinalPathNameByHandleW(
      file.GetPlatformFile(), resolved.data(), resolved.size(),
      FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (!length || length >= resolved.size())
    return {};
  const std::wstring_view native_path(resolved.data(), length);
  constexpr std::wstring_view kDosPrefix = L"\\\\?\\";
  if (!native_path.starts_with(kDosPrefix) ||
      !base::FilePath::CompareEqualIgnoreCase(
          native_path.substr(kDosPrefix.size()), path.value()) ||
      !(checkpoint ? MatchesDownloadedPrefix(file, size, hash)
                   : MatchesDownloadedFile(file, size, hash))) {
    return {};
  }
  return file;
}

}  // namespace

bool VerifyDownloadCheckpoint(const base::FilePath& path,
                              int64_t size,
                              std::string_view hash) {
  return OpenVerifiedDownloadFile(path, GENERIC_READ, size, hash,
                                  /*checkpoint=*/true).IsValid();
}

base::FilePath VerifyDownloadedFileContents(const base::FilePath& path,
                                             int64_t size,
                                             std::string_view hash) {
  return OpenVerifiedDownloadFile(path, GENERIC_READ, size, hash).IsValid()
             ? path
             : base::FilePath();
}

bool DeleteVerifiedDownloadFile(const base::FilePath& path,
                                int64_t size,
                                std::string_view hash) {
  auto file = OpenVerifiedDownloadFile(path, GENERIC_READ | DELETE, size, hash);
  if (!file.IsValid())
    return false;
  // Delete the verified handle while excluding concurrent writers and renames.
  FILE_DISPOSITION_INFO disposition{TRUE};
  return ::SetFileInformationByHandle(file.GetPlatformFile(), FileDispositionInfo,
                                      &disposition, sizeof(disposition)) != 0;
}

void RunDownloadFileAction(DownloadFileAction action,
                           const base::FilePath& path) {
  base::ThreadPool::CreateCOMSTATaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE})
      ->PostTask(FROM_HERE, base::BindOnce(&RunShellAction, action, path));
}

}  // namespace zitter::content_embedder
