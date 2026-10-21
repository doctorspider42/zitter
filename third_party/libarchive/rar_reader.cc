// Copyright 2026 the Zitter contributors
// Licensed under the Apache License, Version 2.0; see LICENSE.
#include "third_party/zitter_libarchive/rar_reader.h"

#include <algorithm>
#include <limits>
#include "base/compiler_specific.h"
#include "base/numerics/checked_math.h"
#include "third_party/zitter_libarchive/archive_entry.h"

namespace third_party_libarchive {
FileReader::FileReader(base::File file) : file_(std::move(file)) {}
FileReader::~FileReader() = default;
int64_t FileReader::Read(base::span<uint8_t> data) {
  auto count = file_.ReadAtCurrentPos(data);
  return count ? static_cast<int64_t>(*count) : -1;
}
bool FileReader::Seek(int64_t offset) { return file_.Seek(base::File::FROM_BEGIN, offset) == offset; }
int64_t FileReader::Tell() { return file_.Seek(base::File::FROM_CURRENT, 0); }
int64_t FileReader::GetLength() { return file_.GetLength(); }
FileWriter::FileWriter(base::File file) : file_(std::move(file)) {}
FileWriter::~FileWriter() = default;
bool FileWriter::Write(base::span<const uint8_t> data) {
  auto written = file_.WriteAtCurrentPos(data);
  return written && *written == data.size();
}
void FileWriter::Close() {}
RarReader::RarReader() = default;
RarReader::~RarReader() { if (archive_) archive_read_free(archive_); }
la_ssize_t RarReader::ReadCallback(struct archive*, void* context, const void** data) {
  auto* self = static_cast<RarReader*>(context);
  *data = self->read_buffer_.data();
  return self->reader_->Read(self->read_buffer_);
}
la_int64_t RarReader::SeekCallback(struct archive*, void* context, la_int64_t offset, int origin) {
  auto* self = static_cast<RarReader*>(context);
  const int64_t anchor = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? self->reader_->Tell()
      : origin == SEEK_END ? self->reader_->GetLength() : -1;
  int64_t target;
  if (anchor < 0 || !base::CheckAdd(anchor, offset).AssignIfValid(&target) || target < 0 ||
      target > self->reader_->GetLength() || !self->reader_->Seek(target)) return ARCHIVE_FATAL;
  return target;
}
bool RarReader::Open(std::unique_ptr<RarReaderDelegate> delegate, base::File temp_file) {
  if (archive_ || !delegate || !temp_file.IsValid()) return false;
  reader_ = std::move(delegate);
  temp_file_ = std::move(temp_file);
  if (!writer_) writer_ = std::make_unique<FileWriter>(temp_file_.Duplicate());
  archive_ = archive_read_new();
  if (!archive_) return false;
  archive_read_support_filter_none(archive_);
  archive_read_support_format_rar(archive_);
  archive_read_support_format_rar5(archive_);
  archive_read_set_seek_callback(archive_, &SeekCallback);
  if (archive_read_open2(archive_, this, nullptr, &ReadCallback, nullptr, nullptr) != ARCHIVE_OK) return false;
  const int status = archive_read_next_header(archive_, &next_entry_);
  has_pending_header_ = status == ARCHIVE_OK;
  // libarchive does not decrypt RAR. Unsupported/encrypted headers fail closed.
  headers_encrypted_ = archive_read_has_encrypted_entries(archive_) > 0;
  if (status != ARCHIVE_OK && status != ARCHIVE_EOF) { read_error_ = true; return false; }
  return true;
}
bool RarReader::ExtractNextEntry() {
  if (!archive_ || read_error_ || write_error_) return false;
  if (++entries_ > 10000) { read_error_ = true; return false; }
  if (!has_pending_header_) {
    const int status = archive_read_next_header(archive_, &next_entry_);
    if (status == ARCHIVE_EOF) return false;
    if (status != ARCHIVE_OK) { read_error_ = true; return false; }
  }
  has_pending_header_ = false;
  const char* path = archive_entry_pathname_utf8(next_entry_);
  const auto size = archive_entry_size(next_entry_);
  const int type = archive_entry_filetype(next_entry_);
  if (!path || size < 0 || size > 64 * 1024 * 1024 ||
      (type != AE_IFREG && type != AE_IFDIR) || archive_entry_hardlink(next_entry_)) {
    read_error_ = true; return false;
  }
  current_entry_ = {base::FilePath(path), type == AE_IFDIR,
                    archive_entry_is_encrypted(next_entry_) > 0, 0, false};
  if (current_entry_.file_path.IsAbsolute() || current_entry_.file_path.ReferencesParent()) {
    read_error_ = true; return false;
  }
  if (current_entry_.is_encrypted) {
    // Unsupported encryption must never become a successful clean scan or a
    // password-correct result. Preserve no plaintext claim, fail the analysis.
    headers_encrypted_ = true;
    read_error_ = true;
    return false;
  }
  if (!temp_file_.SetLength(0) || temp_file_.Seek(base::File::FROM_BEGIN, 0) != 0) {
    write_error_ = true; return false;
  }
  size_t extracted = 0;
  while (!current_entry_.is_directory) {
    const la_ssize_t count = archive_read_data(archive_, output_buffer_.data(), output_buffer_.size());
    if (count == 0) break;
    if (count < 0 || static_cast<size_t>(count) > output_buffer_.size() || extracted + count > 64 * 1024 * 1024 || extracted_total_ + count > 1024 * 1024 * 1024) { read_error_ = true; return false; }
    // SAFETY: libarchive returned a byte count within the supplied buffer.
    if (!writer_->Write(base::span<const uint8_t>(output_buffer_).first(static_cast<size_t>(count)))) {
      write_error_ = true; return false;
    }
    extracted += count;
    extracted_total_ += count;
  }
  writer_->Close();
  if (extracted != static_cast<size_t>(size)) { read_error_ = true; return false; }
  current_entry_.file_size = extracted;
  current_entry_.contents_valid = true;
  return true;
}
void RarReader::SetPassword(const std::string& password) { password_ = password; }
void RarReader::SetWriterDelegate(std::unique_ptr<RarWriterDelegate> writer) { writer_ = std::move(writer); }
bool RarReader::HeadersEncrypted() const { return headers_encrypted_; }
bool RarReader::HeaderDecryptionFailed() const { return read_error_ && headers_encrypted_; }
bool RarReader::HasWriteError() const { return write_error_; }
bool RarReader::HasReadError() const { return read_error_; }
}  // namespace third_party_libarchive
