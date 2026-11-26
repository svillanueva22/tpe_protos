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

#include "accesslog.h"
#include "args.h"
#include "config.h"
#include "logger.h"
#include "metrics.h"
#include "netutils.h"
#include "selector.h"
#include "users.h"

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

// Handler provisorio de ambos sockets pasivos: acepta, loguea y cierra.
// TODO Parte A: reemplazar por socksv5_passive_accept en socks_handler.
// TODO Parte D: reemplazar por mgmt_passive_accept en mgmt_handler.
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

// Un handler por servicio: cada parte cambia solo el suyo, sin tocar listen_on.
static const struct fd_handler socks_handler = {
    .handle_read  = passive_accept,  // TODO Parte A: socksv5_passive_accept
    .handle_close = passive_close,
};

static const struct fd_handler mgmt_handler = {
    .handle_read  = passive_accept,  // TODO Parte D: mgmt_passive_accept
    .handle_close = passive_close,
};

static bool
listen_on(fd_selector selector, const struct fd_handler *handler,
          const char *addr, const unsigned short port, const char *name) {
    const int fd = passive_socket(addr, port);
    if (fd < 0) {
        return false;
    }
    const selector_status ss = selector_register(selector, fd, handler, OP_READ, (void *) name);
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

    // Estado global del servidor (cada módulo es dueño de lo suyo)
    config_init(args.disectors_enabled);
    users_init();
    metrics_init();
    for (int i = 0; i < args.nusers; i++) {
        const users_status us = users_add(args.users[i].name, args.users[i].pass);
        if (us != USERS_OK) {
            LOG(ERROR, "cargando usuario %s (código %d)", args.users[i].name, (int) us);
            ret = 1;
            goto finally;
        }
    }
    // El access log va a un archivo, bufferizado (docs/decisiones-parte-c.md, D1).
    // TODO: opción de línea de comandos para elegir la ruta (por ahora fija).
    if (!accesslog_init(ACCESSLOG_DEFAULT_PATH)) {
        LOG(ERROR, "abriendo el access log %s: %s", ACCESSLOG_DEFAULT_PATH, strerror(errno));
        ret = 1;
        goto finally;
    }

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
        socks_listening += listen_on(selector, &socks_handler, "0.0.0.0", args.socks_port, "SOCKS");
        socks_listening += listen_on(selector, &socks_handler, "::",      args.socks_port, "SOCKS");
    } else {
        socks_listening += listen_on(selector, &socks_handler, args.socks_addr, args.socks_port, "SOCKS");
    }
    if (socks_listening == 0
        || !listen_on(selector, &mgmt_handler, args.mng_addr, args.mng_port, "management")) {
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
        // Un solo write() del access log por vuelta (también despierta por timeout, cada 10 s)
        accesslog_flush();
    }
    LOG(INFO, "cerrando");

finally:
    // destruir el selector cierra los sockets pasivos (passive_close)
    if (selector != NULL) {
        selector_destroy(selector);
    }
    selector_close();
    accesslog_close();
    return ret;
}
