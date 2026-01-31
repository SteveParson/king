#include "json.h"

#include <ctype.h>
#include <string.h>

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int read_string_value(const char *p, char *out, size_t out_cap) {
    if (!p || *p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p) break;
            if (*p == '"' || *p == '\\' || *p == '/') {
                if (i + 1 < out_cap) out[i++] = *p;
            } else if (*p == 'b') {
                if (i + 1 < out_cap) out[i++] = '\b';
            } else if (*p == 'f') {
                if (i + 1 < out_cap) out[i++] = '\f';
            } else if (*p == 'n') {
                if (i + 1 < out_cap) out[i++] = '\n';
            } else if (*p == 'r') {
                if (i + 1 < out_cap) out[i++] = '\r';
            } else if (*p == 't') {
                if (i + 1 < out_cap) out[i++] = '\t';
            } else if (*p == 'u') {
                int count = 0;
                while (count < 4 && p[1]) {
                    p++;
                    count++;
                }
            }
        } else {
            if (i + 1 < out_cap) out[i++] = *p;
        }
        p++;
    }
    if (out_cap > 0) out[i < out_cap ? i : out_cap - 1] = '\0';
    return 1;
}

const char *json_find_key(const char *json, const char *key) {
    if (!json || !key) return NULL;
    size_t klen = strlen(key);
    const char *p = json;
    while ((p = strstr(p, "\"")) != NULL) {
        p++;
        if (strncmp(p, key, klen) == 0 && p[klen] == '"') {
            const char *q = p + klen + 1;
            q = skip_ws(q);
            if (*q != ':') {
                p = q;
                continue;
            }
            q++;
            return skip_ws(q);
        }
        p = p + 1;
    }
    return NULL;
}

int json_get_string(const char *json, const char *key, char *out, size_t out_cap) {
    const char *p = json_find_key(json, key);
    return read_string_value(p, out, out_cap);
}

int json_get_int(const char *json, const char *key, long long *out) {
    const char *p = json_find_key(json, key);
    if (!p) return 0;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (!isdigit((unsigned char)*p)) return 0;
    long long v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = neg ? -v : v;
    return 1;
}

static int find_object_range(const char *json, const char *object_key,
                             const char **out_start, const char **out_end) {
    const char *p = json_find_key(json, object_key);
    if (!p || *p != '{') return 0;
    const char *start = p;
    int depth = 0;
    int in_string = 0;
    int esc = 0;
    for (; *p; p++) {
        if (in_string) {
            if (esc) {
                esc = 0;
                continue;
            }
            if (*p == '\\') {
                esc = 1;
            } else if (*p == '\"') {
                in_string = 0;
            }
            continue;
        }
        if (*p == '\"') {
            in_string = 1;
            continue;
        }
        if (*p == '{') depth++;
        if (*p == '}') {
            depth--;
            if (depth == 0) {
                *out_start = start;
                *out_end = p + 1;
                return 1;
            }
        }
    }
    return 0;
}

static int scan_top_level_key(const char *start, const char *end, const char *key,
                              const char **out_value) {
    size_t klen = strlen(key);
    int depth = 0;
    int in_string = 0;
    int esc = 0;
    for (const char *p = start; p < end; p++) {
        if (in_string) {
            if (esc) {
                esc = 0;
                continue;
            }
            if (*p == '\\') {
                esc = 1;
            } else if (*p == '\"') {
                in_string = 0;
            }
            continue;
        }
        if (*p == '\"') {
            const char *s = p + 1;
            const char *q = s;
            while (q < end && *q != '\"') q++;
            if (q >= end) return 0;
            if (depth == 1 && (size_t)(q - s) == klen && strncmp(s, key, klen) == 0) {
                const char *v = q + 1;
                v = skip_ws(v);
                if (*v != ':') return 0;
                v++;
                *out_value = skip_ws(v);
                return 1;
            }
            p = q;
            continue;
        }
        if (*p == '{') depth++;
        if (*p == '}') depth--;
        if (*p == '[') depth++;
        if (*p == ']') depth--;
    }
    return 0;
}

int json_get_string_in_object(const char *json, const char *object_key, const char *key,
                              char *out, size_t out_cap) {
    const char *start = NULL;
    const char *end = NULL;
    const char *val = NULL;
    if (!find_object_range(json, object_key, &start, &end)) return 0;
    if (!scan_top_level_key(start, end, key, &val)) return 0;
    return read_string_value(val, out, out_cap);
}

int json_get_int_in_object(const char *json, const char *object_key, const char *key,
                           long long *out) {
    const char *start = NULL;
    const char *end = NULL;
    const char *val = NULL;
    if (!find_object_range(json, object_key, &start, &end)) return 0;
    if (!scan_top_level_key(start, end, key, &val)) return 0;
    if (!val) return 0;

    int neg = 0;
    if (*val == '-') { neg = 1; val++; }
    if (!isdigit((unsigned char)*val)) return 0;
    long long v = 0;
    while (isdigit((unsigned char)*val)) {
        v = v * 10 + (*val - '0');
        val++;
    }
    *out = neg ? -v : v;
    return 1;
}
