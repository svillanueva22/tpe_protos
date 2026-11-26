#include <string.h>

#include "users.h"

struct entry {
    char name[USERS_CRED_MAX + 1];
    char pass[USERS_CRED_MAX + 1];
};

// Arreglo compacto: los usuarios ocupan [0, count) sin huecos, así users_name_at(i) es directo.
static struct entry table[USERS_MAX];
static size_t count = 0;

static bool
valid(const char *s) {
    if (s == NULL) {
        return false;
    }
    const size_t len = strlen(s);
    return len >= 1 && len <= USERS_CRED_MAX;
}

static struct entry *
find(const char *name, const size_t len) {
    for (size_t i = 0; i < count; i++) {
        if (strlen(table[i].name) == len && memcmp(table[i].name, name, len) == 0) {
            return &table[i];
        }
    }
    return NULL;
}

// Compara sin cortar en la primera diferencia, para no filtrar por tiempo cuánto acertó.
static bool
equal_const_time(const char *a, const size_t alen, const char *b, const size_t blen) {
    unsigned char diff = (unsigned char) (alen != blen);
    const size_t n = alen < blen ? alen : blen;
    for (size_t i = 0; i < n; i++) {
        diff |= (unsigned char) (a[i] ^ b[i]);
    }
    return diff == 0;
}

void
users_init(void) {
    memset(table, 0, sizeof(table));
    count = 0;
}

users_status
users_add(const char *name, const char *pass) {
    if (!valid(name) || !valid(pass)) {
        return USERS_ERR_INVALID;
    }
    if (find(name, strlen(name)) != NULL) {
        return USERS_ERR_EXISTS;
    }
    if (count >= USERS_MAX) {
        return USERS_ERR_FULL;
    }
    // valid() ya garantizó que entran, incluido el '\0'
    memcpy(table[count].name, name, strlen(name) + 1);
    memcpy(table[count].pass, pass, strlen(pass) + 1);
    count++;
    return USERS_OK;
}

users_status
users_del(const char *name) {
    if (!valid(name)) {
        return USERS_ERR_INVALID;
    }
    struct entry *e = find(name, strlen(name));
    if (e == NULL) {
        return USERS_ERR_NOT_FOUND;
    }
    const size_t idx = (size_t) (e - table);
    memmove(&table[idx], &table[idx + 1], (count - idx - 1) * sizeof(table[0]));
    count--;
    // no dejar la contraseña del usuario borrado en el último slot
    memset(&table[count], 0, sizeof(table[0]));
    return USERS_OK;
}

users_status
users_set_pass(const char *name, const char *pass) {
    if (!valid(name) || !valid(pass)) {
        return USERS_ERR_INVALID;
    }
    struct entry *e = find(name, strlen(name));
    if (e == NULL) {
        return USERS_ERR_NOT_FOUND;
    }
    memset(e->pass, 0, sizeof(e->pass));
    memcpy(e->pass, pass, strlen(pass) + 1);
    return USERS_OK;
}

bool
users_check(const char *user, const size_t ulen, const char *pass, const size_t plen) {
    if (user == NULL || pass == NULL || ulen == 0 || ulen > USERS_CRED_MAX
        || plen == 0 || plen > USERS_CRED_MAX) {
        return false;
    }
    const struct entry *e = find(user, ulen);
    if (e == NULL) {
        return false;
    }
    return equal_const_time(e->pass, strlen(e->pass), pass, plen);
}

size_t
users_count(void) {
    return count;
}

const char *
users_name_at(const size_t i) {
    return i < count ? table[i].name : NULL;
}
