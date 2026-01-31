#include "discord.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    log_init_from_env();

    const char *token = getenv("DISCORD_TOKEN");
    const char *reply = getenv("DISCORD_REPLY");
    const char *reaction = getenv("DISCORD_REACTION");

    if (!token || token[0] == '\0') {
        fprintf(stderr, "DISCORD_TOKEN is required (raw token)\n");
        return 1;
    }

    if (!reply) {
        reply = "All hail";
    }

    if (!reaction) {
        reaction = "";
    }

    return discord_run(token, reply, reaction);
}
