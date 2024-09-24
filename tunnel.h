#ifndef TUNNEL_H
#define TUNNEL_H

#include <stdbool.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <sys/queue.h>

#include "obfsm.h"

#define BUFSIZE 4096

#define APP_MODE_CLIENT 0
#define APP_MODE_SERVER 1

typedef struct obf_tunnel {
    struct bufferevent *tunnel_bev;
    struct bufferevent *plain_bev;
    obfuscator_state_machine_t *obfsm;
    bool connected;
    char *tunnel_buf, *service_buf;

    TAILQ_ENTRY(obf_tunnel) tunnels;
} obf_tunnel_t;

typedef TAILQ_HEAD(tunnellist_s, obf_tunnel) tunnellist_t;

typedef struct app_context {
    int mode;
    struct event_base *base;

    tunnellist_t tunnels;
    unsigned char tunnel_count;
    struct sockaddr_in dst_sin;
} app_context_t;


typedef struct callback_context {
    app_context_t *app_ctx;
    obf_tunnel_t *tunnel;
} callback_context_t;


callback_context_t *create_callback_context(app_context_t *app_ctx, obf_tunnel_t *tunnel);
void destroy_callback_context(callback_context_t *ctx);

obf_tunnel_t *create_obf_tunnel(app_context_t *app_ctx);
void destroy_obf_tunnel(callback_context_t *ctx);

void plain_readcb(struct bufferevent *bev, void *user_data);
void tunnel_readcb(struct bufferevent *bev, void *user_data);

void tunnel_eventcb(struct bufferevent *bev, short events, void *user_data);
int obfs_packetcb(unsigned char *data, unsigned short packet_type, unsigned short len, void *user_data);

void client_listener_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *user_data);
void server_listener_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *user_data);

#endif //TUNNEL_H
