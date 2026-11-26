/**
 * main.c - cliente de línea de comandos para el servicio de management
 *          del proxy SOCKSv5 (socks5d).
 *
 * Fase 0: solo abre la conexión TCP contra el servicio de management y
 * confirma que conectó. El protocolo de management en sí (comandos,
 * respuestas) se implementa en la Fase 3 -- ver TODO más abajo.
 */
#include <stdio.h>
#include <unistd.h>

#include "args.h"
#include "logger.h"
#include "tcpClientUtil.h"

int
main(const int argc, char **argv)
{
    struct client_args args;
    parse_client_args(argc, argv, &args);

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%hu", args.mng_port);

    LOG(INFO, "conectando al servicio de management en %s:%s", args.mng_addr, port_str);

    int sock = tcpClientSocket(args.mng_addr, port_str);
    if (sock < 0) {
        fprintf(stderr, "no se pudo conectar a %s:%s\n", args.mng_addr, port_str);
        return 1;
    }

    LOG(INFO, "conectado a %s:%s", args.mng_addr, port_str);
    printf("conectado a %s:%s\n", args.mng_addr, port_str);

    // TODO Fase 3: acá va el protocolo de management (mandar comandos,
    // leer e interpretar respuestas). Por ahora el cliente solo abre la
    // conexión y la cierra.

    close(sock);
    return 0;
}
