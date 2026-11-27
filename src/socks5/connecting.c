#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "connecting.h"
#include "socks5nio.h"

#define CONNECT_TIMEOUT_SEC 30  /* TODO: reemplazar por config_get()->connect_timeout_s */

static enum socks_reply
errno_to_reply(const int e) {
    switch(e) {
        case ENETUNREACH:  return SOCKS_REPLY_NETWORK_UNREACHABLE;
        case EHOSTUNREACH: return SOCKS_REPLY_HOST_UNREACHABLE;
        case ECONNREFUSED: return SOCKS_REPLY_CONNECTION_REFUSED;
        case ETIMEDOUT:    return SOCKS_REPLY_TTL_EXPIRED;
        default:           return SOCKS_REPLY_GENERAL_FAILURE;
    }
}

unsigned
connecting_start(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);

    char str[INET6_ADDRSTRLEN];
    if(s->request.atyp == SOCKS_ATYP_IPV4) {
        inet_ntop(AF_INET,  &s->request.dest.ipv4, str, sizeof(str));
    } else {
        inet_ntop(AF_INET6, &s->request.dest.ipv6, str, sizeof(str));
    }
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned) ntohs(s->request.dest_port));

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_NUMERICHOST | AI_NUMERICSERV;

    s->origin_resolution = NULL;
    if(getaddrinfo(str, port_str, &hints, &s->origin_resolution) != 0
       || s->origin_resolution == NULL) {
        return socksv5_request_reply(key, SOCKS_REPLY_GENERAL_FAILURE);
    }
    s->origin_resolution_current = s->origin_resolution;
    return connecting_try_next(key);
}

unsigned
connecting_try_next(struct selector_key *key) {
    struct socks5 *s       = ATTACHMENT(key);
    int            last_err = EHOSTUNREACH;

    selector_set_interest(key->s, s->client_fd, OP_NOOP);

    while(s->origin_resolution_current != NULL) {
        const struct addrinfo *ai = s->origin_resolution_current;

        memcpy(&s->origin_addr, ai->ai_addr, ai->ai_addrlen);
        s->origin_addr_len = ai->ai_addrlen;

        int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if(sock < 0) {
            last_err = errno;
            s->origin_resolution_current = s->origin_resolution_current->ai_next;
            continue;
        }
        if(selector_fd_set_nio(sock) == -1) {
            last_err = errno;
            close(sock);
            s->origin_resolution_current = s->origin_resolution_current->ai_next;
            continue;
        }
        const int r = connect(sock, ai->ai_addr, ai->ai_addrlen);
        if(r == -1 && errno != EINPROGRESS) {
            last_err = errno;
            close(sock);
            s->origin_resolution_current = s->origin_resolution_current->ai_next;
            continue;
        }

        s->origin_fd = sock;
        s->references++;
        if(SELECTOR_SUCCESS != selector_register(key->s, sock, socksv5_handler(), OP_WRITE, s)) {
            s->references--;
            s->origin_fd = -1;
            last_err = errno;
            close(sock);
            s->origin_resolution_current = s->origin_resolution_current->ai_next;
            continue;
        }
        return REQUEST_CONNECTING;
    }

    return socksv5_request_reply(key, errno_to_reply(last_err));
}

unsigned
connecting_write_ready(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);

    int err = 0;
    socklen_t len = sizeof(err);
    if(getsockopt(key->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
        err = errno;
    }

    if(err == 0) {
        return socksv5_request_reply(key, SOCKS_REPLY_SUCCEEDED);
    }

    if(SELECTOR_SUCCESS != selector_unregister_fd(key->s, key->fd)) {
        close(key->fd);
        s->origin_fd   = -1;
        s->references--;
    }
    s->origin_resolution_current = s->origin_resolution_current->ai_next;
    return connecting_try_next(key);
}
