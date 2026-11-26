#include <string.h>

#include "mgmt_parse.h"

void
mgmt_line_buffer_reset(struct mgmt_line_buffer *lb)
{
    lb->len = 0;
}

int
mgmt_line_feed(struct mgmt_line_buffer *lb, char byte, char *out, size_t outcap)
{
    if (byte == '\n') {
        size_t n = lb->len < outcap - 1 ? lb->len : outcap - 1;
        memcpy(out, lb->data, n);
        out[n] = '\0';
        lb->len = 0;
        return 1;
    }
    if (lb->len + 1 >= sizeof(lb->data)) {
        lb->len = 0;
        return -1;
    }
    lb->data[lb->len++] = byte;
    return 0;
}

int
mgmt_parse_command(char *line, struct mgmt_command *out)
{
    out->argc = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(line, " ", &saveptr);
    while (tok != NULL) {
        if (out->argc >= MGMT_MAX_ARGS) return -1;
        out->argv[out->argc++] = tok;
        tok = strtok_r(NULL, " ", &saveptr);
    }
    return out->argc;
}