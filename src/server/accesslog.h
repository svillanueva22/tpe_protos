#ifndef ACCESSLOG_H_t4FjW8nKz2QpLc6RbXv1Hm9y
#define ACCESSLOG_H_t4FjW8nKz2QpLc6RbXv1Hm9y

#include <sys/socket.h>

#include "request.h"

// TODO Parte C: stub provisorio de la Parte A, reemplazar por la API definitiva.

/** `user' es "" si el cliente no llegó a autenticarse */
void
accesslog_write(const char *user, const struct sockaddr *client_addr,
                const struct request *req, enum socks_reply rep);

#endif
