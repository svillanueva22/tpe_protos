#ifndef POP3_SNIFFER_H_
#define POP3_SNIFFER_H_

/**
 * pop3_sniffer.h -- disector de credenciales POP3 (requerimiento 9, 2ª entrega).
 *
 * Mira el tráfico que pasa por COPY hacia un servidor POP3 (puerto 110), sin modificarlo,
 * y registra el USER/PASS que el servidor confirmó con +OK. Es incremental: se alimenta
 * con los bytes tal cual llegan, aunque vengan de a uno.
 *
 * Lo usa la Parte B en COPY:
 *
 *   if (pop3_sniffer_applies(dest_port)) {   // sniffer prendido y puerto 110
 *       pop3_sniffer_init(&s->sniffer, user, dest, dest_port);   // una vez, al entrar a COPY
 *   }
 *   ...
 *   pop3_sniffer_feed(&s->sniffer, ptr, n, POP3_C2S);   // bytes que van al origen
 *   pop3_sniffer_feed(&s->sniffer, ptr, n, POP3_S2C);   // bytes que vienen del origen
 *
 * `struct pop3_sniffer` está completa acá para poder embeberla en `struct socks5` sin
 * alocar. Es chica a propósito (hay una por conexión).
 *
 * ESTADO: stub. El header es el contrato definitivo; la lógica real se implementa en la
 * Entrega 2 sin cambiar la API. Mientras tanto pop3_sniffer_feed no hace nada.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POP3_SNIFFER_PORT 110

/** Sentido de los bytes que se le pasan a pop3_sniffer_feed. */
enum pop3_dir {
    POP3_C2S,  // cliente -> servidor POP3 (comandos: USER, PASS)
    POP3_S2C,  // servidor POP3 -> cliente (respuestas: +OK, -ERR)
};

struct pop3_sniffer {
    bool     active;               // false: feed() no hace nada
    int      state;                // estado interno del parser (lo define pop3_sniffer.c)
    uint16_t dest_port;
    char     proxy_user[64];       // usuario del proxy (para el log), truncado
    char     dest[64];             // destino (para el log), truncado
    char     pop3_user[64];        // USER capturado, a la espera del +OK
    char     line[96];             // línea en construcción (tope fijo: no crece con la entrada)
    size_t   line_len;
};

/**
 * @return true si hay que disectar una conexión a `dest_port`: el sniffer está habilitado
 *         en config (-N y el toggle de runtime) y el puerto es 110.
 */
bool pop3_sniffer_applies(uint16_t dest_port);

/**
 * Deja listo el sniffer para una conexión. Copia (truncando) `proxy_user` y `dest`;
 * `proxy_user` puede ser NULL.
 */
void pop3_sniffer_init(struct pop3_sniffer *s, const char *proxy_user,
                       const char *dest, uint16_t dest_port);

/**
 * Alimenta al sniffer con `n` bytes que pasaron en el sentido `dir`. No modifica ni
 * consume el buffer: quien lo llama sigue reenviando los mismos bytes.
 */
void pop3_sniffer_feed(struct pop3_sniffer *s, const uint8_t *ptr, size_t n,
                       enum pop3_dir dir);

#endif
