/* Wide-event / canonical log line emitter. */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_log_level = LOG_LEVEL_INFO;
static double g_sample_rate = 1.0; /* keep all by default — bot traffic is tiny */
static const char *g_version = "0.1";
static const char *g_commit = "";
static unsigned g_sample_counter;

log_event log_event_new(const char *event) {
    log_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = event;
    ev.duration_ms = LOG_OMIT_LL;
    ev.gateway_op = LOG_OMIT_LL;
    ev.sequence = LOG_OMIT_LL;
    ev.heartbeat_interval_ms = LOG_OMIT_LL;
    ev.matched_king = LOG_OMIT_I;
    ev.reply_enabled = LOG_OMIT_I;
    ev.reply_http_status = LOG_OMIT_I;
    ev.reaction_http_status = LOG_OMIT_I;
    ev.content_len = LOG_OMIT_I;
    ev.heartbeats_ok = LOG_OMIT_I;
    ev.heartbeats_fail = LOG_OMIT_I;
    ev.error_code = LOG_OMIT_ERR;
    ev.attempt = LOG_OMIT_I;
    ev.max_attempts = LOG_OMIT_I;
    ev.http_status = LOG_OMIT_I;
    return ev;
}

void log_init_from_env(void) {
    const char *lvl = getenv("LOG_LEVEL");
    if (lvl && strcmp(lvl, "error") == 0) {
        g_log_level = LOG_LEVEL_ERROR;
    } else if (lvl && (strcmp(lvl, "info") == 0 || strcmp(lvl, "debug") == 0)) {
        g_log_level = LOG_LEVEL_INFO;
    }

    const char *rate = getenv("LOG_SAMPLE_RATE");
    if (rate && *rate) {
        char *end = NULL;
        double v = strtod(rate, &end);
        if (end != rate && v >= 0.0 && v <= 1.0) {
            g_sample_rate = v;
        }
    }

    const char *ver = getenv("KING_VERSION");
    if (ver && *ver) {
        g_version = ver;
    }
    const char *commit = getenv("KING_COMMIT");
    if (commit && *commit) {
        g_commit = commit;
    }
}

static int append_fmt(char *buf, size_t cap, size_t *off, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static int append_fmt(char *buf, size_t cap, size_t *off, const char *fmt, ...) {
    if (*off >= cap) {
        return -1;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, cap - *off, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *off) {
        *off = cap;
        return -1;
    }
    *off += (size_t)n;
    return 0;
}

static int append_json_str(char *buf, size_t cap, size_t *off, const char *s) {
    if (append_fmt(buf, cap, off, "\"") != 0) {
        return -1;
    }
    for (size_t i = 0; s[i]; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '"' || ch == '\\') {
            if (append_fmt(buf, cap, off, "\\%c", ch) != 0) {
                return -1;
            }
        } else if (ch == '\n') {
            if (append_fmt(buf, cap, off, "\\n") != 0) {
                return -1;
            }
        } else if (ch == '\r') {
            if (append_fmt(buf, cap, off, "\\r") != 0) {
                return -1;
            }
        } else if (ch == '\t') {
            if (append_fmt(buf, cap, off, "\\t") != 0) {
                return -1;
            }
        } else if (ch < 32) {
            if (append_fmt(buf, cap, off, "\\u%04x", ch) != 0) {
                return -1;
            }
        } else {
            if (append_fmt(buf, cap, off, "%c", ch) != 0) {
                return -1;
            }
        }
    }
    return append_fmt(buf, cap, off, "\"");
}

static int append_field_str(char *buf, size_t cap, size_t *off, int *first, const char *key,
                            const char *val) {
    if (!val) {
        return 0;
    }
    if (!*first && append_fmt(buf, cap, off, ",") != 0) {
        return -1;
    }
    *first = 0;
    if (append_fmt(buf, cap, off, "\"%s\":", key) != 0) {
        return -1;
    }
    return append_json_str(buf, cap, off, val);
}

static int append_field_ll(char *buf, size_t cap, size_t *off, int *first, const char *key,
                           long long val, long long omit) {
    if (val == omit) {
        return 0;
    }
    if (!*first && append_fmt(buf, cap, off, ",") != 0) {
        return -1;
    }
    *first = 0;
    return append_fmt(buf, cap, off, "\"%s\":%lld", key, val);
}

static int append_field_i(char *buf, size_t cap, size_t *off, int *first, const char *key, int val,
                          int omit) {
    if (val == omit) {
        return 0;
    }
    if (!*first && append_fmt(buf, cap, off, ",") != 0) {
        return -1;
    }
    *first = 0;
    return append_fmt(buf, cap, off, "\"%s\":%d", key, val);
}

static int append_field_bool(char *buf, size_t cap, size_t *off, int *first, const char *key,
                             int val) {
    if (val == LOG_OMIT_I) {
        return 0;
    }
    if (!*first && append_fmt(buf, cap, off, ",") != 0) {
        return -1;
    }
    *first = 0;
    return append_fmt(buf, cap, off, "\"%s\":%s", key, val ? "true" : "false");
}

int log_format_event(char *buf, size_t cap, const log_event *ev) {
    if (!buf || cap < 2 || !ev || !ev->event) {
        return -1;
    }

    size_t off = 0;
    int first = 1;
    if (append_fmt(buf, cap, &off, "{") != 0) {
        return -1;
    }

    char ts[32];
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);

    if (append_field_str(buf, cap, &off, &first, "timestamp", ts) != 0 ||
        append_field_str(buf, cap, &off, &first, "service", "king") != 0 ||
        append_field_str(buf, cap, &off, &first, "version", g_version) != 0 ||
        append_field_i(buf, cap, &off, &first, "pid", (int)getpid(), LOG_OMIT_ERR) != 0) {
        return -1;
    }
    if (g_commit && *g_commit) {
        if (append_field_str(buf, cap, &off, &first, "commit", g_commit) != 0) {
            return -1;
        }
    }

    if (append_field_str(buf, cap, &off, &first, "event", ev->event) != 0 ||
        append_field_str(buf, cap, &off, &first, "outcome", ev->outcome) != 0 ||
        append_field_ll(buf, cap, &off, &first, "duration_ms", ev->duration_ms, LOG_OMIT_LL) != 0 ||
        append_field_str(buf, cap, &off, &first, "request_id", ev->request_id) != 0 ||
        append_field_str(buf, cap, &off, &first, "channel_id", ev->channel_id) != 0 ||
        append_field_str(buf, cap, &off, &first, "message_id", ev->message_id) != 0 ||
        append_field_ll(buf, cap, &off, &first, "gateway_op", ev->gateway_op, LOG_OMIT_LL) != 0 ||
        append_field_ll(buf, cap, &off, &first, "sequence", ev->sequence, LOG_OMIT_LL) != 0 ||
        append_field_str(buf, cap, &off, &first, "gateway_t", ev->gateway_t) != 0 ||
        append_field_ll(buf, cap, &off, &first, "heartbeat_interval_ms", ev->heartbeat_interval_ms,
                        LOG_OMIT_LL) != 0 ||
        append_field_bool(buf, cap, &off, &first, "matched_king", ev->matched_king) != 0 ||
        append_field_bool(buf, cap, &off, &first, "reply_enabled", ev->reply_enabled) != 0 ||
        append_field_i(buf, cap, &off, &first, "reply_http_status", ev->reply_http_status,
                       LOG_OMIT_I) != 0 ||
        append_field_i(buf, cap, &off, &first, "reaction_http_status", ev->reaction_http_status,
                       LOG_OMIT_I) != 0 ||
        append_field_i(buf, cap, &off, &first, "content_len", ev->content_len, LOG_OMIT_I) != 0 ||
        append_field_i(buf, cap, &off, &first, "heartbeats_ok", ev->heartbeats_ok, LOG_OMIT_I) !=
            0 ||
        append_field_i(buf, cap, &off, &first, "heartbeats_fail", ev->heartbeats_fail,
                       LOG_OMIT_I) != 0 ||
        append_field_str(buf, cap, &off, &first, "error_type", ev->error_type) != 0 ||
        append_field_str(buf, cap, &off, &first, "error_message", ev->error_message) != 0 ||
        append_field_i(buf, cap, &off, &first, "error_code", ev->error_code, LOG_OMIT_ERR) != 0 ||
        append_field_i(buf, cap, &off, &first, "attempt", ev->attempt, LOG_OMIT_I) != 0 ||
        append_field_i(buf, cap, &off, &first, "max_attempts", ev->max_attempts, LOG_OMIT_I) != 0 ||
        append_field_str(buf, cap, &off, &first, "http_method", ev->http_method) != 0 ||
        append_field_str(buf, cap, &off, &first, "http_path", ev->http_path) != 0 ||
        append_field_i(buf, cap, &off, &first, "http_status", ev->http_status, LOG_OMIT_I) != 0 ||
        append_field_str(buf, cap, &off, &first, "host", ev->host) != 0) {
        return -1;
    }

    if (append_fmt(buf, cap, &off, "}") != 0) {
        return -1;
    }
    return (int)off;
}

static int should_emit(int level, const log_event *ev) {
    if (level > g_log_level) {
        return 0;
    }
    if (level <= LOG_LEVEL_ERROR) {
        return 1;
    }
    if (ev->outcome && strcmp(ev->outcome, "error") == 0) {
        return 1;
    }
    if (ev->matched_king == 1) {
        return 1;
    }
    if (g_sample_rate >= 1.0) {
        return 1;
    }
    if (g_sample_rate <= 0.0) {
        return 0;
    }
    g_sample_counter++;
    unsigned threshold = (unsigned)(g_sample_rate * 1000.0);
    return (g_sample_counter % 1000) < threshold;
}

void log_emit(int level, const log_event *ev) {
#if LOG_LEVEL_COMPILE < LOG_LEVEL_ERROR
    (void)level;
    (void)ev;
#else
    if (!ev || !should_emit(level, ev)) {
        return;
    }
    char line[2048];
    if (log_format_event(line, sizeof(line), ev) < 0) {
        fprintf(stderr,
                "{\"event\":\"log.format_error\",\"outcome\":\"error\",\"service\":\"king\"}\n");
        return;
    }
    fputs(line, stderr);
    fputc('\n', stderr);
#endif
}
