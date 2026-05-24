/**
 * @file app_banner.h
 * @brief 启动 Banner ASCII 艺术图案
 *
 * @note 每行建议不超过 80 列，避免串口终端换行错位。
 * @note 使用 \r\n 结尾以确保各种终端正确换行。
 */

#ifndef APP_BANNER_H
#define APP_BANNER_H

#define APP_BANNER_LOGO                                                                                                \
    "\r\n"                                                                                                             \
    "                   .__             .___\r\n"                                                                      \
    "________ ____ ______ |  |   ____   __| _/\r\n"                                                                    \
    "\\___   // __ \\____ \\|  |  /  _ \\ / __ |\r\n"                                                                  \
    " /    /\\  ___/|  |_> >  |_(  <_> ) /_/ |\r\n"                                                                    \
    "/_____ \\___  >   __/|____/\\____/\\____ |\r\n"                                                                   \
    "      \\/    \\/|__|                    \\/\r\n"

#define APP_BANNER_INFO                                                                                                \
    "  %-14s %s\r\n"                                                                                                   \
    "  %-14s %s\r\n"                                                                                                   \
    "  %-14s %s / %s\r\n"                                                                                              \
    "  %-14s %s\r\n"                                                                                                   \
    "  %-14s 0x%06X\r\n"                                                                                               \
    "  %-14s %s\r\n"

#endif /* APP_BANNER_H */
