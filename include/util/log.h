#ifndef LOG_H
#define LOG_H

#include "stdio.h"

#define COLOR_RESET   "\x1b[0m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"

#define DBG 0
#define INFO 1
#define WARN 2
#define ERR 3
#define DISABLE 4

#ifndef LOG_LVL
#define LOG_LVL INFO
#endif

#if 0
#define generic_log(lvl, fmt, ...) \
    (void)fprintf(stderr, "%s:%d %s() "lvl": "fmt"\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define generic_log(lvl, fmt, ...) \
    (void)fprintf(stderr, lvl": "fmt"\n", ##__VA_ARGS__)
#endif

#if DBG >= LOG_LVL
#define logenter() logd("Enter")
#else
#define logenter(...)
#endif

#if DBG >= LOG_LVL
#define logd(...) generic_log(COLOR_GREEN"[Dbg]"COLOR_RESET, ##__VA_ARGS__)
#else
#define logd(...)
#endif

#if INFO >= LOG_LVL
#define logi(...) generic_log(COLOR_BLUE"[INF]"COLOR_RESET, ##__VA_ARGS__)
#else
#define logi(...)
#endif

#if WARN >= LOG_LVL
#define logw(...) generic_log(COLOR_YELLOW"[WRN]"COLOR_RESET, ##__VA_ARGS__)
#else
#define logw(...)
#endif

#if ERR >= LOG_LVL
#define loge(...) generic_log(COLOR_RED"[ERR]"COLOR_RESET, ##__VA_ARGS__)
#else
#define loge(...)
#endif

#endif // LOG_H
