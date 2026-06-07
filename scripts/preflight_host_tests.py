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


def is_posix_host_board(board: str) -> bool:
    name = board.strip().lower()
    if not name:
        return True
    return name in ("native_sim", "native_posix")


def main() -> int:
    board_env = os.environ.get("ZEPHYR_TEST_BOARD", "").strip()
    board = board_env if board_env else "native_sim (auto)"

    host = platform.platform()
    print(f"[preflight] host={host}")
    print(f"[preflight] requested_board={board}")

    if is_windows() and not in_wsl() and is_posix_host_board(board_env):
        print("[preflight] ERROR: native_sim/native_posix require Linux POSIX host.")
        print("[preflight] Use WSL/Linux for host tests, or set a non-POSIX board (e.g. qemu_riscv32).")
        print("[preflight] Suggested next steps:")
        print("  1) WSL/Linux: source <scripts>/setup_env.sh && <scripts>/run_tests.sh")
        print("  2) Windows QEMU: $env:ZEPHYR_TEST_BOARD='qemu_riscv32'; .\\<scripts>\\run_tests.ps1")
        print("     (<scripts> = scripts/ in framework repo, or framework/scripts/ in app repo)")
        print("     (see docs/zh-CN/10-环境与构建/14-QEMU仿真运行指南.md §6)")
        return 2

    if is_windows() and not in_wsl() and board_env:
        print("[preflight] non-POSIX board on Windows; ensure QEMU_BIN_PATH is set for QEMU boards.")

    west = run(["west", "--version"])
    if west.returncode != 0:
        print("[preflight] ERROR: west not available in PATH.")
        print("[preflight] Suggested fix:")
        print("  - Activate your virtualenv and run setup_env first.")
        print("  - Verify with: west --version")
        return 2
    print(f"[preflight] {west.stdout.strip()}")

    boards = run(["west", "boards"])
    if boards.returncode != 0:
        print("[preflight] WARN: failed to query boards via `west boards`.")
        print("[preflight] Suggested fix: run `west update` then retry.")
        return 0

    available = boards.stdout.splitlines()
    has_native_sim = "native_sim" in available
    has_native_posix = "native_posix" in available
    print(f"[preflight] native_sim={has_native_sim}, native_posix={has_native_posix}")

    if not has_native_sim and not has_native_posix:
        print("[preflight] WARN: no host simulation board found.")
        print("[preflight] Suggested fix: ensure Zephyr modules are initialized (`west update`).")
    else:
        print("[preflight] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
