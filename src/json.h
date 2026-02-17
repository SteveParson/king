/* Minimal JSON key extraction for Discord payloads. */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>

typedef struct {
    const char *object_key;
    const char *member_key;
} json_object_key;

const char *json_find_key(const char *json, const char *key);
int json_get_string(const char *json, const char *key, char *out, size_t out_cap);
int json_get_int(const char *json, const char *key, long long *out);
int json_get_string_in_object(const char *json, const json_object_key *key, char *out,
                              size_t out_cap);
int json_get_int_in_object(const char *json, const json_object_key *key, long long *out);

#endif
