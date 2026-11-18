"""Run a command on a separate Windows desktop without switching to it."""

import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import runpy
import subprocess
import sys
from uuid import uuid4


DESKTOP_PREFIX = "ZitterTests-"


class StartupInfo(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD), ("lpReserved", wintypes.LPWSTR),
        ("lpDesktop", wintypes.LPWSTR), ("lpTitle", wintypes.LPWSTR),
        ("dwX", wintypes.DWORD), ("dwY", wintypes.DWORD),
        ("dwXSize", wintypes.DWORD), ("dwYSize", wintypes.DWORD),
        ("dwXCountChars", wintypes.DWORD), ("dwYCountChars", wintypes.DWORD),
        ("dwFillAttribute", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
        ("wShowWindow", wintypes.WORD), ("cbReserved2", wintypes.WORD),
        ("lpReserved2", ctypes.c_void_p), ("hStdInput", wintypes.HANDLE),
        ("hStdOutput", wintypes.HANDLE), ("hStdError", wintypes.HANDLE),
    ]


class ProcessInformation(ctypes.Structure):
    _fields_ = [
        ("hProcess", wintypes.HANDLE), ("hThread", wintypes.HANDLE),
        ("dwProcessId", wintypes.DWORD), ("dwThreadId", wintypes.DWORD),
    ]


def current_desktop_name():
    user = ctypes.WinDLL("user32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentThreadId.restype = wintypes.DWORD
    user.GetThreadDesktop.argtypes = (wintypes.DWORD,)
    user.GetThreadDesktop.restype = wintypes.HANDLE
    user.GetUserObjectInformationW.argtypes = (
        wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p,
    )
    desktop = user.GetThreadDesktop(kernel.GetCurrentThreadId())
    assigned_name = ctypes.create_unicode_buffer(256)
    user_object_name = 2
    if not user.GetUserObjectInformationW(desktop, user_object_name, assigned_name,
                                          ctypes.sizeof(assigned_name), None):
        raise ctypes.WinError(ctypes.get_last_error())
    return assigned_name.value


def parse_probe_args(parser, script, *, visible_for=()):
    if sys.platform != "win32":
        return parser.parse_args()

    parser.add_argument("--visible", action="store_true",
                        help="Show test windows on the current desktop for debugging")
    args = parser.parse_args()
    if args.visible:
        return args
    for option in visible_for:
        if getattr(args, option):
            parser.error(f"--{option.replace('_', '-')} requires --visible on Windows")
    try:
        if not current_desktop_name().startswith(DESKTOP_PREFIX):
            raise SystemExit(run_on_test_desktop(Path(script), sys.argv[1:]))
    except OSError as error:
        raise SystemExit(f"Could not start the Windows test desktop: {error}") from error
    return args


def run_worker(desktop_name, script, arguments):
    if current_desktop_name() != desktop_name:
        raise SystemExit("Windows did not assign the test desktop to the worker")

    sys.argv = [script, *arguments]
    runpy.run_path(script, run_name="__main__")


def run_on_test_desktop(script, arguments):
    user = ctypes.WinDLL("user32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    user.CreateDesktopW.argtypes = (
        wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD,
        wintypes.DWORD, ctypes.c_void_p,
    )
    user.CreateDesktopW.restype = wintypes.HANDLE
    user.CloseDesktop.argtypes = (wintypes.HANDLE,)
    kernel.GetStartupInfoW.argtypes = (ctypes.POINTER(StartupInfo),)
    kernel.CreateProcessW.argtypes = (
        wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p,
        wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR,
        ctypes.POINTER(StartupInfo), ctypes.POINTER(ProcessInformation),
    )
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))

    name = f"{DESKTOP_PREFIX}{uuid4().hex}"
    desktop_create_window = 0x0002
    desktop = user.CreateDesktopW(name, None, None, 0, desktop_create_window, None)
    if not desktop:
        raise ctypes.WinError(ctypes.get_last_error())

    try:
        # subprocess.STARTUPINFO cannot specify lpDesktop. Start one Python process
        # on this desktop and let its test subprocesses inherit the assignment.
        script = script.resolve()
        worker = (
            "import sys; sys.path.insert(0, sys.argv.pop(1)); "
            "from windows_test_desktop import run_worker; "
            "run_worker(sys.argv[1], sys.argv[2], sys.argv[3:])"
        )
        argv = [sys.executable, "-c", worker, str(Path(__file__).resolve().parent),
                name, str(script), *arguments]
        command = ctypes.create_unicode_buffer(subprocess.list2cmdline(argv))
        startup = StartupInfo()
        kernel.GetStartupInfoW(ctypes.byref(startup))
        startup.cb = ctypes.sizeof(startup)
        startup.lpDesktop = name
        process = ProcessInformation()
        if not kernel.CreateProcessW(sys.executable, command, None, None, True, 0,
                                     None, None, ctypes.byref(startup), ctypes.byref(process)):
            raise ctypes.WinError(ctypes.get_last_error())
        kernel.CloseHandle(process.hThread)
        try:
            wait_timeout = 258
            wait_result = kernel.WaitForSingleObject(process.hProcess, 250)
            while wait_result == wait_timeout:
                wait_result = kernel.WaitForSingleObject(process.hProcess, 250)
            if wait_result != 0:
                raise ctypes.WinError(ctypes.get_last_error())
            exit_code = wintypes.DWORD()
            if not kernel.GetExitCodeProcess(process.hProcess, ctypes.byref(exit_code)):
                raise ctypes.WinError(ctypes.get_last_error())
            return exit_code.value
        finally:
            kernel.CloseHandle(process.hProcess)
    finally:
        user.CloseDesktop(desktop)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="Executable and arguments, separated from launcher options by --")
    args = parse_probe_args(parser, __file__)
    command = args.command
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        parser.error("provide an executable after --")
    if sys.platform != "win32":
        parser.error("the desktop launcher requires Windows")
    try:
        return subprocess.call(command, stdin=sys.stdin, stdout=sys.stdout, stderr=sys.stderr,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    except OSError as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    raise SystemExit(main())
