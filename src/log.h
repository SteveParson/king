/* Header-only logging with runtime level and compile-time stripping. */
#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LOG_LEVEL_ERROR = 0, LOG_LEVEL_INFO = 1, LOG_LEVEL_DEBUG = 2 };

#ifndef LOG_LEVEL_COMPILE
#define LOG_LEVEL_COMPILE LOG_LEVEL_DEBUG
#endif

static int g_log_level = LOG_LEVEL_INFO;

static inline void log_init_from_env(void) {
    const char *lvl = getenv("LOG_LEVEL");
    if (!lvl || !*lvl) return;
    if (strcmp(lvl, "error") == 0) g_log_level = LOG_LEVEL_ERROR;
    else if (strcmp(lvl, "debug") == 0) g_log_level = LOG_LEVEL_DEBUG;
}

#if LOG_LEVEL_COMPILE >= LOG_LEVEL_ERROR
__attribute__((format(printf, 1, 2)))
static inline void log_error(const char *fmt, ...) {
    if (g_log_level < LOG_LEVEL_ERROR) return;
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[ERROR] "); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}
#else
#define log_error(...) do {} while (0)
#endif

#if LOG_LEVEL_COMPILE >= LOG_LEVEL_INFO
__attribute__((format(printf, 1, 2)))
static inline void log_info(const char *fmt, ...) {
    if (g_log_level < LOG_LEVEL_INFO) return;
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[INFO] "); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}
#else
#define log_info(...) do {} while (0)
#endif

#if LOG_LEVEL_COMPILE >= LOG_LEVEL_DEBUG
__attribute__((format(printf, 1, 2)))
static inline void log_debug(const char *fmt, ...) {
    if (g_log_level < LOG_LEVEL_DEBUG) return;
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[DEBUG] "); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}
#else
#define log_debug(...) do {} while (0)
#endif

#endif /* LOG_H */
