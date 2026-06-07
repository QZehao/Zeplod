#!/usr/bin/env bash
# Run ztest suite with a host simulation board (native_sim preferred, native_posix fallback).
# Sources scripts/setup_env.sh (requires zephyr_config.env).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/setup_env.sh"
python "${ZP_SCRIPTS_ROOT}/preflight_host_tests.py"
ROOT="${ZP_WORK_ROOT}"
TESTS_DIR="${ZP_TESTS_DIR}"
BUILD_DIR="${ZEPHYR_TEST_BUILD_DIR:-build_tests}"
CONF_FILE="${ZEPHYR_TEST_CONF:-prj.conf;prj_test_extensions.conf}"

pick_board() {
    if [ -n "${ZEPHYR_TEST_BOARD:-}" ]; then
        echo "${ZEPHYR_TEST_BOARD}"
        return
    fi
    if west boards 2>/dev/null | grep -qx 'native_sim'; then
        echo 'native_sim'
        return
    fi
    echo 'native_posix'
}

BOARD="$(pick_board)"
cd "${TESTS_DIR}"

echo "Mode: ${ZP_MODE}, board: ${BOARD}, CONF_FILE: ${CONF_FILE}, build-dir: ${BUILD_DIR}"
west build -b "${BOARD}" . --build-dir "${ROOT}/${BUILD_DIR}" -p always -- -DCONF_FILE="${CONF_FILE}"
west build -t run --build-dir "${ROOT}/${BUILD_DIR}"
