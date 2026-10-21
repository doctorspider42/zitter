// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZITTER_LIBARCHIVE_RAR_READER_H_
#define ZITTER_LIBARCHIVE_RAR_READER_H_

#include <array>
#include "third_party/zitter_libarchive/archive.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/platform_file.h"
#include "base/memory/scoped_refptr.h"

#include "third_party/zitter_libarchive/rar_delegates.h"

struct archive;
struct archive_entry;

// Zitter: replaced proprietary UnRAR internals with libarchive (2026).
namespace third_party_libarchive {

// Delegate that provides file access using a base::File.
class FileReader : public RarReaderDelegate {
 public:
  explicit FileReader(base::File file);
  ~FileReader() override;

  int64_t Read(base::span<uint8_t> data) override;
  bool Seek(int64_t offset) override;
  int64_t Tell() override;
  int64_t GetLength() override;

 private:
  base::File file_;
};


// Writer delegate that writes to a file.
class FileWriter : public RarWriterDelegate {
 public:
  explicit FileWriter(base::File file);
  ~FileWriter() override;

  bool Write(base::span<const uint8_t> data) override;

  void Close() override;

 private:
  base::File file_;
};

// This class is used for extracting RAR files, one entry at a time.
class RarReader {
 public:
  struct EntryInfo {
    // The relative path of this entry, within the archive.
    base::FilePath file_path;

    // Whether the entry is a directory or a file.
    bool is_directory;

    // Whether the entry has encrypted contents.
    bool is_encrypted;

    // The actual size of the entry.
    size_t file_size;

    // Whether the contents are valid
    bool contents_valid;
  };

  RarReader();
  ~RarReader();

  // Opens the RAR archive using the provided |delegate| for reading, and uses
  // |temp_file| for extracting each entry.
  bool Open(std::unique_ptr<RarReaderDelegate> delegate, base::File temp_file);

  // Extracts the next entry in the RAR archive. Returns true on success and
  // updates the information in |current_entry()|.
  bool ExtractNextEntry();

  // Returns the EntryInfo for the most recently extracted entry in the RAR
  // archive.
  const EntryInfo& current_entry() { return current_entry_; }

  void SetPassword(const std::string& password);

  // Sets a custom writer delegate. If not set, a default FileWriter using
  // the temp_file passed to Open() will be used.
  void SetWriterDelegate(std::unique_ptr<RarWriterDelegate> writer);

  bool HeadersEncrypted() const;
  bool HeaderDecryptionFailed() const;
  // Indicates whether there was a disk error since the last ExtractNextEntry().
  bool HasWriteError() const;
  bool HasReadError() const;

 private:
  // The temporary file used for extracting each entry. This allows RAR
  // extraction to safely occur within a sandbox.
  base::File temp_file_;

  // The high-level file delegate.
  std::unique_ptr<RarReaderDelegate> reader_;

  // Information for the current entry in the RAR archive.
  EntryInfo current_entry_;

  struct archive* archive_ = nullptr;
  struct archive_entry* next_entry_ = nullptr;
  std::array<uint8_t, 64 * 1024> read_buffer_{};
  std::array<uint8_t, 64 * 1024> output_buffer_{};
  size_t extracted_total_ = 0, entries_ = 0;
  bool has_pending_header_ = false;
  bool read_error_ = false;
  bool headers_encrypted_ = false;
  static la_int64_t SeekCallback(struct archive*, void*, la_int64_t, int);
  static la_ssize_t ReadCallback(struct archive*, void*, const void**);
  // Password used for encrypted entries.
  std::string password_;

  // Writer delegate.
  std::unique_ptr<RarWriterDelegate> writer_;

  // Whether a write error occurred.
  bool write_error_ = false;
};

}  // namespace third_party_libarchive

#endif  // ZITTER_LIBARCHIVE_RAR_READER_H_
