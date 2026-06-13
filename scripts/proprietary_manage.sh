#!/bin/bash
# =============================================================================
# Proprietary module manager (stub)
# =============================================================================
# Usage: scripts/proprietary_manage.sh <status|enable|disable|enable-all|disable-all|check>
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROPRIETARY_DIR="${FRAMEWORK_ROOT}/src/proprietary"

KNOWN_MODULES=(
    mesh_communication
    module_manager_pro
    ota_manager
    security_crypto
    cellular_5g_usb
    usb_host_cdc_ecm
)

usage() {
    echo "Usage: proprietary_manage.sh <command> [module]"
    echo ""
    echo "Commands:"
    echo "  status              Show proprietary module availability"
    echo "  enable  <module>    Enable a proprietary module (requires files in src/proprietary/)"
    echo "  disable <module>    Disable a proprietary module"
    echo "  enable-all          Enable all known proprietary modules"
    echo "  disable-all         Disable all proprietary modules"
    echo "  check               Verify Kconfig / CMake consistency"
    echo "  help                Show this help"
    echo ""
    echo "Proprietary modules are not included in the open-source repository."
    echo "Contact china_qzh@163.com for commercial licensing."
}

available_modules() {
    if [ ! -d "${PROPRIETARY_DIR}" ]; then
        return
    fi
    find "${PROPRIETARY_DIR}" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort
}

command="${1:-status}"
module="${2:-}"

case "${command}" in
    help|--help|-h)
        usage
        exit 0
        ;;
    status)
        echo "Proprietary module status"
        echo "========================="
        mapfile -t available < <(available_modules)
        if [ ${#available[@]} -eq 0 ]; then
            echo "No proprietary modules present in src/proprietary/."
            echo "Contact china_qzh@163.com for commercial licensing."
        else
            for m in "${KNOWN_MODULES[@]}"; do
                if printf '%s\n' "${available[@]}" | grep -qx "${m}"; then
                    printf '  %-25s %s\n' "${m}" "AVAILABLE"
                else
                    printf '  %-25s %s\n' "${m}" "NOT PRESENT"
                fi
            done
        fi
        exit 0
        ;;
    check)
        if [ ! -d "${PROPRIETARY_DIR}" ]; then
            echo "check: src/proprietary/ does not exist; nothing to verify."
            exit 0
        fi
        mapfile -t available < <(available_modules)
        echo "check: found ${#available[@]} proprietary module(s): ${available[*]}"
        exit 0
        ;;
    enable)
        if [ -z "${module}" ]; then
            echo "enable: module name required" >&2
            exit 1
        fi
        mapfile -t available < <(available_modules)
        if [ ! -d "${PROPRIETARY_DIR}" ] || ! printf '%s\n' "${available[@]}" | grep -qx "${module}"; then
            echo "enable: '${module}' is not available. Proprietary modules require commercial licensing." >&2
            exit 1
        fi
        echo "enable: '${module}' is present but CMake/Kconfig integration must be configured manually."
        exit 0
        ;;
    disable)
        if [ -z "${module}" ]; then
            echo "disable: module name required" >&2
            exit 1
        fi
        echo "disable: '${module}' disabled (no proprietary modules are enabled by default in this repository)."
        exit 0
        ;;
    enable-all)
        mapfile -t available < <(available_modules)
        if [ ${#available[@]} -eq 0 ]; then
            echo "enable-all: no proprietary modules present."
            exit 0
        fi
        echo "enable-all: found ${#available[@]} module(s): ${available[*]}"
        echo "enable-all: CMake/Kconfig integration must be configured manually."
        exit 0
        ;;
    disable-all)
        echo "disable-all: all proprietary modules are now disabled."
        exit 0
        ;;
    *)
        echo "Unknown command: ${command}" >&2
        usage >&2
        exit 1
        ;;
esac
