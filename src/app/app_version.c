/**
 * @file app_version.c
 * @brief Application Version Information Implementation
 *
 * Software version management with build time and git information.
 *
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include "app_version.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(app_version, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * Version Information Storage
 * ============================================================================= */

/* Version structure for easy access */
typedef struct {
    uint8_t     major;
    uint8_t     minor;
    uint8_t     patch;
    uint8_t     reserved;
    const char* git_commit;
    const char* git_branch;
    const char* git_tag;
    const char* build_timestamp;
    const char* build_target;
    const char* build_type;
    const char* compiler_version;
} app_version_info_t;

/* Static version info */
static const app_version_info_t g_version_info = {.major = PROJECT_VERSION_MAJOR,
                                                  .minor = PROJECT_VERSION_MINOR,
                                                  .patch = PROJECT_VERSION_PATCH,
                                                  .reserved = 0,
                                                  .git_commit = GIT_COMMIT_HASH,
                                                  .git_branch = GIT_BRANCH,
                                                  .git_tag = GIT_TAG,
                                                  .build_timestamp = BUILD_TIMESTAMP,
                                                  .build_target = BUILD_TARGET,
                                                  .build_type = BUILD_TYPE,
                                                  .compiler_version = COMPILER_VERSION};

/* =============================================================================
 * Version API Implementation
 * ============================================================================= */

int app_version_get_string(char* buffer, size_t size) {
    if (buffer == NULL || size < VERSION_STRING_MAX_LEN) {
        return -1;
    }

    snprintf(buffer, size, "%d.%d.%d", g_version_info.major, g_version_info.minor, g_version_info.patch);

    return 0;
}

int app_version_get_info_string(char* buffer, size_t size) {
    if (buffer == NULL || size < VERSION_INFO_STRING_MAX_LEN) {
        return -1;
    }

    snprintf(buffer, size, "v%d.%d.%d (%s) [%s] %s - %s", g_version_info.major, g_version_info.minor,
             g_version_info.patch, g_version_info.git_commit, g_version_info.build_type, g_version_info.build_timestamp,
             g_version_info.build_target);

    return 0;
}

uint32_t app_version_get_code(void) {
    return APP_VERSION_CODE;
}

uint8_t app_version_get_major(void) {
    return g_version_info.major;
}

uint8_t app_version_get_minor(void) {
    return g_version_info.minor;
}

uint8_t app_version_get_patch(void) {
    return g_version_info.patch;
}

const char* app_version_get_git_commit(void) {
    return g_version_info.git_commit;
}

const char* app_version_get_git_branch(void) {
    return g_version_info.git_branch;
}

const char* app_version_get_build_timestamp(void) {
    return g_version_info.build_timestamp;
}

const char* app_version_get_build_target(void) {
    return g_version_info.build_target;
}

bool app_version_check(uint8_t major, uint8_t minor, uint8_t patch) {
    return APP_VERSION_CODE == VERSION_ENCODE(major, minor, patch);
}

void app_version_print(void) {
    char version_str[VERSION_STRING_MAX_LEN];
    char info_str[VERSION_INFO_STRING_MAX_LEN];

    app_version_get_string(version_str, sizeof(version_str));
    app_version_get_info_string(info_str, sizeof(info_str));

    LOG_INF("========================================");
    LOG_INF("  Application Version Information");
    LOG_INF("========================================");
    LOG_INF("  Version:     %s", version_str);
    LOG_INF("  Version Code: 0x%06X", APP_VERSION_CODE);
    LOG_INF("  Git Commit:  %s", g_version_info.git_commit);
    LOG_INF("  Git Branch:  %s", g_version_info.git_branch);
    LOG_INF("  Git Tag:     %s", g_version_info.git_tag);
    LOG_INF("  Build Time:  %s", g_version_info.build_timestamp);
    LOG_INF("  Build Target: %s", g_version_info.build_target);
    LOG_INF("  Build Type:  %s", g_version_info.build_type);
    LOG_INF("  Compiler:    %s %s", COMPILER_NAME, g_version_info.compiler_version);
    LOG_INF("========================================");
}

/* =============================================================================
 * Shell Commands
 * ============================================================================= */

#ifdef CONFIG_SHELL

#include <zephyr/shell/shell.h>

static int cmd_version(const struct shell* shell, size_t argc, char** argv) {
    char version_str[VERSION_STRING_MAX_LEN];
    char info_str[VERSION_INFO_STRING_MAX_LEN];

    app_version_get_string(version_str, sizeof(version_str));
    app_version_get_info_string(info_str, sizeof(info_str));

    shell_print(shell, "Version: %s", version_str);
    shell_print(shell, "Info: %s", info_str);
    shell_print(shell, "Git Commit: %s", g_version_info.git_commit);
    shell_print(shell, "Git Branch: %s", g_version_info.git_branch);
    shell_print(shell, "Build Time: %s", g_version_info.build_timestamp);
    shell_print(shell, "Build Target: %s", g_version_info.build_target);
    shell_print(shell, "Build Type: %s", g_version_info.build_type);

    return 0;
}

SHELL_CMD_ARG_REGISTER(version, NULL, "Show version information", cmd_version, 0, 0);

#endif /* CONFIG_SHELL */
