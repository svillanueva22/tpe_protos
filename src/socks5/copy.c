#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

#include "copy.h"
#include "socks5nio.h"
#include "metrics.h"
#include "selector.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static struct copy *
copy_ptr(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    if(key->fd == s->client_fd) return &s->client.copy;
    if(key->fd == s->origin_fd) return &s->orig.copy;
    abort();
}

static bool
copy_done(const struct socks5 *s) {
    return s->client.copy.duplex == OP_NOOP
        && s->orig.copy.duplex   == OP_NOOP;
}

void
copy_arrival(unsigned state, struct selector_key *key) {
    (void) state;
    struct socks5 *s = ATTACHMENT(key);

    s->client.copy.fd    = &s->client_fd;
    s->client.copy.rb    = &s->read_buffer;
    s->client.copy.wb    = &s->write_buffer;
    s->client.copy.other = &s->orig.copy;
    s->client.copy.duplex = OP_READ
                          | (buffer_can_read(&s->write_buffer) ? OP_WRITE : OP_NOOP);

    s->orig.copy.fd    = &s->origin_fd;
    s->orig.copy.rb    = &s->write_buffer;
    s->orig.copy.wb    = &s->read_buffer;
    s->orig.copy.other = &s->client.copy;
    s->orig.copy.duplex = OP_READ
                        | (buffer_can_read(&s->read_buffer) ? OP_WRITE : OP_NOOP);

    selector_set_interest(key->s, s->client_fd, s->client.copy.duplex);
    selector_set_interest(key->s, s->origin_fd, s->orig.copy.duplex);
}

unsigned
copy_read_ready(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct copy   *d = copy_ptr(key);

    size_t   count;
    uint8_t *ptr = buffer_write_ptr(d->rb, &count);
    if(count == 0) {
        return COPY;
    }

    const ssize_t n = recv(*d->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(d->rb, n);
        metrics_bytes(key->fd == s->client_fd ? METRICS_C2O : METRICS_O2C, (size_t) n);
        d->other->duplex |= OP_WRITE;
        selector_set_interest(key->s, *d->other->fd, d->other->duplex);
    } else if(n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        d->duplex = INTEREST_OFF(d->duplex, OP_READ);
        selector_set_interest(key->s, *d->fd, d->duplex);
        if(!buffer_can_read(d->rb)) {
            shutdown(*d->other->fd, SHUT_WR);
            d->other->duplex = INTEREST_OFF(d->other->duplex, OP_WRITE);
            selector_set_interest(key->s, *d->other->fd, d->other->duplex);
        }
    }

    return copy_done(s) ? SOCKS_DONE : COPY;
}

unsigned
copy_write_ready(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct copy   *d = copy_ptr(key);

    size_t   count;
    uint8_t *ptr = buffer_read_ptr(d->wb, &count);
    if(count == 0) {
        d->duplex = INTEREST_OFF(d->duplex, OP_WRITE);
        selector_set_interest(key->s, *d->fd, d->duplex);
        return copy_done(s) ? SOCKS_DONE : COPY;
    }

    const ssize_t n = send(*d->fd, ptr, count, MSG_NOSIGNAL);
    if(n < 0) {
        if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return COPY;
        }
        return SOCKS_DONE;
    }
    buffer_read_adv(d->wb, n);

    if(!buffer_can_read(d->wb)) {
        d->duplex = INTEREST_OFF(d->duplex, OP_WRITE);
        selector_set_interest(key->s, *d->fd, d->duplex);
        if(!(d->other->duplex & OP_READ)) {
            shutdown(*d->fd, SHUT_WR);
        }
    }

    return copy_done(s) ? SOCKS_DONE : COPY;
}
