#ifndef NET_H
#define NET_H

#include <openssl/ssl.h>

typedef struct {
    int fd;
    SSL *ssl;
    SSL_CTX *ctx;
} tls_conn;

tls_conn tls_connect(const char *host, const char *port);
void tls_close(tls_conn *c);

int https_request(const char *host, const char *port, const char *req,
                  char *resp, size_t resp_cap, size_t *resp_len);

#endif
