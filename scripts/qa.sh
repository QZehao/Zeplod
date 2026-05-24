#!/usr/bin/env bash
# Unified quality entrypoint.
# Usage: ./scripts/qa.sh [test|san|twister|all]
set -euo pipefail

MODE="${1:-all}"

case "${MODE}" in
  test)
    ./scripts/run_tests.sh
    ;;
  san)
    ./scripts/run_sanitizers.sh
    ;;
  twister)
    ./scripts/run_twister.sh
    ;;
  all)
    ./scripts/run_tests.sh
    ./scripts/run_sanitizers.sh
    ./scripts/run_twister.sh
    ;;
  *)
    echo "Usage: ./scripts/qa.sh [test|san|twister|all]" >&2
    exit 2
    ;;
esac
