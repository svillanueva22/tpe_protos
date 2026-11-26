/**
 * request.c - parser incremental del request SOCKSv5 (RFC 1928 §4)
 *
 * Basado en código de Juan F. Codagnone — Protocolos de Comunicación, ITBA
 */
#include <string.h>

#include "hello.h"
#include "request.h"

void
request_parser_init(struct request_parser *p) {
    p->state = request_version;
    p->n     = 0;
    p->i     = 0;
    memset(p->request, 0, sizeof(*p->request));
}

static enum request_state
dstaddr_begin(struct request_parser *p, const uint8_t n) {
    p->n = n;
    p->i = 0;
    return request_dstaddr;
}

static enum request_state
atyp(struct request_parser *p, const uint8_t b) {
    p->request->atyp = b;
    switch(b) {
        case SOCKS_ATYP_IPV4:
            return dstaddr_begin(p, sizeof(p->request->dest.ipv4));
        case SOCKS_ATYP_IPV6:
            return dstaddr_begin(p, sizeof(p->request->dest.ipv6));
        case SOCKS_ATYP_DOMAIN:
            return request_dstaddr_fqdn_len;
        default:
            return request_error_unsupported_atyp;
    }
}

static enum request_state
dstaddr(struct request_parser *p, const uint8_t b) {
    struct request *r = p->request;
    uint8_t *dst;

    switch(r->atyp) {
        case SOCKS_ATYP_IPV4:
            dst = (uint8_t *) &r->dest.ipv4;
            break;
        case SOCKS_ATYP_IPV6:
            dst = (uint8_t *) &r->dest.ipv6;
            break;
        default:
            dst = (uint8_t *) r->dest.fqdn;
            break;
    }
    dst[p->i++] = b;
    if(p->i < p->n) {
        return request_dstaddr;
    }
    if(r->atyp == SOCKS_ATYP_DOMAIN) {
        r->dest.fqdn[p->i] = '\0';
    }
    p->i = 0;
    return request_dstport;
}

static enum request_state
dstport(struct request_parser *p, const uint8_t b) {
    uint8_t *port = (uint8_t *) &p->request->dest_port;
    port[p->i++] = b;
    return p->i < sizeof(p->request->dest_port) ? request_dstport : request_done;
}

enum request_state
request_parser_feed(struct request_parser *p, const uint8_t b) {
    switch(p->state) {
        case request_version:
            p->state = SOCKS_VERSION == b ? request_cmd : request_error_unsupported_version;
            break;
        case request_cmd:
            // el CMD se valida al terminar, así se lee el mensaje completo
            p->request->cmd = b;
            p->state        = request_rsv;
            break;
        case request_rsv:
            p->state = request_atyp;
            break;
        case request_atyp:
            p->state = atyp(p, b);
            break;
        case request_dstaddr_fqdn_len:
            p->state = b == 0 ? request_error_invalid_fqdn : dstaddr_begin(p, b);
            break;
        case request_dstaddr:
            p->state = dstaddr(p, b);
            break;
        case request_dstport:
            p->state = dstport(p, b);
            break;
        case request_done:
        case request_error_unsupported_version:
        case request_error_unsupported_atyp:
        case request_error_invalid_fqdn:
            break;
    }
    return p->state;
}

bool
request_is_done(const enum request_state state, bool *errored) {
    bool ret = false;
    switch(state) {
        case request_error_unsupported_version:
        case request_error_unsupported_atyp:
        case request_error_invalid_fqdn:
            if(errored != NULL) {
                *errored = true;
            }
            ret = true;
            break;
        case request_done:
            ret = true;
            break;
        default:
            break;
    }
    return ret;
}

enum request_state
request_consume(buffer *b, struct request_parser *p, bool *errored) {
    enum request_state st = p->state;

    while(!request_is_done(st, errored) && buffer_can_read(b)) {
        st = request_parser_feed(p, buffer_read(b));
    }
    return st;
}

const char *
request_error(const struct request_parser *p) {
    switch(p->state) {
        case request_error_unsupported_version:
            return "unsupported version";
        case request_error_unsupported_atyp:
            return "unsupported address type";
        case request_error_invalid_fqdn:
            return "invalid domain name";
        default:
            return "";
    }
}

void
request_parser_close(struct request_parser *p) {
    (void) p;
}

int
request_marshall(buffer *b, const enum socks_reply reply, const struct sockaddr *bnd) {
    const uint8_t *addr;
    size_t addr_len;
    in_port_t port;
    uint8_t atyp;
    static const uint8_t any[4] = { 0 };

    if(bnd != NULL && bnd->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *) bnd;
        atyp     = SOCKS_ATYP_IPV4;
        addr     = (const uint8_t *) &in->sin_addr;
        addr_len = sizeof(in->sin_addr);
        port     = in->sin_port;
    } else if(bnd != NULL && bnd->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *) bnd;
        atyp     = SOCKS_ATYP_IPV6;
        addr     = (const uint8_t *) &in6->sin6_addr;
        addr_len = sizeof(in6->sin6_addr);
        port     = in6->sin6_port;
    } else {
        atyp     = SOCKS_ATYP_IPV4;
        addr     = any;
        addr_len = sizeof(any);
        port     = 0;
    }

    size_t n;
    uint8_t *buff = buffer_write_ptr(b, &n);
    const size_t len = 4 + addr_len + sizeof(port);
    if(n < len) {
        return -1;
    }
    buff[0] = SOCKS_VERSION;
    buff[1] = reply;
    buff[2] = 0x00;
    buff[3] = atyp;
    memcpy(buff + 4, addr, addr_len);
    memcpy(buff + 4 + addr_len, &port, sizeof(port));
    buffer_write_adv(b, len);
    return (int) len;
}
