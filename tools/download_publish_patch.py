#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Stage protected intermediate file reopening and final publication."""

from pathlib import Path


BASE_FILE_HEADER = "components/download/public/common/base_file.h"
BASE_FILE = "components/download/internal/common/base_file.cc"
DOWNLOAD_FILE = "components/download/internal/common/download_file_impl.cc"

NATIVE_MOVE = """// Zitter: the intermediate file is already in the destination directory
// with its inherited permissions. Publish without replacing another file.
DownloadInterruptReason PublishWithoutReplacing(const base::FilePath& source,
                                                 const base::FilePath& target) {
  if (source.DirName() != target.DirName())
    return DOWNLOAD_INTERRUPT_REASON_FILE_FAILED;

  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
#if BUILDFLAG(IS_MAC)
  if (::renamex_np(source.value().c_str(), target.value().c_str(), RENAME_EXCL) == 0)
    return DOWNLOAD_INTERRUPT_REASON_NONE;
#elif BUILDFLAG(IS_WIN)
  if (::MoveFileExW(source.value().c_str(), target.value().c_str(), 0))
    return DOWNLOAD_INTERRUPT_REASON_NONE;
#else
  return DOWNLOAD_INTERRUPT_REASON_FILE_FAILED;
#endif
  return ConvertFileErrorToInterruptReason(base::File::GetLastFileError());
}

"""

VERIFIED_MOVE = """// Zitter: verify the received artifact immediately before publication.
bool MatchesCompletedDownload(base::File& file,
                              int64_t size,
                              base::span<const uint8_t> expected_hash) {
  if (!file.IsValid() || size < 0 || file.GetLength() != size)
    return false;

  crypto::hash::Hasher hasher(crypto::hash::kSha256);
  std::array<uint8_t, 64 * 1024> buffer;
  for (int64_t offset = 0; offset < size;) {
    const auto count = static_cast<size_t>(
        std::min<int64_t>(size - offset, buffer.size()));
    const auto chunk = base::span(buffer).first(count);
    if (file.Read(offset, chunk) != count)
      return false;
    hasher.Update(chunk);
    offset += count;
  }
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  hasher.Finish(digest);
  return file.GetLength() == size && base::span(digest) == expected_hash;
}

DownloadInterruptReason PublishWithoutReplacing(
    const base::FilePath& source,
    const base::FilePath& target,
    int64_t size,
    base::span<const uint8_t> expected_hash) {
  if (source.DirName() != target.DirName())
    return DOWNLOAD_INTERRUPT_REASON_FILE_FAILED;

  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
#if BUILDFLAG(IS_MAC)
  const auto directory = source.DirName();
  const auto source_name = source.BaseName();
  const auto target_name = target.BaseName();
  base::ScopedFD dir(HANDLE_EINTR(::open(
      directory.value().c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW)));
  char resolved_directory[PATH_MAX];
  if (!dir.is_valid() ||
      ::fcntl(dir.get(), F_GETPATH, resolved_directory) != 0 ||
      base::FilePath(resolved_directory) != directory) {
    return DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED;
  }

  base::File file(HANDLE_EINTR(::openat(
      dir.get(), source_name.value().c_str(),
      O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)));
  base::stat_wrapper_t before;
  base::stat_wrapper_t current;
  if (!file.IsValid() ||
      base::File::Fstat(file.GetPlatformFile(), &before) != 0 ||
      !S_ISREG(before.st_mode) || before.st_nlink != 1 ||
      !MatchesCompletedDownload(file, size, expected_hash) ||
      ::fstatat(dir.get(), source_name.value().c_str(), &current,
                AT_SYMLINK_NOFOLLOW) != 0 ||
      current.st_dev != before.st_dev || current.st_ino != before.st_ino ||
      current.st_size != before.st_size || current.st_nlink != 1 ||
      current.st_ctimespec.tv_sec != before.st_ctimespec.tv_sec ||
      current.st_ctimespec.tv_nsec != before.st_ctimespec.tv_nsec ||
      ::fcntl(dir.get(), F_GETPATH, resolved_directory) != 0 ||
      base::FilePath(resolved_directory) != directory) {
    return DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED;
  }

  if (::renameatx_np(dir.get(), source_name.value().c_str(), dir.get(),
                     target_name.value().c_str(), RENAME_EXCL) == 0) {
    return DOWNLOAD_INTERRUPT_REASON_NONE;
  }
#elif BUILDFLAG(IS_WIN)
  const auto directory = source.DirName();
  base::win::ScopedHandle dir(::CreateFileW(
      directory.value().c_str(), FILE_TRAVERSE | FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info;
  if (!dir.is_valid() ||
      !::GetFileInformationByHandle(dir.get(), &info) ||
      !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    return DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED;
  }

  // Exclude writers and renames until the same handle has been published.
  const HANDLE handle = ::CreateFileW(
      source.value().c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  base::File file(handle);
  if (!file.IsValid() || ::GetFileType(handle) != FILE_TYPE_DISK ||
      !::GetFileInformationByHandle(handle, &info) ||
      (info.dwFileAttributes &
       (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
      info.nNumberOfLinks != 1 ||
      !MatchesCompletedDownload(file, size, expected_hash)) {
    return DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED;
  }

  const auto target_name = target.BaseName();
  constexpr size_t kMaxNameLength = 255;
  if (target_name.value().size() > kMaxNameLength)
    return DOWNLOAD_INTERRUPT_REASON_FILE_NAME_TOO_LONG;
  alignas(FILE_RENAME_INFO)
      std::array<uint8_t, sizeof(FILE_RENAME_INFO) +
                              kMaxNameLength * sizeof(wchar_t)> buffer{};
  auto* rename_info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
  rename_info->ReplaceIfExists = FALSE;
  rename_info->RootDirectory = dir.get();
  const auto name_bytes = base::as_byte_span(target_name.value());
  rename_info->FileNameLength = static_cast<DWORD>(name_bytes.size());
  base::span(buffer)
      .subspan(offsetof(FILE_RENAME_INFO, FileName), name_bytes.size())
      .copy_from(name_bytes);
  if (::SetFileInformationByHandle(handle, FileRenameInfo, rename_info,
                                   static_cast<DWORD>(buffer.size()))) {
    return DOWNLOAD_INTERRUPT_REASON_NONE;
  }
#else
  return DOWNLOAD_INTERRUPT_REASON_FILE_FAILED;
#endif
  return ConvertFileErrorToInterruptReason(base::File::GetLastFileError());
}

"""

PROTECTED_REOPEN = """bool ProtectDownloadFiles() {
  return base::CommandLine::InitializedForCurrentProcess() &&
         base::CommandLine::ForCurrentProcess()->HasSwitch(
             "zitter-content-protect-download-target");
}

// Open the intermediate file itself. A replaced link must never redirect
// download writes to another file, even if its contents match the prefix.
base::File OpenProtectedIntermediateFile(const base::FilePath& path) {
#if BUILDFLAG(IS_MAC)
  const int fd = HANDLE_EINTR(
      ::open(path.value().c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  if (fd < 0) {
    return base::File(errno == ELOOP ? base::File::FILE_ERROR_SECURITY
                                    : base::File::GetLastFileError());
  }
  base::File file(fd);
  base::stat_wrapper_t info;
  if (base::File::Fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_nlink != 1) {
    return base::File(base::File::FILE_ERROR_SECURITY);
  }
  return file;
#elif BUILDFLAG(IS_WIN)
  const HANDLE handle =
      ::CreateFileW(path.value().c_str(), GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return base::File(base::File::GetLastFileError());
  base::File file(handle);
  BY_HANDLE_FILE_INFORMATION info;
  if (::GetFileType(handle) != FILE_TYPE_DISK ||
      !::GetFileInformationByHandle(handle, &info) ||
      (info.dwFileAttributes &
       (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
      info.nNumberOfLinks != 1) {
    return base::File(base::File::FILE_ERROR_SECURITY);
  }
  return file;
#else
  return base::File(base::File::FILE_ERROR_SECURITY);
#endif
}

"""


PATCHES = {
    BASE_FILE_HEADER: (
        ("  DownloadInterruptReason Rename(const base::FilePath& full_path);",
         """  // The caller can require a same-directory move without replacement.
  DownloadInterruptReason Rename(const base::FilePath& full_path,
                                 bool replace_existing = true);"""),
    ),
    BASE_FILE: (
        ('#include "base/compiler_specific.h"\n',
         '#include "base/command_line.h"\n#include "base/compiler_specific.h"\n'),
        ('#include "build/build_config.h"\n',
         """#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "base/posix/eintr_wrapper.h"
#endif
"""),
        ('#include "crypto/hash.h"\n',
         """#include "crypto/hash.h"

#include "base/threading/scoped_blocking_call.h"

#if BUILDFLAG(IS_MAC)
#include <stdio.h>
#elif BUILDFLAG(IS_WIN)
#include <windows.h>
#endif
"""),
        ("namespace {\nclass FileErrorData", "namespace {\n" + NATIVE_MOVE + "class FileErrorData"),
        ("void InitializeFile(base::File* file, const base::FilePath& file_path) {",
         PROTECTED_REOPEN +
         "void InitializeFile(base::File* file, const base::FilePath& file_path) {"),
        ("  return Open(hash_so_far, bytes_wasted);",
         """  if (ProtectDownloadFiles() && bytes_so_far_ > 0) {
    if (is_sparse_file_ || hash_so_far.empty()) {
      return LogInterruptReason("Missing verifiable download prefix", 0,
                                DOWNLOAD_INTERRUPT_REASON_FILE_HASH_MISMATCH);
    }
    // Recompute the hash from disk instead of reusing the streaming state.
    secure_hash_.reset();
  }
  return Open(hash_so_far, bytes_wasted);"""),
        ("""  // Create a new file if it is not provided.
  if (!file_.IsValid()) {
    InitializeFile(&file_, full_path_);""",
         """  const bool protect_intermediate = ProtectDownloadFiles();
  // Create a new file if it is not provided.
  if (!file_.IsValid()) {
    if (protect_intermediate) {
      file_ = OpenProtectedIntermediateFile(full_path_);
    } else {
      InitializeFile(&file_, full_path_);
    }"""),
        ("""    if (!file_.IsValid()) {
      return LogNetError("Open/Initialize File",""",
         """    if (protect_intermediate &&
        file_.error_details() == base::File::FILE_ERROR_SECURITY) {
      return LogInterruptReason("Unsafe reopened download file", 0,
                                DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED);
    }
    if (!file_.IsValid()) {
      return LogNetError("Open/Initialize File","""),
        ("DownloadInterruptReason BaseFile::Rename(const base::FilePath& new_path) {",
         """DownloadInterruptReason BaseFile::Rename(const base::FilePath& new_path,
                                        bool replace_existing) {"""),
        ("""  if (need_to_move_file) {
    base::CreateDirectory(new_path.DirName());""",
         """  if (need_to_move_file && !replace_existing) {
    rename_result = PublishWithoutReplacing(full_path_, new_path);
  } else if (need_to_move_file) {
    base::CreateDirectory(new_path.DirName());"""),
    ),
    DOWNLOAD_FILE: (
        ('#include "base/files/file_util.h"\n',
         '#include "base/command_line.h"\n#include "base/files/file_util.h"\n'),
        ("  DownloadInterruptReason reason = file_.Rename(new_path);",
         """  const bool protect_final_target =
      (parameters->option & ANNOTATE_WITH_SOURCE_INFORMATION) &&
      base::CommandLine::InitializedForCurrentProcess() &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          "zitter-content-protect-download-target");
  DownloadInterruptReason reason =
      file_.Rename(new_path, /*replace_existing=*/!protect_final_target);"""),
    ),
}


# Apply after the original no-replacement patch. This also upgrades an existing
# Content staging checkout without reverting other patches.
VERIFIED_PUBLICATION_PATCHES = {
    BASE_FILE_HEADER: (
        ("#include <stdint.h>\n", "#include <stdint.h>\n\n#include <array>\n"),
        ("""  std::optional<crypto::hash::Hasher> secure_hash_;
""",
         """  std::optional<crypto::hash::Hasher> secure_hash_;
  std::array<uint8_t, crypto::hash::kSha256Size> protected_final_hash_{};
  int64_t protected_final_size_ = -1;
"""),
    ),
    BASE_FILE: (
        ('#include <memory>\n', '#include <algorithm>\n#include <array>\n#include <memory>\n'),
        ('#include "base/posix/eintr_wrapper.h"\n',
         '#include "base/files/scoped_file.h"\n#include "base/posix/eintr_wrapper.h"\n'),
        ("#include <windows.h>\n",
         '#include <windows.h>\n\n#include "base/win/scoped_handle.h"\n'),
        (NATIVE_MOVE, VERIFIED_MOVE),
        ("""  if (new_path == full_path_)
    return DOWNLOAD_INTERRUPT_REASON_NONE;""",
         """  if (new_path == full_path_) {
    return replace_existing ? DOWNLOAD_INTERRUPT_REASON_NONE
                            : DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED;
  }"""),
        ("    rename_result = PublishWithoutReplacing(full_path_, new_path);",
         """    rename_result = PublishWithoutReplacing(
        full_path_, new_path, protected_final_size_, protected_final_hash_);"""),
        ("""  Close();
  return std::exchange(secure_hash_, std::nullopt);""",
         """  if (ProtectDownloadFiles() && secure_hash_) {
    protected_final_size_ =
        is_sparse_file_ && expected_size > 0 ? expected_size : bytes_so_far_;
    crypto::hash::Hasher(*secure_hash_).Finish(protected_final_hash_);
  }
  Close();
  return std::exchange(secure_hash_, std::nullopt);"""),
    ),
}


DESTINATION_OBSERVER = "components/download/public/common/download_destination_observer.h"
DESTINATION_OBSERVER_IMPL = "components/download/public/common/download_destination_observer.cc"
DOWNLOAD_ITEM_HEADER = "components/download/public/common/download_item_impl.h"
DOWNLOAD_ITEM = "components/download/internal/common/download_item_impl.cc"

CHECKPOINT_PATCHES = {
    BASE_FILE_HEADER: (
        ("  int64_t bytes_so_far() const { return bytes_so_far_; }",
         """  int64_t bytes_so_far() const { return bytes_so_far_; }
  std::string SnapshotCompletedPrefix(int64_t bytes_so_far) const;"""),
    ),
    BASE_FILE: (
        ("std::string BaseFile::DebugString() const {",
         """std::string BaseFile::SnapshotCompletedPrefix(int64_t bytes_so_far) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!in_progress() || is_sparse_file_ || !secure_hash_ ||
      bytes_so_far <= 0 || bytes_so_far != bytes_so_far_) {
    return {};
  }
  std::string digest(crypto::hash::kSha256Size, '\\0');
  crypto::hash::Hasher(*secure_hash_).Finish(base::as_writable_byte_span(digest));
  return digest;
}

std::string BaseFile::DebugString() const {"""),
    ),
    DESTINATION_OBSERVER: (
        ("#include <optional>\n", "#include <optional>\n#include <string>\n"),
        ("  virtual void DestinationError(",
         """  // Content can attach a digest of the written stream to a progress update.
  virtual void DestinationUpdateWithCheckpoint(
      int64_t bytes_so_far,
      int64_t bytes_per_sec,
      const std::vector<DownloadItem::ReceivedSlice>& received_slices,
      std::string prefix_hash);

  virtual void DestinationError("""),
    ),
    DESTINATION_OBSERVER_IMPL: (
        ("DownloadDestinationObserver::~DownloadDestinationObserver() = default;",
         """DownloadDestinationObserver::~DownloadDestinationObserver() = default;

void DownloadDestinationObserver::DestinationUpdateWithCheckpoint(
    int64_t bytes_so_far,
    int64_t bytes_per_sec,
    const std::vector<DownloadItem::ReceivedSlice>& received_slices,
    std::string) {
  DestinationUpdate(bytes_so_far, bytes_per_sec, received_slices);
}"""),
    ),
    DOWNLOAD_ITEM_HEADER: (
        ("  void DestinationError(\n",
         """  void DestinationUpdateWithCheckpoint(
      int64_t bytes_so_far,
      int64_t bytes_per_sec,
      const std::vector<DownloadItem::ReceivedSlice>& received_slices,
      std::string prefix_hash) override;
  void DestinationError(
"""),
    ),
    DOWNLOAD_ITEM: (
        ("void DownloadItemImpl::DestinationError(",
         """void DownloadItemImpl::DestinationUpdateWithCheckpoint(
    int64_t bytes_so_far,
    int64_t bytes_per_sec,
    const std::vector<DownloadItem::ReceivedSlice>& received_slices,
    std::string prefix_hash) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  destination_info_.hash = std::move(prefix_hash);
  DestinationUpdate(bytes_so_far, bytes_per_sec, received_slices);
}

void DownloadItemImpl::DestinationError("""),
    ),
    DOWNLOAD_FILE: (
        ("""void DownloadFileImpl::SendUpdate() {
  // TODO""",
         """void DownloadFileImpl::SendUpdate() {
  if (base::CommandLine::InitializedForCurrentProcess() &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          "zitter-content-protect-download-target")) {
    const int64_t bytes_so_far = TotalBytesReceived();
    std::string prefix_hash;
#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
    if (!obfuscator_)
#endif
      prefix_hash = file_.SnapshotCompletedPrefix(bytes_so_far);
    main_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            &DownloadDestinationObserver::DestinationUpdateWithCheckpoint,
            observer_, bytes_so_far, rate_estimator_.GetCountPerSecond(),
            received_slices_, std::move(prefix_hash)));
    return;
  }
  // TODO"""),
    ),
}


def _replace(source, old, new, name):
    if source.count(new) == 1 and old not in source.replace(new, "", 1):
        return source
    if source.count(old) != 1 or new in source:
        raise ValueError(f"Unexpected download publication layout in {name}")
    return source.replace(old, new, 1)


def prepare(checkout):
    root = Path(checkout).resolve()
    plan = {}
    for name in dict.fromkeys((*PATCHES, *CHECKPOINT_PATCHES)):
        replacements = PATCHES.get(name, ())
        path = root / name
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            raise ValueError(f"Download publication path escapes the checkout: {name}")
        original = path.read_bytes()
        patched = original.decode()
        publication = VERIFIED_PUBLICATION_PATCHES.get(name, ())
        for old, new in replacements:
            for earlier, verified in publication:
                if earlier in new:
                    upgraded = new.replace(earlier, verified)
                    if patched.count(upgraded) == 1:
                        break
            else:
                patched = _replace(patched, old, new, name)
        for old, new in (*publication, *CHECKPOINT_PATCHES.get(name, ())):
            patched = _replace(patched, old, new, name)
        plan[path] = (original, patched.encode())
    return plan
