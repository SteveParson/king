#ifndef NET_H
#define NET_H

#include <openssl/ssl.h>

typedef struct {
    int fd;
    SSL* ssl;
    SSL_CTX* ctx;
} tls_conn;

typedef struct {
    const char* host;
    const char* port;
} net_endpoint;

tls_conn tls_connect(const net_endpoint* endpoint);
void tls_close(tls_conn* c);

int https_request(const net_endpoint* endpoint, const char* req, char* resp, size_t resp_cap,
                  size_t* resp_len);

#endif
