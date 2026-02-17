/* TLS networking with mbedtls for cosmopolitan. */
#ifndef NET_H
#define NET_H

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <stddef.h>

/* Opaque TLS connection handle. */
typedef struct {
    int fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt ca;
} tls_conn;

/* Initialize the global CA certificate store. Call once at startup. */
int tls_init_ca(void);

/* Connect via TLS to host:port. Returns 0 on success, -1 on failure. */
int tls_connect(tls_conn *c, const char *host, const char *port);

/* Close and free TLS resources. */
void tls_close(tls_conn *c);

/* Write exactly len bytes over TLS. Returns 0 on success. */
int tls_write(tls_conn *c, const void *buf, size_t len);

/* Read up to cap bytes. Returns bytes read, or -1 on error. */
int tls_read(tls_conn *c, void *buf, size_t cap);

/* Read exactly len bytes. Returns 0 on success. */
int tls_read_exact(tls_conn *c, void *buf, size_t len);

/* Convenience: open a TLS connection, send a raw HTTP request,
   read the full response, close the connection. */
int https_request(const char *host, const char *port, const char *req, char *resp, size_t resp_cap);

#endif
