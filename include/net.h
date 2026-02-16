#ifndef NET_H
#define NET_H

#include <bearssl.h>
#include <stddef.h>

typedef struct {
    int fd;
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
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
