#ifndef MGMT_PARSE_H_
#define MGMT_PARSE_H_

#include <stddef.h>

#define MGMT_MAX_ARGS 4
#define MGMT_LINE_MAX 256

struct mgmt_line_buffer {
    char data[MGMT_LINE_MAX];
    size_t len;
};

void mgmt_line_buffer_reset(struct mgmt_line_buffer *lb);

/* Alimenta un byte al buffer (parsing incremental, de a 1 byte).
 * Si completa una linea (encuentra '\n'), la deja en `out` (sin '\n',
 * terminada en '\0') y devuelve 1. Si todavia no hay linea completa,
 * devuelve 0. Si la linea excede MGMT_LINE_MAX, descarta el buffer y
 * devuelve -1. */
int mgmt_line_feed(struct mgmt_line_buffer *lb, char byte, char *out, size_t outcap);

struct mgmt_command {
    char *argv[MGMT_MAX_ARGS];
    int argc;
};

/* Tokeniza una linea por espacios simples, EN LUGAR (modifica `line`).
 * Devuelve la cantidad de tokens, o -1 si hay mas de MGMT_MAX_ARGS. */
int mgmt_parse_command(char *line, struct mgmt_command *out);

#endif