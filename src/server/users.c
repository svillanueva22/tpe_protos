#include <string.h>

#include "args.h"
#include "users.h"

// TODO Parte C: stub provisorio de la Parte A (guarda los punteros, no copia).
static const char *names[MAX_USERS];
static const char *passes[MAX_USERS];
static int nusers;

bool
users_add(const char *user, const char *pass) {
    if(nusers == MAX_USERS) {
        return false;
    }
    names[nusers]  = user;
    passes[nusers] = pass;
    nusers++;
    return true;
}

bool
users_check(const char *user, const char *pass) {
    for(int i = 0; i < nusers; i++) {
        if(strcmp(names[i], user) == 0) {
            return strcmp(passes[i], pass) == 0;
        }
    }
    return false;
}
