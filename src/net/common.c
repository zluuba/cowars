#include "common.h"
#include "log.h"

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>

int set_nonblocking(int fd) {
    logenter();
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

const char* epoll_events_to_str(uint32_t ev) {
    static char buf[256];
    buf[0] = '\0';

    if (ev & EPOLLIN)    strcat(buf, "EPOLLIN|");
    if (ev & EPOLLOUT)   strcat(buf, "EPOLLOUT|");
    if (ev & EPOLLERR)   strcat(buf, "EPOLLERR|");
    if (ev & EPOLLHUP)   strcat(buf, "EPOLLHUP|");
    if (ev & EPOLLRDHUP) strcat(buf, "EPOLLRDHUP|");
    if (ev & EPOLLET)    strcat(buf, "EPOLLET|");
    if (ev & EPOLLPRI)   strcat(buf, "EPOLLPRI|");

    size_t len = strlen(buf);
    if (len > 0) {
        buf[len - 1] = '\0';
    }
    return buf;
}
