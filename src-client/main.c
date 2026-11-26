/**
 * main.c - cliente de linea de comandos para el servicio de management
 *          del proxy SOCKSv5 (socks5d). Implementa el protocolo de
 *          docs/mgmt-rfc.txt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <unistd.h>
#include <sys/socket.h>

#include "args.h"
#include "logger.h"
#include "tcpClientUtil.h"

#define LINE_BUF 512

#define EXIT_OK            0
#define EXIT_CONN_ERROR    1
#define EXIT_AUTH_ERROR    2
#define EXIT_SERVER_ERROR  3
#define EXIT_USAGE_ERROR   4

/* Lee una linea terminada en '\n' (sin incluirlo). Devuelve su longitud,
 * o -1 si la conexion se cerro o hubo error. Lee byte a byte: simple y
 * suficiente para un cliente de linea de comandos (no es el hot path). */
static int
read_line(int sock, char *buf, size_t cap)
{
    size_t n = 0;
    while (n + 1 < cap) {
        char c;
        ssize_t r = recv(sock, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '\n') { buf[n] = '\0'; return (int) n; }
        buf[n++] = c;
    }
    buf[n] = '\0';
    return (int) n;
}

static int
send_line(int sock, const char *line)
{
    char tmp[LINE_BUF];
    size_t len = strlen(line);
    if (len + 2 >= sizeof(tmp)) return -1;
    memcpy(tmp, line, len);
    tmp[len] = '\n';
    size_t total = len + 1;
    size_t sent = 0;
    while (sent < total) {
        ssize_t s = send(sock, tmp + sent, total - sent, 0);
        if (s <= 0) return -1;
        sent += (size_t) s;
    }
    return 0;
}

/* Lee una respuesta del servidor.
 *
 * IMPORTANTE: un 'OK' a secas no alcanza para saber si el servidor va a
 * mandar un listado despues (ver RFC, seccion 3) -- eso lo sabe el CLIENTE,
 * segun que comando mando (igual que un cliente POP3 sabe que LIST
 * devuelve multilinea porque mando LIST). Por eso `multiline` es un
 * parametro, no algo que se adivina de la respuesta.
 *
 * Devuelve 0 en OK, -1 en ERR (mensaje en errbuf), -2 en error de red. */
static int
read_response(int sock, int multiline, char *errbuf, size_t errcap)
{
    char line[LINE_BUF];
    if (read_line(sock, line, sizeof(line)) < 0) return -2;

    if (strncmp(line, "ERR ", 4) == 0) {
        snprintf(errbuf, errcap, "%s", line + 4);
        return -1;
    }
    if (multiline) {
        /* la primera linea ya fue 'OK'; lo que sigue es el listado hasta '.' */
        for (;;) {
            if (read_line(sock, line, sizeof(line)) < 0) return -2;
            if (strcmp(line, ".") == 0) break;
            printf("%s\n", line);
        }
        return 0;
    }
    if (strcmp(line, "OK") == 0) {
        return 0;
    }
    if (strncmp(line, "OK ", 3) == 0) {
        printf("%s\n", line + 3);
        return 0;
    }
    snprintf(errbuf, errcap, "respuesta inesperada del servidor: %s", line);
    return -1;
}

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
        return EXIT_CONN_ERROR;
    }

    char line[LINE_BUF];
    if (read_line(sock, line, sizeof(line)) < 0 || strncmp(line, "MGMT ", 5) != 0) {
        fprintf(stderr, "banner invalido o conexion cerrada por el servidor\n");
        close(sock);
        return EXIT_CONN_ERROR;
    }

    char cmd[LINE_BUF];
    char errbuf[LINE_BUF];

    snprintf(cmd, sizeof(cmd), "AUTH %s %s", args.admin_user, args.admin_pass);
    if (send_line(sock, cmd) < 0) {
        fprintf(stderr, "error de red enviando AUTH\n");
        close(sock);
        return EXIT_CONN_ERROR;
    }
    if (read_response(sock, 0, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "autenticacion fallida: %s\n", errbuf);
        close(sock);
        return EXIT_AUTH_ERROR;
    }

    const char *group = args.cmd_argv[0];
    int multiline = 0;

    if (strcasecmp(group, "metrics") == 0 && args.cmd_argc == 1) {
        snprintf(cmd, sizeof(cmd), "METRICS");
        multiline = 1;
    } else if (strcasecmp(group, "users") == 0 && args.cmd_argc == 2 && strcasecmp(args.cmd_argv[1], "list") == 0) {
        snprintf(cmd, sizeof(cmd), "USERS LIST");
        multiline = 1;
    } else if (strcasecmp(group, "users") == 0 && args.cmd_argc == 4 && strcasecmp(args.cmd_argv[1], "add") == 0) {
        snprintf(cmd, sizeof(cmd), "USERS ADD %s %s", args.cmd_argv[2], args.cmd_argv[3]);
    } else if (strcasecmp(group, "users") == 0 && args.cmd_argc == 3 && strcasecmp(args.cmd_argv[1], "del") == 0) {
        snprintf(cmd, sizeof(cmd), "USERS DEL %s", args.cmd_argv[2]);
    } else if (strcasecmp(group, "users") == 0 && args.cmd_argc == 4 && strcasecmp(args.cmd_argv[1], "passwd") == 0) {
        snprintf(cmd, sizeof(cmd), "USERS PASSWD %s %s", args.cmd_argv[2], args.cmd_argv[3]);
    } else if (strcasecmp(group, "config") == 0 && args.cmd_argc == 3 && strcasecmp(args.cmd_argv[1], "get") == 0) {
        snprintf(cmd, sizeof(cmd), "CONFIG GET %s", args.cmd_argv[2]);
    } else if (strcasecmp(group, "config") == 0 && args.cmd_argc == 4 && strcasecmp(args.cmd_argv[1], "set") == 0) {
        snprintf(cmd, sizeof(cmd), "CONFIG SET %s %s", args.cmd_argv[2], args.cmd_argv[3]);
    } else {
        fprintf(stderr, "subcomando invalido o argumentos incorrectos: %s\n", group);
        send_line(sock, "QUIT");
        close(sock);
        return EXIT_USAGE_ERROR;
    }

    int rc = EXIT_OK;
    if (send_line(sock, cmd) < 0) {
        fprintf(stderr, "error de red enviando el comando\n");
        close(sock);
        return EXIT_CONN_ERROR;
    }
    if (read_response(sock, multiline, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "error del servidor: %s\n", errbuf);
        rc = EXIT_SERVER_ERROR;
    }

    send_line(sock, "QUIT");
    read_line(sock, line, sizeof(line)); /* drena el OK del QUIT */
    close(sock);
    return rc;
}
