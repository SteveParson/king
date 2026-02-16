/* TLS + HTTPS helpers used by REST and gateway connections. */
#include "net.h"

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Resolve and connect to a host:port, returning a connected socket or -1. */
static int tcp_connect(const char* host, const char* port) {
    struct addrinfo hints;
    struct addrinfo* res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

/* Establish a TLS connection for an endpoint. */
tls_conn tls_connect(const net_endpoint* endpoint) {
    tls_conn c;
    c.fd = -1;
    c.ssl = NULL;
    c.ctx = NULL;

    SSL_library_init();
    SSL_load_error_strings();

    c.ctx = SSL_CTX_new(TLS_client_method());
    if (!c.ctx) {
        return c;
    }

    c.fd = tcp_connect(endpoint->host, endpoint->port);
    if (c.fd < 0) {
        return c;
    }

    c.ssl = SSL_new(c.ctx);
    if (!c.ssl) {
        return c;
    }

    SSL_set_tlsext_host_name(c.ssl, endpoint->host);
    SSL_set_fd(c.ssl, c.fd);

    if (SSL_connect(c.ssl) <= 0) {
        fprintf(stderr, "TLS connect failed\n");
        return c;
    }

    return c;
}

void tls_close(tls_conn* c) {
    if (!c) {
        return;
    }
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    }
    if (c->fd >= 0) {
        close(c->fd);
    }
    if (c->ctx) {
        SSL_CTX_free(c->ctx);
    }
    c->ssl = NULL;
    c->ctx = NULL;
    c->fd = -1;
}

/* Minimal HTTPS request: send raw request and read the full response. */
int https_request(const net_endpoint* endpoint, const char* req, char* resp, size_t resp_cap,
                  size_t* resp_len) {
    tls_conn c = tls_connect(endpoint);
    if (!c.ssl) {
        return -1;
    }

    size_t req_len = strlen(req);
    size_t sent = 0;
    while (sent < req_len) {
        int n = SSL_write(c.ssl, req + sent, (int)(req_len - sent));
        if (n <= 0) {
            tls_close(&c);
            return -1;
        }
        sent += (size_t)n;
    }

    size_t total = 0;
    while (1) {
        if (total + 1 >= resp_cap) {
            break;
        }
        int n = SSL_read(c.ssl, resp + total, (int)(resp_cap - total - 1));
        if (n <= 0) {
            break;
        }
        total += (size_t)n;
    }
    resp[total] = '\0';
    if (resp_len) {
        *resp_len = total;
    }

    tls_close(&c);
    return 0;
}
