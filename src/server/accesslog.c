#include <stdio.h>
#include <string.h>
#include <time.h>

#include "accesslog.h"
#include "netutils.h"

// RFC 1929 limita el usuario a 255 bytes y RFC 1928 el FQDN a 255: entran enteros.
#define USER_BUF 256
#define DEST_BUF 256

static FILE *out = NULL;
static bool  owns_file = false;  // true si hay que fclose() (no es stdout)

bool
accesslog_init(const char *path) {
    accesslog_close();
    if (path == NULL) {
        out = stdout;
        owns_file = false;
        return true;
    }
    out = fopen(path, "a");
    if (out == NULL) {
        return false;
    }
    owns_file = true;
    return true;
}

void
accesslog_close(void) {
    if (out != NULL) {
        fflush(out);
        if (owns_file) {
            fclose(out);
        }
    }
    out = NULL;
    owns_file = false;
}

// Copia `src` a `dst` reemplazando espacios y caracteres de control por '_'.
static void
sanitize(char *dst, const size_t dstlen, const char *src) {
    size_t i = 0;
    for (; src != NULL && src[i] != '\0' && i + 1 < dstlen; i++) {
        const unsigned char c = (unsigned char) src[i];
        dst[i] = (c <= 0x20 || c == 0x7f) ? '_' : (char) c;
    }
    dst[i] = '\0';
}

static const char *
atyp_name(const enum accesslog_atyp atyp) {
    switch (atyp) {
        case ACCESSLOG_IPV4: return "ipv4";
        case ACCESSLOG_FQDN: return "fqdn";
        case ACCESSLOG_IPV6: return "ipv6";
    }
    return "unknown";
}

void
accesslog_write(const char *user, const struct sockaddr *client,
                const enum accesslog_atyp atyp, const char *dest,
                const uint16_t dest_port, const uint8_t rep) {
    if (out == NULL) {
        return;
    }

    char when[32];
    const time_t now = time(NULL);
    struct tm tm;
    if (gmtime_r(&now, &tm) == NULL || strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
        snprintf(when, sizeof(when), "-");
    }

    char who[USER_BUF];
    sanitize(who, sizeof(who), user);
    if (who[0] == '\0') {
        snprintf(who, sizeof(who), "-");
    }

    char cli[SOCKADDR_TO_HUMAN_MIN] = "-";
    if (client != NULL) {
        sockaddr_to_human(cli, sizeof(cli), client);
    }

    char dst[DEST_BUF];  // un FQDN de 255 bytes entra entero
    sanitize(dst, sizeof(dst), dest);
    if (dst[0] == '\0') {
        snprintf(dst, sizeof(dst), "-");
    }

    fprintf(out, "%s user=%s client=%s atyp=%s dest=%s:%u rep=0x%02x\n",
            when, who, cli, atyp_name(atyp), dst, (unsigned) dest_port, (unsigned) rep);
    // Cada línea se vuelca enseguida: si el proceso muere no se pierde el último request.
    fflush(out);
}
