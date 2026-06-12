/**
 * @file zeplod_api_defs.h
 * @brief Zeplod 公共 API 命名空间与版本符号
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-12
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            统一对外头文件入口
 */

#ifndef ZEPLOD_API_DEFS_H
#define ZEPLOD_API_DEFS_H

/**
 * @brief 应用语义化版本（由 CMake target_compile_definitions 注入）
 *
 * 若未定义则回退为 "unknown"，便于 IDE/静态分析解析。
 */
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "unknown"
#endif

#ifndef PROJECT_VERSION_MAJOR
#define PROJECT_VERSION_MAJOR 0
#endif

#ifndef PROJECT_VERSION_MINOR
#define PROJECT_VERSION_MINOR 0
#endif

#ifndef PROJECT_VERSION_PATCH
#define PROJECT_VERSION_PATCH 0
#endif

#endif /* ZEPLOD_API_DEFS_H */
