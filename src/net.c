/* TLS networking with mbedtls. */
#include "net.h"
#include "cacerts.h"
#include "log.h"

#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Global CA certificate chain, initialized once. */
static mbedtls_x509_crt g_ca;
static int g_ca_ready;

int tls_init_ca(void) {
    if (g_ca_ready)
        return 0;
    mbedtls_x509_crt_init(&g_ca);
    int rc = mbedtls_x509_crt_parse(&g_ca, (const unsigned char*)CA_BUNDLE_PEM, CA_BUNDLE_PEM_LEN);
    if (rc != 0) {
        log_error("CA cert parse failed: -0x%04x", -rc);
        return -1;
    }
    g_ca_ready = 1;
    return 0;
}

static int tcp_connect(const char* host, const char* port) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int entropy_func(void* data, unsigned char* output, size_t len) {
    return mbedtls_entropy_func(data, output, len);
}

/* Custom BIO callbacks for mbedtls over a raw socket fd. */
static int bio_send(void* ctx, const unsigned char* buf, size_t len) {
    int fd = *(int*)ctx;
    ssize_t ret = write(fd, buf, len);
    if (ret <= 0)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return (int)ret;
}

static int bio_recv(void* ctx, unsigned char* buf, size_t len) {
    int fd = *(int*)ctx;
    ssize_t ret = read(fd, buf, len);
    if (ret <= 0)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return (int)ret;
}

int tls_connect(tls_conn* c, const char* host, const char* port) {
    memset(c, 0, sizeof(*c));
    c->fd = -1;

    c->fd = tcp_connect(host, port);
    if (c->fd < 0)
        return -1;

    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_ctr_drbg_init(&c->drbg);
    mbedtls_entropy_init(&c->entropy);

    if (mbedtls_ctr_drbg_seed(&c->drbg, entropy_func, &c->entropy, (const unsigned char*)"king",
                              4) != 0) {
        goto fail;
    }

    if (mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        goto fail;
    }

    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&c->conf, &g_ca, NULL);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);

    if (mbedtls_ssl_setup(&c->ssl, &c->conf) != 0)
        goto fail;
    if (mbedtls_ssl_set_hostname(&c->ssl, host) != 0)
        goto fail;

    mbedtls_ssl_set_bio(&c->ssl, &c->fd, bio_send, bio_recv, NULL);

    int ret = mbedtls_ssl_handshake(&c->ssl);
    if (ret != 0) {
        log_error("TLS handshake failed: -0x%04x", -ret);
        goto fail;
    }

    return 0;

fail:
    tls_close(c);
    return -1;
}

void tls_close(tls_conn* c) {
    if (!c)
        return;
    mbedtls_ssl_close_notify(&c->ssl);
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    mbedtls_ctr_drbg_free(&c->drbg);
    mbedtls_entropy_free(&c->entropy);
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
}

int tls_write(tls_conn* c, const void* buf, size_t len) {
    const unsigned char* p = buf;
    while (len > 0) {
        int ret = mbedtls_ssl_write(&c->ssl, p, len);
        if (ret < 0)
            return -1;
        p += ret;
        len -= (size_t)ret;
    }
    return 0;
}

int tls_read(tls_conn* c, void* buf, size_t cap) {
    int ret = mbedtls_ssl_read(&c->ssl, buf, cap);
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        return 0;
    return ret;
}

int tls_read_exact(tls_conn* c, void* buf, size_t len) {
    unsigned char* p = buf;
    while (len > 0) {
        int ret = mbedtls_ssl_read(&c->ssl, p, len);
        if (ret <= 0)
            return -1;
        p += ret;
        len -= (size_t)ret;
    }
    return 0;
}

int https_request(const char* host, const char* port, const char* req, char* resp,
                  size_t resp_cap) {
    tls_conn c;
    if (tls_connect(&c, host, port) != 0)
        return -1;

    if (tls_write(&c, req, strlen(req)) != 0) {
        tls_close(&c);
        return -1;
    }

    size_t total = 0;
    while (total + 1 < resp_cap) {
        int n = tls_read(&c, resp + total, resp_cap - total - 1);
        if (n <= 0)
            break;
        total += (size_t)n;
    }
    resp[total] = '\0';

    tls_close(&c);
    return 0;
}
