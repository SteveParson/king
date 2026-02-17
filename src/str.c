/* String utilities — extracted from king.c for testability. */
#include "str.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int contains_case(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) {
        return 0;
    }
    size_t hlen = strlen(hay);
    size_t nlen = strlen(needle);
    if (nlen > hlen) {
        return 0;
    }
    for (size_t i = 0; i + nlen <= hlen; i++) {
        int match = 1;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) {
            int before = (i == 0) || !isalnum((unsigned char)hay[i - 1]);
            int after = (i + nlen == hlen) || !isalnum((unsigned char)hay[i + nlen]);
            if (before && after) {
                return 1;
            }
        }
    }
    return 0;
}

void json_escape(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < cap; i++) {
        unsigned char ch = (unsigned char)in[i];
        if (ch == '"' || ch == '\\') {
            if (o + 2 >= cap) {
                break;
            }
            out[o++] = '\\';
            out[o++] = (char)ch;
        } else if (ch == '\n') {
            out[o++] = '\\';
            out[o++] = 'n';
        } else if (ch == '\r') {
            out[o++] = '\\';
            out[o++] = 'r';
        } else if (ch == '\t') {
            out[o++] = '\\';
            out[o++] = 't';
        } else if (ch < 32) {
            if (o + 6 >= cap) {
                break;
            }
            snprintf(out + o, 7, "\\u%04x", ch);
            o += 6;
        } else {
            out[o++] = (char)ch;
        }
    }
    out[o] = '\0';
}

void url_encode(const char *in, char *out, size_t cap) {
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 4 < cap; i++) {
        unsigned char ch = (unsigned char)in[i];
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out[o++] = (char)ch;
        } else {
            if (o + 3 >= cap) {
                break;
            }
            out[o++] = '%';
            out[o++] = hex[(ch >> 4) & 0xF];
            out[o++] = hex[ch & 0xF];
        }
    }
    out[o] = '\0';
}

int http_status_code(const char *resp) {
    if (!resp) {
        return -1;
    }
    const char *p = strstr(resp, "HTTP/");
    if (!p) {
        return -1;
    }
    p = strchr(p, ' ');
    if (!p) {
        return -1;
    }
    p++;
    int code = 0;
    while (isdigit((unsigned char)*p)) {
        code = (code * 10) + (*p - '0');
        p++;
    }
    return code;
}
