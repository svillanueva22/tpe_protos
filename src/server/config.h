#ifndef CONFIG_H_
#define CONFIG_H_

/**
 * config.h -- configuración en runtime del servidor.
 *
 * La modifica la Parte D desde el protocolo de management, sin reiniciar. Los demás la
 * leen con config_get(). Para que un cambio se aplique, quien lee tiene que consultar
 * el valor cada vez que lo necesita y no cachearlo.
 *
 * Qué aplica cada cambio:
 *   - buffer_size       : solo a las conexiones que se acepten DESPUÉS del cambio.
 *   - connect_timeout_s : a los connect() que se inicien después del cambio.
 *   - idle_timeout_s    : a las conexiones nuevas; 0 = sin timeout de inactividad.
 *   - sniffer_enabled   : de inmediato, al próximo dato que pase por COPY.
 *   - log_level         : de inmediato (cambia el nivel global de logger.h).
 */

#include <stdbool.h>
#include <stddef.h>

#include "logger.h"

/** Valores por defecto. */
#define CONFIG_DEFAULT_BUFFER_SIZE       4096
#define CONFIG_DEFAULT_CONNECT_TIMEOUT_S 10
#define CONFIG_DEFAULT_IDLE_TIMEOUT_S    300

/** Rangos válidos (los setters rechazan lo que quede afuera). */
#define CONFIG_BUFFER_SIZE_MIN       512
#define CONFIG_BUFFER_SIZE_MAX       (1024 * 1024)
#define CONFIG_CONNECT_TIMEOUT_MIN_S 1
#define CONFIG_CONNECT_TIMEOUT_MAX_S 300
#define CONFIG_IDLE_TIMEOUT_MAX_S    86400

struct config {
    size_t       buffer_size;        // bytes de cada buffer de COPY (uno por sentido)
    unsigned     connect_timeout_s;  // segundos para completar el connect al origen
    unsigned     idle_timeout_s;     // segundos sin tráfico antes de cortar; 0 = nunca
    bool         sniffer_enabled;    // disector de credenciales (POP3)
    LOG_LEVEL    log_level;          // nivel mínimo del logger de debug
};

/**
 * Carga los valores por defecto. `sniffer_enabled` viene de la línea de comandos
 * (`-N` lo deja en false). Llamar una vez al arrancar (y desde los tests).
 */
void config_init(bool sniffer_enabled);

/** Configuración vigente. Solo lectura: para cambiarla usar los config_set_*. */
const struct config *config_get(void);

/** Los setters devuelven false (y no cambian nada) si el valor está fuera de rango. */
bool config_set_buffer_size(size_t bytes);
bool config_set_connect_timeout(unsigned seconds);
bool config_set_idle_timeout(unsigned seconds);
void config_set_sniffer(bool enabled);
bool config_set_log_level(LOG_LEVEL level);

#endif
