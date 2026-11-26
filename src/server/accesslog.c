#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "accesslog.h"

// RFC 1929 limita el usuario a 255 bytes y RFC 1928 el FQDN a 255: entran enteros.
#define USER_BUF 256
#define DEST_BUF 256
// "[" + IPv6 (hasta 45) + "]:" + puerto (5) + '\0'
#define ADDR_BUF 64
// Buffer de stdio del archivo: las líneas se juntan acá y se vuelcan en accesslog_flush().
#define FILE_BUF_SIZE (16 * 1024)

static FILE *out = NULL;
static bool  owns_file = false;  // true si hay que fclose() (no es stdout)
static char  file_buf[FILE_BUF_SIZE];

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
    // Totalmente bufferizado: write() al archivo solo cuando se llena o en accesslog_flush().
    setvbuf(out, file_buf, _IOFBF, sizeof(file_buf));
    owns_file = true;
    return true;
}

void
accesslog_flush(void) {
    if (out != NULL) {
        fflush(out);
    }
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

// "host:puerto"; una IPv6 va entre corchetes ("[::1]:5000") para que no se confunda con el puerto.
static void
format_hostport(char *dst, const size_t dstlen, const char *host, const bool ipv6, const unsigned port) {
    snprintf(dst, dstlen, ipv6 ? "[%s]:%u" : "%s:%u", host, port);
}

static void
format_client(char *dst, const size_t dstlen, const struct sockaddr *client) {
    char ip[INET6_ADDRSTRLEN];
    if (client != NULL && client->sa_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *) client;
        if (inet_ntop(AF_INET, &a->sin_addr, ip, sizeof(ip)) != NULL) {
            format_hostport(dst, dstlen, ip, false, ntohs(a->sin_port));
            return;
        }
    } else if (client != NULL && client->sa_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *) client;
        if (inet_ntop(AF_INET6, &a->sin6_addr, ip, sizeof(ip)) != NULL) {
            format_hostport(dst, dstlen, ip, true, ntohs(a->sin6_port));
            return;
        }
    }
    snprintf(dst, dstlen, "-");
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

    char cli[ADDR_BUF];
    format_client(cli, sizeof(cli), client);

    char host[DEST_BUF];  // un FQDN de 255 bytes entra entero
    sanitize(host, sizeof(host), dest);
    if (host[0] == '\0') {
        snprintf(host, sizeof(host), "-");
    }
    char dst[DEST_BUF + 8];
    format_hostport(dst, sizeof(dst), host, atyp == ACCESSLOG_IPV6, (unsigned) dest_port);

    // Sin fflush acá: la línea queda en el buffer hasta accesslog_flush() (una vez por
    // vuelta del selector) o hasta que el buffer se llene. Así no hay un write() por request.
    fprintf(out, "%s user=%s client=%s atyp=%s dest=%s rep=0x%02x\n",
            when, who, cli, atyp_name(atyp), dst, (unsigned) rep);
}
