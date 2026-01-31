#ifndef DISCORD_H
#define DISCORD_H

#include <stddef.h>

typedef struct {
    const char* token;
    const char* reply;
    const char* reaction;
} discord_config;

int discord_run(const discord_config* config);

#endif
