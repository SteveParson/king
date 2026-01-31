#ifndef LOG_H
#define LOG_H

void log_init_from_env(void);
void log_set_level(int level);
int log_get_level(void);

void log_error(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_debug(const char *fmt, ...);

#endif
