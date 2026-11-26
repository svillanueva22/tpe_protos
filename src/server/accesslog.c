#include "accesslog.h"
#include "logger.h"
#include "netutils.h"

// TODO Parte C: stub provisorio de la Parte A, solo loguea a stderr.
void
accesslog_write(const char *user, const struct sockaddr *client_addr,
                const struct request *req, enum socks_reply rep) {
    char client[SOCKADDR_TO_HUMAN_MIN];
    LOG(INFO, "access: user=%s client=%s atyp=%d rep=%d", user,
        sockaddr_to_human(client, sizeof(client), client_addr), req->atyp, rep);
}
