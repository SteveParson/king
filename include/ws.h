#ifndef WS_H
#define WS_H

#include <openssl/ssl.h>
#include <stddef.h>

int ws_send_text(SSL *ssl, const char *text, size_t len);
int ws_read_text(SSL *ssl, char *out, size_t out_cap, size_t *out_len);
int ws_handshake(SSL *ssl, const char *host, const char *path);

#endif
