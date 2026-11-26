/**
 * hello.c - parser incremental del mensaje de selección de método (RFC 1928 §3)
 *
 * Basado en código de Juan F. Codagnone — Protocolos de Comunicación, ITBA
 */
#include <stdio.h>

#include "hello.h"

void
hello_parser_init(struct hello_parser *p) {
    p->state     = hello_version;
    p->remaining = 0;
}

enum hello_state
hello_parser_feed(struct hello_parser *p, const uint8_t b) {
    switch(p->state) {
        case hello_version:
            p->state = SOCKS_VERSION == b ? hello_nmethods
                                          : hello_error_unsupported_version;
            break;
        case hello_nmethods:
            p->remaining = b;
            // NMETHODS = 0: no hay métodos aceptables, se responde 0xFF
            p->state     = b == 0 ? hello_done : hello_methods;
            break;
        case hello_methods:
            if(p->on_authentication_method != NULL) {
                p->on_authentication_method(p, b);
            }
            p->remaining--;
            if(p->remaining == 0) {
                p->state = hello_done;
            }
            break;
        case hello_done:
        case hello_error_unsupported_version:
            break;
    }
    return p->state;
}

bool
hello_is_done(const enum hello_state state, bool *errored) {
    bool ret = false;
    switch(state) {
        case hello_error_unsupported_version:
            if(errored != NULL) {
                *errored = true;
            }
            ret = true;
            break;
        case hello_done:
            ret = true;
            break;
        default:
            break;
    }
    return ret;
}

enum hello_state
hello_consume(buffer *b, struct hello_parser *p, bool *errored) {
    enum hello_state st = p->state;

    while(!hello_is_done(st, errored) && buffer_can_read(b)) {
        st = hello_parser_feed(p, buffer_read(b));
    }
    return st;
}

const char *
hello_error(const struct hello_parser *p) {
    switch(p->state) {
        case hello_error_unsupported_version:
            return "unsupported version";
        default:
            return "";
    }
}

void
hello_parser_close(struct hello_parser *p) {
    (void) p;
}

int
hello_marshall(buffer *b, const uint8_t method) {
    size_t n;
    uint8_t *buff = buffer_write_ptr(b, &n);
    if(n < 2) {
        return -1;
    }
    buff[0] = SOCKS_VERSION;
    buff[1] = method;
    buffer_write_adv(b, 2);
    return 2;
}
