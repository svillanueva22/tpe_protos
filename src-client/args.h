#ifndef CLIENT_ARGS_H_
#define CLIENT_ARGS_H_

struct client_args {
    const char *mng_addr;
    unsigned short mng_port;
};

/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * args con los defaults o la seleccion del usuario. Puede
 * cortar la ejecucion (-h, -v, o un argumento invalido).
 */
void
parse_client_args(const int argc, char **argv, struct client_args *args);

#endif