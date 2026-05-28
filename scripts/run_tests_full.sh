#!/usr/bin/env bash
# Run ztest with IPC overlay + example modules (CI full matrix locally).
set -euo pipefail

export ZEPHYR_TEST_CONF="prj.conf;prj_native_sim.conf;prj_ci_examples.conf"
export ZEPHYR_TEST_BUILD_DIR="${ZEPHYR_TEST_BUILD_DIR:-build_tests_full}"

exec "$(dirname "${BASH_SOURCE[0]}")/run_tests.sh" "$@"
