#include "tunnel.h"
#include "log.h"
#include <stddef.h>
#include <malloc.h>
#include <string.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

static void set_tcp_no_delay(evutil_socket_t fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,   &one, sizeof one);
}

callback_context_t *create_callback_context(app_context_t *app_ctx, obf_tunnel_t *tunnel) {
    callback_context_t *ctx = (callback_context_t *) malloc(sizeof(callback_context_t));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->app_ctx = app_ctx;
    ctx->tunnel = tunnel;
    return ctx;
}

void destroy_callback_context(callback_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    free(ctx);
}

obf_tunnel_t *create_obf_tunnel(app_context_t *app_ctx) {
    obf_tunnel_t *tun_ctx = (obf_tunnel_t *)malloc(sizeof(obf_tunnel_t));
    if (tun_ctx == NULL) {
        return NULL;
    }
    memset(tun_ctx, 0, sizeof(obf_tunnel_t));

    tun_ctx->connected = false;
    tun_ctx->obfsm = NULL;

    tun_ctx->tunnel_buf = (char *) malloc(BUFSIZE);
    tun_ctx->service_buf = (char *) malloc(BUFSIZE);

    TAILQ_INSERT_TAIL(&app_ctx->tunnels, tun_ctx, tunnels);
    return tun_ctx;
}

void destroy_obf_tunnel(callback_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    app_context_t *app_ctx = ctx->app_ctx;
    obf_tunnel_t *tun_ctx = ctx->tunnel;

    TAILQ_REMOVE(&app_ctx->tunnels, tun_ctx, tunnels);

    // close the connections
    bufferevent_free(tun_ctx->tunnel_bev);
    bufferevent_free(tun_ctx->plain_bev);

    // destroy obfuscated state machine if exists
    if (tun_ctx->obfsm != NULL) {
        destroy_obfsm(tun_ctx->obfsm);
    }
    if (tun_ctx->tunnel_buf != NULL) {
        free(tun_ctx->tunnel_buf);
    }
    if (tun_ctx->service_buf != NULL) {
        free(tun_ctx->service_buf);
    }
    free(tun_ctx);
    destroy_callback_context(ctx);
}

void tunnel_eventcb(struct bufferevent *bev, short events, void *user_data) {
    callback_context_t *ctx = (callback_context_t *)user_data;
    log_debug("tunnel_eventcb()");

    if (events & BEV_EVENT_CONNECTED) {
        evutil_socket_t fd = bufferevent_getfd(bev);
        set_tcp_no_delay(fd);
        ctx->tunnel->connected = true;
        log_info("tunnel connected");
        return;
    } else if (events & BEV_EVENT_ERROR) {
        log_error("failed to create tunnel connection");
    } else if (events & BEV_EVENT_EOF) {
        log_info("tunnel disconnected");
    }
    // disconnect everything connection
    destroy_obf_tunnel(ctx);
}

void plain_readcb(struct bufferevent *bev, void *user_data) {
    callback_context_t *ctx = (callback_context_t *)user_data;

    struct evbuffer *input = bufferevent_get_input(bev);
    log_debug("plain_readcb()");

    size_t bytes_read;
    do {
        bytes_read = evbuffer_remove(input, ctx->tunnel->service_buf, BUFSIZE);
        if (bytes_read == 0) {
            break;
        }
        exchange_packet_desc_t res = obfsm_pack(ctx->tunnel->obfsm, 0, bytes_read, ctx->tunnel->service_buf);
        if (res.data == NULL) {
            continue;
        }
        bufferevent_write(ctx->tunnel->tunnel_bev, res.data, res.size);
        free(res.data);
    } while (bytes_read == BUFSIZE);
}


int obfs_packetcb(unsigned char *data, unsigned short packet_type, unsigned short len, void *user_data) {
    callback_context_t *ctx = (callback_context_t *)user_data;
    if (packet_type == 0) {
        bufferevent_write(ctx->tunnel->plain_bev, data, len);
    }
}


void tunnel_readcb(struct bufferevent *bev, void *user_data) {
    callback_context_t *ctx = (callback_context_t *)user_data;
    log_debug("tunnel_readcb()");

    struct evbuffer *input = bufferevent_get_input(bev);

    if (!ctx->tunnel->connected) { // wtf?
        return;
    }

    size_t bytes_read;
    do {
        bytes_read = evbuffer_remove(input, ctx->tunnel->tunnel_buf, BUFSIZE);
        if (bytes_read == 0) {
            break;
        }
        obfsm_consume(ctx->tunnel->obfsm, ctx->tunnel->tunnel_buf, bytes_read, obfs_packetcb, ctx);
    } while (bytes_read == BUFSIZE);
}

void client_listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
                               struct sockaddr *sa, int socklen, void *user_data) {
    app_context_t *app_ctx = (app_context_t *)user_data;
    log_info("got client connection");

    obf_tunnel_t *tunnel = create_obf_tunnel(app_ctx);
    if (tunnel == NULL) {
        struct bufferevent *bev = bufferevent_socket_new(app_ctx->base, fd, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_free(bev);
        return;
    }

    tunnel->plain_bev = bufferevent_socket_new(app_ctx->base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!tunnel->plain_bev) {
        log_error("failed to construct bufferevent");
        event_base_loopbreak(app_ctx->base);
        return;
    }

    callback_context_t *ctx = create_callback_context(app_ctx, tunnel);
    if (ctx == NULL) {
        bufferevent_free(tunnel->tunnel_bev);
    }

    // must be created before the child connection made since client can already send data
    ctx->tunnel->obfsm = create_obfsm();

    // plain connection
    bufferevent_setcb(tunnel->plain_bev, plain_readcb, NULL, tunnel_eventcb, ctx);
    bufferevent_enable(tunnel->plain_bev, EV_READ | EV_CLOSED);

    // tunnel connection
    tunnel->tunnel_bev = bufferevent_socket_new(app_ctx->base, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!tunnel->tunnel_bev) {
        log_error("failed to construct bufferevent");
        event_base_loopbreak(app_ctx->base);
        return;
    }

    bufferevent_setcb(tunnel->tunnel_bev, tunnel_readcb, NULL, tunnel_eventcb, ctx);
    bufferevent_enable(tunnel->tunnel_bev, EV_READ | EV_CLOSED);

    if (bufferevent_socket_connect(tunnel->tunnel_bev, (struct sockaddr *)&ctx->app_ctx->dst_sin, sizeof(ctx->app_ctx->dst_sin)) < 0)
    {
        log_error("failed to create tunnel connection");
        destroy_obf_tunnel(ctx);
        return;
    }
}

void server_listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
                        struct sockaddr *sa, int socklen, void *user_data) {
    app_context_t *app_ctx = (app_context_t *)user_data;
    log_info("got tunnel connection");

    obf_tunnel_t *tunnel = create_obf_tunnel(app_ctx);
    if (tunnel == NULL) {
        struct bufferevent *bev = bufferevent_socket_new(app_ctx->base, fd, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_free(bev);
        return;
    }

    tunnel->tunnel_bev = bufferevent_socket_new(app_ctx->base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!tunnel->tunnel_bev) {
        log_error("failed to construct bufferevent");
        event_base_loopbreak(app_ctx->base);
        return;
    }

    callback_context_t *ctx = create_callback_context(app_ctx, tunnel);
    if (ctx == NULL) {
        bufferevent_free(tunnel->tunnel_bev);
    }

    // must be created before the child connection made since client can already send data
    ctx->tunnel->obfsm = create_obfsm();

    bufferevent_setcb(tunnel->tunnel_bev, tunnel_readcb, NULL, tunnel_eventcb, ctx);
    bufferevent_enable(tunnel->tunnel_bev, EV_READ | EV_CLOSED);

    // service connection
    tunnel->plain_bev = bufferevent_socket_new(app_ctx->base, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!tunnel->plain_bev) {
        log_error("failed to construct bufferevent");
        event_base_loopbreak(app_ctx->base);
        return;
    }
    bufferevent_setcb(tunnel->plain_bev, plain_readcb, NULL, tunnel_eventcb, ctx);
    bufferevent_enable(tunnel->plain_bev, EV_READ | EV_CLOSED);

    if (bufferevent_socket_connect(tunnel->plain_bev, (struct sockaddr *)&ctx->app_ctx->dst_sin, sizeof(ctx->app_ctx->dst_sin)) < 0)
    {
        log_error("failed to create service connection");
        destroy_obf_tunnel(ctx);
        return;
    }
}
