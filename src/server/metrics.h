#ifndef METRICS_H_b7HsN2qLc9XvT4mWkR1pZy6e
#define METRICS_H_b7HsN2qLc9XvT4mWkR1pZy6e

#include <stdint.h>

// TODO Parte C: stub provisorio de la Parte A, reemplazar por la API definitiva.

void
metrics_conn_open(void);

void
metrics_conn_close(void);

/** `rep' es un enum socks_reply (request.h) */
void
metrics_rep(uint8_t rep);

void
metrics_auth_fail(void);

typedef enum { METRICS_C2O = 0, METRICS_O2C = 1 } metrics_direction;

void
metrics_bytes(metrics_direction dir, size_t n);

#endif
