#!/usr/bin/env python3
"""
BDFR PerformanceCapture EasyMocap worker bootstrap.

This worker intentionally keeps EasyMocap and its Python/CUDA dependency stack
outside the C++ core. It currently provides dependency detection and a stable
JSON-lines control handshake. Solving hooks are added behind this boundary.
"""

import argparse
import importlib.util
import json
import sys
from typing import Any, Dict

PROTOCOL = 1


def send(payload: Dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(payload, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def dependency_status() -> Dict[str, Any]:
    available = importlib.util.find_spec("easymocap") is not None
    return {
        "type": "hello",
        "protocol": PROTOCOL,
        "adapter": "EasyMocap",
        "available": available,
        "capabilities": [
            "body",
            "head",
            "left_hand",
            "right_hand",
            "smpl",
            "smplx",
            "mano",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", action="store_true")
    args = parser.parse_args()

    hello = dependency_status()
    send(hello)
    if args.probe:
        return 0 if hello["available"] else 2

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            message = json.loads(line)
        except json.JSONDecodeError as exc:
            send({"type": "error", "error": f"invalid_json:{exc.msg}"})
            continue

        command = message.get("command")
        if command == "ping":
            send({"type": "pong", "protocol": PROTOCOL})
        elif command == "status":
            send(dependency_status())
        elif command == "shutdown":
            send({"type": "bye"})
            return 0
        else:
            send({
                "type": "error",
                "error": "unsupported_command",
                "command": command,
            })

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
