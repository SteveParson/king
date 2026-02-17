/* Minimal client-side WebSocket framing for Discord Gateway. */
#include "ws.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

static int random_bytes(unsigned char* buf, size_t len) {
    return (getrandom(buf, len, 0) == (ssize_t)len) ? 0 : -1;
}

static void base64_encode(const unsigned char* in, size_t in_len, char* out, size_t out_cap) {
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, o = 0;
    while (i < in_len && o + 4 < out_cap) {
        size_t rem = in_len - i;
        unsigned a = rem > 0 ? in[i++] : 0;
        unsigned b = rem > 1 ? in[i++] : 0;
        unsigned c = rem > 2 ? in[i++] : 0;
        unsigned t = (a << 16) | (b << 8) | c;
        out[o++] = b64[(t >> 18) & 0x3F];
        out[o++] = b64[(t >> 12) & 0x3F];
        out[o++] = rem > 1 ? b64[(t >> 6) & 0x3F] : '=';
        out[o++] = rem > 2 ? b64[t & 0x3F] : '=';
    }
    out[o] = '\0';
}

int ws_handshake(tls_conn* c, const char* host, const char* path) {
    unsigned char key_raw[16];
    if (random_bytes(key_raw, sizeof(key_raw)) != 0)
        return -1;

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
    if (n <= 0 || (size_t)n >= sizeof(req))
        return -1;

    if (tls_write(c, req, (size_t)n) != 0)
        return -1;

    char resp[1024];
    int r = tls_read(c, resp, sizeof(resp) - 1);
    if (r <= 0)
        return -1;
    resp[r] = '\0';

    return strstr(resp, "101") ? 0 : -1;
}

int ws_send_text(tls_conn* c, const char* text, size_t len) {
    unsigned char header[14];
    size_t hlen = 0;
    header[0] = 0x81; /* FIN + text opcode */

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
        for (int i = 0; i < 8; i++)
            header[2 + i] = (len >> (56 - i * 8)) & 0xFF;
        hlen = 10;
    }

    unsigned char mask[4];
    if (random_bytes(mask, 4) != 0)
        return -1;
    memcpy(header + hlen, mask, 4);
    hlen += 4;

    unsigned char* frame = malloc(hlen + len);
    if (!frame)
        return -1;
    memcpy(frame, header, hlen);
    for (size_t i = 0; i < len; i++)
        frame[hlen + i] = (unsigned char)text[i] ^ mask[i % 4];

    int rc = tls_write(c, frame, hlen + len);
    free(frame);
    return rc;
}

int ws_read_text(tls_conn* c, char* out, size_t out_cap, size_t* out_len) {
    unsigned char hdr[2];
    if (tls_read_exact(c, hdr, 2) != 0)
        return -1;

    unsigned char opcode = hdr[0] & 0x0F;
    int masked = (hdr[1] & 0x80) != 0;
    uint64_t plen = hdr[1] & 0x7F;

    if (plen == 126) {
        unsigned char ext[2];
        if (tls_read_exact(c, ext, 2) != 0)
            return -1;
        plen = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (plen == 127) {
        unsigned char ext[8];
        if (tls_read_exact(c, ext, 8) != 0)
            return -1;
        plen = 0;
        for (int i = 0; i < 8; i++)
            plen = (plen << 8) | ext[i];
    }

    unsigned char mask[4] = {0};
    if (masked && tls_read_exact(c, mask, 4) != 0)
        return -1;

    if (plen + 1 > out_cap) {
        /* Drain oversized payload */
        unsigned char tmp[512];
        size_t rem = (size_t)plen;
        while (rem > 0) {
            size_t chunk = rem > sizeof(tmp) ? sizeof(tmp) : rem;
            if (tls_read_exact(c, tmp, chunk) != 0)
                return -1;
            rem -= chunk;
        }
        return -1;
    }

    if (tls_read_exact(c, (unsigned char*)out, (size_t)plen) != 0)
        return -1;

    if (masked) {
        for (uint64_t i = 0; i < plen; i++)
            out[i] ^= mask[i % 4];
    }
    out[plen] = '\0';
    if (out_len)
        *out_len = (size_t)plen;

    /* Close frame */
    if (opcode == 0x8)
        return -1;
    /* Only accept text frames */
    if (opcode != 0x1)
        return -1;
    return 0;
}
