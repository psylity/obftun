#include <stdio.h>
#include <stdlib.h>
#include <libconfig.h>
#include <argp.h>
#include <stdbool.h>
#include <unistd.h>
#include <stddef.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

#include "log.h"
#include "tunnel.h"

extern bool logger_allow_verbose;

#define DEFAULT_CONFIG_PATH "/etc/obftun.conf"
#define DEFAULT_BIND_ADDRESS "127.0.0.1:28726"

const char *argp_program_version = "obftunnel v0.1";
const char *argp_program_bug_address = "<psylity@gmail.com>";
static char doc[] = "Another TCP/UDP tunnel to obfuscate the connection";
static struct argp_option options[] = {
        { "server", 's', 0, 0, "server mode."},
        { "client", 'c', 0, 0, "client mode."},
        { "peer", 'p', "ADDR:PORT", 0, "peer address."},
        { "bind", 'b', "ADDR:PORT", 0, "bind address. Default is "DEFAULT_BIND_ADDRESS},
        { "bind-tcp", 'T', 0, 0, "bind at tcp. This is default behaviour."},
        { "bind-udp", 'U', 0, 0, "bind at udp"},
        { "config", 'C', "PATH", 0, "configuration file path. Default is "DEFAULT_CONFIG_PATH},
        { "peer-tcp", 't', 0, 0, "connect to peer over tcp."},
        { "peer-udp", 'u', 0, 0, "connect to peer over udp. This is default behavior."},
        { "verbose", 'v', 0, 0, "verbose mode."},
        { 0 }
};

struct arguments {
    bool server;
    bool client;
    char *config;
    char *peer;
    char *bind;
    bool bind_tcp;
    bool bind_udp;
    bool peer_tcp;
    bool peer_udp;
    bool verbose;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    switch (key) {
        case 's': arguments->server = true; break;
        case 'c': arguments->client = true; break;
        case 'C': arguments->config = arg; break;
        case 'p': arguments->peer = arg; break;
        case 'b': arguments->bind = arg; break;
        case 'T': arguments->bind_tcp = true; break;
        case 'U': arguments->bind_udp = true; break;
        case 't': arguments->peer_tcp = true; break;
        case 'u': arguments->peer_udp = true; break;
        case 'v': arguments->verbose = true; break;
        default: return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { options, parse_opt, NULL, doc, 0, 0, 0 };

int parse_hostport_pair(char *str, char *host, unsigned short *port) {
    char *p = strstr(str, ":");
    if (p == NULL) {
        return -1;
    }
    if (p - str > 15) { // xxx.xxx.xxx.xxx
        return 1;
    }
    memcpy(host, str, (p - str));
    host[(p - str)] = 0;
    int i = atoi(p + 1);
    if ((i == 0) || (i > 65535)) {
        return 1;
    }
    *port = i;
    return 0;
}

static void signal_cb(evutil_socket_t, short, void *);

int main(int argc, char *argv[]) {
    char bind_host[16], peer_host[16];
    unsigned short bind_port, peer_port;
    struct arguments arguments;

    memset(&arguments, 0, sizeof arguments);

    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    config_t cfg;
    config_init(&cfg);

    if (arguments.config != NULL) {
        if (access(arguments.config, F_OK) != 0) {
            log_error("configuration file \"%s\" is not readable.", arguments.config);
            config_destroy(&cfg);
            return EXIT_FAILURE;
        }
    } else {
        if (access(DEFAULT_CONFIG_PATH, F_OK) == 0) {
            arguments.config = DEFAULT_CONFIG_PATH;
        }
    }

    if (arguments.config != NULL) {
        if (!config_read_file(&cfg, arguments.config)) {
            log_error("configuration error %s:%d - %s", config_error_file(&cfg),
                      config_error_line(&cfg), config_error_text(&cfg));
            config_destroy(&cfg);
            return EXIT_FAILURE;
        }

        if (arguments.bind == NULL) {
            config_lookup_string(&cfg, "bind", (const char **)&arguments.bind);
        }
        if (arguments.peer == NULL) {
            config_lookup_string(&cfg, "peer", (const char **)&arguments.peer);
        }
        if (!arguments.bind_tcp && arguments.bind_udp) {
            config_lookup_bool(&cfg, "bind-tcp", (int *)&arguments.bind_tcp);
        }
        if (!arguments.bind_udp && !arguments.bind_tcp) {
            config_lookup_bool(&cfg, "bind-udp", (int *)&arguments.bind_udp);
        }
        if (!arguments.peer_tcp && !arguments.peer_udp) {
            config_lookup_bool(&cfg, "peer-tcp", (int *)&arguments.peer_tcp);
        }
        if (!arguments.peer_udp && !arguments.peer_tcp) {
            config_lookup_bool(&cfg, "peer-udp", (int *)&arguments.peer_udp);
        }
        if (!arguments.client && !arguments.server) {
            config_lookup_bool(&cfg, "client", (int *)&arguments.client);
        }
        if (!arguments.server && !arguments.client) {
            config_lookup_bool(&cfg, "server", (int *)&arguments.server);
        }
        if (!arguments.verbose) {
            config_lookup_bool(&cfg, "verbose", (int *)&arguments.verbose);
        }
    }

    if (arguments.client && arguments.server) {
        log_error("client & server options are mutually exclusive.");
        return EXIT_FAILURE;
    }
    if (!arguments.client && !arguments.server) {
        arguments.client = true;
    }
    if (!arguments.bind_tcp && !arguments.bind_udp) {
        arguments.bind_tcp = true;
    }
    if (!arguments.peer_tcp && !arguments.peer_udp) {
        arguments.peer_tcp = true;
    }
    if (arguments.bind_tcp && arguments.bind_udp) {
        log_error("bind-tcp & bind-udp options are mutually exclusive.");
        return EXIT_FAILURE;
    }
    if (arguments.peer_tcp && arguments.peer_udp) {
        log_error("peer-tcp & peer-udp options are mutually exclusive.");
        return EXIT_FAILURE;
    }
    if (arguments.bind_tcp && arguments.peer_udp) {
        log_error("kindly refuse to start in bind-tcp & peer-udp mode.");
        return EXIT_FAILURE;
    }

    if (arguments.bind == NULL) {
        arguments.bind = DEFAULT_BIND_ADDRESS;
    }

    if (arguments.peer == NULL) {
        log_error("peer address not specified");
        return EXIT_FAILURE;
    }

    if (parse_hostport_pair(arguments.bind, bind_host, &bind_port) != 0) {
        log_error("bind address should be in HOST:PORT format. (E.g. 127.0.0.1:8080)");
        return EXIT_FAILURE;
    }

    if (parse_hostport_pair(arguments.peer, peer_host, &peer_port) != 0) {
        log_error("peer address should be in HOST:PORT format. (E.g. 192.168.0.1:1194)");
        return EXIT_FAILURE;
    }

    if (arguments.bind_udp || arguments.peer_udp) {
        log_error("sorry, UDP mode is not implemented.");
        return EXIT_FAILURE;
    }

    logger_allow_verbose = false;
    if (arguments.verbose) {
        logger_allow_verbose = true;
    }

    struct evconnlistener *listener;
    struct event *signal_event;
    struct sockaddr_in sin = {0};
    app_context_t ctx;
    TAILQ_INIT(&ctx.tunnels);

    // bind address
    bzero(&sin, sizeof(sin));
    inet_pton(AF_INET, bind_host, &sin.sin_addr);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(bind_port);

    // peer address
    bzero(&ctx.dst_sin, sizeof(ctx.dst_sin));
    ctx.dst_sin.sin_family = AF_INET;
    inet_pton(AF_INET, peer_host, &ctx.dst_sin.sin_addr);
    ctx.dst_sin.sin_port = htons(peer_port);

    config_destroy(&cfg);

    ctx.base = event_base_new();
    if (!ctx.base) {
        log_error("failed to create an event_base: exiting");
        return EXIT_FAILURE;
    }

    evconnlistener_cb listener_cb = NULL;

    if (arguments.client) {
        log_info("starting in client mode at %s:%d", bind_host, bind_port);
        ctx.mode = APP_MODE_CLIENT;
        listener_cb = client_listener_cb;
    }

    if (arguments.server) {
        log_info("starting in server mode at %s:%d", bind_host, bind_port);
        ctx.mode = APP_MODE_SERVER;
        listener_cb = server_listener_cb;
    }

    listener = evconnlistener_new_bind(ctx.base, listener_cb, (void *) &ctx,
                                       LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
                                       (struct sockaddr *) &sin,
                                       sizeof(sin));

    if (!listener) {
        fprintf(stderr, "Could not create a listener!\n");
        return EXIT_FAILURE;
    }

    signal_event = evsignal_new(ctx.base, SIGINT, signal_cb, (void *)&ctx);

    if (!signal_event || event_add(signal_event, NULL)<0) {
        log_error("could not create/add a signal event!\n");
        return EXIT_FAILURE;
    }

    event_base_dispatch(ctx.base);
    evconnlistener_free(listener);
    event_free(signal_event);
    event_base_free(ctx.base);

    return EXIT_SUCCESS;
}

static void signal_cb(evutil_socket_t sig, short events, void *user_data) {
    app_context_t *app_ctx = (app_context_t *)user_data;
    struct timeval delay = { 1, 0 };

    log_info("caught an interrupt signal; exiting cleanly in a second.");

    event_base_loopexit(app_ctx->base, &delay);
}
