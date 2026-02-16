/* Entry point: reads env config and starts the Discord gateway loop. */
#include "discord.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    log_init_from_env();

    /* Runtime config comes from environment variables. */
    const char* token = getenv("DISCORD_TOKEN");
    const char* reply = getenv("DISCORD_REPLY");
    const char* reaction = getenv("DISCORD_REACTION");

    if (!token || token[0] == '\0') {
        fprintf(stderr, "DISCORD_TOKEN is required (raw token)\n");
        return 1;
    }

    /* Empty string disables replies; NULL uses the default. */
    if (!reply) {
        reply = "All hail";
    }

    /* Empty string disables reactions. */
    if (!reaction) {
        reaction = "";
    }

    discord_config config;
    config.token = token;
    config.reply = reply;
    config.reaction = reaction;

    return discord_run(&config);
}
