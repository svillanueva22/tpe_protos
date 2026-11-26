#ifndef ACCESSLOG_H_
#define ACCESSLOG_H_

/**
 * accesslog.h -- access log: una línea por request SOCKS (requerimiento 8).
 *
 * Tiene que poder responder "¿quién se conectó a X y cuándo?" con un grep:
 *
 *   2026-11-26T22:42:31Z user=alice client=10.0.0.5:51234 atyp=fqdn dest=example.com:80 rep=0x00
 *
 *   - Fecha ISO-8601 en UTC.
 *   - Formato `clave=valor`, un request por línea.
 *   - user=- si todavía no hubo autenticación. Los espacios y caracteres de control del
 *     usuario y del destino se reemplazan por '_' para no romper el formato.
 *
 * Se escribe en stdout o en un archivo y NO usa el logger de debug (que va a stderr).
 * Cada línea se vuelca con fflush: en un archivo regular no bloquea de forma apreciable.
 *
 * Lo llama la Parte B cuando se conoce el resultado del CONNECT (éxito o error).
 */

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

/** Tipo de dirección del destino. Los valores coinciden con ATYP de RFC 1928 §5. */
enum accesslog_atyp {
    ACCESSLOG_IPV4 = 0x01,
    ACCESSLOG_FQDN = 0x03,
    ACCESSLOG_IPV6 = 0x04,
};

/**
 * Abre el access log. `path == NULL` escribe en stdout; si no, agrega al final del
 * archivo (lo crea si no existe).
 * @return false si no se pudo abrir el archivo.
 */
bool accesslog_init(const char *path);

/** Cierra el access log. Hace falta solo si se abrió un archivo. */
void accesslog_close(void);

/**
 * Registra un request.
 *
 * @param user       usuario autenticado, o NULL si no hubo.
 * @param client     dirección del cliente que se conectó al proxy.
 * @param atyp       tipo de dirección del destino pedido.
 * @param dest       destino como texto: el FQDN pedido o la IP literal. Así se loguea lo que
 *                   pidió el cliente, no a qué IP resolvió.
 * @param dest_port  puerto de destino, en orden de host.
 * @param rep        código REP con el que se respondió (0x00 = éxito).
 *
 * Es no-op si el access log no fue inicializado con accesslog_init.
 */
void accesslog_write(const char *user, const struct sockaddr *client,
                     enum accesslog_atyp atyp, const char *dest,
                     uint16_t dest_port, uint8_t rep);

#endif
