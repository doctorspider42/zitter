// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_ASSISTANT_CODEX_INSTALL_H_
#define ZITTER_CHROMIUM_ASSISTANT_CODEX_INSTALL_H_

#include <cstdint>
#include <string_view>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "build/build_config.h"

namespace zitter::assistant {

// The Codex CLI release Zitter downloads when no Codex is installed. Codex is
// Apache-2.0 (github.com/openai/codex). Bump the version, sizes and SHA-256
// digests together, from the GitHub release's asset list.
inline constexpr std::string_view kManagedCodexVersion = "0.172.0";

struct ManagedCodexAsset {
  std::string_view url;
  int64_t size = 0;
  std::string_view sha256;  // Of the .zst file, lowercase hex.
};

inline constexpr ManagedCodexAsset ManagedCodexDownload() {
#define ZITTER_CODEX_RELEASE "https://github.com/openai/codex/releases/download/rust-v0.172.0/"
#if BUILDFLAG(IS_WIN) && defined(ARCH_CPU_ARM64)
  return {ZITTER_CODEX_RELEASE "codex-aarch64-pc-windows-msvc.exe.zst", 94853678,
          "829c0533ba27e00e9df05c11327413870828335757c980cb6c168eb0da9418c7"};
#elif BUILDFLAG(IS_WIN)
  return {ZITTER_CODEX_RELEASE "codex-x86_64-pc-windows-msvc.exe.zst", 102503045,
          "15fb7134304d40c3456cc1dd90a25abea5ceab654e3fa50157d3fe85870e4b17"};
#elif BUILDFLAG(IS_MAC) && defined(ARCH_CPU_ARM64)
  return {ZITTER_CODEX_RELEASE "codex-aarch64-apple-darwin.zst", 85996227,
          "ce4a2b923ae48277aba16431d2d2cc4ab30b23fc48a48a2190bbe0015aa0ce80"};
#elif BUILDFLAG(IS_MAC)
  return {ZITTER_CODEX_RELEASE "codex-x86_64-apple-darwin.zst", 94354395,
          "f6f7b70deca06cb5afd481cecfd7854015cae3f0e42cc314876b3b059f39a940"};
#else
  return {};
#endif
#undef ZITTER_CODEX_RELEASE
}

// Where the downloaded Codex lives: outside every workspace and profile, so
// all of them share one copy. The assistant helper finds it here too.
// Windows: %LOCALAPPDATA%\Zitter\Tools\codex\<version>\codex.exe
// macOS: ~/Library/Application Support/Zitter/Tools/codex/<version>/codex
inline base::FilePath ManagedCodexDirectory() {
  base::FilePath root;
#if BUILDFLAG(IS_WIN)
  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, &root))
    return {};
#elif BUILDFLAG(IS_MAC)
  if (!base::PathService::Get(base::DIR_APP_DATA, &root))
    return {};
#else
  return {};
#endif
  return root.AppendASCII("Zitter").AppendASCII("Tools").AppendASCII("codex").AppendASCII(
      kManagedCodexVersion);
}

inline base::FilePath ManagedCodexPath() {
  const auto directory = ManagedCodexDirectory();
  if (directory.empty())
    return {};
#if BUILDFLAG(IS_WIN)
  return directory.AppendASCII("codex.exe");
#else
  return directory.AppendASCII("codex");
#endif
}

}  // namespace zitter::assistant

#endif  // ZITTER_CHROMIUM_ASSISTANT_CODEX_INSTALL_H_
