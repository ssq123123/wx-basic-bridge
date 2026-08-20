#!/usr/bin/env python3
"""Build (if needed) and inject wx_hook_bridge.dll into WeChat.

Run from the repository root:

    python python\\tools\\inject_bridge.py
    python python\\tools\\inject_bridge.py --force-build
    python python\\tools\\inject_bridge.py --pid 12345

Stop the remote worker by creating bridge/control/stop.txt or pressing Ctrl+C.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SUPPORTED_WEIXIN_VERSION = "4.1.9.23"
INJECT_DIR = REPO_ROOT / "python" / "inject"
if str(INJECT_DIR) not in sys.path:
    sys.path.insert(0, str(INJECT_DIR))

from wechat_inject import build_dll, inject_hook  # noqa: E402
from wechat_native import choose_wechat_target, find_wechat_processes  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Inject wx_hook_bridge into WeChat.")
    parser.add_argument("--pid", type=int, default=0, help="Target WeChat PID (optional).")
    parser.add_argument("--force-build", action="store_true", help="Rebuild the DLL even if up to date.")
    parser.add_argument("--list", action="store_true", help="List WeChat processes and exit.")
    parser.add_argument(
        "--allow-version-mismatch",
        action="store_true",
        help="Inject even when Weixin.dll is not 4.1.9.23.",
    )
    args = parser.parse_args(argv)

    if args.list:
        procs = find_wechat_processes()
        if not procs:
            print("No WeChat/Weixin process found.", file=sys.stderr)
            return 1
        for p in procs:
            print(
                f"pid={p.get('pid')} name={p.get('name')} "
                f"weixin_base={p.get('weixin_base', 0):#x} "
                f"version={p.get('weixin_version', '')} exe={p.get('exe_path', '')}"
            )
        return 0

    if args.force_build:
        if build_dll(force=True) != 0:
            return 1

    if args.pid:
        import os

        os.environ["WX_TARGET_PID"] = str(args.pid)

    target = choose_wechat_target()
    if not target:
        print("No WeChat/Weixin process found. Start WeChat and login first.", file=sys.stderr)
        return 1

    pid = int(target.get("pid") or 0)
    tid = int(target.get("tid") or 0)
    hwnd = int(target.get("hwnd") or 0)
    weixin_version = str(target.get("weixin_version") or "")
    if weixin_version != SUPPORTED_WEIXIN_VERSION and not args.allow_version_mismatch:
        print(
            f"Unsupported Weixin.dll version: {weixin_version or 'unknown'}; "
            f"expected {SUPPORTED_WEIXIN_VERSION}.",
            file=sys.stderr,
        )
        return 1
    if not pid or not tid:
        print(
            f"Found process pid={pid} but no UI thread. Is the main window open? target={target}",
            file=sys.stderr,
        )
        return 1

    print(
        f"Target: pid={pid} tid={tid} hwnd=0x{hwnd:X} "
        f"name={target.get('name')} title={target.get('title', '')}",
        file=sys.stderr,
    )
    return inject_hook(pid, tid, hwnd)


if __name__ == "__main__":
    raise SystemExit(main())
