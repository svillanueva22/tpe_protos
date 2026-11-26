#ifndef METRICS_H_
#define METRICS_H_

/**
 * metrics.h -- métricas volátiles del servidor (se pierden al reiniciar).
 *
 * Contadores globales en memoria. Los actualizan las Partes A y B; las lee la Parte D
 * (protocolo de management) con metrics_get().
 */

#include <stddef.h>
#include <stdint.h>

/** El mayor código REP de RFC 1928 §6 (0x08: address type not supported). */
#define METRICS_REP_MAX 0x08

typedef enum {
    METRICS_C2O,  // bytes del cliente hacia el origen
    METRICS_O2C,  // bytes del origen hacia el cliente
} metrics_dir;

struct metrics_snapshot {
    uint64_t conn_total;    // conexiones SOCKS aceptadas desde que arrancó (históricas)
    uint64_t conn_current;  // conexiones SOCKS abiertas ahora
    uint64_t conn_peak;     // máximo de conexiones concurrentes que se vio
    uint64_t bytes_c2o;     // bytes transferidos cliente -> origen
    uint64_t bytes_o2c;     // bytes transferidos origen -> cliente
    uint64_t auth_ok;       // autenticaciones RFC 1929 exitosas
    uint64_t auth_fail;     // autenticaciones RFC 1929 fallidas
    uint64_t rep[METRICS_REP_MAX + 1];  // respuestas por código REP (índice = REP)
};

/** Pone todos los contadores en 0. Llamar una vez al arrancar (y desde los tests). */
void metrics_init(void);

/** Se aceptó una conexión SOCKS: suma una histórica y una concurrente. */
void metrics_conn_open(void);

/** Se cerró una conexión SOCKS (una por cada metrics_conn_open): resta una concurrente. */
void metrics_conn_close(void);

/** Se transfirieron `n` bytes en la dirección `dir`. */
void metrics_bytes(metrics_dir dir, size_t n);

/** Se respondió al cliente con el código REP `rep` (0x00 es éxito). Si es > 0x08 se ignora. */
void metrics_rep(uint8_t rep);

/** Resultado de una autenticación RFC 1929. */
void metrics_auth_ok(void);
void metrics_auth_fail(void);

/** Copia el estado actual de los contadores a `out`. */
void metrics_get(struct metrics_snapshot *out);

#endif
