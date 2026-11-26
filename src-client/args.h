#ifndef CLIENT_ARGS_H_
#define CLIENT_ARGS_H_

struct client_args {
    const char *mng_addr;
    unsigned short mng_port;
    const char *admin_user;
    const char *admin_pass;
    int cmd_argc;     /* cantidad de tokens del subcomando (incluye su nombre) */
    char **cmd_argv;  /* subcomando + argumentos, ej: {"users", "add", "pepe", "1234"} */
};

/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * args con los defaults o la seleccion del usuario. Puede
 * cortar la ejecucion (-h, -v, o un argumento invalido).
 */
void
parse_client_args(const int argc, char **argv, struct client_args *args);

#endif