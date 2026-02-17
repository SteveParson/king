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
#define API_HOST     "discord.com"
#define API_PORT     "443"
#define GATEWAY_PATH "/?v=10&encoding=json"
#define BUF_SIZE     65536

/* ── Monotonic clock ──────────────────────────────────────────── */

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* ── REST API helpers ─────────────────────────────────────────── */

static int api_request(const char *auth, const char *method, const char *path,
                       const char *body, char *resp, size_t resp_cap) {
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
    if (n <= 0 || (size_t)n >= sizeof(req)) return -1;
    return https_request(API_HOST, API_PORT, req, resp, resp_cap);
}

static void send_message(const char *auth, const char *channel_id,
                         const char *content) {
    char esc[2048], body[4096], path[256], resp[4096];
    json_escape(content, esc, sizeof(esc));
    snprintf(body, sizeof(body), "{\"content\":\"%s\"}", esc);
    snprintf(path, sizeof(path), "/api/v10/channels/%s/messages", channel_id);
    if (api_request(auth, "POST", path, body, resp, sizeof(resp)) != 0) {
        log_error("Failed to send message");
    } else {
        int status = http_status_code(resp);
        log_info("Message send status %d", status);
        if (status >= 400)
            log_error("Message send failed: %.*s", 200, resp);
    }
}

static void send_reaction(const char *auth, const char *channel_id,
                          const char *message_id, const char *emoji) {
    if (!emoji || !*emoji) return;
    char enc[256], path[512], resp[4096];
    url_encode(emoji, enc, sizeof(enc));
    snprintf(path, sizeof(path),
             "/api/v10/channels/%s/messages/%s/reactions/%s/@me",
             channel_id, message_id, enc);
    if (api_request(auth, "PUT", path, "", resp, sizeof(resp)) != 0)
        log_error("Failed to add reaction");
    else
        log_info("Reaction add status %d", http_status_code(resp));
}

/* ── Gateway operations ───────────────────────────────────────── */

typedef struct {
    tls_conn conn;
    long long heartbeat_interval;
    long long sequence;
    long long next_heartbeat;
} gateway_state;

static int send_identify(tls_conn *c, const char *token) {
    char payload[1024];
    int n = snprintf(payload, sizeof(payload),
        "{\"op\":2,\"d\":{"
        "\"token\":\"%s\","
        "\"intents\":33280,"  /* GUILD_MESSAGES + MESSAGE_CONTENT */
        "\"properties\":{"
        "\"os\":\"cosmo\","
        "\"browser\":\"king-cosmo\","
        "\"device\":\"king-cosmo\""
        "}}}", token);
    if (n <= 0 || (size_t)n >= sizeof(payload)) return -1;
    return ws_send_text(c, payload, (size_t)n);
}

static int send_heartbeat(tls_conn *c, long long seq) {
    char payload[64];
    int n;
    if (seq >= 0)
        n = snprintf(payload, sizeof(payload), "{\"op\":1,\"d\":%lld}", seq);
    else
        n = snprintf(payload, sizeof(payload), "{\"op\":1,\"d\":null}");
    if (n <= 0 || (size_t)n >= sizeof(payload)) return -1;
    return ws_send_text(c, payload, (size_t)n);
}

static int handle_hello(gateway_state *s, const char *token, const char *payload) {
    long long interval = 0;
    json_object_key hb_key = {"d", "heartbeat_interval"};
    if (json_get_int_in_object(payload, &hb_key, &interval))
        s->heartbeat_interval = interval;
    if (send_identify(&s->conn, token) != 0) {
        log_error("Identify failed");
        return -1;
    }
    log_info("Identify sent");
    s->next_heartbeat = now_ms() + s->heartbeat_interval;
    return 0;
}

static void handle_message_create(const char *auth, const char *reply,
                                  const char *reaction, const char *payload) {
    if (strstr(payload, "\"bot\":true")) return;

    char content[2048], channel_id[64], message_id[64];
    json_object_key ck = {"d", "content"};
    json_object_key chk = {"d", "channel_id"};
    json_object_key mk = {"d", "id"};

    if (!json_get_string_in_object(payload, &ck, content, sizeof(content))) return;
    if (!json_get_string_in_object(payload, &chk, channel_id, sizeof(channel_id))) return;
    if (!json_get_string_in_object(payload, &mk, message_id, sizeof(message_id))) return;

    if (contains_case(content, "king")) {
        log_info("Matched 'king' in channel %s", channel_id);
        if (reply && *reply) send_message(auth, channel_id, reply);
        if (reaction && *reaction) send_reaction(auth, channel_id, message_id, reaction);
    } else {
        log_debug("Message ignored");
    }
}

static int wait_for_hello(gateway_state *s, const char *token,
                          char *buf, size_t buf_cap) {
    size_t len = 0;
    while (ws_read_text(&s->conn, buf, buf_cap, &len) == 0) {
        long long op = -1;
        if (!json_get_int(buf, "op", &op)) continue;
        if (op == 10) return handle_hello(s, token, buf);
    }
    return -1;
}

static int poll_and_read(gateway_state *s, char *buf, size_t buf_cap) {
    struct pollfd pfd = {.fd = s->conn.fd, .events = POLLIN};

    long long now = now_ms();
    int timeout = 1000;
    if (s->next_heartbeat > 0) {
        long long diff = s->next_heartbeat - now;
        timeout = diff < 0 ? 0 : (int)diff;
    }

    int rc = poll(&pfd, 1, timeout);
    if (rc < 0) { log_error("poll failed"); return -1; }

    now = now_ms();
    if (s->next_heartbeat > 0 && now >= s->next_heartbeat) {
        int hb_ok = 0;
        int delay = 100000;
        for (int retries = 0; retries < 3 && !hb_ok; retries++) {
            if (send_heartbeat(&s->conn, s->sequence) == 0) {
                hb_ok = 1;
            } else if (retries < 2) {
                log_error("Heartbeat failed, retrying...");
                usleep(delay);
                delay *= 2;
            }
        }
        if (!hb_ok) {
            log_error("Heartbeat failed");
            return -1;
        }
        log_debug("Heartbeat sent");
        s->next_heartbeat = now + s->heartbeat_interval;
    }

    if (rc == 0 || !(pfd.revents & POLLIN)) return 1;

    size_t len = 0;
    if (ws_read_text(&s->conn, buf, buf_cap, &len) != 0) {
        log_error("Gateway read failed");
        return -1;
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
    if (!reaction) reaction = "\xf0\x9f\x91\x91"; /* crown emoji UTF-8 */

    /* Build auth header token (REST needs "Bot " prefix). */
    char auth[512], gateway_token[512];
    if (strncmp(token, "Bot ", 4) == 0) {
        snprintf(auth, sizeof(auth), "%s", token);
        snprintf(gateway_token, sizeof(gateway_token), "%s", token + 4);
    } else {
        snprintf(auth, sizeof(auth), "Bot %s", token);
        snprintf(gateway_token, sizeof(gateway_token), "%s", token);
    }

    if (tls_init_ca() != 0) {
        fprintf(stderr, "Failed to init CA certificates\n");
        return 1;
    }

    gateway_state state = {
        .heartbeat_interval = 45000,
        .sequence = -1,
        .next_heartbeat = 0
    };

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
        if (rc < 0) break;
        if (rc > 0) continue;

        /* Handle gateway opcodes */
        long long op = -1;
        if (json_get_int(buf, "op", &op)) {
            if (op == 11) { log_debug("Heartbeat ACK"); continue; }
            if (op == 7 || op == 9) { log_error("Reconnect requested"); break; }
        }

        /* Update sequence */
        long long seq = -1;
        if (json_get_int(buf, "s", &seq)) state.sequence = seq;

        /* Dispatch events */
        char event[64];
        if (json_get_string(buf, "t", event, sizeof(event))) {
            if (strcmp(event, "MESSAGE_CREATE") == 0)
                handle_message_create(auth, reply, reaction, buf);
            else if (strcmp(event, "READY") == 0)
                log_info("READY event received");
        }
    }

    tls_close(&state.conn);
    return 1;
}
