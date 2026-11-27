#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>

#include "resolv.h"
#include "connecting.h"
#include "socks5nio.h"

struct resolv_job {
    fd_selector    selector;
    int            fd;
    struct socks5 *conn;
};

static void *
resolv_thread(void *arg) {
    struct resolv_job *job = arg;
    struct socks5     *s   = job->conn;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned) ntohs(s->request.dest_port));

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *result = NULL;
    getaddrinfo(s->request.dest.fqdn, port_str, &hints, &result);
    s->origin_resolution = result;

    selector_notify_block(job->selector, job->fd);
    free(job);
    return NULL;
}

unsigned
resolv_init(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);

    struct resolv_job *job = malloc(sizeof(*job));
    if(job == NULL) {
        return socksv5_request_reply(key, SOCKS_REPLY_GENERAL_FAILURE);
    }
    job->selector = key->s;
    job->fd       = key->fd;
    job->conn     = s;
    socks5_ref(s);

    pthread_t tid;
    if(pthread_create(&tid, NULL, resolv_thread, job) != 0) {
        socks5_unref(s);
        free(job);
        return socksv5_request_reply(key, SOCKS_REPLY_GENERAL_FAILURE);
    }
    pthread_detach(tid);

    selector_set_interest_key(key, OP_NOOP);
    return REQUEST_RESOLV;
}

unsigned
resolv_block_ready(struct selector_key *key) {
    struct socks5     *s           = ATTACHMENT(key);
    const int          client_alive = (s->client_fd != -1);
    socks5_unref(s);
    if(!client_alive) {
        return SOCKS_ERROR;
    }
    if(s->origin_resolution == NULL) {
        return socksv5_request_reply(key, SOCKS_REPLY_HOST_UNREACHABLE);
    }
    s->origin_resolution_current = s->origin_resolution;
    return connecting_try_next(key);
}
