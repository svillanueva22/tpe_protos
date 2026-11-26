#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>    /* struct timespec, usado por selector.h */
#include <stdint.h>  /* uint8_t, usado por buffer.h */

#include "selector.h"
#include "buffer.h"
#include "netutils.h"
#include "logger.h"
#include "mgmt.h"
#include "mgmt_parse.h"

#define MGMT_WBUF_SIZE 2048
#define MGMT_ADMIN_MAX 64
#define MAX_STUB_USERS 16

/* ---------------------------------------------------------------------
 * Credenciales del admin, cargadas por mgmt_init().
 * --------------------------------------------------------------------- */
static char g_admin_user[MGMT_ADMIN_MAX];
static char g_admin_pass[MGMT_ADMIN_MAX];

/* ---------------------------------------------------------------------
 * STUBS locales (usuarios de SOCKS, metricas, config). Reemplazar por
 * users.h / metrics.h / config.h reales de la Parte C cuando existan;
 * las firmas siguen la tabla de contratos del plan, asi que el cambio
 * deberia ser solo el #include + borrar este bloque.
 * --------------------------------------------------------------------- */
struct stub_user { char name[32]; char pass[32]; int used; };
static struct stub_user g_users[MAX_STUB_USERS];

static int users_add(const char *user, const char *pass) {
    for (int i = 0; i < MAX_STUB_USERS; i++)
        if (g_users[i].used && strcmp(g_users[i].name, user) == 0) return -1;
    for (int i = 0; i < MAX_STUB_USERS; i++) {
        if (!g_users[i].used) {
            snprintf(g_users[i].name, sizeof g_users[i].name, "%s", user);
            snprintf(g_users[i].pass, sizeof g_users[i].pass, "%s", pass);
            g_users[i].used = 1;
            return 0;
        }
    }
    return -2;
}
static int users_del(const char *user) {
    for (int i = 0; i < MAX_STUB_USERS; i++)
        if (g_users[i].used && strcmp(g_users[i].name, user) == 0) { g_users[i].used = 0; return 0; }
    return -1;
}
static int users_set_pass(const char *user, const char *pass) {
    for (int i = 0; i < MAX_STUB_USERS; i++)
        if (g_users[i].used && strcmp(g_users[i].name, user) == 0) {
            snprintf(g_users[i].pass, sizeof g_users[i].pass, "%s", pass);
            return 0;
        }
    return -1;
}
static void users_list(char *out, size_t outcap) {
    size_t n = 0;
    out[0] = '\0';
    for (int i = 0; i < MAX_STUB_USERS; i++) {
        if (g_users[i].used) {
            int w = snprintf(out + n, outcap - n, "%s\n", g_users[i].name);
            if (w < 0 || (size_t) w >= outcap - n) break;
            n += (size_t) w;
        }
    }
}

static struct {
    unsigned long historicas, concurrentes, bytes_totales, fallos_auth;
} g_metrics;

static void metrics_get(char *out, size_t outcap) {
    snprintf(out, outcap,
        "conexiones_historicas=%lu\nconexiones_concurrentes=%lu\nbytes_totales=%lu\nfallos_auth=%lu\n",
        g_metrics.historicas, g_metrics.concurrentes, g_metrics.bytes_totales, g_metrics.fallos_auth);
}

static int config_get(const char *key, char *out, size_t outcap) {
    if (strcmp(key, "log_level") == 0) { snprintf(out, outcap, "INFO"); return 0; }
    if (strcmp(key, "sniffer") == 0)   { snprintf(out, outcap, "off");  return 0; }
    return -1;
}
static int config_set(const char *key, const char *value) {
    (void) key; (void) value;
    return 0;
}
/* --------------------------------------------------------------------- */

struct mgmt_conn {
    int                     authenticated;
    int                     closing;
    struct mgmt_line_buffer in_lb;
    buffer                  wb;
    uint8_t                 wb_data[MGMT_WBUF_SIZE];
};

#define ATTACHMENT(key) ((struct mgmt_conn *)(key)->data)

static void mgmt_read(struct selector_key *key);
static void mgmt_write(struct selector_key *key);
static void mgmt_close(struct selector_key *key);

static const struct fd_handler mgmt_handler = {
    .handle_read  = mgmt_read,
    .handle_write = mgmt_write,
    .handle_close = mgmt_close,
};

static void
mgmt_queue_line(struct mgmt_conn *c, const char *line) {
    size_t len = strlen(line);
    size_t avail;
    uint8_t *ptr = buffer_write_ptr(&c->wb, &avail);
    if (len + 1 > avail) {
        buffer_compact(&c->wb);
        ptr = buffer_write_ptr(&c->wb, &avail);
    }
    if (len + 1 > avail) {
        LOG(ERROR, "mgmt: respuesta demasiado larga para el buffer, se trunca");
        len = avail > 1 ? avail - 1 : 0;
    }
    memcpy(ptr, line, len);
    ptr[len] = '\n';
    buffer_write_adv(&c->wb, (ssize_t) (len + 1));
}

static void
mgmt_queue_multiline(struct mgmt_conn *c, const char *payload) {
    mgmt_queue_line(c, "OK");
    const char *start = payload;
    while (*start != '\0') {
        const char *nl = strchr(start, '\n');
        size_t len = nl != NULL ? (size_t) (nl - start) : strlen(start);
        char line[256];
        size_t n = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, start, n);
        line[n] = '\0';
        mgmt_queue_line(c, line);
        if (nl == NULL) break;
        start = nl + 1;
    }
    mgmt_queue_line(c, ".");
}

static void
mgmt_dispatch(struct mgmt_conn *c, char *line) {
    struct mgmt_command cmd;
    int argc = mgmt_parse_command(line, &cmd);
    if (argc <= 0) {
        mgmt_queue_line(c, "ERR BAD_COMMAND comando vacio");
        return;
    }

    if (!c->authenticated) {
        if (strcasecmp(cmd.argv[0], "AUTH") == 0 && argc == 3) {
            if (strcmp(cmd.argv[1], g_admin_user) == 0 && strcmp(cmd.argv[2], g_admin_pass) == 0) {
                c->authenticated = 1;
                mgmt_queue_line(c, "OK");
            } else {
                g_metrics.fallos_auth++;
                mgmt_queue_line(c, "ERR BAD_AUTH credenciales invalidas");
            }
        } else {
            mgmt_queue_line(c, "ERR NOT_AUTHENTICATED");
        }
        return;
    }

    if (strcasecmp(cmd.argv[0], "QUIT") == 0 && argc == 1) {
        mgmt_queue_line(c, "OK");
        c->closing = 1;
        return;
    }
    if (strcasecmp(cmd.argv[0], "METRICS") == 0 && argc == 1) {
        char payload[512];
        metrics_get(payload, sizeof(payload));
        mgmt_queue_multiline(c, payload);
        return;
    }
    if (strcasecmp(cmd.argv[0], "USERS") == 0 && argc >= 2) {
        if (strcasecmp(cmd.argv[1], "LIST") == 0 && argc == 2) {
            char payload[512];
            users_list(payload, sizeof(payload));
            mgmt_queue_multiline(c, payload);
        } else if (strcasecmp(cmd.argv[1], "ADD") == 0 && argc == 4) {
            int r = users_add(cmd.argv[2], cmd.argv[3]);
            mgmt_queue_line(c, r == 0 ? "OK" : (r == -1 ? "ERR USER_EXISTS el usuario ya existe"
                                                          : "ERR INTERNAL_ERROR sin lugar para mas usuarios"));
        } else if (strcasecmp(cmd.argv[1], "DEL") == 0 && argc == 3) {
            mgmt_queue_line(c, users_del(cmd.argv[2]) == 0 ? "OK" : "ERR NOT_FOUND usuario inexistente");
        } else if (strcasecmp(cmd.argv[1], "PASSWD") == 0 && argc == 4) {
            mgmt_queue_line(c, users_set_pass(cmd.argv[2], cmd.argv[3]) == 0 ? "OK" : "ERR NOT_FOUND usuario inexistente");
        } else {
            mgmt_queue_line(c, "ERR INVALID_ARGS subcomando de USERS invalido");
        }
        return;
    }
    if (strcasecmp(cmd.argv[0], "CONFIG") == 0 && argc >= 2) {
        if (strcasecmp(cmd.argv[1], "GET") == 0 && argc == 3) {
            char value[128];
            if (config_get(cmd.argv[2], value, sizeof(value)) == 0) {
                char resp[160];
                snprintf(resp, sizeof(resp), "OK %s", value);
                mgmt_queue_line(c, resp);
            } else {
                mgmt_queue_line(c, "ERR NOT_FOUND clave de configuracion inexistente");
            }
        } else if (strcasecmp(cmd.argv[1], "SET") == 0 && argc == 4) {
            mgmt_queue_line(c, config_set(cmd.argv[2], cmd.argv[3]) == 0 ? "OK" : "ERR INVALID_ARGS valor invalido");
        } else {
            mgmt_queue_line(c, "ERR INVALID_ARGS subcomando de CONFIG invalido");
        }
        return;
    }

    mgmt_queue_line(c, "ERR BAD_COMMAND comando desconocido");
}

static void
mgmt_conn_shutdown(struct selector_key *key) {
    selector_unregister_fd(key->s, key->fd); /* dispara mgmt_close */
}

static void
mgmt_read(struct selector_key *key) {
    struct mgmt_conn *c = ATTACHMENT(key);
    uint8_t chunk[256];
    ssize_t n = recv(key->fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
        mgmt_conn_shutdown(key);
        return;
    }
    g_metrics.bytes_totales += (unsigned long) n;

    char line[MGMT_LINE_MAX];
    for (ssize_t i = 0; i < n; i++) {
        int r = mgmt_line_feed(&c->in_lb, (char) chunk[i], line, sizeof(line));
        if (r == 1) {
            mgmt_dispatch(c, line);
        } else if (r == -1) {
            mgmt_queue_line(c, "ERR INVALID_ARGS linea demasiado larga");
        }
    }
    if (buffer_can_read(&c->wb)) {
        selector_set_interest_key(key, OP_WRITE);
    }
}

static void
mgmt_write(struct selector_key *key) {
    struct mgmt_conn *c = ATTACHMENT(key);
    size_t avail;
    uint8_t *ptr = buffer_read_ptr(&c->wb, &avail);
    if (avail > 0) {
        ssize_t n = send(key->fd, ptr, avail, 0);
        if (n <= 0) {
            mgmt_conn_shutdown(key);
            return;
        }
        buffer_read_adv(&c->wb, n);
    }
    if (!buffer_can_read(&c->wb)) {
        if (c->closing) {
            mgmt_conn_shutdown(key);
        } else {
            selector_set_interest_key(key, OP_READ);
        }
    }
}

static void
mgmt_close(struct selector_key *key) {
    close(key->fd);
    if (g_metrics.concurrentes > 0) g_metrics.concurrentes--;
    free(ATTACHMENT(key));
}

void
mgmt_passive_accept(struct selector_key *key) {
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    const int client = accept(key->fd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (client < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != ECONNABORTED) {
            LOG(ERROR, "mgmt accept: %s", strerror(errno));
        }
        return;
    }
    if (selector_fd_set_nio(client) == -1) {
        LOG(ERROR, "mgmt: no se pudo poner el socket en no bloqueante");
        close(client);
        return;
    }

    struct mgmt_conn *c = malloc(sizeof(*c));
    if (c == NULL) {
        close(client);
        return;
    }
    memset(c, 0, sizeof(*c));
    buffer_init(&c->wb, sizeof(c->wb_data), c->wb_data);
    mgmt_line_buffer_reset(&c->in_lb);

    char addrbuf[SOCKADDR_TO_HUMAN_MIN];
    LOG(INFO, "mgmt: nueva conexion de %s",
        sockaddr_to_human(addrbuf, sizeof(addrbuf), (struct sockaddr *) &client_addr));

    mgmt_queue_line(c, "MGMT 1.0");
    g_metrics.historicas++;
    g_metrics.concurrentes++;

    if (SELECTOR_SUCCESS != selector_register(key->s, client, &mgmt_handler, OP_WRITE, c)) {
        LOG(ERROR, "mgmt: no se pudo registrar la conexion en el selector");
        close(client);
        free(c);
        return;
    }
}

void
mgmt_pool_destroy(void) {
    /* no hay pool de reuso en esta implementacion; ver mgmt.h */
}

void
mgmt_init(const char *admin_user, const char *admin_pass) {
    snprintf(g_admin_user, sizeof(g_admin_user), "%s", admin_user);
    snprintf(g_admin_pass, sizeof(g_admin_pass), "%s", admin_pass);
}