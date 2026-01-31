#ifndef LOG_H
#define LOG_H

enum { LOG_LEVEL_ERROR = 0, LOG_LEVEL_INFO = 1, LOG_LEVEL_DEBUG = 2 };

#ifndef LOG_LEVEL_COMPILE
#define LOG_LEVEL_COMPILE LOG_LEVEL_DEBUG
#endif

void log_init_from_env(void);
void log_set_level(int level);
int log_get_level(void);

#if LOG_LEVEL_COMPILE >= LOG_LEVEL_ERROR
void log_error(const char* fmt, ...);
#else
#define log_error(...)                                                                             \
    do {                                                                                           \
    } while (0)
#endif

#if LOG_LEVEL_COMPILE >= LOG_LEVEL_INFO
void log_info(const char* fmt, ...);
#else
#define log_info(...)                                                                              \
    do {                                                                                           \
    } while (0)
#endif

#if LOG_LEVEL_COMPILE >= LOG_LEVEL_DEBUG
void log_debug(const char* fmt, ...);
#else
#define log_debug(...)                                                                             \
    do {                                                                                           \
    } while (0)
#endif

#endif
