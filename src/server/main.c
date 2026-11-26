#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "args.h"
#include "logger.h"
#include "netutils.h"
#include "selector.h"

static volatile sig_atomic_t done = 0;

static void
sigterm_handler(const int signal) {
    (void) signal;
    done = 1;
}

static void
passive_close(struct selector_key *key) {
    close(key->fd);
}

// TODO Fase 1: reemplazar por socksv5_passive_accept / el accept de management
static void
passive_accept(struct selector_key *key) {
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    const int client = accept(key->fd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (client < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != ECONNABORTED) {
            LOG(ERROR, "accept: %s", strerror(errno));
        }
        return;
    }

    char buff[SOCKADDR_TO_HUMAN_MIN];
    LOG(INFO, "conexión %s desde %s", (const char *) key->data,
        sockaddr_to_human(buff, sizeof(buff), (struct sockaddr *) &client_addr));
    close(client);
}

static int
passive_socket(const char *addr, const unsigned short port) {
    char service[6];
    snprintf(service, sizeof(service), "%hu", port);

    // AI_NUMERICHOST: solo acepta IPs literales, así getaddrinfo no consulta DNS
    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags    = AI_PASSIVE | AI_NUMERICHOST,
    };
    struct addrinfo *res = NULL;
    const int gai = getaddrinfo(addr, service, &hints, &res);
    if (gai != 0) {
        LOG(ERROR, "dirección inválida %s: %s", addr, gai_strerror(gai));
        return -1;
    }

    const char *err = NULL;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        err = "socket";
        goto finally;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));
    // sin esto, en Linux :: también toma IPv4 y el bind de 0.0.0.0 al mismo puerto falla
    if (res->ai_family == AF_INET6) {
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){ 1 }, sizeof(int));
    }
    if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
        err = "bind";
    } else if (listen(fd, SOMAXCONN) < 0) {
        err = "listen";
    } else if (selector_fd_set_nio(fd) == -1) {
        err = "O_NONBLOCK";
    }

finally:
    if (err != NULL) {
        LOG(ERROR, "%s %s:%s: %s", err, addr, service, strerror(errno));
        if (fd >= 0) {
            close(fd);
        }
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static bool
listen_on(fd_selector selector, const char *addr, const unsigned short port, const char *name) {
    static const struct fd_handler passive_handler = {
        .handle_read  = passive_accept,
        .handle_close = passive_close,
    };

    const int fd = passive_socket(addr, port);
    if (fd < 0) {
        return false;
    }
    const selector_status ss = selector_register(selector, fd, &passive_handler, OP_READ, (void *) name);
    if (ss != SELECTOR_SUCCESS) {
        LOG(ERROR, "registrando %s:%hu: %s", addr, port, selector_error(ss));
        close(fd);
        return false;
    }
    LOG(INFO, "%s escuchando en %s:%hu", name, addr, port);
    return true;
}

int
main(const int argc, char **argv) {
    struct socks5args args;
    parse_args(argc, argv, &args);

    // no tenemos nada que leer de stdin y ahorramos un fd
    close(0);

    int ret = 0;
    fd_selector selector = NULL;

    struct sigaction sa = { .sa_handler = sigterm_handler };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    const struct selector_init conf = {
        .signal = SIGALRM,
        .select_timeout = {
            .tv_sec  = 10,
            .tv_nsec = 0,
        },
    };
    selector_status ss = selector_init(&conf);
    if (ss != SELECTOR_SUCCESS) {
        LOG(ERROR, "inicializando selector: %s", strerror(errno));
        ret = 2;
        goto finally;
    }

    selector = selector_new(1024);
    if (selector == NULL) {
        LOG(ERROR, "creando selector: %s", strerror(errno));
        ret = 2;
        goto finally;
    }

    // Con la dirección por defecto alcanza con que levante una de las dos familias
    int socks_listening = 0;
    if (args.socks_addr == NULL) {
        socks_listening += listen_on(selector, "0.0.0.0", args.socks_port, "SOCKS");
        socks_listening += listen_on(selector, "::",      args.socks_port, "SOCKS");
    } else {
        socks_listening += listen_on(selector, args.socks_addr, args.socks_port, "SOCKS");
    }
    if (socks_listening == 0 || !listen_on(selector, args.mng_addr, args.mng_port, "management")) {
        ret = 1;
        goto finally;
    }

    while (!done) {
        ss = selector_select(selector);
        if (ss != SELECTOR_SUCCESS) {
            LOG(ERROR, "sirviendo: %s", ss == SELECTOR_IO ? strerror(errno) : selector_error(ss));
            ret = 2;
            break;
        }
    }
    LOG(INFO, "cerrando");

finally:
    // destruir el selector cierra los sockets pasivos (passive_close)
    if (selector != NULL) {
        selector_destroy(selector);
    }
    selector_close();
    return ret;
}
