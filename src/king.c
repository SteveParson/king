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

static void send_message(const char *auth, const char *channel_id, const char *content) {
    char esc[2048];
    char body[4096];
    char path[256];
    char resp[4096];

    json_escape(content, esc, sizeof(esc));
    snprintf(body, sizeof(body), "{\"content\":\"%s\"}", esc);
    snprintf(path, sizeof(path), "/api/v10/channels/%s/messages", channel_id);

    if (api_request(auth, "POST", path, body, resp, sizeof(resp)) != 0) {
        log_error("Failed to send message");
        return;
    }

    int status = http_status_code(resp);
    log_info("Message send status %d", status);
    if (status >= 400) {
        log_error("Message send failed: %.*s", 200, resp);
    }
}

static void send_reaction(const char *auth, const char *channel_id, const char *message_id,
                          const char *emoji) {
    if (!emoji || !*emoji) {
        return;
    }

    char enc[256];
    char path[512];
    char resp[4096];

    url_encode(emoji, enc, sizeof(enc));
    snprintf(path, sizeof(path), "/api/v10/channels/%s/messages/%s/reactions/%s/@me", channel_id,
             message_id, enc);

    if (api_request(auth, "PUT", path, "", resp, sizeof(resp)) != 0) {
        log_error("Failed to add reaction");
        return;
    }
    log_info("Reaction add status %d", http_status_code(resp));
}

/* ── Gateway operations ───────────────────────────────────────── */

typedef struct {
    tls_conn conn;
    long long heartbeat_interval;
    long long sequence;
    long long next_heartbeat;
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
            log_debug("Heartbeat sent");
            s->next_heartbeat = now_ms() + s->heartbeat_interval;
            return 0;
        }
        if (attempt + 1 < HB_MAX_RETRIES) {
            log_error("Heartbeat failed, retrying...");
            usleep(delay);
            delay *= 2;
        }
    }
    log_error("Heartbeat failed");
    return -1;
}

static int handle_hello(gateway_state *s, const char *token, const char *payload) {
    long long interval = 0;
    json_object_key hb_key = {"d", "heartbeat_interval"};

    if (json_get_int_in_object(payload, &hb_key, &interval)) {
        s->heartbeat_interval = interval;
    }
    if (send_identify(&s->conn, token) != 0) {
        log_error("Identify failed");
        return -1;
    }
    log_info("Identify sent");
    s->next_heartbeat = now_ms() + s->heartbeat_interval;
    return 0;
}

static void handle_message_create(const char *auth, const char *reply, const char *reaction,
                                  const char *payload) {
    if (strstr(payload, "\"bot\":true")) {
        return;
    }

    char content[2048];
    char channel_id[64];
    char message_id[64];
    json_object_key content_key = {"d", "content"};
    json_object_key channel_key = {"d", "channel_id"};
    json_object_key id_key = {"d", "id"};

    if (!json_get_string_in_object(payload, &content_key, content, sizeof(content)) ||
        !json_get_string_in_object(payload, &channel_key, channel_id, sizeof(channel_id)) ||
        !json_get_string_in_object(payload, &id_key, message_id, sizeof(message_id))) {
        return;
    }

    if (!contains_case(content, "king")) {
        log_debug("Message ignored");
        return;
    }

    log_info("Matched 'king' in channel %s", channel_id);
    if (reply && *reply) {
        send_message(auth, channel_id, reply);
    }
    if (reaction && *reaction) {
        send_reaction(auth, channel_id, message_id, reaction);
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
        log_error("poll failed");
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
        log_error("Gateway read failed");
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
            log_debug("Heartbeat ACK");
            return 0;
        }
        if (op == OP_RECONNECT || op == OP_INVALID_SESSION) {
            log_error("Reconnect requested");
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
        handle_message_create(auth, reply, reaction, buf);
    } else if (strcmp(event, "READY") == 0) {
        log_info("READY event received");
    }
    return 0;
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    log_init_from_env();

    const char *token = getenv("DISCORD_TOKEN");
    const char *reply = getenv("DISCORD_REPLY");
    const char *reaction = getenv("DISCORD_REACTION");

    if (!token || !*token) {
        fprintf(stderr, "DISCORD_TOKEN is required\n");
        return 1;
    }
    if (!reaction) {
        reaction = DEFAULT_REACTION;
    }

    char auth[512];
    char gateway_token[512];
    normalize_token(token, auth, sizeof(auth), gateway_token, sizeof(gateway_token));

    if (tls_init_ca() != 0) {
        fprintf(stderr, "Failed to init CA certificates\n");
        return 1;
    }

    gateway_state state = {.heartbeat_interval = 45000, .sequence = -1, .next_heartbeat = 0};

    if (tls_connect(&state.conn, GATEWAY_HOST, API_PORT) != 0) {
        log_error("Gateway TLS connect failed");
        return 1;
    }
    log_info("Connected to gateway TLS");

    if (ws_handshake(&state.conn, GATEWAY_HOST, GATEWAY_PATH) != 0) {
        log_error("WebSocket handshake failed");
        tls_close(&state.conn);
        return 1;
    }
    log_info("WebSocket handshake complete");

    char buf[BUF_SIZE];
    if (wait_for_hello(&state, gateway_token, buf, sizeof(buf)) != 0) {
        tls_close(&state.conn);
        return 1;
    }

    while (1) {
        int rc = poll_and_read(&state, buf, sizeof(buf));
        if (rc < 0) {
            break;
        }
        if (rc > 0) {
            continue; /* idle / heartbeat-only tick */
        }
        if (handle_gateway_payload(&state, auth, reply, reaction, buf) != 0) {
            break;
        }
    }

    tls_close(&state.conn);
    return 1;
}
