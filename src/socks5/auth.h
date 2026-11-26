#ifndef AUTH_H_q3nZc8VdLx1KbWfR7tPyE2sA
#define AUTH_H_q3nZc8VdLx1KbWfR7tPyE2sA

#include <stdint.h>
#include <stdbool.h>

#include "buffer.h"

/**
 * auth.c - parser incremental de la autenticación usuario/contraseña (RFC 1929)
 *
 *   +----+------+----------+------+----------+
 *   |VER | ULEN |  UNAME   | PLEN |  PASSWD  |
 *   +----+------+----------+------+----------+
 *   | 1  |  1   | 1 to 255 |  1   | 1 to 255 |
 *   +----+------+----------+------+----------+
 */

#define AUTH_VERSION     0x01
#define AUTH_STATUS_OK   0x00
#define AUTH_STATUS_FAIL 0x01
#define AUTH_MAX_LEN     255

enum auth_state {
    auth_version,
    auth_ulen,
    auth_uname,
    auth_plen,
    auth_passwd,
    auth_done,
    auth_error_unsupported_version,
    auth_error_invalid_length,
};

struct auth_parser {
    enum auth_state state;
    uint8_t remaining;
    uint8_t i;

    /** terminados en '\0' al completar cada campo */
    char username[AUTH_MAX_LEN + 1];
    char password[AUTH_MAX_LEN + 1];
};

void
auth_parser_init(struct auth_parser *p);

enum auth_state
auth_parser_feed(struct auth_parser *p, const uint8_t b);

/** igual que hello_consume: los bytes posteriores al mensaje quedan en `b' */
enum auth_state
auth_consume(buffer *b, struct auth_parser *p, bool *errored);

bool
auth_is_done(const enum auth_state state, bool *errored);

const char *
auth_error(const struct auth_parser *p);

/** borra las credenciales de memoria */
void
auth_parser_close(struct auth_parser *p);

/** escribe la respuesta `VER STATUS'. Retorna -1 si no hay espacio */
int
auth_marshall(buffer *b, const uint8_t status);

#endif
