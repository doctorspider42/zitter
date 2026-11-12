// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "download_file_actions.h"

#import <AppKit/AppKit.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <unistd.h>
#include <sys/stat.h>

#include "base/apple/foundation_util.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/posix/eintr_wrapper.h"

namespace zitter::content_embedder {

base::FilePath VerifyDownloadFilePath(const base::FilePath& path) {
  base::FilePath canonical;
  base::stat_wrapper_t info;
  struct statfs volume;
  if (!base::NormalizeFilePath(path, &canonical) || canonical != path ||
      base::File::Lstat(path, &info) != 0 || !S_ISREG(info.st_mode) ||
      statfs(path.value().c_str(), &volume) != 0 ||
      !(volume.f_flags & MNT_LOCAL)) {
    return {};
  }
  return canonical;
}

namespace {

base::FilePath VerifyFileContents(const base::FilePath& path,
                                 int64_t size,
                                 std::string_view hash,
                                 bool checkpoint) {
  if (VerifyDownloadFilePath(path).empty())
    return {};
  base::File file(HANDLE_EINTR(::open(
      path.value().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  base::stat_wrapper_t before;
  if (!file.IsValid() || base::File::Fstat(file.GetPlatformFile(), &before) != 0 ||
      !S_ISREG(before.st_mode) || (checkpoint && before.st_nlink != 1) ||
      !(checkpoint ? MatchesDownloadedPrefix(file, size, hash)
                   : MatchesDownloadedFile(file, size, hash))) {
    return {};
  }

  // Reject a replacement or rewrite detected during the read.
  base::stat_wrapper_t current;
  if (base::File::Lstat(path, &current) != 0 ||
      current.st_dev != before.st_dev || current.st_ino != before.st_ino ||
      (checkpoint && current.st_nlink != 1) ||
      current.st_ctimespec.tv_sec != before.st_ctimespec.tv_sec ||
      current.st_ctimespec.tv_nsec != before.st_ctimespec.tv_nsec) {
    return {};
  }
  return VerifyDownloadFilePath(path);
}

}  // namespace

base::FilePath VerifyDownloadedFileContents(const base::FilePath& path,
                                             int64_t size,
                                             std::string_view hash) {
  return VerifyFileContents(path, size, hash, /*checkpoint=*/false);
}

bool VerifyDownloadCheckpoint(const base::FilePath& path,
                              int64_t size,
                              std::string_view hash) {
  return !VerifyFileContents(path, size, hash, /*checkpoint=*/true).empty();
}

bool DeleteVerifiedDownloadFile(const base::FilePath& path,
                                int64_t size,
                                std::string_view hash) {
  const auto directory = path.DirName();
  const auto name = path.BaseName();
  base::ScopedFD dir(HANDLE_EINTR(::open(
      directory.value().c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW)));
  char resolved_directory[PATH_MAX];
  struct statfs volume;
  if (!dir.is_valid() ||
      ::fcntl(dir.get(), F_GETPATH, resolved_directory) != 0 ||
      base::FilePath(resolved_directory) != directory ||
      ::fstatfs(dir.get(), &volume) != 0 || !(volume.f_flags & MNT_LOCAL)) {
    return false;
  }
  base::File file(HANDLE_EINTR(::openat(
      dir.get(), name.value().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  base::stat_wrapper_t before;
  if (!file.IsValid() || base::File::Fstat(file.GetPlatformFile(), &before) != 0 ||
      !S_ISREG(before.st_mode) || before.st_nlink != 1 ||
      !MatchesDownloadedFile(file, size, hash)) {
    return false;
  }

  // Recheck the leaf inside the opened directory immediately before unlinking.
  struct stat current;
  if (::fstatat(dir.get(), name.value().c_str(), &current, AT_SYMLINK_NOFOLLOW) != 0 ||
      current.st_dev != before.st_dev || current.st_ino != before.st_ino ||
      current.st_size != before.st_size || current.st_nlink != 1 ||
      current.st_ctimespec.tv_sec != before.st_ctimespec.tv_sec ||
      current.st_ctimespec.tv_nsec != before.st_ctimespec.tv_nsec) {
    return false;
  }
  return ::unlinkat(dir.get(), name.value().c_str(), 0) == 0;
}

void RunDownloadFileAction(DownloadFileAction action,
                           const base::FilePath& path) {
  NSURL* url = base::apple::FilePathToNSURL(path);
  if (!url)
    return;
  NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
  if (action == DownloadFileAction::kReveal) {
    [workspace activateFileViewerSelectingURLs:@[ url ]];
  } else {
    [workspace openURL:url
         configuration:[NSWorkspaceOpenConfiguration configuration]
     completionHandler:nil];
  }
}

}  // namespace zitter::content_embedder
