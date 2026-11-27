/** Basado en código de Juan F. Codagnone — Protocolos de Comunicación, ITBA */
#ifndef SOCKS5NIO_H_Lw3pXc7Rk9TzQm2VbN5yHf8d
#define SOCKS5NIO_H_Lw3pXc7Rk9TzQm2VbN5yHf8d

#include <stdint.h>
#include <netdb.h>
#include <sys/socket.h>

#include "buffer.h"
#include "selector.h"
#include "stm.h"
#include "hello.h"
#include "auth.h"
#include "request.h"

#define SOCKS5_BUFFER_SIZE 4096

/** máquina de estados de una conexión. Dueños en docs/plan-tp.md §1 */
enum socks_v5state {
    /** OP_READ cliente. → HELLO_WRITE */
    HELLO_READ,
    /** OP_WRITE cliente. → AUTH_READ, o SOCKS_ERROR si no hay método aceptable */
    HELLO_WRITE,
    /** OP_READ cliente. → AUTH_WRITE */
    AUTH_READ,
    /** OP_WRITE cliente. → REQUEST_READ, o SOCKS_ERROR si las credenciales no valen */
    AUTH_WRITE,
    /** OP_READ cliente. → REQUEST_RESOLV / REQUEST_CONNECTING, o REQUEST_WRITE con error */
    REQUEST_READ,
    /** getaddrinfo en otro thread, termina en on_block_ready (Parte B) */
    REQUEST_RESOLV,
    /** connect no bloqueante, OP_WRITE origen (Parte B) */
    REQUEST_CONNECTING,
    /** OP_WRITE cliente. → COPY si REP = 0x00, si no SOCKS_ERROR */
    REQUEST_WRITE,
    /** relay en ambos sentidos (Parte B) */
    COPY,

    SOCKS_DONE,
    SOCKS_ERROR,
};

struct hello_st {
    buffer               *rb, *wb;
    struct hello_parser   parser;
    uint8_t               method;
};

struct auth_st {
    buffer               *rb, *wb;
    struct auth_parser    parser;
    uint8_t               status;
};

struct request_st {
    buffer               *rb, *wb;
    struct request_parser parser;
};

// TODO Parte B: completar con lo que necesiten REQUEST_CONNECTING y COPY.
struct connecting {
    buffer               *wb;
};

struct copy {
    int                  *fd;
    buffer               *rb, *wb;
    fd_interest           duplex;
    struct copy          *other;
};

/**
 * Una única alocación por conexión. `references' cuenta los fds registrados
 * en el selector que lo apuntan: quien registre origin_fd tiene que sumar uno.
 */
struct socks5 {
    int                           client_fd;
    struct sockaddr_storage       client_addr;
    socklen_t                     client_addr_len;

    int                           origin_fd;
    struct sockaddr_storage       origin_addr;
    socklen_t                     origin_addr_len;
    int                           origin_domain;
    /** se libera al destruir la conexión */
    struct addrinfo              *origin_resolution;
    struct addrinfo              *origin_resolution_current;

    /** "" hasta que el cliente se autentica */
    char                          username[AUTH_MAX_LEN + 1];
    /** request parseado; sigue válido después de REQUEST_READ */
    struct request                request;
    enum socks_reply              reply;

    struct state_machine          stm;

    union {
        struct hello_st           hello;
        struct auth_st            auth;
        struct request_st         request;
        struct copy               copy;
    } client;
    union {
        struct connecting         conn;
        struct copy               copy;
    } orig;

    /**
     * read_buffer: lo que manda el cliente (en COPY, cliente → origen).
     * write_buffer: lo que se le manda al cliente (en COPY, origen → cliente).
     * Lo que el cliente mande pegado al request queda en read_buffer.
     */
    uint8_t                       raw_buff_a[SOCKS5_BUFFER_SIZE];
    uint8_t                       raw_buff_b[SOCKS5_BUFFER_SIZE];
    buffer                        read_buffer;
    buffer                        write_buffer;

    unsigned                      references;
    struct socks5                *next;
};

#define ATTACHMENT(key) ((struct socks5 *)(key)->data)

/** handler de lectura del socket pasivo SOCKS */
void
socksv5_passive_accept(struct selector_key *key);

/** libera el pool; llamar después de selector_destroy */
void
socksv5_pool_destroy(void);

void socks5_ref(struct socks5 *s);
void socks5_unref(struct socks5 *s);

/** handler a usar al registrar origin_fd en el selector */
const struct fd_handler *
socksv5_handler(void);

/**
 * Arma la respuesta al request con `reply', registra métricas y access log,
 * y deja el cliente en OP_WRITE y el origen (si hay) en OP_NOOP.
 * Retorna el próximo estado (REQUEST_WRITE, o SOCKS_ERROR si falló).
 * Es el final de REQUEST_RESOLV / REQUEST_CONNECTING, con éxito o con error.
 */
unsigned
socksv5_request_reply(struct selector_key *key, enum socks_reply reply);

#endif
