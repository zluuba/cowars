#define LOG_LVL INFO
#include "util.h"
#include "server.h"
#include "common.h"

#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>

#define EMPTY_SLOT_FD -1

struct ServerClient* find_client_by_fd(int fd, struct ServerClient* clients, size_t max_clients);
struct ServerClient* find_client_by_addr(struct sockaddr_in *addr, struct ServerClient* clients,
                                         size_t max_clients);
int server_send_to_via_tcp(struct ServerRunningCtx *server, struct ServerClient *client,
                           struct TlvMsg *msg);
int server_send_to_via_udp(struct ServerRunningCtx *server, struct ServerClient *client,
                           struct TlvMsg *msg);

int server_accept_clients(struct ServerRunningCtx *ctx);
int server_udp_recv(struct ServerRunningCtx *ctx);
int server_client_recv(struct ServerRunningCtx *ctx, struct ServerClient *client);
int server_client_send(struct ServerRunningCtx *ctx, struct ServerClient *client);
void server_client_drop(struct ServerRunningCtx *ctx, struct ServerClient *client);

int server_init(struct ServerCfg *cfg, struct Server *server) {
    logenter();
    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    rei(tcp_fd < 0, "create tcp socket: %s", strerror(errno));
    reci(set_nonblocking(tcp_fd), {close(tcp_fd);}, "set nonblocking mode");
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(cfg->port),
    };
    reci(0 > bind(tcp_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)),
         {close(tcp_fd);},
         "bind server to %d: %s", cfg->port, strerror(errno));
    reci(listen(tcp_fd, (int)cfg->max_clients), {close(tcp_fd);}, "listen clients");

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    reci(udp_fd < 0, {close(tcp_fd); }, "create udp socket: %s", strerror(errno));
    reci(set_nonblocking(udp_fd), {close(tcp_fd); close(udp_fd);}, "set nonblocking mode");
    reci(0 > bind(udp_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)),
         {close(tcp_fd); close(udp_fd);},
         "bind server to %d: %s", cfg->port, strerror(errno));

    logi("Server started on tcp port: %d, tcp fd: %d, udp fd: %d", cfg->port, tcp_fd, udp_fd);
    memset(server, 0, sizeof(struct Server));
    server->cfg = *cfg;
    server->tcp_fd = tcp_fd;
    server->udp_fd = udp_fd;
    return 0;
}

int server_run(struct Server *server) {
    logenter();
    int epoll_fd = epoll_create1(0);
    rei(epoll_fd < 0, "create epoll fd: %s", strerror(errno));
    struct ServerClient clients[server->cfg.max_clients];
    for (size_t i = 0; i < server->cfg.max_clients; i++) {
        memset(&clients[i], 0, sizeof(struct ServerClient));
        clients[i].tcp_fd = EMPTY_SLOT_FD;
    }

    struct ServerRunningCtx ctx = {
        .server = server,
        .max_clients = server->cfg.max_clients,
        .clients = clients,
        .epoll_fd = epoll_fd,
    };

    struct epoll_event new_client_ev = {};
    new_client_ev.events = EPOLLIN;
    new_client_ev.data.fd = server->tcp_fd;
    reci(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server->tcp_fd, &new_client_ev), {close(epoll_fd);},
         "add event for server tcp fd: %d", server->tcp_fd);
    new_client_ev.data.fd = server->udp_fd;
    reci(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server->udp_fd, &new_client_ev), {close(epoll_fd);},
         "add event for server udp fd: %d", server->udp_fd);

    const size_t MAX_EVENTS = server->cfg.max_events;
    struct epoll_event events[MAX_EVENTS];

    while (1) {
        int events_cnt = epoll_wait(epoll_fd, events, (int)MAX_EVENTS, -1);
        if (events_cnt < 0) {
            if (errno == EINTR) {
                continue;
            }
            loge("Epoll error: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < events_cnt; i++) {
            int rc = 0, fd = events[i].data.fd;

            if (fd == server->tcp_fd) {
                if (!(events[i].events & EPOLLIN)) {
                    loge("Unexpected event for server tcp fd:%d: %s",
                         fd, epoll_events_to_str(events[i].events));
                    break;
                }
                rc = server_accept_clients(&ctx);
            } else if (fd == server->udp_fd) {
                if (!(events[i].events & EPOLLIN)) {
                    loge("Unexpected event for server udp fd:%d: %s",
                         fd, epoll_events_to_str(events[i].events));
                    break;
                }
                rc = server_udp_recv(&ctx);
            } else {
                struct ServerClient *client = find_client_by_fd(fd, ctx.clients, ctx.max_clients);
                if (!client) {
                    loge("Unexpected error: client not found for fd:%d", fd);
                    break;
                }
                if (!(events[i].events & (EPOLLIN | EPOLLOUT))) {
                    loge("Unexpected event for client: %d", events[i].events);
                    server_client_drop(&ctx, client);
                    continue;
                }
                if (events[i].events & (EPOLLIN | EPOLLET)) {
                    rc = server_client_recv(&ctx, client);
                }
                if (events[i].events & EPOLLOUT) {
                    rc = server_client_send(&ctx, client);
                }
            }
            if (rc) {
                loge("Failed to process event: fd: %d", fd);
                break;
            }
        }
    }
    for (size_t i = 0; i < ctx.max_clients; i++) {
        server_client_drop(&ctx, &clients[i]);
    }
    close(epoll_fd);
    return -1;
}

int server_deinit(struct Server *server) {
    logenter();
    logi("Server stoped: port: %d, tcp fd: %d, udp fd: %d",
         server->cfg.port, server->tcp_fd, server->udp_fd);
    close(server->tcp_fd);
    close(server->udp_fd);
    return 0;
}

int server_accept_clients(struct ServerRunningCtx *ctx) {
    logenter();
    while (1) {
        struct sockaddr_in addr = {};
        socklen_t addr_len = sizeof(addr);
        int incoming_fd = accept(ctx->server->tcp_fd, (struct sockaddr *)&addr, &addr_len);
        if (incoming_fd < 0) {
            if (errno == EWOULDBLOCK) {
                break;
            }
            loge("Accept error: %s", strerror(errno));
            return -1;
        }

        char incoming_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, incoming_ip, sizeof(incoming_ip));
        logi("Incoming connection: fd:%d, from ip:%s, port:%d",
            incoming_fd, incoming_ip, ntohs(addr.sin_port));

        struct ServerClient* empty_slot;
        empty_slot = find_client_by_fd(EMPTY_SLOT_FD, ctx->clients, ctx->max_clients);
        if (!empty_slot) {
            logi("Drop incoming connection, server is full: max clients: %zu", ctx->max_clients);
            close(incoming_fd);
            break;
        }

        reci(set_nonblocking(incoming_fd), {close(incoming_fd);},
             "set nonblocking mode for incoming fd:%d", incoming_fd);

        struct epoll_event client_ev = {0};
        client_ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        client_ev.data.fd = incoming_fd;
        reci(epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, incoming_fd, &client_ev),
             {close(incoming_fd);},
             "create event for incoming client: fd:%d", incoming_fd);

        memset(empty_slot, 0, sizeof(struct ServerClient));
        empty_slot->tcp_fd = incoming_fd;
        empty_slot->addr = addr;
        logi("Client connected: fd:%d, from ip:%s, port:%d",
             incoming_fd, incoming_ip, ntohs(addr.sin_port));
    }
    return 0;
}

int server_udp_recv(struct ServerRunningCtx *ctx) {
    while (1) {
        uint8_t buff[MSG_MAX_LEN] = {};
        struct sockaddr_in addr = {};
        socklen_t addr_len = sizeof(addr);
        ssize_t recv_bytes = recvfrom(ctx->server->udp_fd, buff, MSG_MAX_LEN, 0,
                                      (struct sockaddr *)&addr, &addr_len);
        if (recv_bytes < 0) {
            if (errno == EWOULDBLOCK) {
                break;
            }
            loge("Server udp socket error: %s", strerror(errno));
            return -1;
        }

        if (!recv_bytes) {
            break;
        }

        if (recv_bytes < (ssize_t)MSG_HDR_SIZE) {
            logw("Drop invalid package: len: %zd", recv_bytes);
            continue;
        }

        struct ServerClient *client = find_client_by_addr(&addr, ctx->clients, ctx->max_clients);
        if (!client) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            logd("Msg from unexpected addr: %s:%d", ip, addr.sin_port);
            continue;
        }

        struct TlvMsg msg = {};
        msg.is_important = false; // received via udp
        memcpy(&msg.tag, buff, sizeof(msg.tag));
        msg.tag = ntohs(msg.tag);
        memcpy(&msg.len, buff + sizeof(msg.tag), sizeof(msg.len));
        msg.len = ntohs(msg.len);
        msg.val = buff + MSG_HDR_SIZE;

        server_msg_cb msg_cb = ctx->server->cfg.msg_cbs[msg.tag];
        msg_cb = msg_cb? msg_cb: ctx->server->cfg.msg_default_cb;
        void *cb_ctx = ctx->server->cfg.cb_ctx;
        if (msg_cb && msg_cb(ctx, client, &msg, cb_ctx)) {
            loge("Failed to exec msg callback");
            server_client_drop(ctx, client);
            break;
        }
    }
    return 0;
}

int server_client_recv(struct ServerRunningCtx *ctx, struct ServerClient *client) {
    logenter();
    while (1) {
        size_t expected_bytes = client->len + MSG_HDR_SIZE - client->in_buff_len;
        uint8_t *buff = client->in_buff + client->in_buff_len;
        ssize_t recv_bytes = recv(client->tcp_fd, buff, expected_bytes, 0);
        if (recv_bytes < 0) {
            if (errno == EWOULDBLOCK) {
                break;
            }
            loge("Socket error: %s", strerror(errno));
            server_client_drop(ctx, client);
            break;
        }

        if (!recv_bytes) {
            server_client_drop(ctx, client);
            break;
        }

        client->in_buff_len += (size_t)recv_bytes;

        if (client->in_buff_len >= MSG_HDR_SIZE) {
            memcpy(&client->tag, client->in_buff, sizeof(uint16_t));
            client->tag = ntohs(client->tag);
            memcpy(&client->len, client->in_buff + sizeof(uint16_t), sizeof(uint16_t));
            client->len = ntohs(client->len);
        }

        if (client->in_buff_len > (size_t)client->len + MSG_HDR_SIZE) {
            loge("Unexpected error: len: %d, read %zu bytes", client->len, client->in_buff_len);
            return -1;
        }

        if (client->in_buff_len == (size_t)client->len + MSG_HDR_SIZE) {
            logd("MSG: tag: %d len: %d", client->tag, client->len);
            server_msg_cb msg_cb = ctx->server->cfg.msg_cbs[client->tag];
            msg_cb = msg_cb? msg_cb: ctx->server->cfg.msg_default_cb;
            struct TlvMsg msg = {
                .is_important = true, // received via tcp
                .tag = client->tag,
                .len = client->len,
                .val = client->in_buff + MSG_HDR_SIZE
            };
            void *cb_ctx = ctx->server->cfg.cb_ctx;
            if (msg_cb && msg_cb(ctx, client, &msg, cb_ctx)) {
                loge("Failed to exec msg callback");
                server_client_drop(ctx, client);
                break;
            }
            client->tag = 0;
            client->len = 0;
            client->in_buff_len = 0;
        }
    }
    return 0;
}

int server_client_send(struct ServerRunningCtx *ctx, struct ServerClient *client) {
    logenter();
    while (client->out_buff_len) {
        ssize_t send_bytes = send(client->tcp_fd, client->out_buff, client->out_buff_len, 0);
        if (send_bytes < 0) {
            if (errno == EWOULDBLOCK) {
                break;
            }
            loge("Socket error: %s", strerror(errno));
            server_client_drop(ctx, client);
            break;
        }
        void *data_start = client->out_buff + send_bytes;
        client->out_buff_len -= (size_t)send_bytes;
        if (client->out_buff_len) {
            memmove(client->out_buff, data_start, client->out_buff_len);
        } else {
            struct epoll_event ev = {0};
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = client->tcp_fd;
            rei(epoll_ctl(ctx->epoll_fd, EPOLL_CTL_MOD, client->tcp_fd, &ev),
                "modify fd expected events: fd:%d", client->tcp_fd);
            break;
        }
    }
    return 0;
}

void server_client_drop(struct ServerRunningCtx *ctx, struct ServerClient *client) {
    logi("Disconnect client: fd: %d", client->tcp_fd);
    epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, client->tcp_fd, NULL);
    close(client->tcp_fd);
    memset(client, 0, sizeof(struct ServerClient));
    client->tcp_fd = EMPTY_SLOT_FD;
}

int server_send_to_via_tcp(struct ServerRunningCtx *server, struct ServerClient *client,
                           struct TlvMsg *msg) {
    logenter();
    if (msg_encode_to_buf(msg, client->out_buff, &client->out_buff_len, MSG_MAX_LEN)) {
        server_client_drop(server, client);
        return 0;
    }
    struct epoll_event ev = {0};
    ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
    ev.data.fd = client->tcp_fd;
    rei(epoll_ctl(server->epoll_fd, EPOLL_CTL_MOD, client->tcp_fd, &ev),
        "modify fd expected events: fd:%d", client->tcp_fd);
    return 0;
}

int server_send_to_via_udp(struct ServerRunningCtx *server, struct ServerClient *client,
                           struct TlvMsg *msg) {
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
        .msg_name = &client->addr,
        .msg_namelen = sizeof(client->addr),
        .msg_iov = iov,
        .msg_iovlen = 3,
    };

    ssize_t send_bytes = sendmsg(server->server->udp_fd, &io_msg, 0);
    if (send_bytes < (ssize_t)(sizeof(tag) + sizeof(len) + msg->len)) {
        logw("SERV Drop sending msg: sendmsg rc: %zd", send_bytes);
    }
    return 0;
}

int server_send_to(struct ServerRunningCtx *server, struct ServerClient *client,
                   struct TlvMsg *msg) {
    logenter();
    int rc;
    if (msg->is_important) {
        rc = server_send_to_via_tcp(server, client, msg);
    } else {
        rc = server_send_to_via_udp(server, client, msg);
    }
    return rc;
}

int server_send_to_other(struct ServerRunningCtx *server, struct ServerClient *client,
                         struct TlvMsg *msg) {
    for (size_t i = 0; i < server->max_clients; i++) {
        if (server->clients[i].tcp_fd != EMPTY_SLOT_FD &&
            server->clients[i].tcp_fd != client->tcp_fd) {
            rei(server_send_to(server, &server->clients[i], msg),
                "send message to client: fd:%d", server->clients[i].tcp_fd);
        }
    }
    return 0;
}

int server_send_to_each(struct ServerRunningCtx *server, struct TlvMsg *msg) {
    for (size_t i = 0; i < server->max_clients; i++) {
        if (server->clients[i].tcp_fd != EMPTY_SLOT_FD) {
            rei(server_send_to(server, &server->clients[i], msg),
                "send message to client: fd:%d", server->clients[i].tcp_fd);
        }
    }
    return 0;
}



struct ServerClient* find_client_by_fd(int fd, struct ServerClient* clients, size_t max_clients) {
    for (size_t i = 0; i < max_clients; i++) {
        if (clients[i].tcp_fd == fd) {
            return &clients[i];
        }
    }
    return NULL;
}

struct ServerClient* find_client_by_addr(struct sockaddr_in *addr, struct ServerClient* clients,
                                         size_t max_clients) {
    for (size_t i = 0; i < max_clients; i++) {
        if (clients[i].addr.sin_port == addr->sin_port &&
            clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            clients[i].addr.sin_family == addr->sin_family) {
            return &clients[i];
        }
    }
    return NULL;
}
