#!/usr/bin/env python3
"""Preflight checks for host-based Zephyr tests (native_sim/native_posix)."""

from __future__ import annotations

import os
import platform
import subprocess
import sys


def is_windows() -> bool:
    return os.name == "nt" or platform.system().lower().startswith("win")


def in_wsl() -> bool:
    return "microsoft" in platform.release().lower()


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def main() -> int:
    board = os.environ.get("ZEPHYR_TEST_BOARD", "").strip()
    if not board:
        board = "native_sim (auto)"

    host = platform.platform()
    print(f"[preflight] host={host}")
    print(f"[preflight] requested_board={board}")

    if is_windows() and not in_wsl():
        print("[preflight] ERROR: native_sim/native_posix require Linux POSIX host.")
        print("[preflight] Use WSL/Linux for host tests, or set a non-POSIX hardware board.")
        return 2

    west = run(["west", "--version"])
    if west.returncode != 0:
        print("[preflight] ERROR: west not available in PATH.")
        return 2
    print(f"[preflight] {west.stdout.strip()}")

    boards = run(["west", "boards"])
    if boards.returncode != 0:
        print("[preflight] WARN: failed to query boards via `west boards`.")
        return 0

    available = boards.stdout.splitlines()
    has_native_sim = "native_sim" in available
    has_native_posix = "native_posix" in available
    print(f"[preflight] native_sim={has_native_sim}, native_posix={has_native_posix}")

    if not has_native_sim and not has_native_posix:
        print("[preflight] WARN: no host simulation board found.")
    else:
        print("[preflight] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
