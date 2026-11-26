#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "args.h"

// RFC 1929: ULEN y PLEN ocupan un byte y no pueden ser 0
#define MAX_CREDENTIAL_LEN 255

static unsigned short
port(const char* s)
{
    char* end = 0;
    errno = 0;
    const long sl = strtol(s, &end, 10);

    if (end == s || '\0' != *end
        || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno)
        || sl < 1 || sl > USHRT_MAX)
    {
        fprintf(stderr, "port should be in the range of 1-65535: %s\n", s);
        exit(1);
    }
    return (unsigned short)sl;
}

static void
user(char* s, struct users* user)
{
    char* p = strchr(s, ':');
    if (p == NULL)
    {
        fprintf(stderr, "password not found for user: %s\n", s);
        exit(1);
    }
    *p = 0;
    p++;

    const size_t ulen = strlen(s);
    const size_t plen = strlen(p);
    if (ulen == 0 || ulen > MAX_CREDENTIAL_LEN
        || plen == 0 || plen > MAX_CREDENTIAL_LEN)
    {
        fprintf(stderr, "user and password must be 1-%d bytes long\n",
                MAX_CREDENTIAL_LEN);
        exit(1);
    }
    user->name = s;
    user->pass = p;
}

static void
version(void)
{
    // TODO: número de grupo y licencia
    fprintf(stderr, "socks5v version 0.0\n"
            "ITBA Protocolos de Comunicación 2026/2 -- Grupo X\n"
            "AQUI VA LA LICENCIA\n");
}

static void
usage(const char* progname, const int status)
{
    fprintf(stderr,
            "Usage: %s [OPTION]...\n"
            "\n"
            "   -h               Imprime la ayuda y termina.\n"
            "   -l <SOCKS addr>  Dirección donde servirá el proxy SOCKS. Por defecto 0.0.0.0 y ::.\n"
            "   -L <conf  addr>  Dirección donde servirá el servicio de management. Por defecto 127.0.0.1.\n"
            "   -N               Deshabilita los disectores de credenciales.\n"
            "   -p <SOCKS port>  Puerto entrante conexiones SOCKS. Por defecto 1080.\n"
            "   -P <conf port>   Puerto entrante conexiones configuracion. Por defecto 8080.\n"
            "   -u <name>:<pass> Usuario y contraseña de usuario que puede usar el proxy. Hasta 10.\n"
            "   -v               Imprime información sobre la versión y termina.\n"
            "\n",
            progname);
    exit(status);
}

void
parse_args(const int argc, char** argv, struct socks5args* args)
{
    memset(args, 0, sizeof(*args)); // sobre todo para setear en null los punteros de users

    args->socks_addr = NULL;
    args->socks_port = 1080;

    args->mng_addr = "127.0.0.1";
    args->mng_port = 8080;

    args->disectors_enabled = true;

    int c;
    while ((c = getopt(argc, argv, "hl:L:Np:P:u:v")) != -1)
    {
        switch (c)
        {
        case 'h':
            usage(argv[0], 0);
            break;
        case 'l':
            args->socks_addr = optarg;
            break;
        case 'L':
            args->mng_addr = optarg;
            break;
        case 'N':
            args->disectors_enabled = false;
            break;
        case 'p':
            args->socks_port = port(optarg);
            break;
        case 'P':
            args->mng_port = port(optarg);
            break;
        case 'u':
            if (args->nusers >= MAX_USERS)
            {
                fprintf(stderr, "maximum number of command line users reached: %d.\n", MAX_USERS);
                exit(1);
            }
            user(optarg, args->users + args->nusers);
            for (int i = 0; i < args->nusers; i++)
            {
                if (strcmp(args->users[i].name, args->users[args->nusers].name) == 0)
                {
                    fprintf(stderr, "duplicated user: %s\n", args->users[i].name);
                    exit(1);
                }
            }
            args->nusers++;
            break;
        case 'v':
            version();
            exit(0);
        default:
            // getopt ya imprimió el error
            usage(argv[0], 1);
        }
    }
    if (optind < argc)
    {
        fprintf(stderr, "argument not accepted: ");
        while (optind < argc)
        {
            fprintf(stderr, "%s ", argv[optind++]);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
}
