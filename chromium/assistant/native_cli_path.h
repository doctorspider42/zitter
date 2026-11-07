// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_ASSISTANT_NATIVE_CLI_PATH_H_
#define ZITTER_CHROMIUM_ASSISTANT_NATIVE_CLI_PATH_H_

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/environment.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "build/build_config.h"

namespace zitter::assistant {

inline base::FilePath NativeCommand(base::cstring_view setting, std::string name,
                                    std::string_view preferred = {}) {
  auto env = base::Environment::Create();
  std::vector<base::FilePath> candidates;
  if (!preferred.empty())
    candidates.push_back(base::FilePath::FromUTF8Unsafe(preferred));
  else if (auto path = env->GetVar(setting); path && !path->empty())
    candidates.push_back(base::FilePath::FromUTF8Unsafe(*path));
  else {
#if BUILDFLAG(IS_WIN)
    name += ".exe";
    const char separator = ';';
#else
    const char separator = ':';
#endif
    for (auto dir : base::SplitString(
             env->GetVar("PATH").value_or(""), std::string(1, separator),
             base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      auto root = base::FilePath::FromUTF8Unsafe(dir);
      if (!root.IsAbsolute()) continue;
      candidates.push_back(root.AppendASCII(name));
#if BUILDFLAG(IS_WIN)
      if (name == "codex.exe") {
#if defined(ARCH_CPU_ARM64)
        const char* vendor = "node_modules/@openai/codex/node_modules/@openai/codex-win32-arm64/vendor/aarch64-pc-windows-msvc/bin/codex.exe";
#else
        const char* vendor = "node_modules/@openai/codex/node_modules/@openai/codex-win32-x64/vendor/x86_64-pc-windows-msvc/bin/codex.exe";
#endif
        candidates.push_back(root.AppendASCII(vendor).NormalizePathSeparators());
      }
#endif
    }
#if BUILDFLAG(IS_WIN)
    if (name == "codex.exe") {
      base::FilePath local;
      if (base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local)) {
        base::FileEnumerator versions(local.AppendASCII("OpenAI").AppendASCII("Codex").AppendASCII("bin"),
                                      false, base::FileEnumerator::DIRECTORIES);
        std::vector<std::pair<base::Time, base::FilePath>> desktop;
        for (auto version = versions.Next(); !version.empty(); version = versions.Next())
          desktop.emplace_back(versions.GetInfo().GetLastModifiedTime(), version.AppendASCII("codex.exe"));
        std::ranges::sort(desktop, [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& candidate : desktop) candidates.push_back(candidate.second);
      }
    }
#endif
  }
  for (const auto& path : candidates) {
    if (!path.IsAbsolute())
      continue;
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
    std::array<unsigned char, 4> magic{};
    if (!file.IsValid() ||
        file.ReadAtCurrentPos(base::as_writable_byte_span(magic)).value_or(0) !=
            4)
      continue;
    const auto& b = magic;
    // A direct platform executable, never a shell/Node launcher.
    const bool native =
        (b[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' &&
         magic[3] == 'F') ||
        (magic[0] == 'M' && magic[1] == 'Z') ||
        (b[0] == 0xcf && b[1] == 0xfa && b[2] == 0xed && b[3] == 0xfe) ||
        (b[0] == 0xce && b[1] == 0xfa && b[2] == 0xed && b[3] == 0xfe) ||
        (b[0] == 0xca && b[1] == 0xfe && b[2] == 0xba && b[3] == 0xbe);
    if (native)
      return path;
  }
  return {};
}
}  // namespace zitter::assistant

#endif
