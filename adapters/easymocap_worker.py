#!/usr/bin/env python3
"""
BDFR PerformanceCapture EasyMocap worker/supervisor.

The worker keeps EasyMocap's Python/CUDA/model dependency stack outside the C++
capture core. Control uses versioned JSON-lines over stdin/stdout. Solver process
stdout/stderr are redirected to this worker's stderr so control messages remain
machine-readable.
"""

import argparse
import importlib.util
import json
import os
import subprocess
import sys
import threading
import time
from typing import Any, Dict, List, Optional

PROTOCOL = 2


def send(payload: Dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(payload, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def easymocap_available() -> bool:
    return importlib.util.find_spec("easymocap") is not None


class SolverSupervisor:
    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._process: Optional[subprocess.Popen] = None
        self._argv: List[str] = []
        self._cwd: Optional[str] = None
        self._auto_restart = False
        self._max_restarts = 0
        self._restart_delay = 1.0
        self._restarts = 0
        self._stop_requested = False
        self._generation = 0
        self._last_returncode: Optional[int] = None
        self._last_error = ""

    def _spawn_locked(self) -> bool:
        if not self._argv:
            self._last_error = "empty argv"
            return False

        try:
            self._process = subprocess.Popen(
                self._argv,
                cwd=self._cwd,
                stdin=subprocess.DEVNULL,
                stdout=sys.stderr,
                stderr=sys.stderr,
                shell=False,
            )
        except Exception as exc:  # noqa: BLE001 - report process launch failures
            self._process = None
            self._last_error = f"launch_failed:{exc}"
            return False

        self._last_error = ""
        process = self._process
        generation = self._generation
        threading.Thread(
            target=self._watch_process,
            args=(process, generation),
            name="bdfr-easymocap-watchdog",
            daemon=True,
        ).start()
        return True

    def _watch_process(
        self,
        process: subprocess.Popen,
        generation: int,
    ) -> None:
        returncode = process.wait()

        with self._lock:
            if generation != self._generation or process is not self._process:
                return

            self._last_returncode = returncode
            self._process = None

            should_restart = (
                not self._stop_requested
                and self._auto_restart
                and self._restarts < self._max_restarts
            )
            if not should_restart:
                return

            self._restarts += 1
            delay = self._restart_delay

        time.sleep(delay)

        with self._lock:
            if (
                generation != self._generation
                or self._stop_requested
                or self._process is not None
            ):
                return
            self._spawn_locked()

    def launch(
        self,
        argv: List[str],
        cwd: Optional[str] = None,
        auto_restart: bool = True,
        max_restarts: int = 3,
        restart_delay: float = 1.0,
    ) -> bool:
        if (
            not isinstance(argv, list)
            or not argv
            or not all(isinstance(item, str) and item for item in argv)
        ):
            with self._lock:
                self._last_error = "argv_must_be_nonempty_string_list"
            return False

        if cwd is not None:
            if not isinstance(cwd, str) or not os.path.isdir(cwd):
                with self._lock:
                    self._last_error = "invalid_working_directory"
                return False
            cwd = os.path.abspath(cwd)

        self.stop()

        with self._lock:
            self._generation += 1
            self._argv = list(argv)
            self._cwd = cwd
            self._auto_restart = bool(auto_restart)
            self._max_restarts = max(0, min(int(max_restarts), 20))
            self._restart_delay = max(0.05, min(float(restart_delay), 30.0))
            self._restarts = 0
            self._stop_requested = False
            self._last_returncode = None
            return self._spawn_locked()

    def stop(self, timeout: float = 3.0) -> None:
        with self._lock:
            self._stop_requested = True
            self._generation += 1
            process = self._process
            self._process = None

        if process is None:
            return

        try:
            process.terminate()
            process.wait(timeout=max(0.1, timeout))
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        except Exception as exc:  # noqa: BLE001
            with self._lock:
                self._last_error = f"stop_failed:{exc}"

        with self._lock:
            self._last_returncode = process.returncode

    def restart(self) -> bool:
        with self._lock:
            argv = list(self._argv)
            cwd = self._cwd
            auto_restart = self._auto_restart
            max_restarts = self._max_restarts
            restart_delay = self._restart_delay

        if not argv:
            with self._lock:
                self._last_error = "no_previous_solver_command"
            return False

        return self.launch(
            argv=argv,
            cwd=cwd,
            auto_restart=auto_restart,
            max_restarts=max_restarts,
            restart_delay=restart_delay,
        )

    def status(self) -> Dict[str, Any]:
        with self._lock:
            process = self._process
            running = process is not None and process.poll() is None
            return {
                "running": running,
                "pid": process.pid if running else None,
                "argv": list(self._argv),
                "cwd": self._cwd,
                "auto_restart": self._auto_restart,
                "max_restarts": self._max_restarts,
                "restarts": self._restarts,
                "last_returncode": self._last_returncode,
                "last_error": self._last_error,
            }


def hello_payload(supervisor: SolverSupervisor) -> Dict[str, Any]:
    return {
        "type": "hello",
        "protocol": PROTOCOL,
        "adapter": "EasyMocap",
        "available": easymocap_available(),
        "capabilities": [
            "body",
            "head",
            "left_hand",
            "right_hand",
            "smpl",
            "smplx",
            "mano",
            "process_supervisor",
            "watchdog_restart",
        ],
        "solver": supervisor.status(),
    }


def run_self_test() -> int:
    supervisor = SolverSupervisor()

    assert supervisor.launch(
        [sys.executable, "-c", "import time; time.sleep(0.05)"],
        auto_restart=False,
    )
    time.sleep(0.12)
    status = supervisor.status()
    assert not status["running"]

    assert supervisor.launch(
        [sys.executable, "-c", "import time; time.sleep(5)"],
        auto_restart=False,
    )
    assert supervisor.status()["running"]
    supervisor.stop(timeout=0.5)
    assert not supervisor.status()["running"]

    send({
        "type": "self_test",
        "ok": True,
        "protocol": PROTOCOL,
    })
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    supervisor = SolverSupervisor()
    send(hello_payload(supervisor))

    if args.probe:
        return 0 if easymocap_available() else 2

    try:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue

            try:
                message = json.loads(line)
            except json.JSONDecodeError as exc:
                send({
                    "type": "error",
                    "error": f"invalid_json:{exc.msg}",
                })
                continue

            command = message.get("command")

            if command == "ping":
                send({"type": "pong", "protocol": PROTOCOL})

            elif command == "status":
                send({
                    "type": "status",
                    "protocol": PROTOCOL,
                    "available": easymocap_available(),
                    "solver": supervisor.status(),
                })

            elif command == "launch":
                ok = supervisor.launch(
                    argv=message.get("argv", []),
                    cwd=message.get("cwd"),
                    auto_restart=message.get("auto_restart", True),
                    max_restarts=message.get("max_restarts", 3),
                    restart_delay=message.get("restart_delay", 1.0),
                )
                send({
                    "type": "launch_result",
                    "ok": ok,
                    "solver": supervisor.status(),
                })

            elif command == "stop_solver":
                supervisor.stop()
                send({
                    "type": "stop_result",
                    "ok": True,
                    "solver": supervisor.status(),
                })

            elif command == "restart_solver":
                ok = supervisor.restart()
                send({
                    "type": "restart_result",
                    "ok": ok,
                    "solver": supervisor.status(),
                })

            elif command == "shutdown":
                supervisor.stop()
                send({"type": "bye"})
                return 0

            else:
                send({
                    "type": "error",
                    "error": "unsupported_command",
                    "command": command,
                })
    finally:
        supervisor.stop()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
