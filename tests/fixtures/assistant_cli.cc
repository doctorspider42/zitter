// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Native subprocess fixture: inspect isolation flags without contacting a
// model.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include "base/compiler_specific.h"
#include "base/containers/span.h"
int main(int argc, const char** argv) {
  // The OS guarantees argc live argv entries at the process entry point.
  const auto arguments =
      UNSAFE_BUFFERS(base::span(argv, static_cast<size_t>(argc)));
  std::vector<std::string> args(arguments.begin() + 1, arguments.end());
  auto has = [&](const std::string& value) {
    return std::find(args.begin(), args.end(), value) != args.end();
  };
  auto after = [&](const std::string& value) {
    auto it = std::find(args.begin(), args.end(), value);
    return it != args.end() && ++it != args.end() ? *it : std::string();
  };
  const auto input = std::string(std::istreambuf_iterator<char>(std::cin), {});
  if (input.find("messages") == std::string::npos)
    return 2;
  const bool codex = has("exec");
  if (codex) {
    for (const auto* arg :
         {"--json", "--ephemeral", "--ignore-user-config", "--ignore-rules",
          "features.shell_tool=false", "mcp_servers={}",
          "web_search=\"disabled\"", "approval_policy=\"never\""})
      if (!has(arg))
        return 3;
    const auto* home = std::getenv("CODEX_HOME");
    if (!home ||
        !std::filesystem::equivalent(std::filesystem::path(home),
                                     std::filesystem::current_path()) ||
        !std::filesystem::exists(after("--output-schema")))
      return 4;
    if (after("--sandbox") != "read-only")
      return 5;
    if (input.find("untrusted_tool_results") != std::string::npos &&
        input.find("Native CLI image test") != std::string::npos &&
        !std::filesystem::exists(after("--image")))
      return 6;
  } else {
    if (after("--tools") != "" ||
        after("--mcp-config") != "{\"mcpServers\":{}}" ||
        !has("--strict-mcp-config") || !has("--no-session-persistence") ||
        after("--setting-sources") != "")
      return 7;
    if (input.find("untrusted_tool_results") != std::string::npos &&
        input.find("Native CLI image test") != std::string::npos &&
        (after("--input-format") != "stream-json" ||
         input.find("image/jpeg") == std::string::npos))
      return 8;
  }
  if (after("--model") == "invalid-utf8") {
    std::cout.put(static_cast<char>(0xff));
    std::cout << '\n';
    return 0;
  }
  if (after("--model") == "slow-cli")
    std::this_thread::sleep_for(std::chrono::seconds(10));
  if (codex)
    std::cout
        << R"({"type":"item.completed","item":{"type":"agent_message","text":"{\"answer\":\"CLI integration complete\",\"actions\":[]}"}})"
        << '\n';
  else
    std::cout
        << R"({"type":"result","structured_output":{"answer":"CLI integration complete","actions":[]}})"
        << '\n';
  return 0;
}
