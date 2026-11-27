#ifndef ACCESSLOG_H_
#define ACCESSLOG_H_

/**
 * accesslog.h -- access log: una línea por request SOCKS (requerimiento 8).
 *
 * Tiene que poder responder "¿quién se conectó a X y cuándo?" con un grep:
 *
 *   2026-11-26T22:42:31Z event=request user=alice client=10.0.0.5:51234 atyp=fqdn dest=example.com:80 ip=93.184.216.34 rep=0x00
 *   2026-11-26T22:42:35Z event=request user=bob client=[::1]:5000 atyp=ipv6 dest=[2001:db8::1]:22 ip=2001:db8::1 rep=0x05
 *   2026-11-26T22:42:40Z event=request user=bob client=[::1]:5001 atyp=fqdn dest=no-existe.example:80 ip=- rep=0x04
 *   2026-11-26T22:42:45Z event=auth_fail user=admin client=203.0.113.5:40022
 *
 *   - Fecha ISO-8601 en UTC.
 *   - Formato `clave=valor`, un evento por línea. `event=` dice de qué tipo es: `request`
 *     (un CONNECT con su resultado) o `auth_fail` (credenciales RFC 1929 rechazadas,
 *     decisión D3). Un `grep user=bob` muestra todo lo que hizo o intentó ese usuario.
 *     En `client=` y `dest=` las IPv6 van entre
 *     corchetes porque llevan puerto; `ip=` no lleva puerto y va sin corchetes.
 *   - dest= es lo que pidió el cliente; ip= es la IP a la que realmente se conectó el
 *     proxy (decisión D2). Sirve cuando una queja externa trae la IP del servidor y no el
 *     nombre, y deja registrado a qué IP resolvió el DNS en ese momento.
 *   - user=- si todavía no hubo autenticación; ip=- si no hubo IP (el nombre no resolvió).
 *     Los espacios y caracteres de control del usuario y del destino se reemplazan por '_'
 *     para no romper el formato.
 *
 * Se escribe a un archivo y NO usa el logger de debug (que va a stderr).
 *
 * E/S (decisión registrada en docs/decisiones-parte-c.md, D1): las líneas se acumulan en
 * un buffer de stdio y se vuelcan con accesslog_flush(), que main llama UNA vez por vuelta
 * del selector. Así hay a lo sumo un write() por vuelta y no uno por request. Limitación:
 * ese write() es bloqueante; en un archivo regular lo normal es que el kernel lo absorba,
 * pero un disco o un NFS colgado frenaría al selector. Por eso el destino por defecto es
 * un archivo y no stdout: un pipe o una terminal sí pueden bloquear de verdad.
 * Si el proceso muere sin pasar por accesslog_close (kill -9), se pierde lo que quedó
 * en el buffer (a lo sumo una vuelta del selector).
 *
 * Lo llama la Parte B cuando se conoce el resultado del CONNECT (éxito o error).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

/** Archivo del access log por defecto (relativo al directorio donde se lanza el servidor). */
#define ACCESSLOG_DEFAULT_PATH "access.log"

/** Tipo de dirección del destino. Los valores coinciden con ATYP de RFC 1928 §5. */
enum accesslog_atyp {
    ACCESSLOG_IPV4 = 0x01,
    ACCESSLOG_FQDN = 0x03,
    ACCESSLOG_IPV6 = 0x04,
};

/**
 * Abre el access log: agrega al final del archivo `path` (lo crea si no existe).
 * `path == NULL` escribe en stdout, pensado solo para desarrollo y tests (ver el
 * comentario de E/S de arriba).
 * @return false si no se pudo abrir el archivo.
 */
bool accesslog_init(const char *path);

/**
 * Vuelca al archivo lo acumulado en el buffer. Lo llama main una vez por vuelta del
 * selector; el resto del código no necesita llamarlo. No-op si no hay access log abierto.
 */
void accesslog_flush(void);

/**
 * Registra un intento de autenticación RFC 1929 rechazado (decisión D3). Lo llama la
 * Parte A una vez, cuando users_check dice que no.
 *
 * @param client  dirección del cliente que se conectó al proxy.
 * @param user    USERNAME tal como lo mandó el cliente: bytes SIN '\0' final, `ulen` de largo
 *                (el mismo par que se le pasa a users_check). Se trunca a 255 bytes y los
 *                espacios y caracteres de control, incluido un '\0', se reemplazan por '_'.
 *                NULL o ulen == 0 se loguea como user=-.
 *
 * La contraseña no se recibe a propósito: no tiene que llegar nunca al log. Ojo: el
 * usuario lo manda un cliente sin autenticar y a veces es una contraseña escrita en el
 * campo equivocado; ese dato queda en el archivo (limitación para el informe).
 *
 * Es no-op si el access log no fue inicializado con accesslog_init.
 */
void accesslog_auth_fail(const struct sockaddr *client, const char *user, size_t ulen);

/** Vuelca lo pendiente y cierra el access log. */
void accesslog_close(void);

/**
 * Registra un request.
 *
 * @param user       usuario autenticado, o NULL si no hubo.
 * @param client     dirección del cliente que se conectó al proxy.
 * @param atyp       tipo de dirección del destino pedido.
 * @param dest       destino como texto, SIN corchetes ni puerto: el FQDN pedido o la IP
 *                   literal. Así se loguea lo que pidió el cliente, no a qué IP resolvió.
 * @param dest_port  puerto de destino, en orden de host.
 * @param dest_ip    IP a la que se conectó el proxy (el puerto de esta estructura se
 *                   ignora). Si hubo que probar varias, la que funcionó; si fallaron todas,
 *                   la última que se intentó. NULL si no hubo ninguna (el nombre no
 *                   resolvió, o el request se rechazó antes de conectar): se loguea ip=-.
 * @param rep        código REP con el que se respondió (0x00 = éxito).
 *
 * Es no-op si el access log no fue inicializado con accesslog_init.
 */
void accesslog_write(const char *user, const struct sockaddr *client,
                     enum accesslog_atyp atyp, const char *dest,
                     uint16_t dest_port, const struct sockaddr *dest_ip,
                     uint8_t rep);

#endif
