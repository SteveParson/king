/* King Discord Bot — Cosmopolitan port.
 * Entry point + Discord Gateway/REST orchestration. */
#include "json.h"
#include "log.h"
#include "net.h"
#include "str.h"
#include "ws.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define GATEWAY_HOST "gateway.discord.gg"
#define API_HOST "discord.com"
#define API_PORT "443"
#define GATEWAY_PATH "/?v=10&encoding=json"
#define BUF_SIZE 65536

/* Discord Gateway opcodes we care about. */
enum {
    OP_DISPATCH = 0,
    OP_HEARTBEAT = 1,
    OP_IDENTIFY = 2,
    OP_RECONNECT = 7,
    OP_INVALID_SESSION = 9,
    OP_HELLO = 10,
    OP_HEARTBEAT_ACK = 11,
};

/* GUILD_MESSAGES (512) | MESSAGE_CONTENT (32768) */
#define GATEWAY_INTENTS 33280

#define HB_MAX_RETRIES 3
#define HB_RETRY_USEC_START 100000

#define DEFAULT_REACTION "\xf0\x9f\x91\x91" /* 👑 */

/* ── Monotonic clock ──────────────────────────────────────────── */

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((long long)ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
}

static void make_request_id(char *out, size_t cap) {
    static unsigned counter;
    snprintf(out, cap, "req_%08lx%04x", (unsigned long)time(NULL), (++counter) & 0xffff);
}

/* ── REST API helpers ─────────────────────────────────────────── */

static int api_request(const char *auth, const char *method, const char *path, const char *body,
                       char *resp, size_t resp_cap) {
    char req[8192];
    size_t blen = body ? strlen(body) : 0;
    int n = snprintf(req, sizeof(req),
                     "%s %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: king-cosmo/0.1\r\n"
                     "Authorization: %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n\r\n"
                     "%s",
                     method, path, API_HOST, auth, blen, body ? body : "");
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        return -1;
    }
    return https_request(API_HOST, API_PORT, req, resp, resp_cap);
}

/* Returns HTTP status, or -1 on transport failure. */
static int send_message(const char *auth, const char *channel_id, const char *content) {
    char esc[2048];
    char body[4096];
    char path[256];
    char resp[4096];

    json_escape(content, esc, sizeof(esc));
    snprintf(body, sizeof(body), "{\"content\":\"%s\"}", esc);
    snprintf(path, sizeof(path), "/api/v10/channels/%s/messages", channel_id);

    if (api_request(auth, "POST", path, body, resp, sizeof(resp)) != 0) {
        return -1;
    }
    return http_status_code(resp);
}

/* Returns HTTP status, -1 on transport failure, LOG_OMIT_I if emoji disabled. */
static int send_reaction(const char *auth, const char *channel_id, const char *message_id,
                         const char *emoji) {
    if (!emoji || !*emoji) {
        return LOG_OMIT_I;
    }

    char enc[256];
    char path[512];
    char resp[4096];

    url_encode(emoji, enc, sizeof(enc));
    snprintf(path, sizeof(path), "/api/v10/channels/%s/messages/%s/reactions/%s/@me", channel_id,
             message_id, enc);

    if (api_request(auth, "PUT", path, "", resp, sizeof(resp)) != 0) {
        return -1;
    }
    return http_status_code(resp);
}

/* ── Gateway operations ───────────────────────────────────────── */

typedef struct {
    tls_conn conn;
    long long heartbeat_interval;
    long long sequence;
    long long next_heartbeat;
    long long session_start_ms;
    unsigned heartbeats_ok;
    unsigned heartbeats_fail;
} gateway_state;

/* Split DISCORD_TOKEN into REST Authorization header and raw gateway token. */
static void normalize_token(const char *token, char *auth, size_t auth_cap, char *gateway_token,
                            size_t gateway_cap) {
    if (strncmp(token, "Bot ", 4) == 0) {
        snprintf(auth, auth_cap, "%s", token);
        snprintf(gateway_token, gateway_cap, "%s", token + 4);
    } else {
        snprintf(auth, auth_cap, "Bot %s", token);
        snprintf(gateway_token, gateway_cap, "%s", token);
    }
}

static int send_identify(tls_conn *c, const char *token) {
    char payload[1024];
    int n = snprintf(payload, sizeof(payload),
                     "{\"op\":%d,\"d\":{"
                     "\"token\":\"%s\","
                     "\"intents\":%d,"
                     "\"properties\":{"
                     "\"os\":\"cosmo\","
                     "\"browser\":\"king-cosmo\","
                     "\"device\":\"king-cosmo\""
                     "}}}",
                     OP_IDENTIFY, token, GATEWAY_INTENTS);
    if (n <= 0 || (size_t)n >= sizeof(payload)) {
        return -1;
    }
    return ws_send_text(c, payload, (size_t)n);
}

static int send_heartbeat(tls_conn *c, long long seq) {
    char payload[64];
    int n;
    if (seq >= 0) {
        n = snprintf(payload, sizeof(payload), "{\"op\":%d,\"d\":%lld}", OP_HEARTBEAT, seq);
    } else {
        n = snprintf(payload, sizeof(payload), "{\"op\":%d,\"d\":null}", OP_HEARTBEAT);
    }
    if (n <= 0 || (size_t)n >= sizeof(payload)) {
        return -1;
    }
    return ws_send_text(c, payload, (size_t)n);
}

static int heartbeat_with_retry(gateway_state *s) {
    int delay = HB_RETRY_USEC_START;

    for (int attempt = 0; attempt < HB_MAX_RETRIES; attempt++) {
        if (send_heartbeat(&s->conn, s->sequence) == 0) {
            s->heartbeats_ok++;
            s->next_heartbeat = now_ms() + s->heartbeat_interval;
            return 0;
        }
        s->heartbeats_fail++;
        if (attempt + 1 < HB_MAX_RETRIES) {
            usleep(delay);
            delay *= 2;
        }
    }

    log_event ev = log_event_new("gateway.heartbeat");
    ev.outcome = "error";
    ev.sequence = s->sequence;
    ev.heartbeat_interval_ms = s->heartbeat_interval;
    ev.attempt = HB_MAX_RETRIES;
    ev.max_attempts = HB_MAX_RETRIES;
    ev.error_type = "HeartbeatError";
    ev.error_message = "heartbeat failed after retries";
    log_emit(LOG_LEVEL_ERROR, &ev);
    return -1;
}

static int handle_hello(gateway_state *s, const char *token, const char *payload) {
    long long interval = 0;
    json_object_key hb_key = {"d", "heartbeat_interval"};
    long long t0 = now_ms();

    if (json_get_int_in_object(payload, &hb_key, &interval)) {
        s->heartbeat_interval = interval;
    }
    if (send_identify(&s->conn, token) != 0) {
        log_event ev = log_event_new("gateway.identify");
        ev.outcome = "error";
        ev.duration_ms = now_ms() - t0;
        ev.gateway_op = OP_HELLO;
        ev.heartbeat_interval_ms = s->heartbeat_interval;
        ev.error_type = "IdentifyError";
        ev.error_message = "ws send identify failed";
        log_emit(LOG_LEVEL_ERROR, &ev);
        return -1;
    }

    s->next_heartbeat = now_ms() + s->heartbeat_interval;

    log_event ev = log_event_new("gateway.hello");
    ev.outcome = "success";
    ev.duration_ms = now_ms() - t0;
    ev.gateway_op = OP_HELLO;
    ev.heartbeat_interval_ms = s->heartbeat_interval;
    log_emit(LOG_LEVEL_INFO, &ev);
    return 0;
}

static void handle_message_create(gateway_state *s, const char *auth, const char *reply,
                                  const char *reaction, const char *payload) {
    if (strstr(payload, "\"bot\":true")) {
        return;
    }

    char content[2048];
    char channel_id[64];
    char message_id[64];
    char request_id[32];
    json_object_key content_key = {"d", "content"};
    json_object_key channel_key = {"d", "channel_id"};
    json_object_key id_key = {"d", "id"};

    if (!json_get_string_in_object(payload, &content_key, content, sizeof(content)) ||
        !json_get_string_in_object(payload, &channel_key, channel_id, sizeof(channel_id)) ||
        !json_get_string_in_object(payload, &id_key, message_id, sizeof(message_id))) {
        return;
    }

    long long t0 = now_ms();
    make_request_id(request_id, sizeof(request_id));

    log_event ev = log_event_new("message.handle");
    ev.request_id = request_id;
    ev.channel_id = channel_id;
    ev.message_id = message_id;
    ev.sequence = s->sequence;
    ev.gateway_t = "MESSAGE_CREATE";
    ev.content_len = (int)strlen(content);
    ev.reply_enabled = (reply && *reply) ? 1 : 0;
    ev.matched_king = contains_case(content, "king") ? 1 : 0;

    if (!ev.matched_king) {
        /* Not a unit of business work — skip (avoid diary noise). */
        return;
    }

    if (reply && *reply) {
        ev.reply_http_status = send_message(auth, channel_id, reply);
    }
    if (reaction && *reaction) {
        ev.reaction_http_status = send_reaction(auth, channel_id, message_id, reaction);
    }

    ev.duration_ms = now_ms() - t0;
    int reply_bad = (ev.reply_http_status != LOG_OMIT_I) &&
                    (ev.reply_http_status < 0 || ev.reply_http_status >= 400);
    int reaction_bad = (ev.reaction_http_status != LOG_OMIT_I) &&
                       (ev.reaction_http_status < 0 || ev.reaction_http_status >= 400);
    if (reply_bad || reaction_bad) {
        ev.outcome = "error";
        ev.error_type = "DiscordRestError";
        ev.error_message = reply_bad ? "reply failed" : "reaction failed";
        log_emit(LOG_LEVEL_ERROR, &ev);
    } else {
        ev.outcome = "success";
        log_emit(LOG_LEVEL_INFO, &ev);
    }
}

static int wait_for_hello(gateway_state *s, const char *token, char *buf, size_t buf_cap) {
    size_t len = 0;
    while (ws_read_text(&s->conn, buf, buf_cap, &len) == 0) {
        long long op = -1;
        if (json_get_int(buf, "op", &op) && op == OP_HELLO) {
            return handle_hello(s, token, buf);
        }
    }
    return -1;
}

/* Returns 0 = payload ready, 1 = idle/timeout, -1 = fatal. */
static int poll_and_read(gateway_state *s, char *buf, size_t buf_cap) {
    struct pollfd pfd = {.fd = s->conn.fd, .events = POLLIN};
    long long now = now_ms();
    int timeout = 1000;

    if (s->next_heartbeat > 0) {
        long long diff = s->next_heartbeat - now;
        timeout = diff < 0 ? 0 : (int)diff;
    }

    int rc = poll(&pfd, 1, timeout);
    if (rc < 0) {
        log_event ev = log_event_new("gateway.poll");
        ev.outcome = "error";
        ev.error_type = "PollError";
        ev.error_message = "poll failed";
        log_emit(LOG_LEVEL_ERROR, &ev);
        return -1;
    }

    now = now_ms();
    if (s->next_heartbeat > 0 && now >= s->next_heartbeat) {
        if (heartbeat_with_retry(s) != 0) {
            return -1;
        }
    }

    if (rc == 0 || !(pfd.revents & POLLIN)) {
        return 1;
    }

    size_t len = 0;
    if (ws_read_text(&s->conn, buf, buf_cap, &len) != 0) {
        log_event ev = log_event_new("gateway.read");
        ev.outcome = "error";
        ev.sequence = s->sequence;
        ev.error_type = "GatewayReadError";
        ev.error_message = "ws read failed";
        log_emit(LOG_LEVEL_ERROR, &ev);
        return -1;
    }
    return 0;
}

/* Handle one gateway JSON payload. Returns -1 if the connection should drop. */
static int handle_gateway_payload(gateway_state *s, const char *auth, const char *reply,
                                  const char *reaction, const char *buf) {
    long long op = -1;
    if (json_get_int(buf, "op", &op)) {
        if (op == OP_HEARTBEAT_ACK) {
            return 0; /* counted via successful heartbeat send */
        }
        if (op == OP_RECONNECT || op == OP_INVALID_SESSION) {
            log_event ev = log_event_new("gateway.reconnect");
            ev.outcome = "reconnect";
            ev.gateway_op = op;
            ev.sequence = s->sequence;
            ev.duration_ms = now_ms() - s->session_start_ms;
            ev.error_type = (op == OP_RECONNECT) ? "Reconnect" : "InvalidSession";
            log_emit(LOG_LEVEL_ERROR, &ev);
            return -1;
        }
    }

    long long seq = -1;
    if (json_get_int(buf, "s", &seq)) {
        s->sequence = seq;
    }

    char event[64];
    if (!json_get_string(buf, "t", event, sizeof(event))) {
        return 0;
    }
    if (strcmp(event, "MESSAGE_CREATE") == 0) {
        handle_message_create(s, auth, reply, reaction, buf);
    } else if (strcmp(event, "READY") == 0) {
        log_event ev = log_event_new("gateway.ready");
        ev.outcome = "success";
        ev.gateway_t = "READY";
        ev.sequence = s->sequence;
        ev.heartbeat_interval_ms = s->heartbeat_interval;
        ev.duration_ms = now_ms() - s->session_start_ms;
        log_emit(LOG_LEVEL_INFO, &ev);
    }
    return 0;
}

static void emit_session_end(gateway_state *s, const char *outcome, const char *error_type,
                             const char *error_message) {
    log_event ev = log_event_new("session.end");
    ev.outcome = outcome;
    ev.duration_ms = now_ms() - s->session_start_ms;
    ev.sequence = s->sequence;
    ev.heartbeat_interval_ms = s->heartbeat_interval;
    ev.heartbeats_ok = (int)s->heartbeats_ok;
    ev.heartbeats_fail = (int)s->heartbeats_fail;
    ev.error_type = error_type;
    ev.error_message = error_message;
    log_emit(outcome && strcmp(outcome, "error") == 0 ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO, &ev);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    log_init_from_env();

    const char *token = getenv("DISCORD_TOKEN");
    const char *reply = getenv("DISCORD_REPLY");
    const char *reaction = getenv("DISCORD_REACTION");

    if (!token || !*token) {
        log_event ev = log_event_new("session.start");
        ev.outcome = "error";
        ev.error_type = "ConfigError";
        ev.error_message = "DISCORD_TOKEN is required";
        log_emit(LOG_LEVEL_ERROR, &ev);
        return 1;
    }
    if (!reaction) {
        reaction = DEFAULT_REACTION;
    }

    char auth[512];
    char gateway_token[512];
    normalize_token(token, auth, sizeof(auth), gateway_token, sizeof(gateway_token));

    gateway_state state = {
        .heartbeat_interval = 45000,
        .sequence = -1,
        .next_heartbeat = 0,
        .session_start_ms = now_ms(),
    };

    if (tls_init_ca() != 0) {
        log_event ev = log_event_new("session.start");
        ev.outcome = "error";
        ev.error_type = "TlsCaError";
        ev.error_message = "CA certificate init failed";
        log_emit(LOG_LEVEL_ERROR, &ev);
        return 1;
    }

    long long t_connect = now_ms();
    if (tls_connect(&state.conn, GATEWAY_HOST, API_PORT) != 0) {
        log_event ev = log_event_new("session.start");
        ev.outcome = "error";
        ev.duration_ms = now_ms() - t_connect;
        ev.host = GATEWAY_HOST;
        ev.error_type = "TlsConnectError";
        ev.error_message = "gateway TLS connect failed";
        log_emit(LOG_LEVEL_ERROR, &ev);
        return 1;
    }

    if (ws_handshake(&state.conn, GATEWAY_HOST, GATEWAY_PATH) != 0) {
        log_event ev = log_event_new("session.start");
        ev.outcome = "error";
        ev.duration_ms = now_ms() - t_connect;
        ev.host = GATEWAY_HOST;
        ev.error_type = "WsHandshakeError";
        ev.error_message = "websocket handshake failed";
        log_emit(LOG_LEVEL_ERROR, &ev);
        tls_close(&state.conn);
        return 1;
    }

    char buf[BUF_SIZE];
    if (wait_for_hello(&state, gateway_token, buf, sizeof(buf)) != 0) {
        emit_session_end(&state, "error", "HelloError", "did not receive HELLO/identify");
        tls_close(&state.conn);
        return 1;
    }

    {
        log_event ev = log_event_new("session.start");
        ev.outcome = "success";
        ev.duration_ms = now_ms() - state.session_start_ms;
        ev.host = GATEWAY_HOST;
        ev.heartbeat_interval_ms = state.heartbeat_interval;
        log_emit(LOG_LEVEL_INFO, &ev);
    }

    while (1) {
        int rc = poll_and_read(&state, buf, sizeof(buf));
        if (rc < 0) {
            emit_session_end(&state, "error", "GatewayLoopError", "poll/read/heartbeat failed");
            break;
        }
        if (rc > 0) {
            continue; /* idle / heartbeat-only tick */
        }
        if (handle_gateway_payload(&state, auth, reply, reaction, buf) != 0) {
            /* reconnect event already emitted */
            emit_session_end(&state, "reconnect", NULL, NULL);
            break;
        }
    }

    tls_close(&state.conn);
    return 1;
}
