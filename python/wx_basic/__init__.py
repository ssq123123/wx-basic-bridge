"""Small file-bridge helpers for the Weixin 4.1.9.23 basic build."""

from .message_store import MessageStore, ingest_inbox, send_text

__all__ = ["MessageStore", "ingest_inbox", "send_text"]
