#include "ws.h"

#include <openssl/rand.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void base64_encode(const unsigned char *in, size_t in_len, char *out, size_t out_cap) {
    static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, o = 0;
    while (i < in_len && o + 4 < out_cap) {
        size_t remaining = in_len - i;
        unsigned int octet_a = remaining > 0 ? in[i++] : 0;
        unsigned int octet_b = remaining > 1 ? in[i++] : 0;
        unsigned int octet_c = remaining > 2 ? in[i++] : 0;
        unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[o++] = b64[(triple >> 18) & 0x3F];
        out[o++] = b64[(triple >> 12) & 0x3F];
        out[o++] = (remaining > 1) ? b64[(triple >> 6) & 0x3F] : '=';
        out[o++] = (remaining > 2) ? b64[triple & 0x3F] : '=';
    }
    out[o] = '\0';
}

int ws_handshake(SSL *ssl, const char *host, const char *path) {
    unsigned char key_raw[16];
    if (RAND_bytes(key_raw, sizeof(key_raw)) != 1) return -1;

    char key_b64[64];
    base64_encode(key_raw, sizeof(key_raw), key_b64, sizeof(key_b64));

    char req[512];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n\r\n",
                     path, host, key_b64);
    if (n <= 0 || (size_t)n >= sizeof(req)) return -1;

    if (SSL_write(ssl, req, n) <= 0) return -1;

    char resp[1024];
    int r = SSL_read(ssl, resp, sizeof(resp) - 1);
    if (r <= 0) return -1;
    resp[r] = '\0';

    if (strstr(resp, "101") == NULL) return -1;
    return 0;
}

int ws_send_text(SSL *ssl, const char *text, size_t len) {
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
    if (RAND_bytes(mask, sizeof(mask)) != 1) return -1;
    memcpy(header + hlen, mask, 4);
    hlen += 4;

    size_t frame_len = hlen + len;
    unsigned char *frame = malloc(frame_len);
    if (!frame) return -1;
    memcpy(frame, header, hlen);
    for (size_t i = 0; i < len; i++) {
        frame[hlen + i] = (unsigned char)text[i] ^ mask[i % 4];
    }

    size_t sent = 0;
    while (sent < frame_len) {
        int n = SSL_write(ssl, frame + sent, (int)(frame_len - sent));
        if (n <= 0) {
            free(frame);
            return -1;
        }
        sent += (size_t)n;
    }
    free(frame);
    return 0;
}

static int read_exact(SSL *ssl, unsigned char *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        int n = SSL_read(ssl, buf + got, (int)(len - got));
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

int ws_read_text(SSL *ssl, char *out, size_t out_cap, size_t *out_len) {
    unsigned char hdr[2];
    if (read_exact(ssl, hdr, 2) != 0) return -1;

    unsigned char opcode = hdr[0] & 0x0F;
    unsigned char masked = (hdr[1] & 0x80) != 0;
    uint64_t payload_len = hdr[1] & 0x7F;

    if (payload_len == 126) {
        unsigned char ext[2];
        if (read_exact(ssl, ext, 2) != 0) return -1;
        payload_len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        unsigned char ext[8];
        if (read_exact(ssl, ext, 8) != 0) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) payload_len = (payload_len << 8) | ext[i];
    }

    unsigned char mask[4];
    if (masked) {
        if (read_exact(ssl, mask, 4) != 0) return -1;
    }

    if (payload_len + 1 > out_cap) {
        size_t to_drain = (size_t)payload_len;
        unsigned char tmp[512];
        while (to_drain > 0) {
            size_t chunk = to_drain > sizeof(tmp) ? sizeof(tmp) : to_drain;
            if (read_exact(ssl, tmp, chunk) != 0) return -1;
            to_drain -= chunk;
        }
        return -1;
    }

    if (read_exact(ssl, (unsigned char *)out, (size_t)payload_len) != 0) return -1;
    if (masked) {
        for (size_t i = 0; i < payload_len; i++) out[i] ^= mask[i % 4];
    }
    out[payload_len] = '\0';
    if (out_len) *out_len = (size_t)payload_len;

    if (opcode == 0x8) return -1;
    if (opcode != 0x1) return -1;

    return 0;
}
