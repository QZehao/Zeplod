#!/usr/bin/env bash
# Run ztest suite with host sanitizers (ASan/UBSan).
# Usage: ./scripts/run_sanitizers.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/setup_env.sh"
python "${ROOT}/scripts/preflight_host_tests.py"

BUILD_DIR="${ZEPHYR_SAN_BUILD_DIR:-build_sanitizers}"
CONF_FILE="${ZEPHYR_TEST_CONF:-prj.conf}"
SANITIZER="${ZEPHYR_SANITIZER:-asan-ubsan}"

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

pick_flags() {
    case "${SANITIZER}" in
        asan)
            echo "-fsanitize=address -fno-omit-frame-pointer -g"
            ;;
        ubsan)
            echo "-fsanitize=undefined -fno-omit-frame-pointer -g"
            ;;
        asan-ubsan)
            echo "-fsanitize=address,undefined -fno-omit-frame-pointer -g"
            ;;
        *)
            echo "Unsupported ZEPHYR_SANITIZER='${SANITIZER}', use asan|ubsan|asan-ubsan" >&2
            exit 2
            ;;
    esac
}

BOARD="$(pick_board)"
SAN_FLAGS="$(pick_flags)"

if [ "$(uname -s)" = "MINGW64_NT" ] || [ "$(uname -s)" = "MINGW32_NT" ] || [ "$(uname -s)" = "MSYS_NT" ]; then
    if [ "${BOARD}" = "native_sim" ] || [ "${BOARD}" = "native_posix" ]; then
        echo "Board '${BOARD}' requires Linux/WSL host for sanitizers." >&2
        exit 2
    fi
fi

cd "${ROOT}/tests"
echo "Board: ${BOARD}, CONF_FILE: ${CONF_FILE}, Sanitizer: ${SANITIZER}, build-dir: ${BUILD_DIR}"
west build -b "${BOARD}" . --build-dir "${ROOT}/${BUILD_DIR}" -p always -- \
    -DCONF_FILE="${CONF_FILE}" \
    -DCMAKE_C_FLAGS="${SAN_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${SAN_FLAGS}"
west build -t run --build-dir "${ROOT}/${BUILD_DIR}"
