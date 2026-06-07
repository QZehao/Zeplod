#!/usr/bin/env bash
# =============================================================================
# Proprietary Modules Management Script (Bash)
# =============================================================================
# Usage:
#   ./proprietary_manage.sh enable          # Enable proprietary modules
#   ./proprietary_manage.sh disable         # Disable proprietary modules
#   ./proprietary_manage.sh status          # Show status
#   ./proprietary_manage.sh update          # Update proprietary modules
# =============================================================================

set -e

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Output functions
info() { echo -e "${CYAN}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# Project directories (proprietary modules live under framework root)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/project_layout.sh"
initialize_zephyr_project_layout "${SCRIPT_DIR}"

PROJECT_ROOT="${ZP_FRAMEWORK_ROOT}"
PROPRIETARY_DIR="$PROJECT_ROOT/src/proprietary"
PROPRIETARY_COMMON_CMAKE="$PROPRIETARY_DIR/proprietary_modules_common.cmake"
CONFIG_FILE="$PROJECT_ROOT/proprietary_modules.conf"
LOCAL_CONFIG_FILE="$PROJECT_ROOT/proprietary_modules.local.conf"

# Module configuration cache
declare -A MODULE_CONFIG

show_help() {
    cat << EOF
Proprietary Modules Management Script

Usage:
    ./proprietary_manage.sh enable      Enable proprietary modules (init submodule)
    ./proprietary_manage.sh disable     Disable proprietary modules (remove submodule content)
    ./proprietary_manage.sh status      Show proprietary modules status
    ./proprietary_manage.sh update      Update proprietary modules to latest version
    ./proprietary_manage.sh help        Show this help

Configuration:
    Create 'proprietary_modules.local.conf' to customize module URLs.
    Format: MODULE_NAME=REPOSITORY_URL

    Example:
        MESH_COMMUNICATION_URL=https://gitee.com/your-repo/mesh_communication.git

Description:
    The proprietary modules directory (src/proprietary) is a Git submodule.
    - Enabled: Can configure and use proprietary module features during build
    - Disabled: Project can still compile normally without proprietary modules

EOF
}

check_git() {
    if ! command -v git &> /dev/null; then
        error "Git is not installed or not in PATH"
    fi
}

is_git_repo() {
    check_git
    git -C "$PROJECT_ROOT" rev-parse --git-dir &> /dev/null
}

get_submodule_status() {
    check_git
    git -C "$PROJECT_ROOT" submodule status -- "src/proprietary" 2>/dev/null || echo ""
}

read_module_config() {
    # Read from default config file
    if [[ -f "$CONFIG_FILE" ]]; then
        while IFS= read -r line || [[ -n "$line" ]]; do
            line=$(echo "$line" | xargs)
            if [[ -n "$line" && ! "$line" =~ ^# && "$line" == *=* ]]; then
                key="${line%%=*}"
                value="${line#*=}"
                if [[ -n "$value" ]]; then
                    MODULE_CONFIG["$key"]="$value"
                fi
            fi
        done < "$CONFIG_FILE"
    fi

    # Read from local config file (overrides default)
    if [[ -f "$LOCAL_CONFIG_FILE" ]]; then
        while IFS= read -r line || [[ -n "$line" ]]; do
            line=$(echo "$line" | xargs)
            if [[ -n "$line" && ! "$line" =~ ^# && "$line" == *=* ]]; then
                key="${line%%=*}"
                value="${line#*=}"
                if [[ -n "$value" ]]; then
                    MODULE_CONFIG["$key"]="$value"
                fi
            fi
        done < "$LOCAL_CONFIG_FILE"
    fi

    # Read from environment variables (highest priority)
    while IFS='=' read -r name value; do
        if [[ "$name" == PROPRIETARY_MODULE_*_URL ]]; then
            MODULE_CONFIG["$name"]="$value"
        fi
    done < <(env)
}

get_module_url() {
    local module_name="$1"
    local env_name="PROPRIETARY_MODULE_${module_name}_URL"
    local config_name="${module_name}_URL"

    # Check environment variable first
    if [[ -n "${!env_name}" ]]; then
        echo "${!env_name}"
        return
    fi

    # Check config
    if [[ -n "${MODULE_CONFIG[$config_name]}" ]]; then
        echo "${MODULE_CONFIG[$config_name]}"
        return
    fi

    echo ""
}

enable_proprietary() {
    info "Enabling proprietary modules..."

    if ! is_git_repo; then
        error "Current directory is not a Git repository"
    fi

    # Read configuration
    read_module_config

    cd "$PROJECT_ROOT"

    # Check if submodule is configured
    if ! git config --file .gitmodules --get "submodule.src/proprietary.url" &> /dev/null; then
        error "Proprietary modules submodule not configured, check .gitmodules file"
    fi

    # Initialize main submodule
    info "Initializing main submodule..."
    if git submodule update --init -- "src/proprietary" 2>/dev/null; then
        success "Main submodule initialized"
    else
        error "Main submodule initialization failed"
    fi

    # Check if we need to configure individual modules
    if [[ -f "$PROPRIETARY_DIR/.gitmodules" ]]; then
        info "Configuring individual modules..."
        cd "$PROPRIETARY_DIR"

        configured_count=0
        skipped_count=0

        # Get list of modules from .gitmodules
        while IFS= read -r line; do
            module_name=$(echo "$line" | awk '{print $2}')
            if [[ -n "$module_name" ]]; then
                module_path="$PROPRIETARY_DIR/$module_name"
                url=$(get_module_url "$module_name")

                if [[ -n "$url" ]]; then
                    printf "  Configuring: %s" "$module_name"
                    git config --file .gitmodules "submodule.$module_name.url" "$url" 2>/dev/null

                    # Clean up old submodule directory if it exists but is broken
                    rm -rf "$module_path" 2>/dev/null

                    git submodule sync -- "$module_name" 2>/dev/null
                    if git submodule update --init -- "$module_name" 2>/dev/null; then
                        echo -e " ${GREEN}[OK]${NC}"
                        ((configured_count++))
                    else
                        echo -e " ${RED}[FAILED]${NC}"
                        warn "  Could not initialize $module_name from $url"
                    fi
                else
                    echo -e "  Skipping: $module_name ${YELLOW}(no URL configured)${NC}"
                    ((skipped_count++))
                fi
            fi
        done < <(git config --file .gitmodules --get-regexp path 2>/dev/null)

        echo ""
        info "Configured: $configured_count, Skipped: $skipped_count"
    fi

    # Verify
    if [[ -f "$PROPRIETARY_COMMON_CMAKE" ]]; then
        success "Proprietary modules enabled"
        info "Directory: $PROPRIETARY_DIR"

        # List available modules
        echo ""
        info "Available modules:"
        find "$PROPRIETARY_DIR" -maxdepth 2 -name "Kconfig" -exec dirname {} \; | while read -r module_dir; do
            module_name=$(basename "$module_dir")
            echo "  - $module_name"
        done
    else
        warn "Proprietary modules directory exists but files are incomplete"
    fi
}

disable_proprietary() {
    info "Disabling proprietary modules..."

    if ! is_git_repo; then
        error "Current directory is not a Git repository"
    fi

    cd "$PROJECT_ROOT"

    # Check if directory exists
    if [[ ! -d "$PROPRIETARY_DIR" ]]; then
        warn "Proprietary modules directory does not exist, nothing to disable"
        return 0
    fi

    # Check for uncommitted changes
    if [[ -d "$PROPRIETARY_DIR/.git" ]]; then
        cd "$PROPRIETARY_DIR"
        status=$(git status --porcelain 2>/dev/null || echo "")
        if [[ -n "$status" ]]; then
            warn "Proprietary modules directory has uncommitted changes:"
            echo "$status"
            read -p "Continue with disable? (y/N) " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                info "Operation cancelled"
                return 0
            fi
        fi
        cd "$PROJECT_ROOT"
    fi

    # Remove submodule content
    info "Removing proprietary modules content..."
    if git submodule deinit -f -- "src/proprietary" 2>/dev/null; then
        success "Proprietary modules disabled"
        info "Project can now compile without proprietary modules"
        info "To re-enable, run: ./proprietary_manage.sh enable"
    else
        # Fallback: manual removal
        warn "Using fallback removal method..."
        rm -rf "$PROPRIETARY_DIR"
        success "Proprietary modules disabled"
    fi
}

show_status() {
    info "Proprietary modules status:"
    echo ""

    # Check if directory exists
    if [[ ! -d "$PROPRIETARY_DIR" ]]; then
        warn "Proprietary modules directory does not exist (src/proprietary)"
        info "Status: Not enabled"
        info "Run './proprietary_manage.sh enable' to enable"
        return 0
    fi

    # Check if directory is empty (deinit leaves empty dir)
    if [[ -z "$(ls -A "$PROPRIETARY_DIR" 2>/dev/null)" ]]; then
        warn "Proprietary modules directory is empty (src/proprietary)"
        info "Status: Not enabled"
        info "Run './proprietary_manage.sh enable' to enable"
        return 0
    fi

    # Check if key files exist
    if [[ ! -f "$PROPRIETARY_COMMON_CMAKE" ]]; then
        warn "Proprietary modules directory exists but content is incomplete"
        info "Status: Needs update"
        info "Run './proprietary_manage.sh update' to fix"
        return 0
    fi

    success "Framework: Enabled"
    info "Directory: $PROPRIETARY_DIR"
    echo ""

    # Get submodule status
    submodule_status=$(get_submodule_status)
    if [[ -n "$submodule_status" ]]; then
        status_char="${submodule_status:0:1}"
        case "$status_char" in
            ' ') info "Git status: Initialized" ;;
            '-') warn "Git status: Not initialized" ;;
            '+') warn "Git status: Updates available" ;;
            *) info "Git status: $submodule_status" ;;
        esac
    fi

    echo ""
    info "Available modules:"
    echo ""

    module_count=$(find "$PROPRIETARY_DIR" -maxdepth 2 -name "Kconfig" 2>/dev/null | wc -l)
    if [[ "$module_count" -gt 0 ]]; then
        find "$PROPRIETARY_DIR" -maxdepth 2 -name "Kconfig" -exec dirname {} \; | sort | while read -r module_dir; do
            module_name=$(basename "$module_dir")
            is_submodule=""

            if [[ -d "$module_dir/.git" ]]; then
                is_submodule=" [submodule]"
            fi

            echo "  $module_name$is_submodule"

            # Try to read module description
            kconfig_file="$module_dir/Kconfig"
            if [[ -f "$kconfig_file" ]]; then
                desc=$(grep -m1 'menu "' "$kconfig_file" 2>/dev/null | sed 's/menu "\([^"]*\)"/\1/' || true)
                if [[ -n "$desc" ]]; then
                    echo "    Description: $desc"
                fi
            fi
        done
    else
        warn "  No modules available (submodules not initialized)"
        info "  Configure URLs in proprietary_modules.local.conf and run update"
    fi

    # Show configuration status
    echo ""
    info "Configuration:"
    if [[ -f "$LOCAL_CONFIG_FILE" ]]; then
        success "  Local config found: proprietary_modules.local.conf"
    elif [[ -f "$CONFIG_FILE" ]]; then
        info "  Using default config: proprietary_modules.conf"
        info "  Create proprietary_modules.local.conf to customize URLs"
    fi
}

update_proprietary() {
    info "Updating proprietary modules..."

    if ! is_git_repo; then
        error "Current directory is not a Git repository"
    fi

    # Read configuration
    read_module_config

    cd "$PROJECT_ROOT"
    local has_errors=0

    # Update main submodule
    info "Updating main proprietary module..."
    if git submodule update --init -- "src/proprietary" 2>/dev/null; then
        success "Main module updated"
    else
        warn "Main module update had issues, continuing..."
    fi

    # Update internal submodules one by one for better error handling
    if [[ -f "$PROPRIETARY_DIR/.gitmodules" ]]; then
        info "Updating internal submodules..."
        cd "$PROPRIETARY_DIR"

        while IFS= read -r line; do
            submod=$(echo "$line" | awk '{print $2}')
            if [[ -n "$submod" ]]; then
                submod_path="$PROPRIETARY_DIR/$submod"
                url=$(get_module_url "$submod")

                printf "  Checking: %s" "$submod"

                if [[ -n "$url" ]]; then
                    git config --file .gitmodules "submodule.$submod.url" "$url" 2>/dev/null
                    git submodule sync -- "$submod" 2>/dev/null
                fi

                if [[ -d "$submod" ]]; then
                    if git submodule update --init -- "$submod" 2>/dev/null; then
                        echo -e " ${GREEN}[OK]${NC}"
                    else
                        echo -e " ${RED}[FAILED]${NC}"
                        if [[ -n "$url" ]]; then
                            warn "  Error updating $submod from $url"
                        else
                            warn "  No URL configured for $submod"
                        fi
                        has_errors=1
                    fi
                fi
            fi
        done < <(git config --file .gitmodules --get-regexp path 2>/dev/null)
    fi

    if [[ $has_errors -eq 1 ]]; then
        echo ""
        warn "Some submodules failed to update."
        warn "Configure URLs in proprietary_modules.local.conf"
    else
        success "Proprietary modules updated successfully"
    fi
}

# Main logic
case "${1:-help}" in
    enable)
        enable_proprietary
        ;;
    disable)
        disable_proprietary
        ;;
    status)
        show_status
        ;;
    update)
        update_proprietary
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        error "Unknown command: $1\nRun './proprietary_manage.sh help' for usage"
        ;;
esac
