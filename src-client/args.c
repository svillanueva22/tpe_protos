#include <stdio.h>     /* for fprintf */
#include <stdlib.h>    /* for exit */
#include <limits.h>    /* LONG_MIN et al */
#include <errno.h>
#include <unistd.h>    /* getopt, optarg, optind */

#include "args.h"

static unsigned short
port(const char *s)
{
    char *end = 0;
    const long sl = strtol(s, &end, 10);

    if (end == s || '\0' != *end
        || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno)
        || sl <= 0 || sl > USHRT_MAX)
    {
        fprintf(stderr, "el puerto debe ser un entero entre 1 y 65535: %s\n", s);
        exit(4);
    }
    return (unsigned short)sl;
}

static void
version(void)
{
    fprintf(stderr,
            "mgmt-client version 0.0\n"
            "ITBA Protocolos de Comunicación 2026/2 -- Grupo X\n");
}

static void
usage(const char *progname)
{
    fprintf(stderr,
            "Uso: %s [-L addr] [-P port] -u usuario -p password <subcomando> [args...]\n"
            "\n"
            "   -h               Imprime esta ayuda y termina.\n"
            "   -L <addr>        Dirección del servicio de management (default 127.0.0.1).\n"
            "   -P <port>        Puerto del servicio de management (default 8080).\n"
            "   -u <usuario>     Usuario administrador (requerido).\n"
            "   -p <password>    Password del administrador (requerido).\n"
            "   -v               Imprime la versión y termina.\n"
            "\n"
            "Subcomandos:\n"
            "   metrics\n"
            "   users list\n"
            "   users add <usuario> <password>\n"
            "   users del <usuario>\n"
            "   users passwd <usuario> <password>\n"
            "   config get <clave>\n"
            "   config set <clave> <valor>\n"
            "\n",
            progname);
    exit(4);
}

void
parse_client_args(const int argc, char **argv, struct client_args *args)
{
    args->mng_addr = "127.0.0.1";
    args->mng_port = 8080;
    args->admin_user = NULL;
    args->admin_pass = NULL;

    int c;
    while ((c = getopt(argc, argv, "hL:P:u:p:v")) != -1)
    {
        switch (c)
        {
        case 'h':
            usage(argv[0]);
            break;
        case 'L':
            args->mng_addr = optarg;
            break;
        case 'P':
            args->mng_port = port(optarg);
            break;
        case 'u':
            args->admin_user = optarg;
            break;
        case 'p':
            args->admin_pass = optarg;
            break;
        case 'v':
            version();
            exit(0);
        default:
            fprintf(stderr, "argumento desconocido.\n");
            exit(4);
        }
    }

    if (args->admin_user == NULL || args->admin_pass == NULL)
    {
        fprintf(stderr, "faltan -u y/o -p (credenciales del administrador).\n");
        usage(argv[0]);
    }

    args->cmd_argc = argc - optind;
    args->cmd_argv = argv + optind;

    if (args->cmd_argc < 1)
    {
        fprintf(stderr, "falta el subcomando.\n");
        usage(argv[0]);
    }
}