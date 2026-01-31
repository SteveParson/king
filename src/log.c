#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_DEBUG = 2
};

static int g_log_level = LOG_LEVEL_INFO;

void log_set_level(int level) {
    if (level < LOG_LEVEL_ERROR) level = LOG_LEVEL_ERROR;
    if (level > LOG_LEVEL_DEBUG) level = LOG_LEVEL_DEBUG;
    g_log_level = level;
}

int log_get_level(void) {
    return g_log_level;
}

void log_init_from_env(void) {
    const char *lvl = getenv("LOG_LEVEL");
    if (!lvl || !*lvl) return;
    if (strcmp(lvl, "error") == 0) {
        log_set_level(LOG_LEVEL_ERROR);
    } else if (strcmp(lvl, "info") == 0) {
        log_set_level(LOG_LEVEL_INFO);
    } else if (strcmp(lvl, "debug") == 0) {
        log_set_level(LOG_LEVEL_DEBUG);
    }
}

static void log_v(const char *tag, const char *fmt, va_list ap) {
    fprintf(stderr, "[%s] ", tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void log_error(const char *fmt, ...) {
    if (g_log_level < LOG_LEVEL_ERROR) return;
    va_list ap;
    va_start(ap, fmt);
    log_v("ERROR", fmt, ap);
    va_end(ap);
}

void log_info(const char *fmt, ...) {
    if (g_log_level < LOG_LEVEL_INFO) return;
    va_list ap;
    va_start(ap, fmt);
    log_v("INFO", fmt, ap);
    va_end(ap);
}

void log_debug(const char *fmt, ...) {
    if (g_log_level < LOG_LEVEL_DEBUG) return;
    va_list ap;
    va_start(ap, fmt);
    log_v("DEBUG", fmt, ap);
    va_end(ap);
}
