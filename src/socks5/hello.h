/** Basado en código de Juan F. Codagnone — Protocolos de Comunicación, ITBA */
#ifndef HELLO_H_Ob38ZNgy7qwEaMkLWG1TzyP5
#define HELLO_H_Ob38ZNgy7qwEaMkLWG1TzyP5

#include <stdint.h>
#include <stdbool.h>

#include "buffer.h"

/**
 * hello.c - parser incremental del mensaje de selección de método (RFC 1928 §3)
 *
 *   +----+----------+----------+
 *   |VER | NMETHODS | METHODS  |
 *   +----+----------+----------+
 *   | 1  |    1     | 1 to 255 |
 *   +----+----------+----------+
 */

#define SOCKS_VERSION                          0x05
#define SOCKS_HELLO_NOAUTHENTICATION_REQUIRED  0x00
#define SOCKS_HELLO_USERNAME_PASSWORD          0x02
#define SOCKS_HELLO_NO_ACCEPTABLE_METHODS      0xFF

enum hello_state {
    hello_version,
    hello_nmethods,
    hello_methods,
    hello_done,
    hello_error_unsupported_version,
};

struct hello_parser {
    /** se llama una vez por cada método que ofrece el cliente */
    void (*on_authentication_method)(struct hello_parser *parser, const uint8_t method);
    void *data;

    enum hello_state state;
    /** métodos que faltan leer */
    uint8_t remaining;
};

void
hello_parser_init(struct hello_parser *p);

enum hello_state
hello_parser_feed(struct hello_parser *p, const uint8_t b);

/**
 * Consume bytes de `b' hasta terminar el mensaje o vaciar el buffer.
 * Los bytes posteriores al mensaje quedan en el buffer.
 */
enum hello_state
hello_consume(buffer *b, struct hello_parser *p, bool *errored);

/** true si el parser terminó, con o sin error (`errored' puede ser NULL) */
bool
hello_is_done(const enum hello_state state, bool *errored);

const char *
hello_error(const struct hello_parser *p);

void
hello_parser_close(struct hello_parser *p);

/** escribe la respuesta `VER METHOD'. Retorna -1 si no hay espacio */
int
hello_marshall(buffer *b, const uint8_t method);

#endif
