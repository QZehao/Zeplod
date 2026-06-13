#!/usr/bin/env bash
# Resolve framework-only vs framework+app repository layout.
# Source from other scripts: source "${SCRIPT_DIR}/project_layout.sh"
#
# Exports: ZP_MODE, ZP_APP_ROOT, ZP_FRAMEWORK_ROOT, ZP_WORK_ROOT,
#          ZP_CONFIG_FILE, ZP_TESTS_DIR, ZP_SCRIPTS_ROOT

_zp_trim() {
    local s="$1"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

_zp_read_kv_file() {
    local file="$1"
    local line key value
    if [ ! -f "$file" ]; then
        return 0
    fi
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line%$'\r'}"
        case "$line" in
            ''|'#'*)
                continue
                ;;
            *=*)
                key="$(_zp_trim "${line%%=*}")"
                value="$(_zp_trim "${line#*=}")"
                if [ -n "$key" ]; then
                    printf -v "ZP_MANIFEST_${key}" '%s' "$value"
                    export "ZP_MANIFEST_${key}"
                fi
                ;;
        esac
    done < "$file"
}

_zp_is_app_wrapper_cmake() {
    local cmake_file="$1"
    if [ ! -f "$cmake_file" ]; then
        return 1
    fi
    if grep -Eq 'add_subdirectory[[:space:]]*\([[:space:]]*framework\b' "$cmake_file"; then
        return 0
    fi
    if grep -Eq 'ZEPHYR_[A-Z0-9_]*_TOPLEVEL_BOOTSTRAP' "$cmake_file"; then
        return 0
    fi
    return 1
}

_zp_get_app_prj_conf() {
    local app_root="$1"
    local manifest_file="${app_root}/zephyr_app.env"
    local candidate count=0 picked=""

    unset ZP_MANIFEST_APP_PRJ_CONF ZP_MANIFEST_QEMU_CONF
    _zp_read_kv_file "$manifest_file"

    if [ -n "${ZP_MANIFEST_APP_PRJ_CONF:-}" ]; then
        printf '%s' "${ZP_MANIFEST_APP_PRJ_CONF}"
        return 0
    fi

    shopt -s nullglob
    for candidate in "${app_root}"/*_prj.conf; do
        picked="$(basename "$candidate")"
        count=$((count + 1))
    done
    shopt -u nullglob

    if [ "$count" -eq 1 ]; then
        printf '%s' "$picked"
        return 0
    fi
    if [ "$count" -gt 1 ] && [ -f "${app_root}/CMakeLists.txt" ]; then
        local project_name
        project_name="$(sed -n 's/.*project[[:space:]]*([[:space:]]*\([A-Za-z0-9_][A-Za-z0-9_]*\).*/\1/p' "${app_root}/CMakeLists.txt" | head -n1 | tr '[:upper:]' '[:lower:]')"
        if [ -n "$project_name" ]; then
            shopt -s nullglob
            for candidate in "${app_root}"/*_prj.conf; do
                local base
                base="$(basename "${candidate%.conf}")"
                base="${base,,}"
                if [[ "$base" == *"$project_name"* ]]; then
                    printf '%s' "$(basename "$candidate")"
                    shopt -u nullglob
                    return 0
                fi
            done
            shopt -u nullglob
        fi
        printf '%s' "$picked"
        return 0
    fi
    return 1
}

initialize_zephyr_project_layout() {
    local scripts_dir="${1:-${SCRIPT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}}"
    local target="${2:-auto}"

    if [ -n "${ZEPHYR_PROJECT_TARGET:-}" ]; then
        target="${ZEPHYR_PROJECT_TARGET}"
    fi

    ZP_SCRIPTS_ROOT="$(cd "$scripts_dir" && pwd)"
    ZP_FRAMEWORK_ROOT="$(cd "${ZP_SCRIPTS_ROOT}/.." && pwd)"

    local parent_root framework_prj framework_cmake parent_cmake
    parent_root="$(cd "${ZP_FRAMEWORK_ROOT}/.." && pwd)"
    framework_prj="${ZP_FRAMEWORK_ROOT}/prj.conf"
    framework_cmake="${ZP_FRAMEWORK_ROOT}/CMakeLists.txt"
    parent_cmake="${parent_root}/CMakeLists.txt"

    ZP_MODE="framework"
    ZP_APP_ROOT="${ZP_FRAMEWORK_ROOT}"
    ZP_WORK_ROOT="${ZP_FRAMEWORK_ROOT}"

    case "$target" in
        app)
            if [ ! -f "$parent_cmake" ]; then
                echo "Target=app but no app wrapper found at ${parent_root}" >&2
                return 1
            fi
            ZP_MODE="app"
            ZP_APP_ROOT="${parent_root}"
            ZP_WORK_ROOT="${parent_root}"
            ;;
        framework)
            ;;
        auto|*)
            if [ -f "$framework_prj" ] && [ -f "$framework_cmake" ] && [ -f "$parent_cmake" ] &&
                _zp_is_app_wrapper_cmake "$parent_cmake"; then
                ZP_MODE="app"
                ZP_APP_ROOT="${parent_root}"
                ZP_WORK_ROOT="${parent_root}"
            fi
            ;;
    esac

    if [ -n "${ZEPHYR_APP_ROOT:-}" ]; then
        ZP_APP_ROOT="$(cd "${ZEPHYR_APP_ROOT}" && pwd)"
        if [ "$ZP_MODE" = "app" ]; then
            ZP_WORK_ROOT="${ZP_APP_ROOT}"
        fi
    fi

    ZP_CONFIG_FILE="${ZP_FRAMEWORK_ROOT}/zephyr_config.env"
    ZP_TESTS_DIR="${ZP_FRAMEWORK_ROOT}/tests"

    export ZP_MODE ZP_APP_ROOT ZP_FRAMEWORK_ROOT ZP_WORK_ROOT
    export ZP_CONFIG_FILE ZP_TESTS_DIR ZP_SCRIPTS_ROOT
    export ZEPHYR_PROJECT_MODE="${ZP_MODE}"
    export ZEPHYR_FRAMEWORK_ROOT="${ZP_FRAMEWORK_ROOT}"
    export ZEPHYR_APP_ROOT="${ZP_APP_ROOT}"
    export ZEPHYR_WORK_ROOT="${ZP_WORK_ROOT}"
}

_zp_get_app_prj_qemu_conf() {
    local app_root="$1"
    local app_prj="$2"
    local manifest_file="${app_root}/zephyr_app.env"
    local candidate count=0 picked=""

    _zp_read_kv_file "$manifest_file"
    if [ -n "${ZP_MANIFEST_APP_PRJ_QEMU_CONF:-}" ]; then
        printf '%s' "${ZP_MANIFEST_APP_PRJ_QEMU_CONF}"
        return 0
    fi

    if [ -n "$app_prj" ] && [[ "$app_prj" == *_prj.conf ]]; then
        candidate="${app_prj%_prj.conf}_prj_qemu.conf"
        if [ -f "${app_root}/${candidate}" ]; then
            printf '%s' "$candidate"
            return 0
        fi
    fi

    shopt -s nullglob
    for candidate in "${app_root}"/*_prj_qemu.conf; do
        picked="$(basename "$candidate")"
        count=$((count + 1))
    done
    shopt -u nullglob
    if [ "$count" -eq 1 ]; then
        printf '%s' "$picked"
        return 0
    fi
    return 1
}

get_zephyr_qemu_conf_file() {
    local app_prj="" app_prj_qemu="" parts="framework/prj.conf"

    if [ -n "${ZEPHYR_QEMU_CONF:-}" ]; then
        printf '%s' "${ZEPHYR_QEMU_CONF}"
        return 0
    fi

    _zp_read_kv_file "${ZP_APP_ROOT}/zephyr_app.env"
    if [ -n "${ZP_MANIFEST_QEMU_CONF:-}" ]; then
        printf '%s' "${ZP_MANIFEST_QEMU_CONF}"
        return 0
    fi

    if [ "$ZP_MODE" = "framework" ]; then
        printf '%s' "prj.conf;conf/profiles/standard.conf;conf/features/data_bus.conf;conf/features/thread_ipc.conf;conf/targets/qemu.conf"
        return 0
    fi

    if app_prj="$(_zp_get_app_prj_conf "${ZP_APP_ROOT}")"; then
        parts="${parts};${app_prj}"
    fi
    if app_prj_qemu="$(_zp_get_app_prj_qemu_conf "${ZP_APP_ROOT}" "$app_prj")"; then
        parts="${parts};${app_prj_qemu}"
    fi
    if [ -f "${ZP_APP_ROOT}/framework/conf/targets/qemu.conf" ]; then
        parts="${parts};framework/conf/targets/qemu.conf"
    fi
    printf '%s' "$parts"
    return 0
}

get_zephyr_package_name() {
    local cmake_file="${ZP_APP_ROOT}/CMakeLists.txt"
    if [ -f "$cmake_file" ]; then
        local name
        name="$(sed -n 's/.*project[[:space:]]*([[:space:]]*\([A-Za-z0-9_][A-Za-z0-9_]*\).*/\1/p' "$cmake_file" | head -n1 | tr '[:upper:]' '[:lower:]')"
        if [ -n "$name" ]; then
            printf '%s' "$name"
            return 0
        fi
    fi
    basename "${ZP_FRAMEWORK_ROOT}"
}

get_zephyr_app_version_file() {
    if [ -f "${ZP_APP_ROOT}/APP_VERSION" ]; then
        printf '%s' "${ZP_APP_ROOT}/APP_VERSION"
        return 0
    fi
    if [ -f "${ZP_FRAMEWORK_ROOT}/APP_VERSION" ]; then
        printf '%s' "${ZP_FRAMEWORK_ROOT}/APP_VERSION"
        return 0
    fi
    return 1
}

write_zephyr_project_banner() {
    echo "Project mode: ${ZP_MODE}"
    echo "Framework:    ${ZP_FRAMEWORK_ROOT}"
    if [ "$ZP_MODE" = "app" ]; then
        echo "App root:     ${ZP_APP_ROOT}"
    fi
    echo "Work root:    ${ZP_WORK_ROOT}"
}

# Firmware printk/LOG use UTF-8; ensure locale is UTF-8 when piping QEMU stdio.
# Set ZEPHYR_CONSOLE_UTF8=0 to skip.
set_zephyr_console_utf8() {
    if [ "${ZEPHYR_CONSOLE_UTF8:-1}" = "0" ]; then
        return 0
    fi
    if [ -z "${LANG:-}" ]; then
        export LANG=C.UTF-8
    fi
    if [ -z "${LC_ALL:-}" ]; then
        export LC_ALL="${LANG}"
    fi
}
