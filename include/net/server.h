#ifndef INCLUDE_SERVER_H
#define INCLUDE_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>

#include "msg.h"

struct ServerRunningCtx;
struct ServerClient;

// return -1 on critical error, client will be dropped, else return 0
typedef int (*server_msg_cb)(struct ServerRunningCtx *server,
                             struct ServerClient *client,
                             struct TlvMsg *msg,
                             void *cb_ctx);

struct ServerCfg {
    uint16_t port;
    size_t max_clients;
    size_t max_events;
    void *cb_ctx;
    server_msg_cb msg_cbs[MSG_MAX_TAG];
    server_msg_cb msg_default_cb;
};

struct ServerClient {
    uint16_t tag;
    uint16_t len;
    uint8_t in_buff[MSG_MAX_LEN];
    size_t in_buff_len;
    uint8_t out_buff[MSG_MAX_LEN];
    size_t out_buff_len;
    int tcp_fd;
    struct sockaddr_in addr;
};

struct Server {
    struct ServerCfg cfg;
    int tcp_fd;
    int udp_fd;
};

struct ServerRunningCtx {
    struct Server *server;
    int epoll_fd;
    struct ServerClient *clients;
    const size_t max_clients;
};

int server_init(struct ServerCfg *cfg, struct Server *server);
int server_run(struct Server *server);
int server_deinit(struct Server *server);

int server_send_to(struct ServerRunningCtx *server, struct ServerClient *client,
                   struct TlvMsg *msg);
int server_send_to_other(struct ServerRunningCtx *server, struct ServerClient *client,
                         struct TlvMsg *msg);
int server_send_to_each(struct ServerRunningCtx *server, struct TlvMsg *msg);

#endif // INCLUDE_SERVER_H
