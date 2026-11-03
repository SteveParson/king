/* Wide-event logger (canonical log lines).
 *
 * Lessons from https://www.loggingsucks.com :
 *  - emit one context-rich JSON event per unit of work
 *  - structured key/value fields (queryable), not diary strings
 *  - high-cardinality IDs + business context on the event
 *  - info + error levels only; tail-sample boring successes
 *
 * Never put secrets (DISCORD_TOKEN) into a log_event.
 */
#ifndef LOG_H
#define LOG_H

#include <limits.h>
#include <stddef.h>

enum { LOG_LEVEL_ERROR = 0, LOG_LEVEL_INFO = 1 };

#ifndef LOG_LEVEL_COMPILE
#define LOG_LEVEL_COMPILE LOG_LEVEL_INFO
#endif

/* Omitted numeric fields use these sentinels (see log_event_new). */
#define LOG_OMIT_I (-1)
#define LOG_OMIT_LL LLONG_MIN
#define LOG_OMIT_ERR INT_MIN

typedef struct log_event {
    const char *event;   /* required: e.g. "message.handle" */
    const char *outcome; /* success | error | ignored | reconnect */

    long long duration_ms;

    /* High-cardinality Discord / correlation IDs */
    const char *request_id;
    const char *channel_id;
    const char *message_id;

    /* Gateway context */
    long long gateway_op;
    long long sequence;
    const char *gateway_t;
    long long heartbeat_interval_ms;

    /* Business context for the king bot */
    int matched_king; /* 0/1, or LOG_OMIT_I */
    int reply_enabled;
    int reply_http_status;
    int reaction_http_status;
    int content_len;
    int heartbeats_ok;
    int heartbeats_fail;

    /* Errors / retries */
    const char *error_type;
    const char *error_message;
    int error_code;
    int attempt;
    int max_attempts;

    /* Optional HTTP / peer context */
    const char *http_method;
    const char *http_path;
    int http_status;
    const char *host;
} log_event;

/* Zero + omit-sentinels; sets .event. */
log_event log_event_new(const char *event);

void log_init_from_env(void);

/* Format one NDJSON object into buf (NUL-terminated). Returns bytes written
 * (excluding NUL), or -1 if truncated/invalid. Pure — used by tests. */
int log_format_event(char *buf, size_t cap, const log_event *ev);

/* Emit to stderr if level/sampling allows. Always keeps errors and
 * matched_king==1. Other info events subject to LOG_SAMPLE_RATE. */
void log_emit(int level, const log_event *ev);

#endif /* LOG_H */
