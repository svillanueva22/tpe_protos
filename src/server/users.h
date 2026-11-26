#ifndef USERS_H_r5WkPq2nGx8TbLz3YcVm7Jde
#define USERS_H_r5WkPq2nGx8TbLz3YcVm7Jde

#include <stdbool.h>

// TODO Parte C: stub provisorio de la Parte A, reemplazar por la API definitiva.

/** Retorna false si no hay lugar o el usuario ya existe */
bool
users_add(const char *user, const char *pass);

bool
users_check(const char *user, const char *pass);

#endif
