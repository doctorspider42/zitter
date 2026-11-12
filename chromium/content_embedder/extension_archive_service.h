// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_EXTENSION_ARCHIVE_SERVICE_H_
#define ZITTER_CONTENT_EXTENSION_ARCHIVE_SERVICE_H_

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "content/public/utility/content_utility_client.h"

namespace zitter::content_embedder {

inline constexpr char kExtensionArchiveFileName[] = "fixture.crx";
inline constexpr size_t kMaxFixtureFileSize = 16 * 1024 * 1024;
inline constexpr size_t kMaxFixtureArchiveSize = 32 * 1024 * 1024;

std::optional<base::FilePath> ExtensionArchiveResourcePath(std::string_view name);
using ExtractedExtensionFiles = std::map<std::string, std::string>;

class ExtensionArchiveExtraction {
 public:
  using Completion =
      base::OnceCallback<void(std::optional<ExtractedExtensionFiles>)>;

  // Keep the request alive until completion. Destruction cancels delivery.
  static std::unique_ptr<ExtensionArchiveExtraction> Start(
      std::string_view verified_archive,
      bool web_store,
      Completion completion);
  virtual ~ExtensionArchiveExtraction() = default;

 protected:
  ExtensionArchiveExtraction() = default;
};

std::optional<ExtractedExtensionFiles> ExtractExtensionFixture(
    std::string_view verified_archive, bool web_store = false);

class LabUtilityClient : public content::ContentUtilityClient {
 public:
  void RegisterMainThreadServices(mojo::ServiceFactory& services) override;
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_EXTENSION_ARCHIVE_SERVICE_H_
