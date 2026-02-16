/* TLS + HTTPS helpers used by REST and gateway connections. */
#include "net.h"

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Trust anchor: GTS Root R4 (Google Trust Services).
 * Discord's certificate chain terminates at this CA.
 * Subject: C=US, O=Google Trust Services LLC, CN=GTS Root R4
 * Key: EC P-384 (secp384r1)
 */
static const unsigned char TA_DN[] = {
    0x30, 0x47, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x55, 0x53, 0x31, 0x22, 0x30, 0x20, 0x06, 0x03, 0x55, 0x04, 0x0a,
    0x13, 0x19, 0x47, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x20, 0x54, 0x72, 0x75,
    0x73, 0x74, 0x20, 0x53, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x20,
    0x4c, 0x4c, 0x43, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x13, 0x0b, 0x47, 0x54, 0x53, 0x20, 0x52, 0x6f, 0x6f, 0x74, 0x20, 0x52,
    0x34};

static const unsigned char TA_EC_Q[] = {
    0x04, 0xf3, 0x74, 0x73, 0xa7, 0x68, 0x8b, 0x60, 0xae, 0x43, 0xb8, 0x35,
    0xc5, 0x81, 0x30, 0x7b, 0x4b, 0x49, 0x9d, 0xfb, 0xc1, 0x61, 0xce, 0xe6,
    0xde, 0x46, 0xbd, 0x6b, 0xd5, 0x61, 0x18, 0x35, 0xae, 0x40, 0xdd, 0x73,
    0xf7, 0x89, 0x91, 0x30, 0x5a, 0xeb, 0x3c, 0xee, 0x85, 0x7c, 0xa2, 0x40,
    0x76, 0x3b, 0xa9, 0xc6, 0xb8, 0x47, 0xd8, 0x2a, 0xe7, 0x92, 0x91, 0x6a,
    0x73, 0xe9, 0xb1, 0x72, 0x39, 0x9f, 0x29, 0x9f, 0xa2, 0x98, 0xd3, 0x5f,
    0x5e, 0x58, 0x86, 0x65, 0x0f, 0xa1, 0x84, 0x65, 0x06, 0xd1, 0xdc, 0x8b,
    0xc9, 0xc7, 0x73, 0xc8, 0x8c, 0x6a, 0x2f, 0xe5, 0xc4, 0xab, 0xd1, 0x1d,
    0x8a};

static const br_x509_trust_anchor TAS[] = {{
    {(unsigned char *)TA_DN, sizeof TA_DN},
    BR_X509_TA_CA,
    {BR_KEYTYPE_EC,
     {.ec = {BR_EC_secp384r1, (unsigned char *)TA_EC_Q, sizeof TA_EC_Q}}},
}};

#define TAS_NUM 1

/* Low-level I/O callbacks for BearSSL's simplified I/O wrapper. */
static int sock_read(void* ctx, unsigned char* buf, size_t len) {
    int fd = *(int*)ctx;
    ssize_t rlen = read(fd, buf, len);
    if (rlen <= 0) {
        return -1;
    }
    return (int)rlen;
}

static int sock_write(void* ctx, const unsigned char* buf, size_t len) {
    int fd = *(int*)ctx;
    ssize_t wlen = write(fd, buf, len);
    if (wlen <= 0) {
        return -1;
    }
    return (int)wlen;
}

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
    memset(&c.sc, 0, sizeof(c.sc));
    memset(&c.xc, 0, sizeof(c.xc));

    c.fd = tcp_connect(endpoint->host, endpoint->port);
    if (c.fd < 0) {
        return c;
    }

    br_ssl_client_init_full(&c.sc, &c.xc, TAS, TAS_NUM);
    br_ssl_engine_set_buffer(&c.sc.eng, c.iobuf, sizeof(c.iobuf), 1);

    if (br_ssl_client_reset(&c.sc, endpoint->host, 0) == 0) {
        fprintf(stderr, "TLS reset failed\n");
        close(c.fd);
        c.fd = -1;
        return c;
    }

    br_sslio_init(&c.ioc, &c.sc.eng, sock_read, &c.fd, sock_write, &c.fd);

    return c;
}

void tls_close(tls_conn* c) {
    if (!c) {
        return;
    }
    if (c->fd >= 0) {
        br_sslio_close(&c->ioc);
        close(c->fd);
    }
    c->fd = -1;
}

/* Minimal HTTPS request: send raw request and read the full response. */
int https_request(const net_endpoint* endpoint, const char* req, char* resp, size_t resp_cap,
                  size_t* resp_len) {
    tls_conn c = tls_connect(endpoint);
    if (c.fd < 0) {
        return -1;
    }

    size_t req_len = strlen(req);
    if (br_sslio_write_all(&c.ioc, req, req_len) != 0) {
        tls_close(&c);
        return -1;
    }
    br_sslio_flush(&c.ioc);

    size_t total = 0;
    while (total + 1 < resp_cap) {
        int n = br_sslio_read(&c.ioc, resp + total, resp_cap - total - 1);
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
