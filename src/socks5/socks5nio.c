/**
 * socks5nio.c - controla el flujo de un proxy SOCKSv5 (sockets no bloqueantes)
 *
 * Basado en código de Juan F. Codagnone — Protocolos de Comunicación, ITBA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>

#include "socks5nio.h"
#include "resolv.h"
#include "connecting.h"
#include "copy.h"
#include "users.h"
#include "metrics.h"
#include "accesslog.h"
#include "logger.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  // no existe en macOS; SIGPIPE se ignora en main
#endif

#define N(x) (sizeof(x)/sizeof((x)[0]))

////////////////////////////////////////////////////////////////////////////////
// I/O parcial
////////////////////////////////////////////////////////////////////////////////

/** -1 si hubo error o EOF, 0 si hay que volver a esperar, si no los bytes leídos */
static ssize_t
recv_to_buffer(const int fd, buffer *b) {
    size_t count;
    uint8_t *ptr = buffer_write_ptr(b, &count);
    if(count == 0) {
        return -1;
    }
    const ssize_t n = recv(fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(b, n);
        return n;
    }
    if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return 0;
    }
    return -1;
}

/** -1 si hubo error, 0 si quedan bytes por mandar, 1 si se vació el buffer */
static int
send_from_buffer(const int fd, buffer *b) {
    size_t count;
    uint8_t *ptr = buffer_read_ptr(b, &count);
    const ssize_t n = send(fd, ptr, count, MSG_NOSIGNAL);
    if(n < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    }
    buffer_read_adv(b, n);
    return buffer_can_read(b) ? 0 : 1;
}

////////////////////////////////////////////////////////////////////////////////
// HELLO
////////////////////////////////////////////////////////////////////////////////

static void
on_hello_method(struct hello_parser *p, const uint8_t method) {
    uint8_t *selected = p->data;

    if(SOCKS_HELLO_USERNAME_PASSWORD == method) {
        *selected = method;
    }
}

static void
hello_read_init(const unsigned state, struct selector_key *key) {
    (void) state;
    struct hello_st *d = &ATTACHMENT(key)->client.hello;

    d->rb                              = &ATTACHMENT(key)->read_buffer;
    d->wb                              = &ATTACHMENT(key)->write_buffer;
    d->method                          = SOCKS_HELLO_NO_ACCEPTABLE_METHODS;
    d->parser.data                     = &d->method;
    d->parser.on_authentication_method = on_hello_method;
    hello_parser_init(&d->parser);
}

static unsigned
hello_read(struct selector_key *key) {
    struct hello_st *d = &ATTACHMENT(key)->client.hello;
    bool error = false;

    const ssize_t n = recv_to_buffer(key->fd, d->rb);
    if(n < 0) {
        return SOCKS_ERROR;
    }
    const enum hello_state st = hello_consume(d->rb, &d->parser, &error);
    if(!hello_is_done(st, &error)) {
        return HELLO_READ;
    }
    if(error
       || -1 == hello_marshall(d->wb, d->method)
       || SELECTOR_SUCCESS != selector_set_interest_key(key, OP_WRITE)) {
        return SOCKS_ERROR;
    }
    return HELLO_WRITE;
}

static unsigned
hello_write(struct selector_key *key) {
    struct hello_st *d = &ATTACHMENT(key)->client.hello;

    const int r = send_from_buffer(key->fd, d->wb);
    if(r <= 0) {
        return r < 0 ? SOCKS_ERROR : HELLO_WRITE;
    }
    // con 0xFF el RFC pide que el cliente cierre; cerramos nosotros igual
    if(SOCKS_HELLO_NO_ACCEPTABLE_METHODS == d->method
       || SELECTOR_SUCCESS != selector_set_interest_key(key, OP_READ)) {
        return SOCKS_ERROR;
    }
    return AUTH_READ;
}

////////////////////////////////////////////////////////////////////////////////
// AUTH (RFC 1929)
////////////////////////////////////////////////////////////////////////////////

static void
auth_read_init(const unsigned state, struct selector_key *key) {
    (void) state;
    struct auth_st *d = &ATTACHMENT(key)->client.auth;

    d->rb     = &ATTACHMENT(key)->read_buffer;
    d->wb     = &ATTACHMENT(key)->write_buffer;
    d->status = AUTH_STATUS_FAIL;
    auth_parser_init(&d->parser);
}

static void
auth_read_close(const unsigned state, struct selector_key *key) {
    (void) state;
    auth_parser_close(&ATTACHMENT(key)->client.auth.parser);
}

static unsigned
auth_read(struct selector_key *key) {
    struct socks5  *s = ATTACHMENT(key);
    struct auth_st *d = &s->client.auth;
    bool error = false;

    const ssize_t n = recv_to_buffer(key->fd, d->rb);
    if(n < 0) {
        return SOCKS_ERROR;
    }
    const enum auth_state st = auth_consume(d->rb, &d->parser, &error);
    if(!auth_is_done(st, &error)) {
        return AUTH_READ;
    }
    if(error) {
        return SOCKS_ERROR;
    }

    if(users_check(d->parser.username, d->parser.password)) {
        d->status = AUTH_STATUS_OK;
        memcpy(s->username, d->parser.username, sizeof(s->username));
    } else {
        metrics_auth_fail();
    }
    if(-1 == auth_marshall(d->wb, d->status)
       || SELECTOR_SUCCESS != selector_set_interest_key(key, OP_WRITE)) {
        return SOCKS_ERROR;
    }
    return AUTH_WRITE;
}

static unsigned
auth_write(struct selector_key *key) {
    struct auth_st *d = &ATTACHMENT(key)->client.auth;

    const int r = send_from_buffer(key->fd, d->wb);
    if(r <= 0) {
        return r < 0 ? SOCKS_ERROR : AUTH_WRITE;
    }
    if(AUTH_STATUS_OK != d->status
       || SELECTOR_SUCCESS != selector_set_interest_key(key, OP_READ)) {
        return SOCKS_ERROR;
    }
    return REQUEST_READ;
}

////////////////////////////////////////////////////////////////////////////////
// REQUEST
////////////////////////////////////////////////////////////////////////////////

static void
request_read_init(const unsigned state, struct selector_key *key) {
    (void) state;
    struct socks5     *s = ATTACHMENT(key);
    struct request_st *d = &s->client.request;

    d->rb             = &s->read_buffer;
    d->wb             = &s->write_buffer;
    d->parser.request = &s->request;
    request_parser_init(&d->parser);
}

static unsigned
request_connect(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    if(s->request.atyp == SOCKS_ATYP_DOMAIN) {
        return resolv_init(key);
    }
    return connecting_start(key);
}

static unsigned
request_process(struct selector_key *key, const enum request_state st) {
    switch(st) {
        case request_error_unsupported_atyp:
            return socksv5_request_reply(key, SOCKS_REPLY_ATYP_NOT_SUPPORTED);
        case request_error_invalid_fqdn:
            return socksv5_request_reply(key, SOCKS_REPLY_GENERAL_FAILURE);
        case request_error_unsupported_version:
            return SOCKS_ERROR;
        default:
            break;
    }
    if(SOCKS_CMD_CONNECT != ATTACHMENT(key)->request.cmd) {
        return socksv5_request_reply(key, SOCKS_REPLY_COMMAND_NOT_SUPPORTED);
    }
    return request_connect(key);
}

static unsigned
request_read(struct selector_key *key) {
    struct request_st *d = &ATTACHMENT(key)->client.request;

    const ssize_t n = recv_to_buffer(key->fd, d->rb);
    if(n < 0) {
        return SOCKS_ERROR;
    }
    const enum request_state st = request_consume(d->rb, &d->parser, NULL);
    if(!request_is_done(st, NULL)) {
        return REQUEST_READ;
    }
    return request_process(key, st);
}

unsigned
socksv5_request_reply(struct selector_key *key, const enum socks_reply reply) {
    struct socks5 *s = ATTACHMENT(key);
    struct sockaddr_storage bnd;
    socklen_t bnd_len = sizeof(bnd);
    const struct sockaddr *bnd_addr = NULL;

    s->reply = reply;
    if(SOCKS_REPLY_SUCCEEDED == reply && s->origin_fd != -1
       && 0 == getsockname(s->origin_fd, (struct sockaddr *) &bnd, &bnd_len)) {
        bnd_addr = (struct sockaddr *) &bnd;
    }
    metrics_rep(reply);
    accesslog_write(s->username, (struct sockaddr *) &s->client_addr, &s->request, reply);

    if(-1 == request_marshall(&s->write_buffer, reply, bnd_addr)
       || SELECTOR_SUCCESS != selector_set_interest(key->s, s->client_fd, OP_WRITE)) {
        return SOCKS_ERROR;
    }
    if(s->origin_fd != -1
       && SELECTOR_SUCCESS != selector_set_interest(key->s, s->origin_fd, OP_NOOP)) {
        return SOCKS_ERROR;
    }
    return REQUEST_WRITE;
}

static unsigned
request_write(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);

    const int r = send_from_buffer(key->fd, &s->write_buffer);
    if(r <= 0) {
        return r < 0 ? SOCKS_ERROR : REQUEST_WRITE;
    }
    return SOCKS_REPLY_SUCCEEDED == s->reply ? COPY : SOCKS_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
// Tabla de estados
////////////////////////////////////////////////////////////////////////////////

static const struct state_definition client_statbl[] = {
    {
        .state            = HELLO_READ,
        .on_arrival       = hello_read_init,
        .on_read_ready    = hello_read,
    }, {
        .state            = HELLO_WRITE,
        .on_write_ready   = hello_write,
    }, {
        .state            = AUTH_READ,
        .on_arrival       = auth_read_init,
        .on_departure     = auth_read_close,
        .on_read_ready    = auth_read,
    }, {
        .state            = AUTH_WRITE,
        .on_write_ready   = auth_write,
    }, {
        .state            = REQUEST_READ,
        .on_arrival       = request_read_init,
        .on_read_ready    = request_read,
    }, {
        .state            = REQUEST_RESOLV,
        .on_block_ready   = resolv_block_ready,
    }, {
        .state            = REQUEST_CONNECTING,
        .on_write_ready   = connecting_write_ready,
    }, {
        .state            = REQUEST_WRITE,
        .on_write_ready   = request_write,
    }, {
        .state            = COPY,
        .on_arrival       = copy_arrival,
        .on_read_ready    = copy_read_ready,
        .on_write_ready   = copy_write_ready,
    }, {
        .state            = SOCKS_DONE,
    }, {
        .state            = SOCKS_ERROR,
    },
};

////////////////////////////////////////////////////////////////////////////////
// Pool de struct socks5
////////////////////////////////////////////////////////////////////////////////

static const unsigned  max_pool  = 50;
static unsigned        pool_size = 0;
static struct socks5  *pool      = NULL;

static struct socks5 *
socks5_new(const int client_fd) {
    struct socks5 *ret;

    if(pool == NULL) {
        ret = malloc(sizeof(*ret));
    } else {
        ret  = pool;
        pool = pool->next;
        // FIX: el esqueleto no descontaba pool_size al sacar del pool
        pool_size--;
    }
    if(ret == NULL) {
        return NULL;
    }
    memset(ret, 0x00, sizeof(*ret));

    ret->client_fd       = client_fd;
    ret->origin_fd       = -1;
    ret->client_addr_len = sizeof(ret->client_addr);

    ret->stm.initial   = HELLO_READ;
    ret->stm.max_state = SOCKS_ERROR;
    ret->stm.states    = client_statbl;
    stm_init(&ret->stm);

    buffer_init(&ret->read_buffer,  N(ret->raw_buff_a), ret->raw_buff_a);
    buffer_init(&ret->write_buffer, N(ret->raw_buff_b), ret->raw_buff_b);

    ret->references = 1;
    metrics_conn_open();
    return ret;
}

void
socks5_ref(struct socks5 *s) {
    s->references++;
}

void
socks5_unref(struct socks5 *s) {
    if(s == NULL) {
        return;
    }
    if(s->references > 1) {
        s->references--;
        return;
    }
    // FIX: el esqueleto solo liberaba la resolución fuera del pool
    if(s->origin_resolution != NULL) {
        freeaddrinfo(s->origin_resolution);
        s->origin_resolution = NULL;
    }
    metrics_conn_close();
    if(pool_size < max_pool) {
        s->next = pool;
        pool    = s;
        pool_size++;
    } else {
        free(s);
    }
}

void
socksv5_pool_destroy(void) {
    struct socks5 *next, *s;
    for(s = pool; s != NULL; s = next) {
        next = s->next;
        free(s);
    }
    pool      = NULL;
    pool_size = 0;
}

////////////////////////////////////////////////////////////////////////////////
// Handlers del selector
////////////////////////////////////////////////////////////////////////////////

static void socksv5_read   (struct selector_key *key);
static void socksv5_write  (struct selector_key *key);
static void socksv5_block  (struct selector_key *key);
static void socksv5_close  (struct selector_key *key);
static const struct fd_handler socks5_handler = {
    .handle_read   = socksv5_read,
    .handle_write  = socksv5_write,
    .handle_close  = socksv5_close,
    .handle_block  = socksv5_block,
};

const struct fd_handler *
socksv5_handler(void) {
    return &socks5_handler;
}

void
socksv5_passive_accept(struct selector_key *key) {
    struct sockaddr_storage       client_addr;
    socklen_t                     client_addr_len = sizeof(client_addr);
    struct socks5                *state           = NULL;

    const int client = accept(key->fd, (struct sockaddr *) &client_addr,
                              &client_addr_len);
    if(client == -1) {
        if(errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != ECONNABORTED) {
            LOG(ERROR, "accept: %s", strerror(errno));
        }
        return;
    }
    if(selector_fd_set_nio(client) == -1) {
        goto fail;
    }
    state = socks5_new(client);
    if(state == NULL) {
        goto fail;
    }
    memcpy(&state->client_addr, &client_addr, client_addr_len);
    state->client_addr_len = client_addr_len;

    if(SELECTOR_SUCCESS != selector_register(key->s, client, &socks5_handler,
                                             OP_READ, state)) {
        goto fail;
    }
    return;
fail:
    close(client);
    socks5_unref(state);
}

static void
socksv5_done(struct selector_key *key);

static void
socksv5_read(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum socks_v5state st = stm_handler_read(stm, key);

    if(SOCKS_ERROR == st || SOCKS_DONE == st) {
        socksv5_done(key);
    }
}

static void
socksv5_write(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum socks_v5state st = stm_handler_write(stm, key);

    if(SOCKS_ERROR == st || SOCKS_DONE == st) {
        socksv5_done(key);
    }
}

static void
socksv5_block(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum socks_v5state st = stm_handler_block(stm, key);

    if(SOCKS_ERROR == st || SOCKS_DONE == st) {
        socksv5_done(key);
    }
}

/** cada fd registrado se cierra acá, al desregistrarse (también en selector_destroy) */
static void
socksv5_close(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);

    if(s->client_fd == key->fd) {
        s->client_fd = -1;
    } else if(s->origin_fd == key->fd) {
        s->origin_fd = -1;
    }
    close(key->fd);
    if(s->references == 1) {
        stm_handler_close(&s->stm, key);
    }
    socks5_unref(s);
}

static void
socksv5_done(struct selector_key *key) {
    // se copian antes: el primer unregister puede liberar el struct
    const int fds[] = {
        ATTACHMENT(key)->client_fd,
        ATTACHMENT(key)->origin_fd,
    };
    for(unsigned i = 0; i < N(fds); i++) {
        // FIX: el esqueleto hacía abort(); un fd no registrado solo se cierra
        if(fds[i] != -1 && SELECTOR_SUCCESS != selector_unregister_fd(key->s, fds[i])) {
            close(fds[i]);
        }
    }
}
