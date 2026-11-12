// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "workspace_native_messaging_mac.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/task/thread_pool.h"
#include "content/public/browser/browser_context.h"
#include "native_message_process_host.h"
#include "native_process_launcher.h"
#include "net/base/file_stream.h"
#include "url/gurl.h"

namespace zitter::content_embedder {
namespace {

using extensions::NativeProcessLauncher;

bool ValidHostName(std::string_view name) {
  return !name.empty() && name.size() <= 255 && name.front() != '.' &&
         name.back() != '.' && name.find("..") == std::string_view::npos &&
         std::ranges::all_of(name, [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
         });
}

bool IsRegularFile(const base::FilePath& path) {
  struct stat info {};
  return lstat(path.value().c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

struct LaunchState {
  ~LaunchState() {
    if (process.IsValid()) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
          base::BindOnce(&base::EnsureProcessTerminated, std::move(process)));
    }
  }

  NativeProcessLauncher::LaunchResult result = NativeProcessLauncher::RESULT_FAILED_TO_START;
  base::Process process;
  base::ScopedFD incoming;
  base::ScopedFD outgoing;
};

std::unique_ptr<LaunchState> LaunchHost(const base::FilePath& user_directory,
                                        bool system_directory, const GURL& origin,
                                        const std::string& name) {
  auto state = std::make_unique<LaunchState>();
  if (!ValidHostName(name)) {
    state->result = NativeProcessLauncher::RESULT_INVALID_NAME;
    return state;
  }
  const std::string manifest_name = name + ".json";
  base::FilePath manifest;
  if (!user_directory.empty()) {
    const auto user_manifest = user_directory.AppendASCII(manifest_name);
    if (base::PathExists(user_manifest) || base::IsLink(user_manifest))
      manifest = user_manifest;
  }
  if (manifest.empty() && system_directory) {
    manifest = base::FilePath("/Library/Application Support/Zitter/NativeMessagingHosts")
                   .AppendASCII(manifest_name);
  }
  if (manifest.empty() || !base::PathExists(manifest)) {
    state->result = NativeProcessLauncher::RESULT_NOT_FOUND;
    return state;
  }
  std::string bytes;
  if (!IsRegularFile(manifest) || !base::ReadFileToStringWithMaxSize(manifest, &bytes, 64 * 1024))
    return state;
  auto data = base::JSONReader::ReadDict(bytes, base::JSON_PARSE_RFC);
  if (!data || !data->FindString("name") || *data->FindString("name") != name ||
      !data->FindString("type") || *data->FindString("type") != "stdio") {
    return state;
  }
  const auto* allowed = data->FindList("allowed_origins");
  if (!allowed || !allowed->contains(origin.spec())) {
    state->result = NativeProcessLauncher::RESULT_FORBIDDEN;
    return state;
  }
  const auto* path = data->FindString("path");
  if (!path || path->find('\0') != std::string::npos)
    return state;
  const base::FilePath program = base::FilePath::FromUTF8Unsafe(*path);
  base::FilePath executable;
  if (!program.IsAbsolute() || program.ReferencesParent() ||
      !base::NormalizeFilePath(program, &executable) || !IsRegularFile(executable)) {
    return state;
  }

  int read_pipe[2];
  if (HANDLE_EINTR(pipe(read_pipe)) != 0)
    return state;
  base::ScopedFD incoming(read_pipe[0]);
  base::ScopedFD child_output(read_pipe[1]);
  int write_pipe[2];
  if (HANDLE_EINTR(pipe(write_pipe)) != 0)
    return state;
  base::ScopedFD child_input(write_pipe[0]);
  base::ScopedFD outgoing(write_pipe[1]);
  base::CommandLine command(executable);
  command.AppendArg(origin.spec());
  base::LaunchOptions options;
  options.current_directory = executable.DirName();
  options.disclaim_responsibility = true;
  options.fds_to_remap.emplace_back(child_input.get(), STDIN_FILENO);
  options.fds_to_remap.emplace_back(child_output.get(), STDOUT_FILENO);
  state->process = base::LaunchProcess(command, options);
  if (state->process.IsValid()) {
    state->incoming = std::move(incoming);
    state->outgoing = std::move(outgoing);
    state->result = NativeProcessLauncher::RESULT_SUCCESS;
  }
  return state;
}

class WorkspaceNativeProcessLauncher final : public NativeProcessLauncher {
 public:
  WorkspaceNativeProcessLauncher(base::FilePath user_directory, bool system_directory)
      : user_directory_(std::move(user_directory)), system_directory_(system_directory) {}

  void Launch(const GURL& origin, const std::string& native_host_name,
              LaunchedCallback callback) const override {
    auto worker = base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});
    worker->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&LaunchHost, user_directory_, system_directory_, origin, native_host_name),
        base::BindOnce(
            [](scoped_refptr<base::SequencedTaskRunner> worker, LaunchedCallback reply,
               std::unique_ptr<LaunchState> state) {
              if (reply.IsCancelled())
                return;
              if (state->result != RESULT_SUCCESS) {
                std::move(reply).Run(state->result, base::Process(), base::kInvalidPlatformFile,
                                     nullptr, nullptr);
                return;
              }
              const int read_fd = state->incoming.get();
              auto input = std::make_unique<net::FileStream>(
                  base::File(std::move(state->incoming)), worker);
              // A read triggered by an earlier readiness notification can
              // wait for the host's next reply. Keep writes independent.
              auto output = std::make_unique<net::FileStream>(
                  base::File(std::move(state->outgoing)),
                  base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}));
              std::move(reply).Run(RESULT_SUCCESS, std::move(state->process), read_fd,
                                   std::move(input), std::move(output));
            },
            worker, std::move(callback)));
  }

 private:
  const base::FilePath user_directory_;
  const bool system_directory_;
};

}  // namespace

std::unique_ptr<extensions::NativeMessageHost> CreateWorkspaceNativeMessageHost(
    content::BrowserContext& context, const std::string& extension_id,
    const std::string& host_name, bool allow_user_level) {
  const auto user_directory = allow_user_level
      ? context.GetPath().DirName().AppendASCII("NativeMessagingHosts") : base::FilePath();
  const bool system_directory = !base::CommandLine::ForCurrentProcess()->HasSwitch(
      "zitter-content-workspace-probe");
  return extensions::NativeMessageProcessHost::CreateWithLauncher(
      extension_id, host_name,
      std::make_unique<WorkspaceNativeProcessLauncher>(user_directory, system_directory));
}

}  // namespace zitter::content_embedder
