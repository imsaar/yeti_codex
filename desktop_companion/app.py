#!/usr/bin/env python3
"""Desktop controller for Companion 313 firmware."""

from __future__ import annotations

import argparse
import json
import sys
import time
from typing import Any

import requests


class CompanionClient:
    def __init__(self, host: str, timeout: float = 3.0) -> None:
        self.base = f"http://{host}".rstrip("/")
        self.timeout = timeout

    def _post(self, path: str, payload: dict[str, Any]) -> dict[str, Any]:
        resp = requests.post(f"{self.base}{path}", json=payload, timeout=self.timeout)
        resp.raise_for_status()
        if not resp.text:
            return {"ok": True}
        return resp.json()

    def _get(self, path: str) -> dict[str, Any]:
        resp = requests.get(f"{self.base}{path}", timeout=self.timeout)
        resp.raise_for_status()
        return resp.json()

    def status(self) -> dict[str, Any]:
        return self._get("/status")

    def emotion(self, emotion: str) -> dict[str, Any]:
        return self._post("/emotion", {"emotion": emotion})

    def speak(self, text: str) -> dict[str, Any]:
        return self._post("/speak", {"text": text})

    def add_note(self, note: str) -> dict[str, Any]:
        return self._post("/notes", {"note": note})

    def add_reminder(self, minutes: int, message: str) -> dict[str, Any]:
        return self._post("/reminders", {"minutes": minutes, "message": message})

    def clear(self) -> dict[str, Any]:
        return self._post("/clear", {})


def print_json(payload: dict[str, Any]) -> None:
    print(json.dumps(payload, indent=2, sort_keys=True))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Desktop companion client for ESP32-C3 robot")
    parser.add_argument("--host", default="192.168.4.1", help="Robot host or IP (default: 192.168.4.1)")

    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status", help="Read current companion state")

    emo = sub.add_parser("emotion", help="Set active emotion")
    emo.add_argument("name", choices=["neutral", "happy", "sad", "sleepy", "angry", "surprised", "thinking"])

    speak = sub.add_parser("speak", help="Set speech line on OLED")
    speak.add_argument("text")

    note = sub.add_parser("note", help="Add note")
    note.add_argument("text")

    rem = sub.add_parser("reminder", help="Add reminder in minutes")
    rem.add_argument("minutes", type=int)
    rem.add_argument("message")

    watch = sub.add_parser("watch", help="Watch status updates")
    watch.add_argument("--interval", type=float, default=2.0)

    sub.add_parser("clear", help="Clear notes/reminders")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    client = CompanionClient(args.host)

    try:
        if args.command == "status":
            print_json(client.status())
        elif args.command == "emotion":
            print_json(client.emotion(args.name))
        elif args.command == "speak":
            print_json(client.speak(args.text))
        elif args.command == "note":
            print_json(client.add_note(args.text))
        elif args.command == "reminder":
            print_json(client.add_reminder(args.minutes, args.message))
        elif args.command == "watch":
            while True:
                print_json(client.status())
                time.sleep(max(0.25, args.interval))
        elif args.command == "clear":
            print_json(client.clear())
        else:
            parser.print_help()
            return 2

        return 0
    except requests.RequestException as exc:
        print(f"Request failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
