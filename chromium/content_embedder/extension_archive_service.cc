// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "extension_archive_service.h"

#include "printing/buildflags/buildflags.h"
#include "pdf/buildflags.h"

#if BUILDFLAG(ENABLE_PRINTING) && BUILDFLAG(ENABLE_PDF)
#include "workspace_pdf_preview.h"
#endif

#if BUILDFLAG(ENABLE_PRINTING)
#include "components/services/print_compositor/print_compositor_impl.h"
#include "components/services/print_compositor/public/mojom/print_compositor.mojom.h"
#include "content/public/utility/utility_thread.h"
#endif

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/icu_string_conversions.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/service_process_host.h"
#include "mojo/public/cpp/base/big_buffer.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/service_factory.h"
#include "net/base/filename_util.h"
#include "sandbox/policy/sandbox.h"
#include "sandbox/policy/switches.h"
#include "third_party/zlib/google/zip_reader.h"
#include "zitter_content/extension_archive.mojom.h"

namespace zitter::content_embedder {

std::optional<base::FilePath> ExtensionArchiveResourcePath(std::string_view name) {
  if (name.empty() || name.size() > 512 ||
      name.find('\\') != std::string_view::npos ||
      name.find('\0') != std::string_view::npos ||
      !base::IsStringUTF8(name)) {
    return std::nullopt;
  }
  const auto parts = base::SplitStringPiece(
      name, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() > 16 ||
      base::EqualsCaseInsensitiveASCII(parts.front(), kExtensionArchiveFileName) ||
      base::EqualsCaseInsensitiveASCII(parts.front(), "_metadata")) {
    return std::nullopt;
  }
  base::FilePath path;
  for (const auto part : parts) {
    const auto component = base::FilePath::FromUTF8Unsafe(part);
    if (part.empty() || part == "." || part == ".." ||
        !net::IsSafePortablePathComponent(component)) {
      return std::nullopt;
    }
    path = path.Append(component);
  }
  return path;
}

namespace {

using ArchiveFiles = base::flat_map<std::string, std::vector<uint8_t>>;

constexpr size_t kMaxArchiveEntries = 2048;
// Keep the reply below Content's 128 MiB Mojo message limit, including paths
// and serialization overhead.
constexpr size_t kMaxExtractedBytes = 96 * 1024 * 1024;

class ArchivePaths {
 public:
  bool Add(std::string_view name, bool directory) {
    if (!ExtensionArchiveResourcePath(name))
      return false;
    size_t separator = 0;
    do {
      separator = name.find('/', separator);
      const auto prefix = name.substr(0, separator);
      const bool explicit_entry = separator == std::string_view::npos;
      const bool is_directory = !explicit_entry || directory;
      std::string key;
      if (!base::ConvertToUtf8AndNormalize(
              base::UTF16ToUTF8(base::i18n::FoldCase(base::UTF8ToUTF16(prefix))),
              "UTF-8", &key)) {
        return false;
      }
      auto [entry, inserted] = paths_.try_emplace(
          key, PathEntry{std::string(prefix), is_directory, explicit_entry});
      if (!inserted &&
          (entry->second.name != prefix ||
           entry->second.directory != is_directory ||
           (entry->second.explicit_entry && explicit_entry))) {
        return false;
      }
      entry->second.explicit_entry |= explicit_entry;
      if (!explicit_entry)
        ++separator;
    } while (separator != std::string_view::npos);
    return true;
  }

 private:
  struct PathEntry {
    std::string name;
    bool directory;
    bool explicit_entry;
  };
  std::map<std::string, PathEntry> paths_;
};

std::optional<ArchiveFiles> ReadArchive(const std::string& archive, bool web_store) {
  zip::ZipReader reader;
  if (!reader.OpenFromString(archive))
    return std::nullopt;

  ArchiveFiles files;
  ArchivePaths paths;
  size_t count = 0;
  size_t total = 0;
  bool saw_store_directory = false;
  bool saw_store_contents = false;
  while (const auto* entry = reader.Next()) {
    std::string_view name = entry->path_in_original_encoding;
    if (entry->is_directory && name.ends_with('/'))
      name.remove_suffix(1);
    if (++count > kMaxArchiveEntries || entry->is_unsafe || entry->is_encrypted ||
        entry->is_symbolic_link || entry->path != entry->physical_path) {
      return std::nullopt;
    }
    // Chrome's signed verification index is not an executable resource.
    // This host verifies the full CRX and never exposes the index to extensions.
    if (web_store && name == "_metadata" && entry->is_directory &&
        !std::exchange(saw_store_directory, true)) {
      continue;
    }
    if (web_store && name == "_metadata/verified_contents.json" &&
        !entry->is_directory && !std::exchange(saw_store_contents, true)) {
      continue;
    }
    if (!paths.Add(name, entry->is_directory))
      return std::nullopt;
    if (entry->is_directory)
      continue;
    const size_t remaining =
        std::min(kMaxFixtureFileSize, kMaxExtractedBytes - total);
    // ZipReader skips extraction and validation when given a zero limit.
    const size_t read_limit = std::max(remaining, size_t{1});
    std::string contents;
    if (entry->original_size < 0 ||
        static_cast<uint64_t>(entry->original_size) > remaining ||
        !reader.ExtractCurrentEntryToString(read_limit, &contents) ||
        contents.size() > remaining) {
      return std::nullopt;
    }
    total += contents.size();
    files.emplace(std::string(name),
                  std::vector<uint8_t>(contents.begin(), contents.end()));
  }
  if (!reader.ok() || !files.contains("manifest.json"))
    return std::nullopt;
  return files;
}

class ExtensionArchiveImpl final : public mojom::ExtensionArchive {
 public:
  explicit ExtensionArchiveImpl(
      mojo::PendingReceiver<mojom::ExtensionArchive> receiver)
      : receiver_(this, std::move(receiver)) {
    CHECK(!base::CommandLine::ForCurrentProcess()->HasSwitch(
        sandbox::policy::switches::kNoSandbox));
    CHECK(sandbox::policy::Sandbox::IsProcessSandboxed());
  }

  void Extract(mojo_base::BigBuffer archive, bool web_store, ExtractCallback callback) override {
    const auto pid = static_cast<uint32_t>(base::GetCurrentProcId());
    if (archive.size() == 0 || archive.size() > kMaxFixtureArchiveSize) {
      std::move(callback).Run(pid, std::nullopt);
      return;
    }
    const std::string bytes(base::as_string_view(base::span(archive)));
    std::move(callback).Run(pid, ReadArchive(bytes, web_store));
  }

 private:
  mojo::Receiver<mojom::ExtensionArchive> receiver_;
};

std::optional<ExtractedExtensionFiles> ValidateExtractedFiles(
    uint32_t pid,
    const std::optional<ArchiveFiles>& files) {
  if (pid == 0 || pid == static_cast<uint32_t>(base::GetCurrentProcId()))
    return std::nullopt;
  if (!files || files->size() > kMaxArchiveEntries ||
      !files->contains("manifest.json")) {
    return std::nullopt;
  }
  ArchivePaths paths;
  size_t total = 0;
  ExtractedExtensionFiles result;
  for (const auto& [name, contents] : *files) {
    if (!paths.Add(name, /*directory=*/false) ||
        contents.size() > kMaxFixtureFileSize) {
      return std::nullopt;
    }
    total += contents.size();
    if (total > kMaxExtractedBytes)
      return std::nullopt;
    result.emplace(name, base::as_string_view(base::span(contents)));
  }
  std::puts("Zitter Content extracted its extension in a sandboxed utility");
  return result;
}

class ExtensionArchiveRequest final : public ExtensionArchiveExtraction {
 public:
  ExtensionArchiveRequest(std::string_view archive, bool web_store, Completion completion)
      : completion_(std::move(completion)),
        remote_(content::ServiceProcessHost::Launch<mojom::ExtensionArchive>(
            content::ServiceProcessHost::Options()
                .WithDisplayName("Zitter extension archive")
                .Pass())) {
    remote_.set_disconnect_handler(base::BindOnce(
        &ExtensionArchiveRequest::Fail, base::Unretained(this)));
    timeout_.Start(FROM_HERE, base::Seconds(10), this,
                   &ExtensionArchiveRequest::Fail);
    remote_->Extract(
        mojo_base::BigBuffer(base::as_byte_span(archive)), web_store,
        base::BindOnce(&ExtensionArchiveRequest::OnReply, base::Unretained(this)));
  }

 private:
  void OnReply(uint32_t pid, const std::optional<ArchiveFiles>& files) {
    Finish(ValidateExtractedFiles(pid, files));
  }

  void Fail() { Finish(std::nullopt); }

  void Finish(std::optional<ExtractedExtensionFiles> files) {
    timeout_.Stop();
    remote_.reset();
    // The caller may destroy the request from its callback.
    std::move(completion_).Run(std::move(files));
  }

  Completion completion_;
  mojo::Remote<mojom::ExtensionArchive> remote_;
  base::OneShotTimer timeout_;
};

}  // namespace

std::unique_ptr<ExtensionArchiveExtraction> ExtensionArchiveExtraction::Start(
    std::string_view verified_archive,
    bool web_store,
    Completion completion) {
  return std::make_unique<ExtensionArchiveRequest>(verified_archive, web_store,
                                                   std::move(completion));
}

std::optional<ExtractedExtensionFiles> ExtractExtensionFixture(
    std::string_view verified_archive, bool web_store) {
  // Startup callers hold the Workspace lock and have no BrowserContext yet.
  base::RunLoop loop(base::RunLoop::Type::kNestableTasksAllowed);
  std::optional<ExtractedExtensionFiles> files;
  auto request = ExtensionArchiveExtraction::Start(
      verified_archive, web_store,
      base::BindOnce(
          [](std::optional<ExtractedExtensionFiles>* result,
             base::OnceClosure done,
             std::optional<ExtractedExtensionFiles> extracted) {
            *result = std::move(extracted);
            std::move(done).Run();
          },
          &files, loop.QuitClosure()));
  loop.Run();
  return files;
}

void LabUtilityClient::RegisterMainThreadServices(mojo::ServiceFactory& services) {
#if BUILDFLAG(ENABLE_PRINTING) && BUILDFLAG(ENABLE_PDF)
  RegisterPdfPreviewService(services);
#endif
#if BUILDFLAG(ENABLE_PRINTING)
  services.Add(+[](mojo::PendingReceiver<printing::mojom::PrintCompositor>
                       receiver) {
    CHECK(!base::CommandLine::ForCurrentProcess()->HasSwitch(
        sandbox::policy::switches::kNoSandbox));
    CHECK(sandbox::policy::Sandbox::IsProcessSandboxed());
    return std::make_unique<printing::PrintCompositorImpl>(
        std::move(receiver), /*initialize_environment=*/true,
        content::UtilityThread::Get()->GetIOTaskRunner());
  });
#endif
  services.Add(
      +[](mojo::PendingReceiver<mojom::ExtensionArchive> receiver) {
        return std::make_unique<ExtensionArchiveImpl>(std::move(receiver));
      });
}

}  // namespace zitter::content_embedder
