#include <string.h>

#include "auth.h"

void
auth_parser_init(struct auth_parser *p) {
    memset(p, 0, sizeof(*p));
    p->state = auth_version;
}

enum auth_state
auth_parser_feed(struct auth_parser *p, const uint8_t b) {
    switch(p->state) {
        case auth_version:
            p->state = AUTH_VERSION == b ? auth_ulen : auth_error_unsupported_version;
            break;
        case auth_ulen:
        case auth_plen:
            if(b == 0) {
                p->state = auth_error_invalid_length;
            } else {
                p->remaining = b;
                p->i         = 0;
                p->state     = p->state == auth_ulen ? auth_uname : auth_passwd;
            }
            break;
        case auth_uname:
        case auth_passwd: {
            char *field = p->state == auth_uname ? p->username : p->password;
            field[p->i++] = (char) b;
            if(--p->remaining == 0) {
                field[p->i] = '\0';
                p->state = p->state == auth_uname ? auth_plen : auth_done;
            }
            break;
        }
        case auth_done:
        case auth_error_unsupported_version:
        case auth_error_invalid_length:
            break;
    }
    return p->state;
}

bool
auth_is_done(const enum auth_state state, bool *errored) {
    bool ret = false;
    switch(state) {
        case auth_error_unsupported_version:
        case auth_error_invalid_length:
            if(errored != NULL) {
                *errored = true;
            }
            ret = true;
            break;
        case auth_done:
            ret = true;
            break;
        default:
            break;
    }
    return ret;
}

enum auth_state
auth_consume(buffer *b, struct auth_parser *p, bool *errored) {
    enum auth_state st = p->state;

    while(!auth_is_done(st, errored) && buffer_can_read(b)) {
        st = auth_parser_feed(p, buffer_read(b));
    }
    return st;
}

const char *
auth_error(const struct auth_parser *p) {
    switch(p->state) {
        case auth_error_unsupported_version:
            return "unsupported version";
        case auth_error_invalid_length:
            return "invalid length";
        default:
            return "";
    }
}

void
auth_parser_close(struct auth_parser *p) {
    memset(p->password, 0, sizeof(p->password));
}

int
auth_marshall(buffer *b, const uint8_t status) {
    size_t n;
    uint8_t *buff = buffer_write_ptr(b, &n);
    if(n < 2) {
        return -1;
    }
    buff[0] = AUTH_VERSION;
    buff[1] = status;
    buffer_write_adv(b, 2);
    return 2;
}
