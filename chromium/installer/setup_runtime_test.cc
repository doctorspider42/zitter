// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Standalone Win32 integration tests; fixtures are copies of this executable,
// never an installed browser or the user's profile.
#include "chromium/installer/setup_runtime.h"

#include <shellapi.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
namespace fs = std::filesystem;
bool ignore_close = false;
const fs::path* closed_marker = nullptr;

LRESULT CALLBACK FixtureWindow(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_CLOSE) {
    if (!ignore_close) {
      std::ofstream(*closed_marker) << "session saved";
      DestroyWindow(window);
      PostQuitMessage(0);
    }
    return 0;
  }
  return DefWindowProcW(window, message, w, l);
}

int Fixture(const fs::path& marker, bool ignore) {
  const fs::path saved = marker.wstring() + L".closed";
  closed_marker = &saved;
  ignore_close = ignore;
  WNDCLASSW type{};
  type.lpfnWndProc = FixtureWindow;
  type.hInstance = GetModuleHandleW(nullptr);
  type.lpszClassName = L"ZitterSetupTest";
  RegisterClassW(&type);
  HWND window = CreateWindowW(type.lpszClassName, L"Zitter setup test fixture", WS_OVERLAPPEDWINDOW,
                              0, 0, 200, 100, nullptr, nullptr, type.hInstance, nullptr);
  if (!window) return 3;
  // A real file proves that Explorer started the executable, not just that COM
  // accepted the request. Record the working directory as well.
  std::ofstream(marker) << fs::current_path().string();
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return 0;
}

bool WaitForFile(const fs::path& path) {
  const auto deadline = GetTickCount64() + 10000;
  while (!fs::exists(path) && GetTickCount64() < deadline) Sleep(50);
  return fs::exists(path);
}

struct FixtureProcess {
  HANDLE process = nullptr;
  ~FixtureProcess() {
    // Test fixtures only. Production close never terminates the browser.
    if (process) {
      if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
        TerminateProcess(process, 99);
        WaitForSingleObject(process, 5000);
      }
      CloseHandle(process);
    }
  }
  bool Start(const fs::path& executable, const fs::path& marker, bool ignore) {
    std::wstring command = L"\"" + executable.wstring() + L"\" --fixture \"" + marker.wstring() +
                            L"\" " + (ignore ? L"ignore" : L"close") + L" end";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                         0, nullptr, executable.parent_path().c_str(), &startup, &info)) return false;
    CloseHandle(info.hThread);
    process = info.hProcess;
    return WaitForFile(marker);
  }
};

bool Check(bool passed, const char* name) {
  std::cout << (passed ? "PASS " : "FAIL ") << name << std::endl;
  return passed;
}

bool Tests(const fs::path& self, const fs::path& root) {
  using namespace zitter::setup;
  const fs::path target = root / L"target";
  const fs::path other = root / L"other";
  fs::create_directories(target);
  fs::create_directories(other);
  const fs::path installed = root / L"locked-font.ttf";
  const fs::path pending = root / L"pending-font.ttf";
  std::ofstream(installed) << "original font";
  std::ofstream(pending) << "original font";
  HANDLE font = CreateFileW(installed.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (font == INVALID_HANDLE_VALUE) return false;
  const DWORD unchanged = CommitInstalledFile(pending.wstring(), installed.wstring());
  std::ofstream(pending) << "modified font";
  const DWORD changed = CommitInstalledFile(pending.wstring(), installed.wstring());
  CloseHandle(font);
  if (!Check(unchanged == ERROR_SUCCESS, "unchanged locked font can remain installed") ||
      !Check(changed != ERROR_SUCCESS, "different locked file is never silently skipped") ||
      !Check(CommitInstalledFile(pending.wstring(), installed.wstring()) == ERROR_SUCCESS,
             "complete replacement commits after lock is released")) return false;
  fs::copy_file(self, target / L"zitter.exe");
  fs::copy_file(self, other / L"zitter.exe");
  // Component builds use a shared C++ runtime; official builds are static.
  const fs::path runtime = self.parent_path() / L"libc++.dll";
  if (fs::exists(runtime)) {
    fs::copy_file(runtime, target / runtime.filename());
    fs::copy_file(runtime, other / runtime.filename());
  }
  if (!Check(CloseBrowser(target.wstring(), 100), "absent browser is already closed")) return false;
  {
    FixtureProcess browser, unrelated;
    if (!browser.Start(target / L"zitter.exe", root / L"target.ready", false) ||
        !unrelated.Start(other / L"zitter.exe", root / L"other.ready", false)) return false;
    if (!Check(IsRunning(target.wstring()), "detect running selected installation") ||
        !Check(CloseBrowser(target.wstring(), 5000), "gracefully close running installation") ||
        !Check(WaitForSingleObject(browser.process, 0) == WAIT_OBJECT_0, "wait until browser process exits") ||
        !Check(fs::exists(root / L"target.ready.closed"), "session saved through WM_CLOSE") ||
        !Check(WaitForSingleObject(unrelated.process, 0) == WAIT_TIMEOUT, "other installation stays open")) return false;
    if (!CloseBrowser(other.wstring(), 5000)) return false;
  }
  {
    FixtureProcess browser;
    if (!browser.Start(target / L"zitter.exe", root / L"blocked.ready", true)) return false;
    if (!Check(!CloseBrowser(target.wstring(), 200), "cancelled or hung close blocks installation") ||
        !Check(WaitForSingleObject(browser.process, 0) == WAIT_TIMEOUT, "blocked browser is never force-killed")) return false;
  }
  const fs::path marker = root / L"explorer.ready";
  const std::wstring arguments = L"--fixture \"" + marker.wstring() + L"\" close end";
  const HRESULT launch = LaunchFromExplorer((target / L"zitter.exe").wstring(), target.wstring(), arguments);
  std::cout << "Explorer launch HRESULT: 0x" << std::hex << static_cast<unsigned long>(launch) << std::dec << std::endl;
  if (!Check(SUCCEEDED(launch),
             "Explorer desktop COM launch succeeds")) return false;
  const bool started = WaitForFile(marker);
  bool directory_matches = false;
  if (started) {
    std::string actual;
    std::getline(std::ifstream(marker), actual);
    directory_matches = fs::path(actual) == target;
  }
  const bool closed = CloseBrowser(target.wstring(), 5000);
  return Check(started, "Explorer actually starts executable with quoted arguments") &&
         Check(directory_matches, "Explorer preserves installation working directory") &&
         Check(closed, "Explorer-launched process closes normally");
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  // The CRT supplies argc entries; fixture arguments are read only at argc=5.
#pragma clang unsafe_buffer_usage begin
  if (argc == 5 && std::wstring(argv[1]) == L"--fixture")
    return Fixture(fs::path(argv[2]), std::wstring(argv[3]) == L"ignore");
#pragma clang unsafe_buffer_usage end
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const fs::path root = fs::path(temp) / (L"zitter-setup-runtime-" + std::to_wstring(GetCurrentProcessId()));
  const bool passed = Tests(fs::absolute(argv[0]), root);
  std::error_code error;
  fs::remove_all(root, error);
  CoUninitialize();
  return passed ? 0 : 1;
}
