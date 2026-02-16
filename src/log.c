/* Lightweight logger with runtime level and optional compile-time stripping. */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default to info-level logging unless overridden by LOG_LEVEL env. */
static int g_log_level = LOG_LEVEL_INFO;

void log_set_level(int level) {
    if (level < LOG_LEVEL_ERROR) {
        level = LOG_LEVEL_ERROR;
    }
    if (level > LOG_LEVEL_DEBUG) {
        level = LOG_LEVEL_DEBUG;
    }
    g_log_level = level;
}

int log_get_level(void) {
    return g_log_level;
}

void log_init_from_env(void) {
    /* Accepts: error | info | debug */
    const char* lvl = getenv("LOG_LEVEL");
    if (!lvl || !*lvl) {
        return;
    }
    if (strcmp(lvl, "error") == 0) {
        log_set_level(LOG_LEVEL_ERROR);
    } else if (strcmp(lvl, "info") == 0) {
        log_set_level(LOG_LEVEL_INFO);
    } else if (strcmp(lvl, "debug") == 0) {
        log_set_level(LOG_LEVEL_DEBUG);
    }
}

static const char* log_tag(int level) {
    switch (level) {
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    default:
        return "LOG";
    }
}

static void log_v(int level, const char* fmt, va_list ap) {
    fprintf(stderr, "[%s] ", log_tag(level));
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void log_error(const char* fmt, ...) {
    if (LOG_LEVEL_COMPILE < LOG_LEVEL_ERROR) {
        return;
    }
    if (g_log_level < LOG_LEVEL_ERROR) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    log_v(LOG_LEVEL_ERROR, fmt, ap);
    va_end(ap);
}

#if LOG_LEVEL_COMPILE >= LOG_LEVEL_INFO
void log_info(const char* fmt, ...) {
    if (g_log_level < LOG_LEVEL_INFO) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    log_v(LOG_LEVEL_INFO, fmt, ap);
    va_end(ap);
}
#endif

#if LOG_LEVEL_COMPILE >= LOG_LEVEL_DEBUG
void log_debug(const char* fmt, ...) {
    if (g_log_level < LOG_LEVEL_DEBUG) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    log_v(LOG_LEVEL_DEBUG, fmt, ap);
    va_end(ap);
}
#endif
