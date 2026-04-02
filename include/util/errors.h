#ifndef ERRORS_H
#define ERRORS_H

#include "log.h"

// do code_block and log error and return code if
#define rcci(cond, code_block, code, fmt, ...) \
    do { if (cond) { code_block; loge("Failed to "fmt, ##__VA_ARGS__); return code; } } while(0)

// do code_block and log error and return error code if
#define reci(cond, code_block, fmt, ...) rcci(cond, code_block, -1, fmt, ##__VA_ARGS__)

// log error and return error code if
#define rei(cond, fmt, ...) reci(cond, {}, fmt, ##__VA_ARGS__)

// log error and return code if
#define rci(cond, code, fmt, ...) rcci(cond, {}, code, fmt, ##__VA_ARGS__)

#define __SEGFAULT // *((int*)NULL)=0
#define die(cond, fmt, ...) \
    do { if (cond) { loge("Failed to "fmt, ##__VA_ARGS__); __SEGFAULT; exit (-1); } } while(0)

#endif // ERRORS_H
