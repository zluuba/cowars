#ifndef INCLUDE_CLIENT_H
#define INCLUDE_CLIENT_H

#include <stdint.h>
#include <netinet/in.h>

#include "msg.h"

struct Client;

// return -1 on critical error, client will disconnected, else return 0
typedef int (*client_msg_cb)(struct Client *client,
                             struct TlvMsg *msg,
                             void *cb_ctx);

struct ClientCfg {
    uint16_t server_port;
    uint8_t server_ip[4];
    void *cb_ctx;
    client_msg_cb msg_cbs[MSG_MAX_TAG];
    client_msg_cb msg_default_cb;
};

struct Client {
    struct ClientCfg cfg;
    int tcp_fd;
    int udp_fd;
    struct sockaddr_in serv_addr;
    uint16_t tag;
    uint16_t len;
    uint8_t in_buff[MSG_MAX_LEN];
    size_t in_buff_len;
    uint8_t out_buff[MSG_MAX_LEN];
    size_t out_buff_len;
};

int client_init(struct ClientCfg *cfg, struct Client *client);
int client_update(struct Client *client);
int client_send(struct Client *client, const struct TlvMsg *msg);
int client_deinit(struct Client *client);

#endif // INCLUDE_CLIENT_H
