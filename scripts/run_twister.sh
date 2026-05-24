#!/usr/bin/env bash
# Run Zephyr twister for tests/ with host simulation boards.
# Usage: ./scripts/run_twister.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/setup_env.sh"

OUT_DIR="${ZEPHYR_TWISTER_OUT_DIR:-twister-out}"
PLATFORM="${ZEPHYR_TWISTER_PLATFORM:-native_sim}"

if ! west twister -h >/dev/null 2>&1; then
    echo "twister not available via west; ensure Zephyr environment is initialized." >&2
    exit 2
fi

cd "${ROOT}"
west twister -T tests -p "${PLATFORM}" -O "${OUT_DIR}" --inline-logs "$@"
