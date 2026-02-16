#ifndef DISCORD_H
#define DISCORD_H

#include <stddef.h>

typedef struct {
    const char* token;
    const char* reply;
    const char* reaction;
} discord_config;

int discord_run(const discord_config* config);

/* Pure string utilities (tested independently in tests/). */
int  contains_case(const char* hay, const char* needle);
void json_escape(const char* input, char* out, size_t out_cap);
void url_encode(const char* input, char* out, size_t out_cap);
int  http_status_code(const char* resp);

#endif
