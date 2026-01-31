#ifndef JSON_H
#define JSON_H

#include <stddef.h>

const char *json_find_key(const char *json, const char *key);
int json_get_string(const char *json, const char *key, char *out, size_t out_cap);
int json_get_int(const char *json, const char *key, long long *out);
int json_get_string_in_object(const char *json, const char *object_key, const char *key,
                              char *out, size_t out_cap);
int json_get_int_in_object(const char *json, const char *object_key, const char *key,
                           long long *out);

#endif
