// Copyright 2026 the Zitter contributors
// SPDX-License-Identifier: Apache-2.0
#include "third_party/zitter_libarchive/rar_reader.h"
#include <algorithm>
#include <iostream>
#include <string>
#include "base/check.h"
#include "base/compiler_specific.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"

class FailWriter final : public third_party_libarchive::RarWriterDelegate {
 public:
  bool Write(base::span<const uint8_t>) override { return false; }
  void Close() override {}
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  base::FilePath fixtures(UNSAFE_BUFFERS(argv[1]));
  base::ScopedTempDir directory;
  CHECK(directory.CreateUniqueTempDir());
  const auto output = directory.GetPath().AppendASCII("out");
  for (const char* name : {"stored.rar", "crc.rar", "truncated.rar", "traversal.rar",
                           "rar5.rar", "solid.rar", "encrypted4.rar", "encrypted5.rar", "headers5.rar", "symlink.rar", "write-error.rar"}) {
    third_party_libarchive::RarReader reader;
    if (std::string(name) == "write-error.rar") reader.SetWriterDelegate(std::make_unique<FailWriter>());
    bool opened = reader.Open(std::make_unique<third_party_libarchive::FileReader>(
        base::File(fixtures.AppendASCII(name), base::File::FLAG_OPEN | base::File::FLAG_READ)),
        base::File(output, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ | base::File::FLAG_WRITE));
    size_t entries = 0;
    while (opened && reader.ExtractNextEntry()) {
      ++entries;
      CHECK(reader.current_entry().contents_valid);
      std::string contents;
      CHECK(base::ReadFileToString(output, &contents));
      CHECK(contents.size() == reader.current_entry().file_size);
      if (std::string(name) == "stored.rar") CHECK(contents == "Zitter open RAR decoder\n");
      if (std::string(name) == "rar5.rar" || std::string(name) == "solid.rar") {
        CHECK(contents.size() == (std::string(name) == "solid.rar" ? 4096u : 1200u));
        // Upstream libarchive test data formula, checked independently of CRC.
        const int magic = std::string(name) == "solid.rar" ? entries : 0;
        for (size_t i = 0; i < contents.size() / 4; ++i) {
          const int k = i + 1;
          uint32_t expected = std::max(0, k * k - 3 * k + 1 + magic);
          uint32_t actual = 0;
          for (int byte = 0; byte < 4; ++byte) actual |= uint32_t(static_cast<uint8_t>(contents[i * 4 + byte])) << (byte * 8);
          CHECK(actual == expected);
        }
      }
    }
    const bool success = opened && !reader.HasReadError() && !reader.HasWriteError();
    const bool expected = std::string(name) == "stored.rar" || std::string(name) == "rar5.rar" || std::string(name) == "solid.rar";
    CHECK(success == expected) << name << " entries=" << entries;
    if (expected) CHECK(entries == (std::string(name) == "solid.rar" ? 4u : 1u));
    if (std::string(name) == "write-error.rar") CHECK(reader.HasWriteError());
    std::cout << name << ": passed\n";
  }
  return 0;
}
