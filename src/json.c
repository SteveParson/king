/* Tiny JSON helpers: enough for Discord gateway payloads. */
#include "json.h"
#include <ctype.h>
#include <string.h>

static const char* skip_ws(const char* p) {
    while (p && *p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int read_string_value(const char* c, char* out, size_t cap) {
    if (!c || *c != '"') {
        return 0;
    }
    c++;
    size_t len = 0;
    while (*c && *c != '"') {
        if (*c == '\\') {
            c++;
            if (!*c) {
                break;
            }
            char ch = 0;
            switch (*c) {
            case '"':
            case '\\':
            case '/':
                ch = *c;
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case 'u':
                for (int i = 0; i < 4 && c[1]; i++) {
                    c++;
                }
                break;
            }
            if (ch && len + 1 < cap) {
                out[len++] = ch;
            }
        } else {
            if (len + 1 < cap) {
                out[len++] = *c;
            }
        }
        c++;
    }
    if (cap > 0) {
        out[len < cap ? len : cap - 1] = '\0';
    }
    return 1;
}

const char* json_find_key(const char* json, const char* key) {
    if (!json || !key) {
        return NULL;
    }
    size_t klen = strlen(key);
    const char* p = json;
    while ((p = strstr(p, "\"")) != NULL) {
        p++;
        if (strncmp(p, key, klen) == 0 && p[klen] == '"') {
            const char* v = skip_ws(p + klen + 1);
            if (*v != ':') {
                p = v;
                continue;
            }
            return skip_ws(v + 1);
        }
        p++;
    }
    return NULL;
}

int json_get_string(const char* json, const char* key, char* out, size_t cap) {
    return read_string_value(json_find_key(json, key), out, cap);
}

int json_get_int(const char* json, const char* key, long long* out) {
    const char* p = json_find_key(json, key);
    if (!p) {
        return 0;
    }
    int neg = 0;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    long long v = 0;
    while (isdigit((unsigned char)*p)) {
        v = (v * 10) + (*p - '0');
        p++;
    }
    *out = neg ? -v : v;
    return 1;
}

/* Find the byte range of a JSON object value. */
static int find_object_range(const char* json, const char* okey, const char** start,
                             const char** end) {
    const char* p = json_find_key(json, okey);
    if (!p || *p != '{') {
        return 0;
    }
    const char* s = p;
    int depth = 0;
    int in_str = 0;
    int esc = 0;
    for (; *p; p++) {
        if (in_str) {
            if (esc) {
                esc = 0;
                continue;
            }
            if (*p == '\\') {
                esc = 1;
            } else if (*p == '"') {
                in_str = 0;
            }
            continue;
        }
        if (*p == '"') {
            in_str = 1;
            continue;
        }
        if (*p == '{') {
            depth++;
        }
        if (*p == '}' && --depth == 0) {
            *start = s;
            *end = p + 1;
            return 1;
        }
    }
    return 0;
}

static const char* find_string_end(const char* s, const char* end) {
    int esc = 0;
    while (s < end) {
        if (esc) {
            esc = 0;
            s++;
            continue;
        }
        if (*s == '\\') {
            esc = 1;
            s++;
            continue;
        }
        if (*s == '"') {
            return s;
        }
        s++;
    }
    return NULL;
}

static int scan_key(const char* start, const char* end, const char* key, const char** val) {
    size_t klen = strlen(key);
    int depth = 0;
    const char* p = start;
    while (p < end) {
        if (*p == '"') {
            const char* ks = p + 1;
            const char* ke = find_string_end(ks, end);
            if (!ke) {
                return 0;
            }
            if (depth == 1 && (size_t)(ke - ks) == klen && strncmp(ks, key, klen) == 0) {
                const char* v = skip_ws(ke + 1);
                if (*v != ':') {
                    return 0;
                }
                *val = skip_ws(v + 1);
                return 1;
            }
            p = ke;
        } else {
            if (*p == '{' || *p == '[') {
                depth++;
            } else if (*p == '}' || *p == ']') {
                depth--;
            }
        }
        p++;
    }
    return 0;
}

int json_get_string_in_object(const char* json, const json_object_key* key, char* out, size_t cap) {
    const char* start;
    const char* end;
    const char* val = NULL;
    if (!find_object_range(json, key->object_key, &start, &end)) {
        return 0;
    }
    if (!scan_key(start, end, key->member_key, &val)) {
        return 0;
    }
    return read_string_value(val, out, cap);
}

int json_get_int_in_object(const char* json, const json_object_key* key, long long* out) {
    const char* start;
    const char* end;
    const char* val = NULL;
    if (!find_object_range(json, key->object_key, &start, &end)) {
        return 0;
    }
    if (!scan_key(start, end, key->member_key, &val)) {
        return 0;
    }
    if (!val) {
        return 0;
    }
    int neg = 0;
    if (*val == '-') {
        neg = 1;
        val++;
    }
    if (!isdigit((unsigned char)*val)) {
        return 0;
    }
    long long v = 0;
    while (isdigit((unsigned char)*val)) {
        v = (v * 10) + (*val - '0');
        val++;
    }
    *out = neg ? -v : v;
    return 1;
}
