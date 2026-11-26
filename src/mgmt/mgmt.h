#ifndef MGMT_H_
#define MGMT_H_

#include "selector.h"

/**
 * Handler de aceptación para el socket pasivo del servicio de management.
 * Se registra con selector_register() sobre el fd que queda en LISTEN
 * del puerto de management (análogo a socksv5_passive_accept de SOCKS).
 */
void mgmt_passive_accept(struct selector_key *key);

/**
 * Libera todos los recursos del pool de conexiones de management.
 * Se llama al cerrar el servidor (SIGINT/SIGTERM), antes de salir.
 */
void mgmt_pool_destroy(void);

#endif