from __future__ import annotations

import ctypes
import os
from ctypes import (
    POINTER,
    byref,
    c_int,
    c_uint32,
    c_void_p,
    c_wchar,
    c_wchar_p,
    cast,
    create_string_buffer,
    create_unicode_buffer,
    sizeof,
    windll,
)
from typing import Any, Dict, List, Optional

kernel32 = windll.kernel32
psapi = windll.psapi
user32 = windll.user32
version = windll.version

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_ENUM_RIGHTS = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
INVALID_HANDLE_VALUE = c_void_p(-1).value


def _setup_fn(dll: Any, name: str, args: list[Any], restype: Any) -> None:
    fn = getattr(dll, name)
    fn.argtypes = args
    fn.restype = restype


_setup_fn(kernel32, "CreateToolhelp32Snapshot", [c_uint32, c_uint32], c_void_p)
_setup_fn(kernel32, "Process32FirstW", [c_void_p, c_void_p], c_int)
_setup_fn(kernel32, "Process32NextW", [c_void_p, c_void_p], c_int)
_setup_fn(kernel32, "CloseHandle", [c_void_p], c_int)
_setup_fn(kernel32, "OpenProcess", [c_uint32, c_int, c_uint32], c_void_p)
_setup_fn(
    kernel32,
    "QueryFullProcessImageNameW",
    [c_void_p, c_uint32, c_wchar_p, POINTER(c_uint32)],
    c_int,
)
_setup_fn(
    psapi,
    "EnumProcessModules",
    [c_void_p, POINTER(c_void_p), c_uint32, POINTER(c_uint32)],
    c_int,
)
_setup_fn(
    psapi,
    "GetModuleFileNameExW",
    [c_void_p, c_void_p, c_wchar_p, c_uint32],
    c_uint32,
)
_setup_fn(user32, "EnumWindows", [c_void_p, c_void_p], c_int)
_setup_fn(user32, "GetClassNameW", [c_void_p, c_wchar_p, c_int], c_int)
_setup_fn(user32, "GetWindowTextW", [c_void_p, c_wchar_p, c_int], c_int)
_setup_fn(
    user32,
    "GetWindowThreadProcessId",
    [c_void_p, POINTER(c_uint32)],
    c_uint32,
)
_setup_fn(user32, "IsWindowVisible", [c_void_p], c_int)
_setup_fn(
    version,
    "GetFileVersionInfoSizeW",
    [c_wchar_p, POINTER(c_uint32)],
    c_uint32,
)
_setup_fn(
    version,
    "GetFileVersionInfoW",
    [c_wchar_p, c_uint32, c_uint32, c_void_p],
    c_int,
)
_setup_fn(
    version,
    "VerQueryValueW",
    [c_void_p, c_wchar_p, POINTER(c_void_p), POINTER(c_uint32)],
    c_int,
)


class VS_FIXEDFILEINFO(ctypes.Structure):
    _fields_ = [
        ("dwSignature", c_uint32),
        ("dwStrucVersion", c_uint32),
        ("dwFileVersionMS", c_uint32),
        ("dwFileVersionLS", c_uint32),
        ("dwProductVersionMS", c_uint32),
        ("dwProductVersionLS", c_uint32),
        ("dwFileFlagsMask", c_uint32),
        ("dwFileFlags", c_uint32),
        ("dwFileOS", c_uint32),
        ("dwFileType", c_uint32),
        ("dwFileSubtype", c_uint32),
        ("dwFileDateMS", c_uint32),
        ("dwFileDateLS", c_uint32),
    ]


def get_file_version(path: str) -> str:
    ignored = c_uint32(0)
    size = version.GetFileVersionInfoSizeW(path, byref(ignored))
    if not size:
        return ""

    data = create_string_buffer(size)
    if not version.GetFileVersionInfoW(path, 0, size, data):
        return ""

    value = c_void_p()
    value_size = c_uint32(0)
    if not version.VerQueryValueW(data, "\\", byref(value), byref(value_size)):
        return ""
    if value_size.value < sizeof(VS_FIXEDFILEINFO):
        return ""

    info = cast(value, POINTER(VS_FIXEDFILEINFO)).contents
    return ".".join(
        str(part)
        for part in (
            info.dwFileVersionMS >> 16,
            info.dwFileVersionMS & 0xFFFF,
            info.dwFileVersionLS >> 16,
            info.dwFileVersionLS & 0xFFFF,
        )
    )


def find_wechat_processes() -> List[Dict[str, Any]]:
    """Return running WeChat processes and their Weixin.dll metadata."""
    results: List[Dict[str, Any]] = []
    snapshot = kernel32.CreateToolhelp32Snapshot(0x2, 0)
    if not snapshot or snapshot == INVALID_HANDLE_VALUE:
        return results

    class ProcessEntry(ctypes.Structure):
        _fields_ = [
            ("size", c_uint32),
            ("usage", c_uint32),
            ("pid", c_uint32),
            ("default_heap", c_void_p),
            ("module_id", c_uint32),
            ("thread_count", c_uint32),
            ("parent_pid", c_uint32),
            ("base_priority", c_int),
            ("flags", c_uint32),
            ("exe", c_wchar * 260),
        ]

    entry = ProcessEntry()
    entry.size = sizeof(ProcessEntry)
    try:
        if not kernel32.Process32FirstW(snapshot, byref(entry)):
            return results

        while True:
            if entry.exe.lower() in ("wechat.exe", "weixin.exe"):
                info: Dict[str, Any] = {"pid": entry.pid, "name": entry.exe}
                process = kernel32.OpenProcess(PROCESS_ENUM_RIGHTS, False, entry.pid)
                if process:
                    try:
                        exe_path = create_unicode_buffer(1024)
                        exe_length = c_uint32(len(exe_path))
                        if kernel32.QueryFullProcessImageNameW(
                            process, 0, exe_path, byref(exe_length)
                        ):
                            info["exe_path"] = exe_path.value

                        modules = (c_void_p * 2048)()
                        needed = c_uint32(0)
                        if psapi.EnumProcessModules(
                            process, modules, sizeof(modules), byref(needed)
                        ):
                            module_count = min(
                                needed.value // sizeof(c_void_p), len(modules)
                            )
                            module_path = create_unicode_buffer(1024)
                            for index in range(module_count):
                                if not psapi.GetModuleFileNameExW(
                                    process,
                                    modules[index],
                                    module_path,
                                    len(module_path),
                                ):
                                    continue
                                if os.path.basename(module_path.value).lower() != "weixin.dll":
                                    continue
                                info["weixin_base"] = int(modules[index] or 0)
                                info["weixin_path"] = module_path.value
                                info["weixin_version"] = get_file_version(
                                    module_path.value
                                )
                                break
                    finally:
                        kernel32.CloseHandle(process)
                results.append(info)

            if not kernel32.Process32NextW(snapshot, byref(entry)):
                break
    finally:
        kernel32.CloseHandle(snapshot)
    return results


def find_wechat_windows(pid: Optional[int] = None) -> List[Dict[str, Any]]:
    """Return WeChat windows with the owning UI thread id."""
    windows: List[Dict[str, Any]] = []
    enum_proc_type = ctypes.WINFUNCTYPE(c_int, c_void_p, c_void_p)

    def on_window(hwnd: int, lparam: int) -> int:
        del lparam
        owner_pid = c_uint32(0)
        thread_id = user32.GetWindowThreadProcessId(hwnd, byref(owner_pid))
        if pid is not None and owner_pid.value != pid:
            return 1

        class_name = create_unicode_buffer(256)
        title = create_unicode_buffer(512)
        user32.GetClassNameW(hwnd, class_name, len(class_name))
        user32.GetWindowTextW(hwnd, title, len(title))
        visible = bool(user32.IsWindowVisible(hwnd))
        if class_name.value == "Qt51514QWindowIcon" or (visible and owner_pid.value):
            windows.append(
                {
                    "hwnd": hwnd,
                    "pid": owner_pid.value,
                    "tid": thread_id,
                    "class": class_name.value,
                    "title": title.value,
                    "visible": visible,
                }
            )
        return 1

    callback = enum_proc_type(on_window)
    user32.EnumWindows(callback, 0)
    windows.sort(
        key=lambda item: (
            item["class"] != "Qt51514QWindowIcon",
            not item["visible"],
        )
    )
    return windows


def choose_wechat_target() -> Optional[Dict[str, Any]]:
    processes = find_wechat_processes()
    if not processes:
        return None

    target_pid = int(os.environ.get("WX_TARGET_PID") or "0")
    main: Optional[Dict[str, Any]] = None
    windows: List[Dict[str, Any]] = []
    if target_pid:
        main = next(
            (
                process
                for process in processes
                if int(process.get("pid") or 0) == target_pid
            ),
            None,
        )
        if main:
            windows = find_wechat_windows(main["pid"])

    if not main:
        process_by_pid = {
            int(process.get("pid") or 0): process for process in processes
        }
        windows = [
            window
            for window in find_wechat_windows()
            if window.get("visible")
            and int(window.get("pid") or 0) in process_by_pid
        ]
        if windows:
            main = process_by_pid[int(windows[0]["pid"])]
            windows = [windows[0]]
        else:
            main = next(
                (
                    process
                    for process in processes
                    if process.get("weixin_base", 0)
                ),
                processes[0],
            )

    target = dict(main)
    if windows:
        target.update(windows[0])
    return target
