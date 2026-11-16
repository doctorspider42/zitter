// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "workspace_native_messaging.h"

#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/unguessable_token.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"
#else
#include <sys/stat.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#endif

#include <algorithm>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
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
#if BUILDFLAG(IS_WIN)
  base::File::Info info;
  return base::GetFileInfo(path, &info) && !info.is_directory &&
         !info.is_symbolic_link;
#else
  struct stat info {};
  return lstat(path.value().c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
}

#if BUILDFLAG(IS_WIN)
base::FilePath RegisteredManifest(const std::string& name, bool allow_user) {
  for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
    if (root == HKEY_CURRENT_USER && !allow_user)
      continue;
    for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
      for (const wchar_t* prefix : {L"SOFTWARE\\Zitter\\NativeMessagingHosts\\",
                                    L"SOFTWARE\\Chromium\\NativeMessagingHosts\\",
                                    L"SOFTWARE\\Google\\Chrome\\NativeMessagingHosts\\"}) {
        const std::wstring key_path = prefix + base::UTF8ToWide(name);
        base::win::RegKey key;
        std::wstring value;
        if (key.Open(root, key_path.c_str(), KEY_QUERY_VALUE | view) == ERROR_SUCCESS &&
            key.ReadValue(nullptr, &value) == ERROR_SUCCESS) {
          return base::FilePath(value);
        }
      }
    }
  }
  return {};
}

// The browser ends are overlapped for net::FileStream. The host inherits
// synchronous handles so it can use ordinary stdin and stdout.
struct HostPipe {
  base::win::ScopedHandle browser;
  base::win::ScopedHandle host;
};

HostPipe OpenHostPipe(const std::wstring& name, bool browser_reads) {
  HostPipe pipe;
  pipe.browser.Set(::CreateNamedPipeW(
      name.c_str(),
      (browser_reads ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND) |
          FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 1, 0, 0, 0, nullptr));
  if (!pipe.browser.is_valid())
    return pipe;

  SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
  pipe.host.Set(::CreateFileW(
      name.c_str(), browser_reads ? GENERIC_WRITE : GENERIC_READ, 0,
      &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  return pipe;
}
#endif

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
  base::ScopedPlatformFile incoming;
  base::ScopedPlatformFile outgoing;
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
  // Native apps such as password managers and device agents register their
  // hosts for Chrome only. Like other Chromium browsers, fall back to Chrome's
  // directories after Zitter's own; each host still lists the extension IDs
  // it accepts in allowed_origins.
  std::vector<base::FilePath> directories;
  if (!user_directory.empty()) {
    directories.push_back(user_directory);
#if BUILDFLAG(IS_MAC)
    // Probes see only their own hosts.
    if (system_directory) {
      directories.push_back(base::GetHomeDir().Append(
          "Library/Application Support/Google/Chrome/NativeMessagingHosts"));
    }
#endif
  }
#if BUILDFLAG(IS_MAC)
  if (system_directory) {
    directories.emplace_back("/Library/Application Support/Zitter/NativeMessagingHosts");
    directories.emplace_back("/Library/Google/Chrome/NativeMessagingHosts");
  }
#endif
  base::FilePath manifest;
  for (const auto& directory : directories) {
    const auto candidate = directory.AppendASCII(manifest_name);
    if (base::PathExists(candidate) || base::IsLink(candidate)) {
      manifest = candidate;
      break;
    }
  }
#if BUILDFLAG(IS_WIN)
  if (manifest.empty() && system_directory)
    manifest = RegisteredManifest(name, !user_directory.empty());
#endif
  if (!manifest.IsAbsolute() || manifest.ReferencesParent() ||
      !base::PathExists(manifest)) {
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
  base::FilePath program = base::FilePath::FromUTF8Unsafe(*path);
#if BUILDFLAG(IS_WIN)
  if (program.empty() || program.ReferencesParent())
    return state;
  if (!program.IsAbsolute())
    program = manifest.DirName().Append(program);
#endif
  base::FilePath executable;
  if (!program.IsAbsolute() || program.ReferencesParent() ||
      !base::NormalizeFilePath(program, &executable) || !IsRegularFile(executable)) {
    return state;
  }

  base::CommandLine command(executable);
  command.AppendArg(origin.spec());
  base::LaunchOptions options;
  options.current_directory = executable.DirName();
#if BUILDFLAG(IS_WIN)
  if (!executable.MatchesFinalExtension(L".exe"))
    return state;
  const std::wstring prefix = L"\\\\.\\pipe\\zitter.nativeMessaging.";
  const std::wstring token = base::UTF8ToWide(base::UnguessableToken::Create().ToString());
  auto incoming = OpenHostPipe(prefix + token + L".out", true);
  auto outgoing = OpenHostPipe(prefix + token + L".in", false);
  if (!incoming.host.is_valid() || !outgoing.host.is_valid())
    return state;
  command.AppendArg("--parent-window=0");
  options.start_hidden = true;
  options.stdin_handle = outgoing.host.get();
  options.stdout_handle = incoming.host.get();
  options.handles_to_inherit = {options.stdin_handle, options.stdout_handle};
#else
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
  options.disclaim_responsibility = true;
  options.fds_to_remap.emplace_back(child_input.get(), STDIN_FILENO);
  options.fds_to_remap.emplace_back(child_output.get(), STDOUT_FILENO);
#endif
  state->process = base::LaunchProcess(command, options);
  if (state->process.IsValid()) {
#if BUILDFLAG(IS_WIN)
    state->incoming = std::move(incoming.browser);
    state->outgoing = std::move(outgoing.browser);
#else
    state->incoming = std::move(incoming);
    state->outgoing = std::move(outgoing);
#endif
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
#if BUILDFLAG(IS_WIN)
              constexpr bool async = true;
#else
              constexpr bool async = false;
#endif
              const auto read_fd = state->incoming.get();
              auto input = std::make_unique<net::FileStream>(
                  base::File(std::move(state->incoming), async), worker);
              // A read triggered by an earlier readiness notification can
              // wait for the host's next reply. Keep writes independent.
              auto output = std::make_unique<net::FileStream>(
                  base::File(std::move(state->outgoing), async),
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
