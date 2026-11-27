// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/assistant/codex_setup.h"

#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/process/launch.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chromium/assistant/codex_install.h"
#include "crypto/hash.h"
#include "third_party/zstd/src/lib/zstd.h"

#if BUILDFLAG(IS_POSIX)
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "base/files/scoped_file.h"
#include "base/posix/eintr_wrapper.h"
#endif
#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif

namespace zitter::assistant {
namespace {

// The executable is about 400 MB; anything far larger is not Codex.
constexpr int64_t kMaxCodexSize = 1024LL * 1024 * 1024;
constexpr size_t kMaxLine = 1024 * 1024;

struct ZstdContextDeleter {
  void operator()(ZSTD_DCtx* context) const { ZSTD_freeDCtx(context); }
};

bool Decompress(base::File& in, base::File& out, crypto::hash::Hasher& hasher) {
  std::unique_ptr<ZSTD_DCtx, ZstdContextDeleter> context(ZSTD_createDCtx());
  if (!context)
    return false;
  std::vector<uint8_t> input(1024 * 1024), output(ZSTD_DStreamOutSize());
  int64_t written = 0;
  size_t pending = 1;  // ZSTD_decompressStream returns 0 once a frame ends.
  for (;;) {
    const auto read = in.ReadAtCurrentPos(base::span(input));
    if (!read)
      return false;
    if (*read == 0)
      break;
    hasher.Update(base::span(input).first(*read));
    ZSTD_inBuffer source{input.data(), *read, 0};
    bool output_full = false;
    while (source.pos < source.size || output_full) {
      ZSTD_outBuffer target{output.data(), output.size(), 0};
      pending = ZSTD_decompressStream(context.get(), &target, &source);
      if (ZSTD_isError(pending))
        return false;
      written += static_cast<int64_t>(target.pos);
      if (written > kMaxCodexSize ||
          !out.WriteAtCurrentPosAndCheck(base::span(output).first(target.pos)))
        return false;
      output_full = target.pos == target.size;
    }
  }
  return pending == 0 && written > 0;
}

// A direct platform executable, the same check the assistant applies when it
// picks a CLI to run.
bool IsNativeExecutable(const base::FilePath& path) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  std::array<uint8_t, 4> magic{};
  if (!file.IsValid() || file.ReadAtCurrentPos(base::span(magic)).value_or(0) != 4)
    return false;
#if BUILDFLAG(IS_WIN)
  return magic[0] == 'M' && magic[1] == 'Z';
#else
  return (magic[0] == 0xcf && magic[1] == 0xfa && magic[2] == 0xed && magic[3] == 0xfe) ||
         (magic[0] == 0xca && magic[1] == 0xfe && magic[2] == 0xba && magic[3] == 0xbe);
#endif
}

// Removes the executables of older managed Codex versions and then their
// directories when they are empty. Never deletes recursively.
void RemoveOlderVersions(const base::FilePath& current) {
  base::FileEnumerator versions(current.DirName(), false, base::FileEnumerator::DIRECTORIES);
  for (auto version = versions.Next(); !version.empty(); version = versions.Next()) {
    if (version == current)
      continue;
    for (const char* name : {"codex.exe", "codex", "codex.exe.partial", "codex.partial"})
      base::DeleteFile(version.AppendASCII(name));
    base::DeleteFile(version);  // Only succeeds when empty.
  }
}

// One end of the pipes to `codex app-server`, line by line.
class Channel {
 public:
#if BUILDFLAG(IS_WIN)
  Channel(HANDLE in, HANDLE out) : in_(in), out_(out) {}
#else
  Channel(base::ScopedFD in, base::ScopedFD out) : in_(std::move(in)), out_(std::move(out)) {}
#endif

  bool Send(const base::DictValue& message) {
    auto line = base::WriteJson(message);
    if (!line)
      return false;
    line->push_back('\n');
#if BUILDFLAG(IS_WIN)
    DWORD written = 0;
    return ::WriteFile(in_.get(), line->data(), static_cast<DWORD>(line->size()), &written,
                       nullptr) &&
           written == line->size();
#else
    return base::WriteFileDescriptor(in_.get(), *line);
#endif
  }

  std::optional<base::DictValue> Receive() {
    for (;;) {
      if (const auto newline = buffer_.find('\n'); newline != std::string::npos) {
        const std::string line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        if (auto message = base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC))
          return message;
        continue;  // Not a protocol message; skip it.
      }
      if (buffer_.size() > kMaxLine)
        return std::nullopt;
      std::array<char, 16384> chunk;
#if BUILDFLAG(IS_WIN)
      DWORD read = 0;
      if (!::ReadFile(out_.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read,
                      nullptr) ||
          read == 0)
        return std::nullopt;
#else
      const ssize_t read = HANDLE_EINTR(::read(out_.get(), chunk.data(), chunk.size()));
      if (read <= 0)
        return std::nullopt;
#endif
      buffer_.append(chunk.data(), static_cast<size_t>(read));
    }
  }

  // Sends a request and waits for its response. Answers requests from Codex
  // with an error: sign-in needs none of them.
  std::optional<base::DictValue> Call(int id, std::string_view method, base::DictValue params) {
    if (!Send(base::DictValue().Set("id", id).Set("method", method).Set("params",
                                                                         std::move(params))))
      return std::nullopt;
    for (;;) {
      auto message = Receive();
      if (!message)
        return std::nullopt;
      if (message->FindString("method")) {
        RefuseRequest(*message);
        continue;
      }
      if (message->FindInt("id") == id)
        return message;
    }
  }

  void RefuseRequest(const base::DictValue& message) {
    const auto* id = message.Find("id");
    if (!id)
      return;  // A notification.
    Send(base::DictValue().Set("id", id->Clone()).Set(
        "error", base::DictValue().Set("code", -32601).Set("message", "Not supported")));
  }

 private:
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle in_, out_;
#else
  base::ScopedFD in_, out_;
#endif
  std::string buffer_;
};

CodexSignInResult Failure(std::string error) {
  CodexSignInResult result;
  result.error = std::move(error);
  return result;
}

std::string PlanOf(const base::DictValue& account) {
  const auto* plan = account.FindString("planType");
  return plan ? *plan : std::string();
}

}  // namespace

bool InstallManagedCodex(const base::FilePath& archive) {
  const auto asset = ManagedCodexDownload();
  const auto target = ManagedCodexPath();
  if (asset.url.empty() || target.empty())
    return false;
  base::File in(archive, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!in.IsValid() || in.GetLength() != asset.size ||
      !base::CreateDirectory(target.DirName()))
    return false;
  const auto partial = target.AddExtensionASCII("partial");
  bool ok = false;
  {
    base::File out(partial, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    crypto::hash::Hasher hasher(crypto::hash::HashKind::kSha256);
    if (out.IsValid() && Decompress(in, out, hasher)) {
      std::array<uint8_t, crypto::hash::kSha256Size> digest;
      hasher.Finish(digest);
      ok = base::HexEncodeLower(digest) == asset.sha256 && out.Flush();
    }
  }
  ok = ok && IsNativeExecutable(partial);
#if BUILDFLAG(IS_POSIX)
  ok = ok && base::SetPosixFilePermissions(partial, 0755);
#endif
  if (!ok || !base::ReplaceFile(partial, target, nullptr)) {
    base::DeleteFile(partial);
    return false;
  }
  RemoveOlderVersions(target.DirName());
  return true;
}

bool IsCodexSignInUrl(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIs("https") || url.has_username() || url.has_password() ||
      url.has_port())
    return false;
  const auto host = url.host();
  return host == "auth.openai.com" || host == "chatgpt.com" || host == "auth.chatgpt.com";
}

CodexSignIn::CodexSignIn() = default;
CodexSignIn::~CodexSignIn() = default;

void CodexSignIn::Cancel() {
  base::AutoLock lock(lock_);
  cancelled_ = true;
#if BUILDFLAG(IS_WIN)
  if (job_.is_valid())
    ::TerminateJobObject(job_.get(), 1);
#else
  if (process_.IsValid())
    ::kill(-process_.Pid(), SIGKILL);
#endif
  if (process_.IsValid())
    process_.Terminate(1, false);
}

CodexSignInResult CodexSignIn::Run(const base::FilePath& codex,
                                   base::OnceCallback<void(GURL)> open_url) {
  base::CommandLine command(codex);
  command.AppendArg("app-server");
  base::LaunchOptions options;
  // Keep a stray API key out: the user asked to sign in with ChatGPT.
  options.environment[FILE_PATH_LITERAL("OPENAI_API_KEY")] = FILE_PATH_LITERAL("");
  options.environment[FILE_PATH_LITERAL("ZITTER_OPENAI_API_KEY")] = FILE_PATH_LITERAL("");
  base::FilePath working;
  if (base::GetTempDir(&working))
    options.current_directory = working;
#if BUILDFLAG(IS_WIN)
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE child_in = nullptr, parent_in = nullptr, parent_out = nullptr, child_out = nullptr;
  if (!::CreatePipe(&child_in, &parent_in, &inherit, 0))
    return Failure("pipe");
  base::win::ScopedHandle child_in_handle(child_in), parent_in_handle(parent_in);
  if (!::CreatePipe(&parent_out, &child_out, &inherit, 0))
    return Failure("pipe");
  base::win::ScopedHandle parent_out_handle(parent_out), child_out_handle(child_out);
  ::SetHandleInformation(parent_in, HANDLE_FLAG_INHERIT, 0);
  ::SetHandleInformation(parent_out, HANDLE_FLAG_INHERIT, 0);
  base::win::ScopedHandle null_handle(::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                                                    &inherit, OPEN_EXISTING, 0, nullptr));
  if (!null_handle.is_valid())
    return Failure("pipe");
  options.start_hidden = true;
  options.stdin_handle = child_in;
  options.stdout_handle = child_out;
  options.stderr_handle = null_handle.get();
  options.handles_to_inherit = {child_in, child_out, null_handle.get()};
  base::win::ScopedHandle job(::CreateJobObject(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job.is_valid() || !::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                                    &limits, sizeof(limits)))
    return Failure("job");
  options.job_handle = job.get();
#else
  base::ScopedFD child_in, parent_in, parent_out, child_out;
  if (!base::CreatePipe(&child_in, &parent_in) || !base::CreatePipe(&parent_out, &child_out))
    return Failure("pipe");
  base::ScopedFD null_fd(HANDLE_EINTR(::open("/dev/null", O_WRONLY | O_CLOEXEC)));
  if (!null_fd.is_valid())
    return Failure("pipe");
  options.fds_to_remap = {{child_in.get(), STDIN_FILENO},
                          {child_out.get(), STDOUT_FILENO},
                          {null_fd.get(), STDERR_FILENO}};
  options.new_process_group = true;
#endif
  {
    base::AutoLock lock(lock_);
    if (cancelled_)
      return Failure("cancelled");
    process_ = base::LaunchProcess(command, options);
    if (!process_.IsValid())
      return Failure("launch");
#if BUILDFLAG(IS_WIN)
    job_ = std::move(job);
#endif
  }
  // Drop the child's ends so reads end when Codex exits.
#if BUILDFLAG(IS_WIN)
  child_in_handle.Close();
  child_out_handle.Close();
  null_handle.Close();
  Channel channel(parent_in_handle.release(), parent_out_handle.release());
#else
  child_in.reset();
  child_out.reset();
  null_fd.reset();
  Channel channel(std::move(parent_in), std::move(parent_out));
#endif

  auto outcome = [&]() -> CodexSignInResult {
    auto initialized = channel.Call(
        1, "initialize",
        base::DictValue().Set("clientInfo", base::DictValue()
                                                .Set("name", "zitter")
                                                .Set("title", "Zitter")
                                                .Set("version", "1")));
    if (!initialized || !initialized->FindDict("result"))
      return Failure(initialized ? "unsupported" : "stopped");
    if (!channel.Send(base::DictValue().Set("method", "initialized")))
      return Failure("stopped");
    auto account = channel.Call(2, "account/read", base::DictValue());
    if (!account)
      return Failure("stopped");
    if (const auto* result = account->FindDict("result")) {
      if (const auto* current = result->FindDict("account")) {
        CodexSignInResult done;
        done.ok = done.already_signed_in = true;
        done.plan = PlanOf(*current);
        return done;
      }
    }
    auto started = channel.Call(3, "account/login/start", base::DictValue().Set("type", "chatgpt"));
    if (!started)
      return Failure("stopped");
    const auto* start = started->FindDict("result");
    const auto* login_id = start ? start->FindString("loginId") : nullptr;
    const auto* auth_url = start ? start->FindString("authUrl") : nullptr;
    if (!login_id || !auth_url || !IsCodexSignInUrl(GURL(*auth_url)))
      return Failure("unsupported");
    std::move(open_url).Run(GURL(*auth_url));
    for (;;) {
      auto message = channel.Receive();
      if (!message)
        return Failure("stopped");
      const auto* method = message->FindString("method");
      if (!method)
        continue;
      if (*method != "account/login/completed") {
        channel.RefuseRequest(*message);
        continue;
      }
      const auto* params = message->FindDict("params");
      if (!params)
        continue;
      if (const auto* id = params->FindString("loginId"); id && *id != *login_id)
        continue;
      if (!params->FindBool("success").value_or(false)) {
        const auto* error = params->FindString("error");
        return Failure(error ? *error : "failed");
      }
      CodexSignInResult done;
      done.ok = true;
      if (auto read = channel.Call(4, "account/read", base::DictValue())) {
        if (const auto* result = read->FindDict("result"))
          if (const auto* current = result->FindDict("account"))
            done.plan = PlanOf(*current);
      }
      return done;
    }
  }();

  base::AutoLock lock(lock_);
#if BUILDFLAG(IS_WIN)
  if (job_.is_valid())
    ::TerminateJobObject(job_.get(), 0);
#else
  ::kill(-process_.Pid(), SIGKILL);
#endif
  process_.Terminate(0, true);
  if (cancelled_)
    return Failure("cancelled");
  return outcome;
}

}  // namespace zitter::assistant
