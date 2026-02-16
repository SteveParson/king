/* WebSocket framing for Discord Gateway. */
#ifndef WS_H
#define WS_H

#include "net.h"
#include <stddef.h>

int ws_handshake(tls_conn *c, const char *host, const char *path);
int ws_send_text(tls_conn *c, const char *text, size_t len);
int ws_read_text(tls_conn *c, char *out, size_t out_cap, size_t *out_len);

#endif
