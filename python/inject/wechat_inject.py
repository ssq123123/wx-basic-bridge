from __future__ import annotations

import os
import shutil
import subprocess
import sys
import time
import uuid
from ctypes import (
    POINTER,
    Structure,
    byref,
    c_char_p,
    c_int,
    c_size_t,
    c_uint32,
    c_void_p,
    c_wchar,
    c_wchar_p,
    cast,
    create_unicode_buffer,
    sizeof,
    windll,
)
from pathlib import Path
from typing import Optional

# python/inject/wechat_inject.py -> repository root
PROJECT_ROOT = Path(__file__).resolve().parents[2]


def _project_path_from_env(name: str, default: str) -> Path:
    path = Path(os.environ.get(name, default))
    return path if path.is_absolute() else PROJECT_ROOT / path


DLL_C_SOURCE = _project_path_from_env(
    "WX_HOOK_DLL_SOURCE",
    r"native\wx_hook_bridge\src\wx_hook_bridge.c",
)
DLL_FILE = _project_path_from_env("WX_HOOK_DLL_FILE", r"dist\wx_hook_bridge.dll")
CONTROL_DIR = PROJECT_ROOT / "bridge" / "control"
INJECT_DLL_DIR = CONTROL_DIR / "inject_dlls"
STOP_FILE = CONTROL_DIR / "stop.txt"
HEARTBEAT_FILE = CONTROL_DIR / "heartbeat.txt"
WM_NULL = 0x0000
WH_GETMESSAGE = 3
WH_CALLWNDPROC = 4
SMTO_ABORTIFHUNG = 0x0002
WAIT_OBJECT_0 = 0x00000000
WAIT_TIMEOUT = 0x00000102
WAIT_FAILED = 0xFFFFFFFF
MAX_PATH = 260
WXB_STARTUP_NONCE_CHARS = 32
WXB_PATH_MAX_SUFFIX_CHARS = 69
WXB_STARTUP_CONFIG_MAGIC = 0x42584257
WXB_STARTUP_CONFIG_FREE_AFTER_READ = 0x00000001


class WxbStartupConfig(Structure):
    _fields_ = [
        ("magic", c_uint32),
        ("size", c_uint32),
        ("flags", c_uint32),
        ("reserved", c_uint32),
        ("work_root", c_wchar * MAX_PATH),
        ("startup_nonce", c_wchar * (WXB_STARTUP_NONCE_CHARS + 1)),
    ]

kernel32 = windll.kernel32
psapi = windll.psapi
user32 = windll.user32

PROCESS_CREATE_THREAD = 0x0002
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
PROCESS_RIGHTS = (
    PROCESS_CREATE_THREAD
    | PROCESS_QUERY_INFORMATION
    | PROCESS_VM_OPERATION
    | PROCESS_VM_READ
    | PROCESS_VM_WRITE
)


def _setup_fn(dll, name, args, restype):
    fn = getattr(dll, name)
    fn.argtypes = args
    fn.restype = restype


_setup_fn(user32, "PostMessageW", [c_void_p, c_uint32, c_void_p, c_void_p], c_int)
_setup_fn(user32, "PostThreadMessageW", [c_uint32, c_uint32, c_void_p, c_void_p], c_int)
_setup_fn(user32, "SendMessageTimeoutW", [c_void_p, c_uint32, c_void_p, c_void_p, c_uint32, c_uint32, POINTER(c_size_t)], c_void_p)
_setup_fn(user32, "SetWindowsHookExW", [c_int, c_void_p, c_void_p, c_uint32], c_void_p)
_setup_fn(user32, "UnhookWindowsHookEx", [c_void_p], c_int)
_setup_fn(kernel32, "GetModuleHandleW", [c_wchar_p], c_void_p)
_setup_fn(kernel32, "LoadLibraryW", [c_wchar_p], c_void_p)
_setup_fn(kernel32, "GetProcAddress", [c_void_p, c_char_p], c_void_p)
_setup_fn(kernel32, "CreateThread", [c_void_p, c_void_p, c_void_p, c_void_p, c_uint32, c_void_p], c_void_p)
_setup_fn(kernel32, "OpenProcess", [c_uint32, c_int, c_uint32], c_void_p)
_setup_fn(kernel32, "CreateRemoteThread", [c_void_p, c_void_p, c_size_t, c_void_p, c_void_p, c_uint32, POINTER(c_uint32)], c_void_p)
_setup_fn(kernel32, "CloseHandle", [c_void_p], c_int)
_setup_fn(kernel32, "VirtualAllocEx", [c_void_p, c_void_p, c_size_t, c_uint32, c_uint32], c_void_p)
_setup_fn(kernel32, "VirtualFreeEx", [c_void_p, c_void_p, c_size_t, c_uint32], c_int)
_setup_fn(kernel32, "WriteProcessMemory", [c_void_p, c_void_p, c_void_p, c_size_t, POINTER(c_size_t)], c_int)
_setup_fn(kernel32, "WaitForSingleObject", [c_void_p, c_uint32], c_uint32)
_setup_fn(kernel32, "GetExitCodeThread", [c_void_p, POINTER(c_uint32)], c_int)
_setup_fn(psapi, "EnumProcessModules", [c_void_p, POINTER(c_void_p), c_uint32, POINTER(c_uint32)], c_int)
_setup_fn(psapi, "GetModuleFileNameExW", [c_void_p, c_void_p, c_wchar_p, c_uint32], c_uint32)


def _find_vcvars64() -> Optional[str]:
    candidates = [
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    roots = [
        Path(r"C:\Program Files\Microsoft Visual Studio"),
        Path(r"C:\Program Files (x86)\Microsoft Visual Studio"),
    ]
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("vcvars64.bat"):
            return str(path)
    return None


def build_dll(force: bool = False) -> int:
    dll_path = DLL_FILE
    source_mtime = _native_source_mtime(DLL_C_SOURCE)
    if not force and dll_path.exists() and source_mtime <= dll_path.stat().st_mtime:
        sys.stderr.write(f"使用已有 DLL: {dll_path}\n")
        return 0

    build_script = _native_build_script(DLL_C_SOURCE)
    if build_script:
        dll_path.parent.mkdir(parents=True, exist_ok=True)
        sys.stderr.write(f"编译 {DLL_C_SOURCE} via {build_script} ...\n")
        result = subprocess.run(
            [
                "powershell",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(build_script),
                "-OutFile",
                str(dll_path),
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
        if result.returncode != 0:
            sys.stderr.write(f"编译失败:\n{result.stdout}\n{result.stderr}\n")
            if "LNK1104" in result.stdout or "LNK1104" in result.stderr:
                sys.stderr.write(
                    "提示: wx_hook_bridge.dll 可能仍被微信/旧 inject 进程加载。"
                    "请先停止 inject，必要时重启微信后再 build。\n"
                )
            return 1
        sys.stderr.write(result.stdout)
        sys.stderr.write(f"编译成功: {dll_path}\n")
        return 0

    vcvars = _find_vcvars64()
    if not vcvars:
        sys.stderr.write("未找到 vcvars64.bat，请安装 VS 2022 C++ Build Tools\n")
        return 1

    dll_path.parent.mkdir(parents=True, exist_ok=True)
    obj_dir = _native_build_dir(DLL_C_SOURCE)
    obj_dir.mkdir(parents=True, exist_ok=True)
    obj_path = obj_dir / f"{DLL_C_SOURCE.stem}.obj"
    sys.stderr.write(f"编译 {DLL_C_SOURCE} ...\n")
    cmd = (
        f'cmd /c "call \"{vcvars}\" >nul 2>&1 && cd /d \"{DLL_C_SOURCE.parent}\" '
        f'&& cl /LD /O2 /GS- /nologo /Fo:\"{obj_path}\" \"{DLL_C_SOURCE.name}\" '
        f'/Fe:\"{dll_path}\" /link kernel32.lib user32.lib"'
    )
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        sys.stderr.write(f"编译失败:\n{result.stdout}\n{result.stderr}\n")
        if "LNK1104" in result.stdout or "LNK1104" in result.stderr:
            sys.stderr.write(
                "提示: wx_hook_bridge.dll 可能仍被微信/旧 inject 进程加载。"
                "请先停止 inject，必要时重启微信后再 build。\n"
            )
        return 1
    sys.stderr.write(f"编译成功: {dll_path}\n")
    return 0


def _native_source_mtime(source: Path) -> float:
    paths = [source]
    if source.parent.name == "src":
        native_root = source.parent.parent
        for rel in ("layers", "modules"):
            directory = source.parent / rel
            if directory.exists():
                paths.extend(directory.glob("*.c"))
        include_dir = source.parent / "include"
        if include_dir.exists():
            paths.extend(include_dir.glob("*.h"))
        build_script = native_root / "build.ps1"
        if build_script.exists():
            paths.append(build_script)
    else:
        layers_dir = source.parent / "layers"
        if layers_dir.exists():
            paths.extend(layers_dir.glob("*.c"))
    return max(path.stat().st_mtime for path in paths)


def _native_build_script(source: Path) -> Path | None:
    if source.parent.name != "src":
        return None
    build_script = source.parent.parent / "build.ps1"
    return build_script if build_script.exists() else None


def _native_build_dir(source: Path) -> Path:
    if source.parent.name == "src":
        return source.parent.parent / "build"
    return source.parent


def _prepare_inject_dll() -> Path:
    """Use a cache-busted DLL copy so Weixin won't reuse a stale loaded module."""
    INJECT_DLL_DIR.mkdir(parents=True, exist_ok=True)
    stamp = int(time.time() * 1000)
    target = INJECT_DLL_DIR / f"{DLL_FILE.stem}.{stamp}{DLL_FILE.suffix}"
    shutil.copy2(DLL_FILE, target)
    return target


def pulse_thread(tid: int, hwnd: int = 0, count: int = 5) -> None:
    for _ in range(count):
        if hwnd:
            result = c_size_t(0)
            user32.SendMessageTimeoutW(c_void_p(hwnd), c_uint32(WM_NULL), None, None, SMTO_ABORTIFHUNG, 100, byref(result))
            user32.PostMessageW(hwnd, WM_NULL, None, None)
        user32.PostThreadMessageW(tid, WM_NULL, None, None)
        time.sleep(0.1)


def _find_remote_module_base(pid: int, module_path: str, timeout_seconds: float = 10.0) -> int:
    target_path = os.path.normcase(os.path.abspath(module_path))
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        process = kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 0, pid)
        if process:
            try:
                mods = (c_void_p * 2048)()
                needed = c_uint32(0)
                if psapi.EnumProcessModules(process, mods, sizeof(mods), byref(needed)):
                    count = needed.value // sizeof(c_void_p)
                    buf = create_unicode_buffer(1024)
                    for i in range(count):
                        if psapi.GetModuleFileNameExW(process, mods[i], buf, len(buf)):
                            cur = os.path.normcase(os.path.abspath(buf.value))
                            if cur == target_path:
                                return int(mods[i] or 0)
            finally:
                kernel32.CloseHandle(process)
        time.sleep(0.1)
    return 0


def _find_remote_module_base_by_name(pid: int, module_name: str, timeout_seconds: float = 10.0) -> int:
    target_name = module_name.lower()
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        process = kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 0, pid)
        if process:
            try:
                mods = (c_void_p * 2048)()
                needed = c_uint32(0)
                if psapi.EnumProcessModules(process, mods, sizeof(mods), byref(needed)):
                    count = needed.value // sizeof(c_void_p)
                    buf = create_unicode_buffer(1024)
                    for i in range(count):
                        if psapi.GetModuleFileNameExW(process, mods[i], buf, len(buf)):
                            if os.path.basename(buf.value).lower() == target_name:
                                return int(mods[i] or 0)
            finally:
                kernel32.CloseHandle(process)
        time.sleep(0.1)
    return 0


def _remote_load_library(pid: int, dll_path: str, timeout_ms: int = 15000) -> bool:
    process = kernel32.OpenProcess(PROCESS_RIGHTS, 0, pid)
    if not process:
        return False
    remote_buf = None
    remote_thread = None
    try:
        wide_path = create_unicode_buffer(dll_path)
        byte_len = sizeof(wide_path)
        remote_buf = kernel32.VirtualAllocEx(process, None, byte_len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        if not remote_buf:
            return False
        written = c_size_t(0)
        if not kernel32.WriteProcessMemory(process, remote_buf, wide_path, byte_len, byref(written)):
            return False
        if int(written.value) != int(byte_len):
            return False
        local_kernel32 = kernel32.GetModuleHandleW("kernel32.dll")
        local_load_library = kernel32.GetProcAddress(local_kernel32, b"LoadLibraryW")
        if not local_kernel32 or not local_load_library:
            return False
        remote_kernel32 = _find_remote_module_base_by_name(pid, "kernel32.dll", timeout_seconds=2.0)
        if not remote_kernel32:
            return False
        load_library_rva = int(local_load_library) - int(local_kernel32)
        remote_load_library = c_void_p(remote_kernel32 + load_library_rva)
        remote_tid = c_uint32(0)
        remote_thread = kernel32.CreateRemoteThread(process, None, 0, remote_load_library, remote_buf, 0, byref(remote_tid))
        if not remote_thread:
            return False
        wait_rc = kernel32.WaitForSingleObject(remote_thread, timeout_ms)
        if wait_rc != WAIT_OBJECT_0:
            sys.stderr.write(
                f"Remote LoadLibraryW timed out: pid={pid} tid={remote_tid.value} "
                f"wait=0x{wait_rc:X} timeout_ms={timeout_ms} dll={dll_path}\n"
            )
            return False
        exit_code = c_uint32(0)
        if not kernel32.GetExitCodeThread(remote_thread, byref(exit_code)):
            sys.stderr.write(
                f"GetExitCodeThread failed after Remote LoadLibraryW: "
                f"pid={pid} tid={remote_tid.value} dll={dll_path}\n"
            )
            return False
        if exit_code.value == 0:
            sys.stderr.write(
                f"Remote LoadLibraryW returned NULL: pid={pid} tid={remote_tid.value} dll={dll_path}\n"
            )
            return False
        return True
    finally:
        if remote_thread:
            kernel32.CloseHandle(remote_thread)
        if remote_buf:
            kernel32.VirtualFreeEx(process, remote_buf, 0, MEM_RELEASE)
        kernel32.CloseHandle(process)


def _utf16_code_units(text: str) -> int:
    return len(text.encode("utf-16-le")) // 2


def _native_work_root() -> str:
    work_root = str(PROJECT_ROOT.resolve())
    while len(work_root) > 3 and work_root[-1] in {"\\", "/"}:
        work_root = work_root[:-1]

    root_units = _utf16_code_units(work_root)
    max_root_units = MAX_PATH - 1 - WXB_PATH_MAX_SUFFIX_CHARS
    if root_units > max_root_units:
        sys.stderr.write(
            "PROJECT_ROOT is too long for native path buffers: "
            f"utf16_units={root_units} max={max_root_units} root={work_root}\n"
        )
        return ""
    return work_root


def _startup_ready_file(startup_nonce: str) -> Path:
    return CONTROL_DIR / f"worker_start_{startup_nonce}.ready"


def _startup_ack_file(startup_nonce: str) -> Path:
    return CONTROL_DIR / f"worker_start_{startup_nonce}.ack"


def _startup_active_file(startup_nonce: str) -> Path:
    return CONTROL_DIR / f"worker_start_{startup_nonce}.active"


def _startup_cancel_file(startup_nonce: str) -> Path:
    return CONTROL_DIR / f"worker_start_{startup_nonce}.cancel"


def _alloc_remote_startup_config(process, startup_nonce: str) -> int:
    work_root = _native_work_root()
    if not work_root:
        return 0
    if len(startup_nonce) != WXB_STARTUP_NONCE_CHARS:
        sys.stderr.write(f"Invalid WxBridgeWorker startup nonce: {startup_nonce}\n")
        return 0

    config = WxbStartupConfig()
    config.magic = WXB_STARTUP_CONFIG_MAGIC
    config.size = sizeof(WxbStartupConfig)
    config.flags = WXB_STARTUP_CONFIG_FREE_AFTER_READ
    config.reserved = 0
    config.work_root = work_root
    config.startup_nonce = startup_nonce

    byte_len = sizeof(config)
    remote_buf = kernel32.VirtualAllocEx(process, None, byte_len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    if not remote_buf:
        return 0

    written = c_size_t(0)
    if not kernel32.WriteProcessMemory(process, remote_buf, byref(config), byte_len, byref(written)):
        kernel32.VirtualFreeEx(process, remote_buf, 0, MEM_RELEASE)
        return 0
    if int(written.value) != int(byte_len):
        kernel32.VirtualFreeEx(process, remote_buf, 0, MEM_RELEASE)
        return 0
    return int(remote_buf)


def _write_startup_ack(ack_file: Path) -> bool:
    try:
        ack_file.write_text("ack", encoding="ascii")
        return True
    except OSError as exc:
        sys.stderr.write(f"Failed to write WxBridgeWorker startup ack file: {ack_file} err={exc}\n")
        return False


def _wait_worker_started(
    worker_thread,
    ready_file: Path,
    ack_file: Path,
    active_file: Path,
    timeout_ms: int,
) -> tuple[bool, str, bool]:
    deadline = time.monotonic() + (timeout_ms / 1000.0)
    while True:
        remaining_ms = int(max(0.0, (deadline - time.monotonic()) * 1000))
        wait_ms = min(100, remaining_ms)
        wait_rc = kernel32.WaitForSingleObject(worker_thread, wait_ms)
        if wait_rc == WAIT_OBJECT_0:
            exit_code = c_uint32(0)
            if kernel32.GetExitCodeThread(worker_thread, byref(exit_code)):
                return False, f"WxBridgeWorker exited early: exit=0x{exit_code.value:X}", False
            return False, "WxBridgeWorker exited early and GetExitCodeThread failed", False
        if wait_rc == WAIT_FAILED:
            return False, "WaitForSingleObject failed while waiting for WxBridgeWorker startup", True

        if ready_file.exists():
            if not _write_startup_ack(ack_file):
                return False, "Failed to acknowledge WxBridgeWorker startup", True
            break

        if remaining_ms <= 0:
            return False, (
                f"Timed out waiting for WxBridgeWorker ready handshake after {timeout_ms}ms: "
                f"ready_file={ready_file}"
            ), True

    active_timeout_ms = int(os.environ.get("WX_WORKER_ACTIVE_TIMEOUT_MS", str(timeout_ms)))
    deadline = time.monotonic() + (active_timeout_ms / 1000.0)
    while True:
        remaining_ms = int(max(0.0, (deadline - time.monotonic()) * 1000))
        wait_ms = min(100, remaining_ms)
        wait_rc = kernel32.WaitForSingleObject(worker_thread, wait_ms)
        if wait_rc == WAIT_OBJECT_0:
            exit_code = c_uint32(0)
            if kernel32.GetExitCodeThread(worker_thread, byref(exit_code)):
                return False, f"WxBridgeWorker exited after startup ack: exit=0x{exit_code.value:X}", False
            return False, "WxBridgeWorker exited after startup ack and GetExitCodeThread failed", False
        if wait_rc == WAIT_FAILED:
            return False, "WaitForSingleObject failed after WxBridgeWorker startup ack; remote worker state is unknown", False

        if active_file.exists():
            return True, "", False

        if remaining_ms <= 0:
            return False, (
                f"Timed out waiting for WxBridgeWorker active confirmation after {active_timeout_ms}ms; "
                "startup ack was already sent, so remote worker state is unknown"
            ), False


def _request_worker_start_cancel(cancel_file: Path) -> None:
    try:
        cancel_file.write_text("cancel", encoding="ascii")
    except OSError as exc:
        sys.stderr.write(f"Failed to write WxBridgeWorker startup cancel file: {cancel_file} err={exc}\n")


def _wait_worker_cancelled(worker_thread, timeout_ms: int) -> bool:
    wait_rc = kernel32.WaitForSingleObject(worker_thread, timeout_ms)
    return wait_rc == WAIT_OBJECT_0


def inject_hook(pid: int, tid: int, hwnd: int = 0) -> int:
    """Load the bridge DLL into Weixin.exe and start its remote worker thread."""

    if build_dll() != 0:
        return 1
    CONTROL_DIR.mkdir(parents=True, exist_ok=True)
    STOP_FILE.unlink(missing_ok=True)
    startup_nonce = uuid.uuid4().hex
    startup_ready_file = _startup_ready_file(startup_nonce)
    startup_ack_file = _startup_ack_file(startup_nonce)
    startup_active_file = _startup_active_file(startup_nonce)
    startup_cancel_file = _startup_cancel_file(startup_nonce)
    startup_ready_file.unlink(missing_ok=True)
    startup_ack_file.unlink(missing_ok=True)
    startup_active_file.unlink(missing_ok=True)
    startup_cancel_file.unlink(missing_ok=True)

    inject_dll = _prepare_inject_dll()
    dll_path = str(inject_dll.resolve())
    module = kernel32.LoadLibraryW(dll_path)
    if not module:
        sys.stderr.write(f"LoadLibraryW failed: {dll_path}\n")
        return 1
    proc = kernel32.GetProcAddress(module, b"WxGetMsgHook")
    if not proc:
        sys.stderr.write("GetProcAddress failed: WxGetMsgHook\n")
        return 1
    worker = kernel32.GetProcAddress(module, b"WxBridgeWorker")
    if not worker:
        sys.stderr.write("GetProcAddress failed: WxBridgeWorker\n")
        return 1
    worker_rva = int(worker) - int(module)

    hook = None
    load_method = "remote_thread"
    remote_load_timeout_ms = int(os.environ.get("WX_REMOTE_LOAD_TIMEOUT_MS", "60000"))
    hook_only = os.environ.get("WX_HOOK_ONLY", "").lower() in {"1", "true", "yes"}
    remote_loaded = False if hook_only else _remote_load_library(pid, dll_path, timeout_ms=remote_load_timeout_ms)
    if not remote_loaded:
        if hook_only:
            sys.stderr.write(f"WX_HOOK_ONLY enabled; using WH_GETMESSAGE hook load: tid={tid} dll={dll_path}\n")
        else:
            sys.stderr.write(
                f"Remote LoadLibraryW failed: pid={pid} dll={dll_path}; "
                "trying WH_GETMESSAGE hook fallback\n"
            )
        hook = user32.SetWindowsHookExW(WH_GETMESSAGE, c_void_p(proc), c_void_p(module), c_uint32(tid))
        if not hook:
            sys.stderr.write(f"SetWindowsHookExW fallback failed: tid={tid} dll={dll_path}\n")
            return 1
        load_method = "wh_getmessage_hook"
        pulse_thread(tid, hwnd, count=20)

    remote_module = _find_remote_module_base(pid, dll_path, timeout_seconds=10.0)
    if not remote_module:
        if hook:
            user32.UnhookWindowsHookEx(hook)
        sys.stderr.write(f"Remote module not loaded in Weixin.exe: pid={pid} dll={dll_path}\n")
        return 1

    process = kernel32.OpenProcess(PROCESS_RIGHTS, 0, pid)
    if not process:
        sys.stderr.write(f"OpenProcess failed: pid={pid}\n")
        return 1
    try:
        worker_thread_id = c_uint32(0)
        remote_worker = c_void_p(remote_module + worker_rva)
        remote_config = _alloc_remote_startup_config(process, startup_nonce)
        if not remote_config:
            if hook:
                user32.UnhookWindowsHookEx(hook)
            sys.stderr.write(f"Failed to allocate remote WxBridgeWorker startup config: root={PROJECT_ROOT.resolve()}\n")
            return 1
        worker_start_timeout_ms = int(os.environ.get("WX_WORKER_START_TIMEOUT_MS", "10000"))
        worker_thread = kernel32.CreateRemoteThread(
            process,
            None,
            0,
            remote_worker,
            c_void_p(remote_config),
            0,
            byref(worker_thread_id),
        )
        if not worker_thread:
            kernel32.VirtualFreeEx(process, c_void_p(remote_config), 0, MEM_RELEASE)
            if hook:
                user32.UnhookWindowsHookEx(hook)
            sys.stderr.write("CreateRemoteThread failed: WxBridgeWorker\n")
            return 1
        worker_started, worker_error, worker_timed_out = _wait_worker_started(
            worker_thread,
            startup_ready_file,
            startup_ack_file,
            startup_active_file,
            worker_start_timeout_ms,
        )
        if not worker_started:
            if worker_timed_out:
                _request_worker_start_cancel(startup_cancel_file)
                worker_cancel_timeout_ms = int(os.environ.get("WX_WORKER_CANCEL_TIMEOUT_MS", "5000"))
                if _wait_worker_cancelled(worker_thread, worker_cancel_timeout_ms):
                    worker_error = f"{worker_error}; startup cancel acknowledged"
                else:
                    worker_error = (
                        f"{worker_error}; startup cancel requested but remote worker state is unknown "
                        f"after {worker_cancel_timeout_ms}ms"
                    )
            if hook:
                user32.UnhookWindowsHookEx(hook)
            kernel32.CloseHandle(worker_thread)
            sys.stderr.write(f"{worker_error}\n")
            return 1
        try:
            startup_ready_file.unlink(missing_ok=True)
            startup_ack_file.unlink(missing_ok=True)
            startup_active_file.unlink(missing_ok=True)
            startup_cancel_file.unlink(missing_ok=True)
        except OSError:
            pass
        kernel32.CloseHandle(worker_thread)
    finally:
        kernel32.CloseHandle(process)

    sys.stderr.write(
        f"已远程加载 DLL: pid={pid} tid={tid} hwnd=0x{int(hwnd):X} method={load_method} dll={dll_path}\n"
    )
    sys.stderr.write(
        f"已在 Weixin.exe 内启动 WxBridgeWorker: base=0x{remote_module:X} rva=0x{worker_rva:X} "
        f"entry=0x{remote_module + worker_rva:X} tid={worker_thread_id.value}\n"
    )
    sys.stderr.write("保持此进程运行；写 stop.txt 后远端 worker 会自行退出。\n")
    try:
        while True:
            if STOP_FILE.exists():
                break
            HEARTBEAT_FILE.write_text(str(int(time.time())), encoding="utf-8")
            time.sleep(1)
    except KeyboardInterrupt:
        STOP_FILE.write_text("stop", encoding="utf-8")
    finally:
        if hook:
            user32.UnhookWindowsHookEx(hook)
    return 0
