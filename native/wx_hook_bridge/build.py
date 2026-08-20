from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-file", default="")
    parser.add_argument("--syntax-only", action="store_true")
    args = parser.parse_args()
    script = Path(__file__).with_name("build.ps1")
    command = ["powershell", "-ExecutionPolicy", "Bypass", "-File", str(script)]
    if args.out_file:
        command += ["-OutFile", args.out_file]
    if args.syntax_only:
        command.append("-SyntaxOnly")
    return subprocess.call(command)


if __name__ == "__main__":
    raise SystemExit(main())
