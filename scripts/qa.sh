#!/usr/bin/env bash
# Unified quality entrypoint.
# Usage: ./scripts/qa.sh [test|san|twister|all]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/project_layout.sh"
initialize_zephyr_project_layout "${SCRIPT_DIR}"

MODE="${1:-all}"

case "${MODE}" in
  test)
    "${ZP_SCRIPTS_ROOT}/run_tests.sh"
    ;;
  san)
    "${ZP_SCRIPTS_ROOT}/run_sanitizers.sh"
    ;;
  twister)
    "${ZP_SCRIPTS_ROOT}/run_twister.sh"
    ;;
  all)
    "${ZP_SCRIPTS_ROOT}/run_tests.sh"
    "${ZP_SCRIPTS_ROOT}/run_sanitizers.sh"
    "${ZP_SCRIPTS_ROOT}/run_twister.sh"
    ;;
  *)
    echo "Usage: ./scripts/qa.sh [test|san|twister|all]" >&2
    exit 2
    ;;
esac
