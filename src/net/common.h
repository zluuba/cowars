#ifndef INCLUDE_COMMON_H
#define INCLUDE_COMMON_H

#include <stdint.h>

int set_nonblocking(int tcp_fd);
const char* epoll_events_to_str(uint32_t ev);

#endif // INCLUDE_COMMON_H
