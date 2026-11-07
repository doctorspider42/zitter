// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <string>
#include <thread>
#include "base/base64.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chromium/assistant/native_cli_path.h"
#include "chromium/assistant/native_transport.h"
#if BUILDFLAG(IS_POSIX)
#include <signal.h>
#include <unistd.h>
#endif
#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/win/scoped_handle.h"
#endif
namespace zitter::assistant {
void runtime_stream(rust::Str id, rust::Str kind, rust::Str chunk);
namespace {
std::string Failure(std::string_view message) {
  return base::WriteJson(base::DictValue().Set("error", message)).value();
}
std::string CliFailure(std::string_view output, std::string_view diagnostics) {
  const auto message = base::ToLowerASCII(std::string(output) + std::string(diagnostics));
  if (message.contains("unauthorized") || message.contains("401") ||
      message.contains("not logged in") || (message.contains("please run") && message.contains("login")) ||
      message.contains("authentication") || message.contains("sign in"))
    return Failure("Sign in to the provider CLI, then retry the connection test.");
  if (message.contains("unexpected argument") || message.contains("unknown option"))
    return Failure("The provider CLI does not support the required options. Update it and retry.");
  if (message.contains("model") && (message.contains("not supported") || message.contains("not found") ||
                                     message.contains("not available") || message.contains("access")))
    return Failure("The service did not find this model or your account cannot use it.");
  if (message.contains("429") || message.contains("usage limit") || message.contains("rate limit"))
    return Failure("The provider limit was reached. Check your plan or API balance.");
  if (message.contains("sandbox") || message.contains("access is denied"))
    return Failure("The provider could not start its sandbox. Run the CLI once in a terminal to finish setup.");
  return Failure("Provider CLI failed. Check installation, supported version and login.");
}
}  // namespace
std::string PerformCLI(std::string id, const base::DictValue& request) {
  const auto* provider = request.FindString("provider");
  const auto* model = request.FindString("model");
  const auto* messages = request.FindList("messages");
  const auto* system = request.FindString("system");
  const auto* schema = request.FindDict("schema");
  if (!provider || !model || !messages || !system || !schema)
    return Failure("Invalid provider request");
  const bool codex = *provider == "codex";
  if (!codex && *provider != "claude")
    return Failure("Unknown provider");
  const auto executable =
      (codex ? NativeCommand("ZITTER_CODEX_PATH", "codex")
             : NativeCommand("ZITTER_CLAUDE_PATH", "claude"));
  if (executable.empty())
    return Failure(codex ? "Install native Codex CLI or set ZITTER_CODEX_PATH "
                           "to its native executable."
                         : "Install native Claude Code or set "
                           "ZITTER_CLAUDE_PATH to its native executable.");
  base::ScopedTempDir temp;
  if (!temp.CreateUniqueTempDir())
    return Failure("Could not prepare isolated provider directory");
#if BUILDFLAG(IS_POSIX)
  if (!base::SetPosixFilePermissions(temp.GetPath(), 0700))
    return Failure("Could not secure provider directory");
#endif
  base::ListValue transcript;
  std::vector<std::string> images;
  for (const auto& m : *messages) {
    const auto* d = m.GetIfDict();
    if (!d)
      continue;
    transcript.Append(
        base::DictValue()
            .Set("role",
                 d->FindString("role") ? *d->FindString("role") : "user")
            .Set("content",
                 d->FindString("content") ? *d->FindString("content") : ""));
    if (const auto* shots = d->FindList("images"))
      for (const auto& image : *shots)
        if (image.is_string() && images.size() < 2)
          images.push_back(image.GetString());
  }
  const auto input =
      base::WriteJson(base::DictValue().Set("messages", std::move(transcript)))
          .value();
  const auto schema_text = base::WriteJson(*schema).value();
  base::CommandLine command(executable);
  base::LaunchOptions options;
#if BUILDFLAG(IS_WIN)
  options.environment[L"ZITTER_OPENAI_API_KEY"]=L"";
#else
  options.environment["ZITTER_OPENAI_API_KEY"]="";
#endif
  options.current_directory = temp.GetPath();
  std::string stdin_text;
  if (codex) {
    command.AppendArg("exec");
    for (const char* flag : {"--json", "--ephemeral", "--ignore-user-config",
                             "--ignore-rules", "--skip-git-repo-check"})
      command.AppendArg(flag);
    command.AppendArg("--model");
    command.AppendArg(*model);
    command.AppendArg("--sandbox");
    command.AppendArg("read-only");
    command.AppendArg("--output-schema");
    command.AppendArgPath(temp.GetPath().AppendASCII("schema.json"));
    if (!base::WriteFile(temp.GetPath().AppendASCII("schema.json"),
                         schema_text))
      return Failure("Could not prepare response schema");
    for (const char* policy :
         {"approval_policy=\"never\"",
          "shell_environment_policy.inherit=\"none\"",
          "features.shell_tool=false", "mcp_servers={}",
          "web_search=\"disabled\"",
          "sandbox_workspace_write.network_access=false"}) {
      command.AppendArg("--config");
      command.AppendArg(policy);
    }
    auto env = base::Environment::Create();
    base::FilePath home;
    if (auto value = env->GetVar("CODEX_HOME"))
      home = base::FilePath::FromUTF8Unsafe(*value);
    else {
      base::PathService::Get(base::DIR_HOME, &home);
      home = home.AppendASCII(".codex");
    }
    const auto auth = home.AppendASCII("auth.json"),
               target = temp.GetPath().AppendASCII("auth.json");
    if (base::PathExists(auth)) {
      if (!base::CopyFile(auth, target))
        return Failure("Could not prepare Codex authentication");
#if BUILDFLAG(IS_POSIX)
      if (!base::SetPosixFilePermissions(target, 0600))
        return Failure("Could not secure Codex authentication");
#endif
    }
    options.environment[FILE_PATH_LITERAL("CODEX_HOME")] =
        temp.GetPath().value();
    for (size_t i = 0; i < images.size(); ++i) {
      auto bytes = base::Base64Decode(images[i]);
      if (!bytes)
        return Failure("Invalid screenshot");
      const auto path = temp.GetPath().AppendASCII("screenshot-" +
                                                   std::to_string(i) + ".jpg");
      if (!base::WriteFile(path, *bytes))
        return Failure("Could not prepare screenshot");
      command.AppendArg("--image");
      command.AppendArgPath(path);
    }
    command.AppendArg("-");
    stdin_text = *system + "\n\n" + input;
  } else {
    for (const char* arg :
         {"-p", "--output-format", "stream-json", "--verbose",
          "--include-partial-messages", "--tools", "", "--strict-mcp-config",
          "--mcp-config", "{\"mcpServers\":{}}", "--no-session-persistence",
          "--setting-sources", "", "--disable-slash-commands"})
      command.AppendArg(arg);
    command.AppendArg("--model");
    command.AppendArg(*model);
    command.AppendArg("--json-schema");
    command.AppendArg(schema_text);
    command.AppendArg("--system-prompt");
    command.AppendArg(*system);
    if (images.empty())
      stdin_text = input;
    else {
      command.AppendArg("--input-format");
      command.AppendArg("stream-json");
      base::ListValue content;
      content.Append(base::DictValue().Set("type", "text").Set("text", input));
      for (const auto& image : images)
        content.Append(base::DictValue()
                           .Set("type", "image")
                           .Set("source", base::DictValue()
                                              .Set("type", "base64")
                                              .Set("media_type", "image/jpeg")
                                              .Set("data", image)));
      content.Append(
          base::DictValue()
              .Set("type", "text")
              .Set("text",
                   "The images are the screenshots the transcript names. You "
                   "have no tools here: request every browser tool only in the "
                   "actions of your structured answer."));
      stdin_text =
          base::WriteJson(
              base::DictValue()
                  .Set("type", "user")
                  .Set("message", base::DictValue()
                                      .Set("role", "user")
                                      .Set("content", std::move(content))))
              .value() +
          "\n";
    }
  }
  const auto in_path = temp.GetPath().AppendASCII("input"),
             out_path = temp.GetPath().AppendASCII("output"),
             diagnostics_path = temp.GetPath().AppendASCII("diagnostics");
  if (!base::WriteFile(in_path, stdin_text))
    return Failure("Could not prepare provider input");
  base::File in(in_path, base::File::FLAG_OPEN | base::File::FLAG_READ),
      out(out_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE),
      diagnostics(diagnostics_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
#if BUILDFLAG(IS_POSIX)
  options.fds_to_remap = {{in.GetPlatformFile(), STDIN_FILENO},
                          {out.GetPlatformFile(), STDOUT_FILENO},
                          {diagnostics.GetPlatformFile(), STDERR_FILENO}};
  options.new_process_group = true;
#elif BUILDFLAG(IS_WIN)
  options.start_hidden = true;
  options.stdin_handle = in.GetPlatformFile();
  options.stdout_handle = out.GetPlatformFile();
  options.stderr_handle = diagnostics.GetPlatformFile();
  options.handles_to_inherit = {in.GetPlatformFile(), out.GetPlatformFile(),
                                diagnostics.GetPlatformFile()};
  for (auto handle : options.handles_to_inherit)
    ::SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  base::win::ScopedHandle job(::CreateJobObject(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job.is_valid() ||
      !::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits)))
    return Failure("Could not isolate provider process");
  options.job_handle = job.get();
#endif
  if (!in.IsValid() || !out.IsValid() || !diagnostics.IsValid())
    return Failure("Could not prepare provider streams");
#if BUILDFLAG(IS_WIN)
  // CommandLine drops empty Windows arguments. Claude needs them after
  // --tools and --setting-sources to disable both features.
  std::wstring command_line;
  for (const auto& arg : command.argv()) {
    if (!command_line.empty())
      command_line += L' ';
    command_line += arg.empty()
                        ? L"\"\""
                        : base::CommandLine::QuoteForCommandLineToArgvW(arg);
  }
  auto process = base::LaunchProcess(command_line, options);
#else
  auto process = base::LaunchProcess(command, options);
#endif
  if (!process.IsValid())
    return Failure(
        "Provider CLI could not start. Check installation and login.");
  const auto started = base::TimeTicks::Now();
  size_t sent = 0;
  int code = 1;
  std::string output;
  for (;;) {
    const bool done =
        process.WaitForExitWithTimeout(base::Milliseconds(100), &code);
    const bool within_limit =
        base::ReadFileToStringWithMaxSize(out_path, &output, 4 * 1024 * 1024);
    const auto complete = output.rfind('\n');
    const bool invalid_utf8 =
        (complete != std::string::npos &&
         !base::IsStringUTF8(
             std::string_view(output).substr(0, complete + 1))) ||
        (done && !base::IsStringUTF8(output));
    const auto diagnostics_size = base::GetFileSize(diagnostics_path);
    if (!within_limit || invalid_utf8 || !diagnostics_size || *diagnostics_size > 512 * 1024 || cancelled(id) ||
        base::TimeTicks::Now() - started > base::Minutes(10)) {
#if BUILDFLAG(IS_POSIX)
      ::kill(-process.Pid(), SIGKILL);
#else
      ::TerminateJobObject(job.get(), 1);
#endif
      process.Terminate(1, true);
      return Failure(
          "Provider CLI stopped, timed out or exceeded its output limit.");
    }
    const auto newline = output.rfind('\n');
    if (newline != std::string::npos && newline + 1 > sent) {
      runtime_stream(id, *provider, output.substr(sent, newline + 1 - sent));
      sent = newline + 1;
    }
    if (done)
      break;
  }
#if BUILDFLAG(IS_POSIX)
  // Reap any agent descendants even when the direct child exited normally.
  ::kill(-process.Pid(), SIGKILL);
#endif
  if (code != 0) {
    std::string details;
    base::ReadFileToStringWithMaxSize(diagnostics_path, &details, 512 * 1024);
    return CliFailure(output, details);
  }
  return base::WriteJson(base::DictValue().Set("body", output)).value();
}
}  // namespace zitter::assistant
