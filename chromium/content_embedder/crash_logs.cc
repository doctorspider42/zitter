// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "crash_logs.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/containers/span.h"
#include "base/no_destructor.h"
#include "base/process/process_handle.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include <fcntl.h>
#include <io.h>
#include <windows.h>

#include "base/base_paths.h"
#include "base/path_service.h"
#include "base/win/registry.h"
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace zitter::content_embedder {
namespace {

// Limits of the log directory. Together they keep it under about 400 MB even
// when every run hits the live limit.
constexpr size_t kKeptRuns = 10;                      // newest run logs
constexpr int64_t kLiveLimit = 64 << 20;              // one running log
constexpr int64_t kTrimAbove = 16 << 20;              // an ended log...
constexpr int64_t kTrimTo = 4 << 20;                  // ...keeps its tail
constexpr size_t kKeptCrashes = 20;                   // newest crash reports
constexpr int64_t kCrashBytes = 100 << 20;            // all of Crashes/
constexpr base::TimeDelta kCrashAge = base::Days(30);  // oldest crash report
constexpr int64_t kTailBytes = 64 << 10;              // read to classify a run

constexpr char kRunPrefix[] = "zitter-";
constexpr char kCleanExit[] = "[zitter] clean exit";
// A SIGTERM (a relaunch by the dev tools, `kill`) ends the run on purpose.
constexpr char kTerminated[] = "[zitter] ended by SIGTERM\n";
// What a crashing Chromium process prints: a failed CHECK or DCHECK, a fatal
// log message, or the in-process stack dump of a signal.
constexpr std::string_view kCrashMarks[] = {":FATAL:", "Check failed",
                                            "Received signal"};

#if BUILDFLAG(IS_WIN)
constexpr int kStderr = 2;
#else
constexpr int kStderr = STDERR_FILENO;
#endif

bool g_active = false;

void WriteRaw(std::string_view text) {
#if BUILDFLAG(IS_WIN)
  _write(kStderr, text.data(), static_cast<unsigned>(text.size()));
#else
  std::ignore = write(kStderr, text.data(), text.size());
#endif
}

// True when stderr goes nowhere: a GUI start rather than a terminal or a test
// harness that reads the output.
bool OutputDiscarded() {
#if BUILDFLAG(IS_WIN)
  const HANDLE handle = ::GetStdHandle(STD_ERROR_HANDLE);
  return !handle || handle == INVALID_HANDLE_VALUE || _fileno(stderr) < 0;
#else
  struct stat err = {};
  if (fstat(STDERR_FILENO, &err) != 0)
    return true;
  struct stat null = {};
  return S_ISCHR(err.st_mode) && stat("/dev/null", &null) == 0 &&
         err.st_rdev == null.st_rdev;
#endif
}

bool ProcessRuns(base::ProcessId pid) {
  if (pid == base::GetCurrentProcId())
    return true;
#if BUILDFLAG(IS_WIN)
  const HANDLE process =
      ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process)
    return false;
  DWORD code = 0;
  const bool running =
      ::GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
  ::CloseHandle(process);
  return running;
#else
  return kill(pid, 0) == 0 || errno == EPERM;
#endif
}

std::string Narrow(const base::FilePath& name) {
#if BUILDFLAG(IS_WIN)
  return name.AsUTF8Unsafe();
#else
  return name.value();
#endif
}

// "zitter-20261125-143012-55133.log" and the reports filed with it share the
// key "zitter-20261125-143012-55133". Other files are their own group.
std::string GroupKey(const std::string& name) {
  if (!base::StartsWith(name, kRunPrefix))
    return name;
  const size_t date = sizeof(kRunPrefix) - 1;
  const size_t pid = date + std::string_view("20261125-143012-").size();
  size_t end = pid;
  while (end < name.size() && base::IsAsciiDigit(name[end]))
    ++end;
  return end > pid ? name.substr(0, end) : name;
}

std::optional<base::ProcessId> RunPid(const std::string& name) {
  const std::string key = GroupKey(name);
  const size_t dash = key.rfind('-');
  int pid = 0;
  if (key == name || dash == std::string::npos ||
      !base::StringToInt(key.substr(dash + 1), &pid) || pid <= 0) {
    return std::nullopt;
  }
  return static_cast<base::ProcessId>(pid);
}

std::string ReadTail(const base::FilePath& path, int64_t bytes) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid())
    return {};
  const int64_t length = file.GetLength();
  const int64_t start = std::max<int64_t>(0, length - bytes);
  std::string tail(static_cast<size_t>(length - start), '\0');
  const auto read = file.Read(start, base::as_writable_byte_span(tail));
  tail.resize(read.value_or(0));
  return tail;
}

// Keeps the end of a log that grew past the limit; the end holds the crash.
void TrimToTail(const base::FilePath& path) {
  std::string tail = ReadTail(path, kTrimTo);
  const size_t line = tail.find('\n');
  if (line != std::string::npos)
    tail.erase(0, line + 1);
  base::WriteFile(path, "[zitter] earlier output removed to save space\n" + tail);
}

#if BUILDFLAG(IS_MAC)
// Whether a macOS crash report names the process or its parent.
bool ReportOfRun(const base::FilePath& report, base::ProcessId pid) {
  base::File file(report, base::File::FLAG_OPEN | base::File::FLAG_READ);
  std::string head(8192, '\0');
  head.resize(file.Read(0, base::as_writable_byte_span(head)).value_or(0));
  for (const char* key : {"\"pid\" : ", "\"parentPid\" : ", "\"responsiblePid\" : "}) {
    if (head.find(base::StringPrintf("%s%d,", key, static_cast<int>(pid))) !=
        std::string::npos) {
      return true;
    }
  }
  return false;
}

// macOS writes a report for every crashed process. Keep copies of those that
// belong to the run, since the system deletes them on its own schedule.
void CopySystemReports(const base::FilePath& log,
                       const base::FilePath& crashes,
                       const std::string& key,
                       base::ProcessId pid) {
  base::File::Info info;
  if (!base::GetFileInfo(log, &info))
    return;
  const base::Time from = info.creation_time - base::Seconds(5);
  const base::Time to = info.last_modified + base::Minutes(2);
  base::FileEnumerator reports(
      base::GetHomeDir().Append("Library/Logs/DiagnosticReports"), false,
      base::FileEnumerator::FILES, "Zitter*.ips");
  for (auto path = reports.Next(); !path.empty(); path = reports.Next()) {
    const base::Time made = reports.GetInfo().GetLastModifiedTime();
    if (made >= from && made <= to && ReportOfRun(path, pid))
      base::CopyFile(path, crashes.Append(key + "-" + path.BaseName().value()));
  }
}
#endif

// Moves the logs of ended runs that crashed to Crashes/.
void CollectCrashes(const base::FilePath& dir,
                    const base::FilePath& crashes,
                    const base::FilePath& current) {
  base::FileEnumerator logs(dir, false, base::FileEnumerator::FILES,
                            FILE_PATH_LITERAL("zitter-*.log"));
  for (auto path = logs.Next(); !path.empty(); path = logs.Next()) {
    const std::string name = Narrow(path.BaseName());
    const auto pid = RunPid(name);
    if (path == current || !pid || ProcessRuns(*pid))
      continue;  // Another Zitter still writes it.
    const std::string tail = ReadTail(path, kTailBytes);
    bool crashed = tail.find(kCleanExit) == std::string::npos &&
                   tail.find(kTerminated) == std::string::npos;
    for (const auto mark : kCrashMarks)
      crashed = crashed || tail.find(mark) != std::string::npos;
    if (!crashed)
      continue;
    const base::FilePath target = crashes.Append(path.BaseName());
    if (!base::Move(path, target))
      continue;
    if (base::GetFileSize(target).value_or(0) > kTrimAbove)
      TrimToTail(target);
#if BUILDFLAG(IS_MAC)
    CopySystemReports(target, crashes, GroupKey(name), *pid);
#endif
  }
}

void PruneRunLogs(const base::FilePath& dir, const base::FilePath& current) {
  std::vector<base::FilePath> logs;
  base::FileEnumerator files(dir, false, base::FileEnumerator::FILES,
                             FILE_PATH_LITERAL("zitter-*.log"));
  for (auto path = files.Next(); !path.empty(); path = files.Next())
    logs.push_back(path);
  // Names start with the start time, so the newest sort last.
  std::sort(logs.begin(), logs.end());
  const size_t excess = logs.size() > kKeptRuns ? logs.size() - kKeptRuns : 0;
  for (size_t i = 0; i < logs.size(); ++i) {
    if (logs[i] == current)
      continue;
    if (i < excess) {
      base::DeleteFile(logs[i]);
    } else if (base::GetFileSize(logs[i]).value_or(0) > kTrimAbove) {
      TrimToTail(logs[i]);
    }
  }
}

void PruneCrashes(const base::FilePath& crashes) {
  struct Group {
    base::Time newest;
    int64_t bytes = 0;
    std::vector<base::FilePath> files;
  };
  std::map<std::string, Group> groups;
  base::FileEnumerator files(crashes, false, base::FileEnumerator::FILES);
  for (auto path = files.Next(); !path.empty(); path = files.Next()) {
    Group& group = groups[GroupKey(Narrow(path.BaseName()))];
    group.newest = std::max(group.newest, files.GetInfo().GetLastModifiedTime());
    group.bytes += files.GetInfo().GetSize();
    group.files.push_back(path);
  }
  std::vector<Group*> order;
  for (auto& [key, group] : groups)
    order.push_back(&group);
  std::sort(order.begin(), order.end(),
            [](const Group* a, const Group* b) { return a->newest > b->newest; });
  const base::Time oldest = base::Time::Now() - kCrashAge;
  int64_t total = 0;
  for (size_t i = 0; i < order.size(); ++i) {
    total += order[i]->bytes;
    if (i < kKeptCrashes && order[i]->newest >= oldest &&
        (i == 0 || total <= kCrashBytes)) {
      continue;
    }
    for (const auto& path : order[i]->files)
      base::DeleteFile(path);
  }
}

#if BUILDFLAG(IS_WIN)
// Windows Error Reporting writes minidumps of crashed zitter.exe processes
// to Crashes/ and keeps at most five of them there itself.
void RegisterLocalDumps(const base::FilePath& crashes) {
  base::FilePath exe;
  if (!base::PathService::Get(base::FILE_EXE, &exe))
    return;
  const std::wstring key =
      L"Software\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\" +
      exe.BaseName().value();
  base::win::RegKey dumps(HKEY_CURRENT_USER, key.c_str(),
                          KEY_QUERY_VALUE | KEY_SET_VALUE);
  if (!dumps.Valid())
    return;
  std::wstring folder;
  if (dumps.ReadValue(L"DumpFolder", &folder) != ERROR_SUCCESS ||
      folder != crashes.value()) {
    dumps.WriteValue(L"DumpFolder", crashes.value().c_str());
  }
  dumps.WriteValue(L"DumpCount", 5);
  dumps.WriteValue(L"DumpType", 1);  // A minidump, a few MB each.
}
#endif

// Restarts a running log that passed the live limit, so a run that floods
// its output cannot fill the disk.
class LiveLimit final : public base::PlatformThread::Delegate {
 public:
  void ThreadMain() override {
    base::PlatformThread::SetName("ZitterRunLog");
    for (;;) {
      base::PlatformThread::Sleep(base::Minutes(1));
#if BUILDFLAG(IS_WIN)
      const int64_t size = _filelengthi64(kStderr);
#else
      struct stat log = {};
      const int64_t size = fstat(kStderr, &log) == 0 ? log.st_size : 0;
#endif
      if (size <= kLiveLimit)
        continue;
#if BUILDFLAG(IS_WIN)
      _chsize_s(kStderr, 0);
#else
      std::ignore = ftruncate(kStderr, 0);
#endif
      WriteRaw("[zitter] the log passed 64 MB; earlier output removed\n");
    }
  }
};

void EndAtExit() {
  EndRunLog(0);
}

#if BUILDFLAG(IS_POSIX)
void OnTerminate(int signal) {
  // Async-signal-safe: write, signal and raise only.
  std::ignore = write(kStderr, kTerminated, sizeof(kTerminated) - 1);
  ::signal(signal, SIG_DFL);
  raise(signal);
}
#endif

}  // namespace

base::FilePath RunLogDirectory() {
#if BUILDFLAG(IS_WIN)
  base::FilePath local;
  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local))
    return {};
  return local.Append(L"Zitter").Append(L"Logs");
#else
  return base::GetHomeDir().Append("Library/Logs/Zitter");
#endif
}

void StartRunLog() {
  if (g_active || !OutputDiscarded())
    return;
  const base::FilePath dir = RunLogDirectory();
  const base::FilePath crashes = dir.Append(FILE_PATH_LITERAL("Crashes"));
  if (dir.empty() || !base::CreateDirectory(crashes))
    return;

  base::Time::Exploded now;
  base::Time::Now().LocalExplode(&now);
  const std::string name = base::StringPrintf(
      "%s%04d%02d%02d-%02d%02d%02d-%d.log", kRunPrefix, now.year, now.month,
      now.day_of_month, now.hour, now.minute, now.second,
      static_cast<int>(base::GetCurrentProcId()));
  const base::FilePath path = dir.AppendASCII(name);

#if BUILDFLAG(IS_WIN)
  const HANDLE handle = ::CreateFileW(
      path.value().c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return;
  const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle),
                                 _O_APPEND | _O_WRONLY | _O_BINARY);
  if (fd < 0) {
    ::CloseHandle(handle);
    return;
  }
  _dup2(fd, 1);
  _dup2(fd, kStderr);
  ::SetStdHandle(STD_OUTPUT_HANDLE, handle);
  ::SetStdHandle(STD_ERROR_HANDLE, handle);
#else
  const int fd = open(path.value().c_str(),
                      O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0600);
  if (fd < 0)
    return;
  // Child processes inherit both descriptors, so their output lands here too.
  dup2(fd, STDOUT_FILENO);
  dup2(fd, STDERR_FILENO);
  close(fd);
#endif
  g_active = true;
  WriteRaw(base::StringPrintf("[zitter] run started %04d-%02d-%02d %02d:%02d:%02d\n",
                              now.year, now.month, now.day_of_month, now.hour,
                              now.minute, now.second));

  CollectCrashes(dir, crashes, path);
  PruneRunLogs(dir, path);
  PruneCrashes(crashes);
#if BUILDFLAG(IS_WIN)
  RegisterLocalDumps(crashes);
#endif
  static base::NoDestructor<LiveLimit> live_limit;
  base::PlatformThread::CreateNonJoinable(0, live_limit.get());
  // A quit that ends in exit() never returns to EndRunLog's caller.
  std::atexit(EndAtExit);

}

void WatchRunTermination() {
#if BUILDFLAG(IS_POSIX)
  // ContentMain resets SIGTERM to its default, which ends the process with no
  // mark, so this goes in after it.
  if (g_active)
    ::signal(SIGTERM, OnTerminate);
#endif
}

void EndRunLog(int exit_code) {
  if (!g_active)
    return;
  g_active = false;
  WriteRaw(base::StringPrintf("%s %d\n", kCleanExit, exit_code));
}

}  // namespace zitter::content_embedder
