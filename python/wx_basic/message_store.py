from __future__ import annotations

import argparse
import hashlib
import json
import os
import sqlite3
import time
import uuid
from pathlib import Path
from typing import Any, Iterable


DEFAULT_ROOT = Path(__file__).resolve().parents[2]


def _atomic_write(path: Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(f".{path.name}.{os.getpid()}.{time.time_ns()}.tmp")
    temp.write_text(data, encoding="utf-8", newline="")
    os.replace(temp, path)


def _json_value(payload: dict[str, Any], key: str, default: Any = "") -> Any:
    value = payload.get(key, default)
    return default if value is None else value


class MessageStore:
    """SQLite archive for the JSON files emitted by the native receive hook."""

    def __init__(self, path: str | Path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(self.path)
        self.connection.row_factory = sqlite3.Row
        self.connection.execute("PRAGMA busy_timeout = 5000")
        self.connection.execute("PRAGMA journal_mode = WAL")
        self.connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS messages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                event_key TEXT NOT NULL UNIQUE,
                msg_id TEXT NOT NULL,
                local_id TEXT NOT NULL DEFAULT '',
                message_type INTEGER NOT NULL DEFAULT 0,
                message_subtype INTEGER NOT NULL DEFAULT 0,
                timestamp TEXT NOT NULL DEFAULT '',
                direction TEXT NOT NULL DEFAULT 'unknown',
                is_incoming INTEGER NOT NULL DEFAULT 0,
                is_outgoing INTEGER NOT NULL DEFAULT 0,
                peer_wxid TEXT NOT NULL DEFAULT '',
                room_id TEXT NOT NULL DEFAULT '',
                sender_wxid TEXT NOT NULL DEFAULT '',
                self_wxid TEXT NOT NULL DEFAULT '',
                raw_from TEXT NOT NULL DEFAULT '',
                raw_to TEXT NOT NULL DEFAULT '',
                content TEXT NOT NULL DEFAULT '',
                signature TEXT NOT NULL DEFAULT '',
                payload_json TEXT NOT NULL,
                source_file TEXT NOT NULL DEFAULT '',
                ingested_at INTEGER NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_messages_timestamp ON messages(timestamp);
            CREATE INDEX IF NOT EXISTS idx_messages_peer ON messages(peer_wxid);
            CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender_wxid);
            CREATE TABLE IF NOT EXISTS inbox_files (
                source_file TEXT PRIMARY KEY,
                event_key TEXT NOT NULL,
                processed_at INTEGER NOT NULL
            );
            """
        )
        self.connection.commit()

    def close(self) -> None:
        self.connection.close()

    def __enter__(self) -> "MessageStore":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    @staticmethod
    def event_key(payload: dict[str, Any], source_file: str = "") -> str:
        """Use the native msg id, with a deterministic fallback for test payloads."""
        msg_id = str(_json_value(payload, "msgId", ""))
        if msg_id:
            local_id = str(_json_value(payload, "localId", ""))
            timestamp = str(_json_value(payload, "timestamp", ""))
            session = str(
                _json_value(payload, "roomId", "")
                or _json_value(payload, "peerWxId", "")
                or _json_value(payload, "rawFrom", "")
            )
            return f"msg:{session}:{msg_id}:{local_id}:{timestamp}"
        raw = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        digest = hashlib.sha256((source_file + "\0" + raw).encode()).hexdigest()
        return f"sha:{digest}"

    def insert(self, payload: dict[str, Any], source_file: str = "") -> bool:
        event_key = self.event_key(payload, source_file)
        try:
            self.connection.execute(
                """
                INSERT INTO messages (
                    event_key, msg_id, local_id, message_type, message_subtype,
                    timestamp, direction, is_incoming, is_outgoing, peer_wxid,
                    room_id, sender_wxid, self_wxid, raw_from, raw_to, content,
                    signature, payload_json, source_file, ingested_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    event_key,
                    str(_json_value(payload, "msgId")),
                    str(_json_value(payload, "localId")),
                    int(_json_value(payload, "type", 0) or 0),
                    int(_json_value(payload, "subType", 0) or 0),
                    str(_json_value(payload, "timestamp")),
                    str(_json_value(payload, "direction", "unknown")),
                    int(bool(_json_value(payload, "isIncoming", False))),
                    int(bool(_json_value(payload, "isSendMsg", False))),
                    str(_json_value(payload, "peerWxId")),
                    str(_json_value(payload, "roomId")),
                    str(_json_value(payload, "senderWxId")),
                    str(_json_value(payload, "selfWxId")),
                    str(_json_value(payload, "rawFrom", _json_value(payload, "from"))),
                    str(_json_value(payload, "rawTo", _json_value(payload, "fromWxId"))),
                    str(_json_value(payload, "content")),
                    str(_json_value(payload, "signature")),
                    json.dumps(payload, ensure_ascii=False, separators=(",", ":")),
                    source_file,
                    int(time.time()),
                ),
            )
        except sqlite3.IntegrityError:
            self.connection.rollback()
            return False
        self.connection.commit()
        return True

    def ingest_file(self, path: str | Path) -> bool:
        file_path = Path(path)
        source_file = str(file_path.resolve())
        if self.connection.execute(
            "SELECT 1 FROM inbox_files WHERE source_file = ?", (source_file,)
        ).fetchone():
            return False
        try:
            payload = json.loads(file_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError):
            return False
        if not isinstance(payload, dict):
            return False
        inserted = self.insert(payload, source_file)
        self.connection.execute(
            "INSERT OR IGNORE INTO inbox_files(source_file, event_key, processed_at) VALUES (?, ?, ?)",
            (source_file, self.event_key(payload, source_file), int(time.time())),
        )
        self.connection.commit()
        return inserted

    def recent(self, limit: int = 50) -> list[sqlite3.Row]:
        return list(
            self.connection.execute(
                "SELECT * FROM messages ORDER BY id DESC LIMIT ?", (max(1, int(limit)),)
            )
        )


def iter_inbox(inbox: str | Path) -> Iterable[Path]:
    return sorted(Path(inbox).glob("*.json"), key=lambda item: item.name)


def ingest_inbox(inbox: str | Path, database: str | Path) -> tuple[int, int]:
    inserted = 0
    seen = 0
    with MessageStore(database) as store:
        for path in iter_inbox(inbox):
            seen += 1
            inserted += int(store.ingest_file(path))
    return inserted, seen


def send_text(
    root: str | Path,
    to: str,
    content: str,
    timeout: float = 20.0,
    poll_interval: float = 0.2,
) -> dict[str, Any]:
    root_path = Path(root)
    control = root_path / "bridge" / "control"
    outbox = root_path / "bridge" / "outbox" / "next.txt"
    control.mkdir(parents=True, exist_ok=True)
    outbox.parent.mkdir(parents=True, exist_ok=True)
    existing = ""
    if outbox.exists():
        try:
            existing = outbox.read_text(encoding="utf-8").strip()
        except OSError:
            existing = ""
    if existing:
        return {"ok": False, "command": "send_text", "message": "outbox busy"}
    seq = str(int(time.time() * 1000))
    # Avoid a same-millisecond collision when a caller sends in a tight loop.
    while (control / f"result_{seq}.json").exists():
        seq = f"{seq}-{uuid.uuid4().hex[:6]}"
    _atomic_write(outbox, f"{seq}\t1\t{to}\t{content}")
    deadline = time.monotonic() + timeout
    result_path = control / f"result_{seq}.json"
    while time.monotonic() < deadline:
        if result_path.exists():
            try:
                value = json.loads(result_path.read_text(encoding="utf-8"))
                return value if isinstance(value, dict) else {"ok": False, "message": "invalid result"}
            except (OSError, UnicodeError, json.JSONDecodeError):
                pass
        time.sleep(poll_interval)
    return {"ok": False, "seq": seq, "command": "send_text", "message": "timeout"}


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Minimal file bridge + SQLite archive")
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    sub = parser.add_subparsers(dest="command", required=True)

    send = sub.add_parser("send", help="write a type-1 send_text command")
    send.add_argument("to")
    send.add_argument("content")
    send.add_argument("--timeout", type=float, default=20.0)

    ingest = sub.add_parser("ingest", help="ingest current inbox JSON files into SQLite")
    ingest.add_argument("--db", type=Path, default=None)
    ingest.add_argument("--inbox", type=Path, default=None)

    watch = sub.add_parser("watch", help="continuously ingest inbox JSON files")
    watch.add_argument("--db", type=Path, default=None)
    watch.add_argument("--inbox", type=Path, default=None)
    watch.add_argument("--interval", type=float, default=1.0)

    show = sub.add_parser("show", help="show recent SQLite messages")
    show.add_argument("--db", type=Path, default=None)
    show.add_argument("--limit", type=int, default=20)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    root = args.root.resolve()
    db = (args.db or root / "bridge" / "messages.sqlite3").resolve() if hasattr(args, "db") else root / "bridge" / "messages.sqlite3"
    inbox = (args.inbox or root / "bridge" / "inbox").resolve() if hasattr(args, "inbox") else root / "bridge" / "inbox"
    if args.command == "send":
        print(json.dumps(send_text(root, args.to, args.content, args.timeout), ensure_ascii=False))
        return 0
    if args.command == "ingest":
        inserted, seen = ingest_inbox(inbox, db)
        print(json.dumps({"inserted": inserted, "seen": seen, "db": str(db)}, ensure_ascii=False))
        return 0
    if args.command == "watch":
        try:
            while True:
                inserted, seen = ingest_inbox(inbox, db)
                if inserted:
                    print(json.dumps({"inserted": inserted, "seen": seen}), flush=True)
                time.sleep(max(0.05, args.interval))
        except KeyboardInterrupt:
            return 0
    if args.command == "show":
        with MessageStore(db) as store:
            for row in store.recent(args.limit):
                print(json.dumps(dict(row), ensure_ascii=False))
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
