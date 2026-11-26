#ifndef USERS_H_
#define USERS_H_

/**
 * users.h -- usuarios del proxy (los que se autentican por RFC 1929).
 *
 * Es el único dueño de la tabla de usuarios. Vive en memoria (se pierde al reiniciar)
 * y se puede modificar en runtime desde el protocolo de management.
 *
 * Uso de cada parte:
 *   - main.c    : users_init() al arrancar y users_add() por cada `-u user:pass`.
 *   - Parte A   : users_check() al terminar el parser de auth.
 *   - Parte D   : users_add / users_del / users_set_pass / users_count / users_name_at.
 *
 * Todo corre en el thread del selector: no es thread-safe ni hace falta que lo sea.
 */

#include <stdbool.h>
#include <stddef.h>

/** RFC 1929: ULEN y PLEN ocupan un byte y no pueden ser 0. */
#define USERS_CRED_MAX 255

/** Cantidad máxima de usuarios simultáneos (línea de comandos + management). */
#define USERS_MAX 64

typedef enum {
    USERS_OK = 0,
    USERS_ERR_EXISTS,     // alta de un nombre que ya existe
    USERS_ERR_NOT_FOUND,  // baja / cambio de contraseña de un usuario inexistente
    USERS_ERR_FULL,       // se alcanzó USERS_MAX
    USERS_ERR_INVALID,    // nombre o contraseña vacíos o de más de USERS_CRED_MAX bytes
} users_status;

/** Vacía la tabla. Llamar una vez al arrancar (y desde los tests). */
void users_init(void);

/** Da de alta a `name` con la contraseña `pass` (strings terminados en '\0'). */
users_status users_add(const char *name, const char *pass);

/** Da de baja a `name`. */
users_status users_del(const char *name);

/** Cambia la contraseña de `name`. */
users_status users_set_pass(const char *name, const char *pass);

/**
 * Valida las credenciales tal como las entrega el parser de auth: byte arrays con su
 * longitud, SIN requerir '\0' final. Con longitudes explícitas un USERNAME con un 0
 * embebido no puede coincidir con un prefijo de un usuario real.
 * La contraseña se compara en tiempo constante.
 *
 * @return true si el usuario existe y la contraseña coincide.
 */
bool users_check(const char *user, size_t ulen, const char *pass, size_t plen);

/** Cantidad de usuarios dados de alta. */
size_t users_count(void);

/**
 * Nombre del i-ésimo usuario (0 <= i < users_count()), o NULL si i está fuera de rango.
 * El puntero vale hasta la próxima alta/baja: copiarlo si hace falta conservarlo.
 * Sirve para listar: `for (i = 0; (n = users_name_at(i)) != NULL; i++)`.
 * Nunca se exponen las contraseñas.
 */
const char *users_name_at(size_t i);

#endif
