#!/usr/bin/env python3
"""CLI for the basic file bridge and its SQLite inbox archive."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
PACKAGE_DIR = ROOT / "python"
if str(PACKAGE_DIR) not in sys.path:
    sys.path.insert(0, str(PACKAGE_DIR))

from wx_basic.message_store import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
