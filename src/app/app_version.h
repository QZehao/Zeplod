/**
 * @file app_version.h
 * @brief Application Version Information Header
 *
 * Software version management with build time and git information.
 *
 * @copyright Copyright (c) 2026
 * @par License
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_VERSION_H
#define APP_VERSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Version Information (Auto-generated from CMake)
 * ============================================================================= */

/* Version numbers */
#ifndef PROJECT_VERSION_MAJOR
#define PROJECT_VERSION_MAJOR 1
#endif

#ifndef PROJECT_VERSION_MINOR
#define PROJECT_VERSION_MINOR 0
#endif

#ifndef PROJECT_VERSION_PATCH
#define PROJECT_VERSION_PATCH 0
#endif

/* Version string */
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

/* Git information (auto-generated) */
#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "unknown"
#endif

#ifndef GIT_BRANCH
#define GIT_BRANCH "unknown"
#endif

#ifndef GIT_TAG
#define GIT_TAG "unknown"
#endif

#ifndef GIT_DIRTY
#define GIT_DIRTY 0
#endif

/* Build information */
#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif

#ifndef BUILD_DATE
#define BUILD_DATE __DATE__
#endif

#ifndef BUILD_TIME
#define BUILD_TIME __TIME__
#endif

#ifndef BUILD_TARGET
#define BUILD_TARGET "generic"
#endif

#ifndef BUILD_TYPE
#ifdef NDEBUG
#define BUILD_TYPE "Release"
#else
#define BUILD_TYPE "Debug"
#endif
#endif

/* Compiler information */
#define COMPILER_NAME                         "GCC"
#define COMPILER_VERSION                      __VERSION__

/* =============================================================================
 * Version Encoding Macros
 * ============================================================================= */

#define VERSION_ENCODE(major, minor, patch)   (((major) << 16) | ((minor) << 8) | (patch))

#define VERSION_MAJOR(version)                (((version) >> 16) & 0xFF)

#define VERSION_MINOR(version)                (((version) >> 8) & 0xFF)

#define VERSION_PATCH(version)                ((version) & 0xFF)

/* Current version as encoded value */
#define APP_VERSION_CODE                      VERSION_ENCODE(PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH)

/* =============================================================================
 * Version Comparison Macros
 * ============================================================================= */

#define VERSION_AT_LEAST(major, minor, patch) (APP_VERSION_CODE >= VERSION_ENCODE(major, minor, patch))

#define VERSION_AT_MOST(major, minor, patch)  (APP_VERSION_CODE <= VERSION_ENCODE(major, minor, patch))

#define VERSION_IS(major, minor, patch)       (APP_VERSION_CODE == VERSION_ENCODE(major, minor, patch))

/* =============================================================================
 * Version String Buffer Size
 * ============================================================================= */

#define VERSION_STRING_MAX_LEN                64
#define VERSION_INFO_STRING_MAX_LEN           256

/* =============================================================================
 * Version API
 * ============================================================================= */

/**
 * @brief Get version string
 * @param buffer Output buffer (at least VERSION_STRING_MAX_LEN bytes)
 * @param size Buffer size
 * @return 0 on success, negative error code on failure
 */
int app_version_get_string(char* buffer, size_t size);

/**
 * @brief Get full version information string
 * @param buffer Output buffer (at least VERSION_INFO_STRING_MAX_LEN bytes)
 * @param size Buffer size
 * @return 0 on success, negative error code on failure
 */
int app_version_get_info_string(char* buffer, size_t size);

/**
 * @brief Get encoded version code
 * @return Version code (encoded major.minor.patch)
 */
uint32_t app_version_get_code(void);

/**
 * @brief Get major version
 * @return Major version number
 */
uint8_t app_version_get_major(void);

/**
 * @brief Get minor version
 * @return Minor version number
 */
uint8_t app_version_get_minor(void);

/**
 * @brief Get patch version
 * @return Patch version number
 */
uint8_t app_version_get_patch(void);

/**
 * @brief Get git commit hash
 * @return Git commit hash string
 */
const char* app_version_get_git_commit(void);

/**
 * @brief Get git branch
 * @return Git branch string
 */
const char* app_version_get_git_branch(void);

/**
 * @brief Get build timestamp
 * @return Build timestamp string
 */
const char* app_version_get_build_timestamp(void);

/**
 * @brief Get build target
 * @return Build target string
 */
const char* app_version_get_build_target(void);

/**
 * @brief Check if running version matches expected version
 * @param major Expected major version
 * @param minor Expected minor version
 * @param patch Expected patch version
 * @return true if versions match, false otherwise
 */
bool app_version_check(uint8_t major, uint8_t minor, uint8_t patch);

/**
 * @brief Print version information to log
 */
void app_version_print(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_VERSION_H */
