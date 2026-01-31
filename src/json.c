#include "json.h"

#include <ctype.h>
#include <string.h>

static const char* skip_ws(const char* ptr) {
    while (ptr && *ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    return ptr;
}

static size_t append_char(char* out, size_t out_cap, size_t out_len, char value) {
    if (out_len + 1 < out_cap) {
        out[out_len++] = value;
    }
    return out_len;
}

static void skip_unicode_escape(const char** cursor) {
    int count = 0;
    while (count < 4 && (*cursor)[1]) {
        (*cursor)++;
        count++;
    }
}

static size_t handle_escape(const char** cursor, char* out, size_t out_cap, size_t out_len) {
    char esc = **cursor;
    switch (esc) {
    case '"':
    case '\\':
    case '/':
        out_len = append_char(out, out_cap, out_len, esc);
        break;
    case 'b':
        out_len = append_char(out, out_cap, out_len, '\b');
        break;
    case 'f':
        out_len = append_char(out, out_cap, out_len, '\f');
        break;
    case 'n':
        out_len = append_char(out, out_cap, out_len, '\n');
        break;
    case 'r':
        out_len = append_char(out, out_cap, out_len, '\r');
        break;
    case 't':
        out_len = append_char(out, out_cap, out_len, '\t');
        break;
    case 'u':
        skip_unicode_escape(cursor);
        break;
    default:
        break;
    }
    return out_len;
}

static int read_string_value(const char* cursor, char* out, size_t out_cap) {
    if (!cursor || *cursor != '"') {
        return 0;
    }
    cursor++;
    size_t out_len = 0;
    while (*cursor && *cursor != '"') {
        if (*cursor == '\\') {
            cursor++;
            if (!*cursor) {
                break;
            }
            out_len = handle_escape(&cursor, out, out_cap, out_len);
        } else {
            out_len = append_char(out, out_cap, out_len, *cursor);
        }
        cursor++;
    }
    if (out_cap > 0) {
        out[out_len < out_cap ? out_len : out_cap - 1] = '\0';
    }
    return 1;
}

const char* json_find_key(const char* json, const char* key) {
    if (!json || !key) {
        return NULL;
    }
    size_t klen = strlen(key);
    const char* ptr = json;
    while ((ptr = strstr(ptr, "\"")) != NULL) {
        ptr++;
        if (strncmp(ptr, key, klen) == 0 && ptr[klen] == '"') {
            const char* value_ptr = ptr + klen + 1;
            value_ptr = skip_ws(value_ptr);
            if (*value_ptr != ':') {
                ptr = value_ptr;
                continue;
            }
            value_ptr++;
            return skip_ws(value_ptr);
        }
        ptr = ptr + 1;
    }
    return NULL;
}

int json_get_string(const char* json, const char* key, char* out, size_t out_cap) {
    const char* ptr = json_find_key(json, key);
    return read_string_value(ptr, out, out_cap);
}

int json_get_int(const char* json, const char* key, long long* out) {
    const char* ptr = json_find_key(json, key);
    if (!ptr) {
        return 0;
    }
    int neg = 0;
    if (*ptr == '-') {
        neg = 1;
        ptr++;
    }
    if (!isdigit((unsigned char)*ptr)) {
        return 0;
    }
    long long value = 0;
    while (isdigit((unsigned char)*ptr)) {
        value = (value * 10) + (*ptr - '0');
        ptr++;
    }
    *out = neg ? -value : value;
    return 1;
}

static int find_object_range(const char* json, const char* object_key, json_range* range) {
    const char* ptr = json_find_key(json, object_key);
    if (!ptr || *ptr != '{') {
        return 0;
    }
    const char* start = ptr;
    int depth = 0;
    int in_string = 0;
    int esc = 0;
    for (; *ptr; ptr++) {
        if (in_string) {
            if (esc) {
                esc = 0;
                continue;
            }
            if (*ptr == '\\') {
                esc = 1;
            } else if (*ptr == '"') {
                in_string = 0;
            }
            continue;
        }
        if (*ptr == '"') {
            in_string = 1;
            continue;
        }
        if (*ptr == '{') {
            depth++;
        }
        if (*ptr == '}') {
            depth--;
            if (depth == 0) {
                range->start = start;
                range->end = ptr + 1;
                return 1;
            }
        }
    }
    return 0;
}

static const char* find_string_end(const char* start, const char* end) {
    int esc = 0;
    const char* ptr = start;
    while (ptr < end) {
        if (esc) {
            esc = 0;
            ptr++;
            continue;
        }
        if (*ptr == '\\') {
            esc = 1;
            ptr++;
            continue;
        }
        if (*ptr == '"') {
            return ptr;
        }
        ptr++;
    }
    return NULL;
}

static void update_depth(char ch, int* depth) {
    if (ch == '{' || ch == '[') {
        (*depth)++;
    } else if (ch == '}' || ch == ']') {
        (*depth)--;
    }
}

static int scan_top_level_key(const char* start, const char* end, const char* key,
                              const char** out_value) {
    size_t klen = strlen(key);
    int depth = 0;
    const char* ptr = start;
    while (ptr < end) {
        if (*ptr == '"') {
            const char* key_start = ptr + 1;
            const char* key_end = find_string_end(key_start, end);
            if (!key_end) {
                return 0;
            }
            if (depth == 1 && (size_t)(key_end - key_start) == klen &&
                strncmp(key_start, key, klen) == 0) {
                const char* value_ptr = key_end + 1;
                value_ptr = skip_ws(value_ptr);
                if (*value_ptr != ':') {
                    return 0;
                }
                value_ptr++;
                *out_value = skip_ws(value_ptr);
                return 1;
            }
            ptr = key_end;
        } else {
            update_depth(*ptr, &depth);
        }
        ptr++;
    }
    return 0;
}

int json_get_string_in_object(const char* json, const json_object_key* key, char* out,
                              size_t out_cap) {
    json_range range;
    const char* val = NULL;
    if (!find_object_range(json, key->object_key, &range)) {
        return 0;
    }
    if (!scan_top_level_key(range.start, range.end, key->member_key, &val)) {
        return 0;
    }
    return read_string_value(val, out, out_cap);
}

int json_get_int_in_object(const char* json, const json_object_key* key, long long* out) {
    json_range range;
    const char* val = NULL;
    if (!find_object_range(json, key->object_key, &range)) {
        return 0;
    }
    if (!scan_top_level_key(range.start, range.end, key->member_key, &val)) {
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
    long long value = 0;
    while (isdigit((unsigned char)*val)) {
        value = (value * 10) + (*val - '0');
        val++;
    }
    *out = neg ? -value : value;
    return 1;
}
