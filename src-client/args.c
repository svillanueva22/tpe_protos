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
        exit(1);
    }
    return (unsigned short)sl;
}

static void
version(void)
{
    fprintf(stderr,
            "mgmt-client version 0.0\n"
            "ITBA Protocolos de Comunicaci\xc3\xb3n 2026/2 -- Grupo X\n");
}

static void
usage(const char *progname)
{
    fprintf(stderr,
            "Uso: %s [OPCION]...\n"
            "\n"
            "   -h               Imprime esta ayuda y termina.\n"
            "   -L <addr>        Direcci\xc3\xb3n del servicio de management (default 127.0.0.1).\n"
            "   -P <port>        Puerto del servicio de management (default 8080).\n"
            "   -v               Imprime la versi\xc3\xb3n y termina.\n"
            "\n",
            progname);
    exit(1);
}

void
parse_client_args(const int argc, char **argv, struct client_args *args)
{
    args->mng_addr = "127.0.0.1";
    args->mng_port = 8080;

    int c;
    while ((c = getopt(argc, argv, "hL:P:v")) != -1)
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
        case 'v':
            version();
            exit(0);
        default:
            fprintf(stderr, "argumento desconocido.\n");
            exit(1);
        }
    }

    if (optind < argc)
    {
        fprintf(stderr, "argumento no aceptado: ");
        while (optind < argc)
        {
            fprintf(stderr, "%s ", argv[optind++]);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
}
