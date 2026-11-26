/** Basado en código de Juan F. Codagnone — Protocolos de Comunicación, ITBA */
#ifndef REQUEST_H_Jx4mT9cWb2RkLpZ6vQnY8fHd
#define REQUEST_H_Jx4mT9cWb2RkLpZ6vQnY8fHd

#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "buffer.h"

/**
 * request.c - parser incremental del request SOCKSv5 (RFC 1928 §4)
 *
 *   +----+-----+-------+------+----------+----------+
 *   |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
 *   +----+-----+-------+------+----------+----------+
 *   | 1  |  1  | X'00' |  1   | Variable |    2     |
 *   +----+-----+-------+------+----------+----------+
 */

enum socks_cmd {
    SOCKS_CMD_CONNECT       = 0x01,
    SOCKS_CMD_BIND          = 0x02,
    SOCKS_CMD_UDP_ASSOCIATE = 0x03,
};

enum socks_atyp {
    SOCKS_ATYP_IPV4   = 0x01,
    SOCKS_ATYP_DOMAIN = 0x03,
    SOCKS_ATYP_IPV6   = 0x04,
};

/** RFC 1928 §6. Mapeo de errores en AGENTS.md §5.3 */
enum socks_reply {
    SOCKS_REPLY_SUCCEEDED             = 0x00,
    SOCKS_REPLY_GENERAL_FAILURE       = 0x01,
    SOCKS_REPLY_NOT_ALLOWED           = 0x02,
    SOCKS_REPLY_NETWORK_UNREACHABLE   = 0x03,
    SOCKS_REPLY_HOST_UNREACHABLE      = 0x04,
    SOCKS_REPLY_CONNECTION_REFUSED    = 0x05,
    SOCKS_REPLY_TTL_EXPIRED           = 0x06,
    SOCKS_REPLY_COMMAND_NOT_SUPPORTED = 0x07,
    SOCKS_REPLY_ATYP_NOT_SUPPORTED    = 0x08,
};

#define SOCKS_FQDN_MAX_LEN 255

union socks_addr {
    /** terminado en '\0' */
    char            fqdn[SOCKS_FQDN_MAX_LEN + 1];
    struct in_addr  ipv4;
    struct in6_addr ipv6;
};

struct request {
    /** uint8_t y no los enums: pueden llegar valores fuera de rango */
    uint8_t          cmd;
    uint8_t          atyp;
    union socks_addr dest;
    /** en network byte order */
    in_port_t        dest_port;
};

enum request_state {
    request_version,
    request_cmd,
    request_rsv,
    request_atyp,
    request_dstaddr_fqdn_len,
    request_dstaddr,
    request_dstport,
    request_done,
    request_error_unsupported_version,
    request_error_unsupported_atyp,
    request_error_invalid_fqdn,
};

struct request_parser {
    struct request    *request;
    enum request_state state;
    /** bytes que faltan del campo actual y bytes leídos */
    uint8_t            n;
    uint8_t            i;
};

/** `p->request' tiene que apuntar a donde dejar el resultado */
void
request_parser_init(struct request_parser *p);

enum request_state
request_parser_feed(struct request_parser *p, const uint8_t b);

/** igual que hello_consume: los bytes posteriores al mensaje quedan en `b' */
enum request_state
request_consume(buffer *b, struct request_parser *p, bool *errored);

bool
request_is_done(const enum request_state state, bool *errored);

const char *
request_error(const struct request_parser *p);

void
request_parser_close(struct request_parser *p);

/**
 * Escribe la respuesta. `bnd' es BND.ADDR/BND.PORT; si es NULL o no es
 * AF_INET/AF_INET6 se manda 0.0.0.0:0. Retorna -1 si no hay espacio.
 */
int
request_marshall(buffer *b, const enum socks_reply reply, const struct sockaddr *bnd);

#endif
