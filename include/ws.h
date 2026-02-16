#ifndef WS_H
#define WS_H

#include <bearssl.h>
#include <stddef.h>

int ws_send_text(br_sslio_context* ioc, const char* text, size_t len);
int ws_read_text(br_sslio_context* ioc, char* out, size_t out_cap, size_t* out_len);
int ws_handshake(br_sslio_context* ioc, const char* host, const char* path);

#endif
