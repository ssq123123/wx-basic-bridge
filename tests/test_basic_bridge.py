from __future__ import annotations

import json
import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from python.wx_basic.message_store import MessageStore, ingest_inbox, send_text


class BasicBridgeTests(unittest.TestCase):
    def test_message_store_ingests_and_deduplicates(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            inbox = root / "bridge" / "inbox"
            db = root / "bridge" / "messages.sqlite3"
            inbox.mkdir(parents=True)
            payload = {
                "msgId": "123",
                "localId": "7",
                "type": "1",
                "subType": "0",
                "timestamp": "1780000000",
                "direction": "incoming",
                "isIncoming": True,
                "isSendMsg": False,
                "peerWxId": "wxid_peer",
                "content": "hello",
            }
            (inbox / "123.json").write_text(json.dumps(payload), encoding="utf-8")
            self.assertEqual(ingest_inbox(inbox, db), (1, 1))
            self.assertEqual(ingest_inbox(inbox, db), (0, 1))
            connection = sqlite3.connect(db)
            try:
                self.assertEqual(connection.execute("SELECT COUNT(*) FROM messages").fetchone()[0], 1)
                self.assertEqual(connection.execute("SELECT content FROM messages").fetchone()[0], "hello")
                self.assertEqual(connection.execute("SELECT COUNT(*) FROM inbox_files").fetchone()[0], 1)
            finally:
                connection.close()

    def test_same_msg_id_in_different_sessions_is_not_collapsed(self) -> None:
        first = {"msgId": "123", "localId": "1", "timestamp": "10", "peerWxId": "room-a"}
        second = {"msgId": "123", "localId": "1", "timestamp": "10", "peerWxId": "room-b"}
        self.assertNotEqual(MessageStore.event_key(first), MessageStore.event_key(second))

    def test_send_text_writes_compatible_outbox_and_reads_result(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            result_dir = root / "bridge" / "control"
            result_dir.mkdir(parents=True)

            def fake_sleep(_seconds: float) -> None:
                result_files = list(result_dir.glob("result_*.json"))
                if not result_files:
                    outbox = root / "bridge" / "outbox" / "next.txt"
                    seq = outbox.read_text(encoding="utf-8").split("\t", 1)[0]
                    (result_dir / f"result_{seq}.json").write_text(
                        json.dumps({"ok": True, "seq": seq, "command": "send_text"}),
                        encoding="utf-8",
                    )
                    outbox.write_text("", encoding="utf-8")

            with patch("python.wx_basic.message_store.time.sleep", side_effect=fake_sleep):
                result = send_text(root, "filehelper", "hello", timeout=1, poll_interval=0)
            self.assertTrue(result["ok"])
            self.assertEqual((root / "bridge" / "outbox" / "next.txt").read_text(encoding="utf-8"), "")


if __name__ == "__main__":
    unittest.main()
