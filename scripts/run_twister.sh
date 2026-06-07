#!/usr/bin/env bash
# Run Zephyr twister for tests/ with host simulation boards.
# Usage: ./scripts/run_twister.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/setup_env.sh"
python "${ZP_SCRIPTS_ROOT}/preflight_host_tests.py"
ROOT="${ZP_WORK_ROOT}"
TESTS_DIR="${ZP_TESTS_DIR}"

OUT_DIR="${ZEPHYR_TWISTER_OUT_DIR:-twister-out}"
PLATFORM="${ZEPHYR_TWISTER_PLATFORM:-native_sim}"

if [ "$(uname -s)" = "MINGW64_NT" ] || [ "$(uname -s)" = "MINGW32_NT" ] || [ "$(uname -s)" = "MSYS_NT" ]; then
    if [ "${PLATFORM}" = "native_sim" ] || [ "${PLATFORM}" = "native_posix" ]; then
        echo "Platform '${PLATFORM}' requires Linux/WSL host for twister." >&2
        exit 2
    fi
fi

if ! west twister -h >/dev/null 2>&1; then
    echo "twister not available via west; ensure Zephyr environment is initialized." >&2
    exit 2
fi

TWISTER_OUT="${OUT_DIR}"
case "${TWISTER_OUT}" in
    /*) ;;
    *) TWISTER_OUT="${ROOT}/${TWISTER_OUT}" ;;
esac

echo "Mode: ${ZP_MODE}, tests: ${TESTS_DIR}, out: ${TWISTER_OUT}"
cd "${ROOT}"
west twister -T "${TESTS_DIR}" -p "${PLATFORM}" -O "${TWISTER_OUT}" --inline-logs "$@"
