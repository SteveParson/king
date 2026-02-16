/* Minimal client-side WebSocket framing for Discord Gateway. */
#include "ws.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OS-native random bytes (replaces OpenSSL RAND_bytes). */
#ifdef __linux__
#include <sys/random.h>
static int random_bytes(unsigned char* buf, size_t len) {
    return (getrandom(buf, len, 0) == (ssize_t)len) ? 0 : -1;
}
#else
/* macOS / BSD */
static int random_bytes(unsigned char* buf, size_t len) {
    arc4random_buf(buf, len);
    return 0;
}
#endif

/* Small base64 encoder used for the WebSocket handshake key. */
static void base64_encode(const unsigned char* in, size_t in_len, unsigned char* out,
                          size_t out_cap) {
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0;
    size_t o = 0;
    while (i < in_len && o + 4 < out_cap) {
        size_t remaining = in_len - i;
        unsigned int octet_a = remaining > 0 ? in[i++] : 0;
        unsigned int octet_b = remaining > 1 ? in[i++] : 0;
        unsigned int octet_c = remaining > 2 ? in[i++] : 0;
        unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[o++] = (unsigned char)b64[(triple >> 18) & 0x3F];
        out[o++] = (unsigned char)b64[(triple >> 12) & 0x3F];
        out[o++] = (unsigned char)((remaining > 1) ? b64[(triple >> 6) & 0x3F] : '=');
        out[o++] = (unsigned char)((remaining > 2) ? b64[triple & 0x3F] : '=');
    }
    out[o] = '\0';
}

/* HTTP Upgrade to WebSocket; validates 101 response. */
int ws_handshake(br_sslio_context* ioc, const char* host, const char* path) {
    unsigned char key_raw[16];
    if (random_bytes(key_raw, sizeof(key_raw)) != 0) {
        return -1;
    }

    unsigned char key_b64[64];
    base64_encode(key_raw, sizeof(key_raw), key_b64, sizeof(key_b64));

    char req[512];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n\r\n",
                     path, host, (const char*)key_b64);
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        return -1;
    }

    if (br_sslio_write_all(ioc, req, (size_t)n) != 0) {
        return -1;
    }
    br_sslio_flush(ioc);

    char resp[1024];
    int r = br_sslio_read(ioc, resp, sizeof(resp) - 1);
    if (r <= 0) {
        return -1;
    }
    resp[r] = '\0';

    if (strstr(resp, "101") == NULL) {
        return -1;
    }
    return 0;
}

/* Client-to-server text frame (masked as required by spec). */
int ws_send_text(br_sslio_context* ioc, const char* text, size_t len) {
    unsigned char header[14];
    size_t hlen = 0;
    header[0] = 0x81;

    if (len <= 125) {
        header[1] = 0x80 | (unsigned char)len;
        hlen = 2;
    } else if (len <= 65535) {
        header[1] = 0x80 | 126;
        header[2] = (len >> 8) & 0xFF;
        header[3] = len & 0xFF;
        hlen = 4;
    } else {
        header[1] = 0x80 | 127;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (len >> (56 - i * 8)) & 0xFF;
        }
        hlen = 10;
    }

    unsigned char mask[4];
    if (random_bytes(mask, sizeof(mask)) != 0) {
        return -1;
    }
    memcpy(header + hlen, mask, 4);
    hlen += 4;

    size_t frame_len = hlen + len;
    unsigned char* frame = malloc(frame_len);
    if (!frame) {
        return -1;
    }
    memcpy(frame, header, hlen);
    for (size_t i = 0; i < len; i++) {
        frame[hlen + i] = (unsigned char)text[i] ^ mask[i % 4];
    }

    int rc = br_sslio_write_all(ioc, frame, frame_len);
    free(frame);
    if (rc != 0) {
        return -1;
    }
    br_sslio_flush(ioc);
    return 0;
}

/* Read exactly len bytes from TLS or fail. */
static int read_exact(br_sslio_context* ioc, unsigned char* buf, size_t len) {
    return br_sslio_read_all(ioc, buf, len);
}

static int read_payload_length(br_sslio_context* ioc, unsigned char len_code,
                               uint64_t* payload_len) {
    if (len_code < 126) {
        *payload_len = len_code;
        return 0;
    }
    if (len_code == 126) {
        unsigned char ext[2];
        if (read_exact(ioc, ext, 2) != 0) {
            return -1;
        }
        *payload_len = ((uint64_t)ext[0] << 8) | ext[1];
        return 0;
    }
    unsigned char ext[8];
    if (read_exact(ioc, ext, 8) != 0) {
        return -1;
    }
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value = (value << 8) | ext[i];
    }
    *payload_len = value;
    return 0;
}

static int read_mask(br_sslio_context* ioc, int masked, unsigned char mask[4]) {
    if (!masked) {
        return 0;
    }
    if (read_exact(ioc, mask, 4) != 0) {
        return -1;
    }
    return 0;
}

static int drain_payload(br_sslio_context* ioc, size_t to_drain) {
    unsigned char tmp[512];
    while (to_drain > 0) {
        size_t chunk = to_drain > sizeof(tmp) ? sizeof(tmp) : to_drain;
        if (read_exact(ioc, tmp, chunk) != 0) {
            return -1;
        }
        to_drain -= chunk;
    }
    return 0;
}

static void unmask_payload(unsigned char* out, uint64_t payload_len, const unsigned char mask[4]) {
    for (uint64_t idx = 0; idx < payload_len; idx++) {
        out[idx] = (unsigned char)(out[idx] ^ mask[idx % 4]);
    }
}

static int validate_opcode(unsigned char opcode) {
    if (opcode == 0x8) {
        return -1;
    }
    if (opcode != 0x1) {
        return -1;
    }
    return 0;
}

/* Read a single text frame into out (no fragmentation handling). */
int ws_read_text(br_sslio_context* ioc, char* out, size_t out_cap, size_t* out_len) {
    unsigned char hdr[2];
    if (read_exact(ioc, hdr, 2) != 0) {
        return -1;
    }

    unsigned char opcode = hdr[0] & 0x0F;
    unsigned char masked = (hdr[1] & 0x80) != 0;
    uint64_t payload_len = 0;
    if (read_payload_length(ioc, hdr[1] & 0x7F, &payload_len) != 0) {
        return -1;
    }

    unsigned char mask[4];
    if (read_mask(ioc, masked, mask) != 0) {
        return -1;
    }

    if (payload_len + 1 > out_cap) {
        size_t to_drain = (size_t)payload_len;
        if (drain_payload(ioc, to_drain) != 0) {
            return -1;
        }
        return -1;
    }

    unsigned char* out_bytes = (unsigned char*)out;
    if (read_exact(ioc, out_bytes, (size_t)payload_len) != 0) {
        return -1;
    }
    if (masked) {
        unmask_payload(out_bytes, payload_len, mask);
    }
    out_bytes[payload_len] = '\0';
    if (out_len) {
        *out_len = (size_t)payload_len;
    }

    return validate_opcode(opcode);
}
