#include <string.h>

#include "config.h"
#include "pop3_sniffer.h"

// Copia `src` a `dst` truncando; deja siempre un '\0' final.
static void
copy_trunc(char *dst, const size_t dstlen, const char *src) {
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    const size_t n = strlen(src) < dstlen - 1 ? strlen(src) : dstlen - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool
pop3_sniffer_applies(const uint16_t dest_port) {
    return config_get()->sniffer_enabled && dest_port == POP3_SNIFFER_PORT;
}

void
pop3_sniffer_init(struct pop3_sniffer *s, const char *proxy_user,
                  const char *dest, const uint16_t dest_port) {
    memset(s, 0, sizeof(*s));
    s->active = true;
    s->dest_port = dest_port;
    copy_trunc(s->proxy_user, sizeof(s->proxy_user), proxy_user);
    copy_trunc(s->dest, sizeof(s->dest), dest);
}

// TODO Entrega 2: parser incremental de USER/PASS confirmado por +OK (RFC 1939).
void
pop3_sniffer_feed(struct pop3_sniffer *s, const uint8_t *ptr, const size_t n,
                  const enum pop3_dir dir) {
    (void) s;
    (void) ptr;
    (void) n;
    (void) dir;
}
