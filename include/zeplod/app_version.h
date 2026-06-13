/**
 * @file app_version.h
 * @brief 应用版本信息头文件
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-04-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-04-01       1.0            zeh            正式发布
 *
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
 * 版本信息（由 CMake 自动生成）
 * ============================================================================= */

/* 版本号 */
#ifndef PROJECT_VERSION_MAJOR
#define PROJECT_VERSION_MAJOR 1
#endif

#ifndef PROJECT_VERSION_MINOR
#define PROJECT_VERSION_MINOR 0
#endif

#ifndef PROJECT_VERSION_PATCH
#define PROJECT_VERSION_PATCH 0
#endif

/* 版本字符串 */
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

/** 与 PROJECT_VERSION_* / PROJECT_VERSION 同源，供应用与模块统一引用 */
#define APP_VERSION_MAJOR  PROJECT_VERSION_MAJOR
#define APP_VERSION_MINOR  PROJECT_VERSION_MINOR
#define APP_VERSION_PATCH  PROJECT_VERSION_PATCH
#define APP_VERSION_STRING PROJECT_VERSION

/* Git 信息（自动生成）*/
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

/* 构建信息 */
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

/* 编译器信息 */
#define COMPILER_NAME                         "GCC"
#define COMPILER_VERSION                      __VERSION__

/* =============================================================================
 * Version Encoding Macros
 * ============================================================================= */

#define VERSION_ENCODE(major, minor, patch)   (((major) << 16) | ((minor) << 8) | (patch))

#define VERSION_MAJOR(version)                (((version) >> 16) & 0xFF)

#define VERSION_MINOR(version)                (((version) >> 8) & 0xFF)

#define VERSION_PATCH(version)                ((version) & 0xFF)

/* 当前版本编码值 */
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
 * @brief 获取版本字符串
 * @param buffer 输出缓冲区，建议至少 VERSION_STRING_MAX_LEN 字节（小于该值将失败）
 * @param size 缓冲区大小
 * @return APP_OK 成功；参数非法或缓冲区过小为 APP_ERR_INVALID_PARAM（见 app_config.h）
 */
int app_version_get_string(char* buffer, size_t size);

/**
 * @brief 获取完整版本信息字符串
 * @param buffer 输出缓冲区，建议至少 VERSION_INFO_STRING_MAX_LEN 字节
 * @param size 缓冲区大小
 * @return APP_OK 成功；参数非法或缓冲区过小为 APP_ERR_INVALID_PARAM
 */
int app_version_get_info_string(char* buffer, size_t size);

/**
 * @brief 获取编码后的版本号
 * @return 版本编码值（编码后的 major.minor.patch）
 */
uint32_t app_version_get_code(void);

/**
 * @brief 获取主版本号
 * @return 主版本号
 */
uint8_t app_version_get_major(void);

/**
 * @brief 获取次版本号
 * @return 次版本号
 */
uint8_t app_version_get_minor(void);

/**
 * @brief 获取补丁版本号
 * @return 补丁版本号
 */
uint8_t app_version_get_patch(void);

/**
 * @brief 获取 Git 提交哈希
 * @return Git 提交哈希字符串
 */
const char* app_version_get_git_commit(void);

/**
 * @brief 获取 Git 分支
 * @return Git 分支字符串
 */
const char* app_version_get_git_branch(void);

/**
 * @brief 获取构建时间戳
 * @return 构建时间戳字符串
 */
const char* app_version_get_build_timestamp(void);

/**
 * @brief 获取构建目标
 * @return 构建目标字符串
 */
const char* app_version_get_build_target(void);

/**
 * @brief 检查运行版本是否与预期版本匹配
 * @param major 预期主版本号
 * @param minor 预期次版本号
 * @param patch 预期补丁版本号
 * @return 版本匹配返回 true，否则返回 false
 */
bool app_version_check(uint8_t major, uint8_t minor, uint8_t patch);

/**
 * @brief 打印版本信息到日志
 *
 * 默认（CONFIG_APP_BOOT_VERBOSE=n）无输出，由启动 Banner 展示摘要；
 * 打开 CONFIG_APP_BOOT_VERBOSE=y 时输出完整多行信息。
 */
void app_version_print(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_VERSION_H */
