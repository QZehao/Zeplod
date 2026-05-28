#!/usr/bin/env bash
# Run ztest with IPC + larger heap (prj.conf + prj_native_sim overlay).
set -euo pipefail

export ZEPHYR_TEST_CONF="prj.conf;prj_native_sim.conf"
export ZEPHYR_TEST_BUILD_DIR="${ZEPHYR_TEST_BUILD_DIR:-build_tests_ipc}"

exec "$(dirname "${BASH_SOURCE[0]}")/run_tests.sh" "$@"
