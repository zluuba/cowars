#define LOG_LVL INFO
#include "util.h"
#include "client.h"
#include "common.h"

#include <stdint.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>

int client_send_via_tpc(struct Client *client, const struct TlvMsg *msg);
int client_send_via_udp(struct Client *client, const struct TlvMsg *msg);

int client_update_recv_tcp(struct Client *client);
int client_update_recv_udp(struct Client *client);
int client_update_send(struct Client *client);

int client_init(struct ClientCfg *cfg, struct Client *client) {
    logenter();
    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    rei(tcp_fd < 0, "create tcp socket: %s", strerror(errno));
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(cfg->server_port),
    };
    memcpy(&serv_addr.sin_addr, cfg->server_ip, sizeof(cfg->server_ip));
    reci(connect(tcp_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)),
         {close(tcp_fd);},
         "connect to %d.%d.%d.%d:%d %s", cfg->server_ip[0], cfg->server_ip[1],
         cfg->server_ip[2], cfg->server_ip[3], cfg->server_port, strerror(errno));
    reci(set_nonblocking(tcp_fd), {close(tcp_fd);}, "set nonblocking mode");

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    reci(udp_fd < 0, {close(tcp_fd); close(udp_fd);}, "create udp socket: %s", strerror(errno));
    struct sockaddr_in local_addr;
    socklen_t local_addr_len = sizeof(local_addr);
    reci(getsockname(tcp_fd, (struct sockaddr *)&local_addr, &local_addr_len),
         {close(tcp_fd); close(udp_fd);}, "get tcp socket addres");
    reci(bind(udp_fd, (struct sockaddr *)&local_addr, local_addr_len),
         {close(tcp_fd); close(udp_fd);},
         "bind udp socket to port: %d: %s", local_addr.sin_port, strerror(errno));
    reci(connect(udp_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)),
         {close(tcp_fd); close(udp_fd);},
         "connect to %d.%d.%d.%d:%d %s", cfg->server_ip[0], cfg->server_ip[1],
         cfg->server_ip[2], cfg->server_ip[3], cfg->server_port, strerror(errno));
    reci(set_nonblocking(udp_fd), {close(tcp_fd); close(udp_fd);}, "set nonblocking mode");

    logi("Client connected to %d.%d.%d.%d:%d", cfg->server_ip[0], cfg->server_ip[1],
         cfg->server_ip[2], cfg->server_ip[3], cfg->server_port);
    memset(client, 0, sizeof(struct Client));
    client->cfg = *cfg;
    client->tcp_fd = tcp_fd;
    client->udp_fd = udp_fd;
    client->serv_addr = serv_addr;
    return 0;
}

int client_update(struct Client *client) {
    logenter();
    rei(client_update_recv_tcp(client), "update client: recv_tcp");
    rei(client_update_recv_udp(client), "update client: recv_udp");
    rei(client_update_send(client), "update client: send");
    return 0;
}

int client_update_recv_tcp(struct Client *ctx) {
    while (1) {
        size_t expected_bytes = ctx->len + MSG_HDR_SIZE - ctx->in_buff_len;
        uint8_t *buf = ctx->in_buff + ctx->in_buff_len;
        ssize_t recv_bytes = recv(ctx->tcp_fd, buf, expected_bytes, 0);
        if (recv_bytes < 0) {
            if (errno == EWOULDBLOCK) {
                break;
            }
            loge("Socket error: %s", strerror(errno));
            return -1;
        }
        if (!recv_bytes) {
            loge("Server close connection");
            return -1;
        }

        ctx->in_buff_len += (size_t)recv_bytes;

        if (ctx->in_buff_len >= MSG_HDR_SIZE) {
            memcpy(&ctx->tag, ctx->in_buff, sizeof(uint16_t));
            ctx->tag = ntohs(ctx->tag);
            memcpy(&ctx->len, ctx->in_buff + sizeof(uint16_t), sizeof(uint16_t));
            ctx->len = ntohs(ctx->len);
        }

        if (ctx->in_buff_len > (size_t)ctx->len + MSG_HDR_SIZE) {
            loge("Unexpected error: len: %d, read %zu bytes", ctx->len, ctx->in_buff_len);
            return -1;
        }

        if (ctx->in_buff_len == (size_t)ctx->len + MSG_HDR_SIZE) {
            logd("MSG: tag: %d len: %d", ctx->tag, ctx->len);
            client_msg_cb msg_cb = ctx->cfg.msg_cbs[ctx->tag];
            msg_cb = msg_cb? msg_cb: ctx->cfg.msg_default_cb;
            struct TlvMsg msg = {
                .is_important = true, // received via tcp
                .tag = ctx->tag,
                .len = ctx->len,
                .val = ctx->in_buff + MSG_HDR_SIZE
            };
            void *cb_ctx = ctx->cfg.cb_ctx;
            if (msg_cb && msg_cb(ctx, &msg, cb_ctx)) {
                loge("Failed to exec msg callback");
                return -1;
            }
            ctx->tag = 0;
            ctx->len = 0;
            ctx->in_buff_len = 0;
        }
    }
    return 0;
}

int client_update_recv_udp(struct Client *ctx) {
    while (1) {
        uint8_t buff[MSG_MAX_LEN] = {};
        ssize_t recv_bytes = recv(ctx->udp_fd, buff, MSG_MAX_LEN, 0);
        if (recv_bytes < 0) {
            if (errno == EWOULDBLOCK) {
                break;
            }
            loge("Client udp socket error: %s", strerror(errno));
            return -1;
        }

        if (!recv_bytes) {
            break;
        }

        if (recv_bytes < (ssize_t)MSG_HDR_SIZE) {
            logw("Drop invalid package: len: %zd", recv_bytes);
            continue;
        }
        struct TlvMsg msg = {};
        msg.is_important = false; // received via udp
        memcpy(&msg.tag, buff, sizeof(msg.tag));
        msg.tag = ntohs(msg.tag);
        memcpy(&msg.len, buff + sizeof(msg.tag), sizeof(msg.len));
        msg.len = ntohs(msg.len);
        msg.val = buff + MSG_HDR_SIZE;

        client_msg_cb msg_cb = ctx->cfg.msg_cbs[msg.tag];
        msg_cb = msg_cb? msg_cb: ctx->cfg.msg_default_cb;
        void *cb_ctx = ctx->cfg.cb_ctx;
        if (msg_cb && msg_cb(ctx, &msg, cb_ctx)) {
            loge("Failed to exec msg callback");
            return -1;
        }
    }
    return 0;
}

int client_update_send(struct Client *ctx) {
    while (ctx->out_buff_len) {
        ssize_t send_bytes = send(ctx->tcp_fd, ctx->out_buff, ctx->out_buff_len, 0);
        if (send_bytes < 0) {
            if (errno == EWOULDBLOCK) {
                break;
            }
            loge("Socket error: %s", strerror(errno));
            return -1;
        }
        void *data_start = ctx->out_buff + send_bytes;
        ctx->out_buff_len -= (size_t)send_bytes;
        if (ctx->out_buff_len) {
            memmove(ctx->out_buff, data_start, ctx->out_buff_len);
        }
    }
    return 0;
}

int client_send_via_tpc(struct Client *client, const struct TlvMsg *msg) {
    return msg_encode_to_buf(msg, client->out_buff, &client->out_buff_len, MSG_MAX_LEN);
}

int client_send_via_udp(struct Client *client, const struct TlvMsg *msg) {
    logenter();
    uint16_t tag = htons(msg->tag);
    uint16_t len = htons(msg->len);
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wcast-qual"
    uint8_t *val = (void *)msg->val;
    #pragma GCC diagnostic pop

    struct iovec iov[3] = {
        { .iov_base = &tag,
          .iov_len  = sizeof(tag) },
        { .iov_base = &len,
          .iov_len  = sizeof(len) },
        { .iov_base = (void *)val,
          .iov_len  = msg->len    }
    };

    struct msghdr io_msg = {
        .msg_name = &client->serv_addr,
        .msg_namelen = sizeof(client->serv_addr),
        .msg_iov = iov,
        .msg_iovlen = 3,
    };

    ssize_t send_bytes = sendmsg(client->udp_fd, &io_msg, 0);
    if (send_bytes < (ssize_t)(sizeof(tag) + sizeof(len) + msg->len)) {
        logw("Drop sending msg: sendmsg rc: %zd", send_bytes);
    }
    return 0;
}

int client_send(struct Client *client, const struct TlvMsg *msg) {
    logenter();
    int rc;
    if (msg->is_important) {
        rc = client_send_via_tpc(client, msg);
    } else {
        rc = client_send_via_udp(client, msg);
    }
    return rc;
}

int client_deinit(struct Client *client) {
    logenter();
    close(client->tcp_fd);
    close(client->udp_fd);
    return 0;
}
