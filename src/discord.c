/* Discord Gateway + REST orchestration and message handling. */
#include "discord.h"
#include "json.h"
#include "log.h"
#include "net.h"
#include "ws.h"

#include <ctype.h>
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

/* Runtime context built from config and token normalization. */
typedef struct {
    const char* auth_token;
    const char* gateway_token;
    const char* reply;
    const char* reaction;
} discord_context;

/* Gateway connection state (heartbeat, sequence tracking). */
typedef struct {
    tls_conn conn;
    long long heartbeat_interval;
    long long sequence;
    long long next_heartbeat;
} gateway_state;

/* Parameters for REST message creation. */
typedef struct {
    const char* auth_token;
    const char* channel_id;
    const char* content;
} message_request;

/* Parameters for REST reaction creation. */
typedef struct {
    const char* auth_token;
    const char* channel_id;
    const char* message_id;
    const char* emoji;
} reaction_request;

typedef struct {
    const char* payload;
    const char* event;
} dispatch_event;

/* Monotonic milliseconds for heartbeat timing. */
static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((long long)ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
}

static unsigned char lower_ascii(unsigned char ch) {
    return (unsigned char)tolower(ch);
}

static int match_at_case(const char* hay, size_t start_idx, const char* needle, size_t needle_len) {
    for (size_t offset = 0; offset < needle_len; offset++) {
        unsigned char hc = (unsigned char)hay[start_idx + offset];
        if (lower_ascii(hc) != lower_ascii((unsigned char)needle[offset])) {
            return 0;
        }
    }
    return 1;
}

/* Case-insensitive whole-word check for the trigger word. */
int contains_case(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle) {
        return 0;
    }
    size_t hay_len = strlen(hay);
    size_t needle_len = strlen(needle);
    if (needle_len > hay_len) {
        return 0;
    }
    for (size_t idx = 0; idx + needle_len <= hay_len; idx++) {
        if (match_at_case(hay, idx, needle, needle_len)) {
            int before_ok = (idx == 0) || !isalnum((unsigned char)hay[idx - 1]);
            int after_ok =
                (idx + needle_len == hay_len) || !isalnum((unsigned char)hay[idx + needle_len]);
            if (before_ok && after_ok) {
                return 1;
            }
        }
    }
    return 0;
}

/* Escape a string for JSON payloads (ASCII control handling only). */
void json_escape(const char* input, char* out, size_t out_cap) {
    size_t out_len = 0;
    for (size_t idx = 0; input[idx] && out_len + 2 < out_cap; idx++) {
        unsigned char ch = (unsigned char)input[idx];
        if (ch == '"' || ch == '\\') {
            if (out_len + 2 >= out_cap) {
                break;
            }
            out[out_len++] = '\\';
            out[out_len++] = (char)ch;
        } else if (ch == '\n') {
            out[out_len++] = '\\';
            out[out_len++] = 'n';
        } else if (ch == '\r') {
            out[out_len++] = '\\';
            out[out_len++] = 'r';
        } else if (ch == '\t') {
            out[out_len++] = '\\';
            out[out_len++] = 't';
        } else if (ch < 32) {
            if (out_len + 6 >= out_cap) {
                break;
            }
            snprintf(out + out_len, 7, "\\u%04x", ch);
            out_len += 6;
        } else {
            out[out_len++] = (char)ch;
        }
    }
    out[out_len] = '\0';
}

/* Percent-encode a string for the reactions endpoint. */
void url_encode(const char* input, char* out, size_t out_cap) {
    static const char* hex = "0123456789ABCDEF";
    size_t out_len = 0;
    for (size_t idx = 0; input[idx] && out_len + 4 < out_cap; idx++) {
        unsigned char ch = (unsigned char)input[idx];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out[out_len++] = (char)ch;
        } else {
            if (out_len + 3 >= out_cap) {
                break;
            }
            out[out_len++] = '%';
            out[out_len++] = hex[(ch >> 4) & 0xF];
            out[out_len++] = hex[ch & 0xF];
        }
    }
    out[out_len] = '\0';
}

/* Thin HTTPS wrapper used by REST helpers. */
static int https_api_request(const char* auth_token, const char* method, const char* path,
                             const char* body, char* resp, size_t resp_cap) {
    static const net_endpoint api_endpoint = {API_HOST, API_PORT};
    char req[8192];
    size_t body_len = body ? strlen(body) : 0;
    int n = snprintf(req, sizeof(req),
                     "%s %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: discord2 (https://example.invalid, 0.1)\r\n"
                     "Authorization: %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n\r\n"
                     "%s",
                     method, path, API_HOST, auth_token, body_len, body ? body : "");
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        return -1;
    }

    size_t resp_len = 0;
    if (https_request(&api_endpoint, req, resp, resp_cap, &resp_len) != 0) {
        return -1;
    }
    return 0;
}

int http_status_code(const char* resp) {
    if (!resp) {
        return -1;
    }
    const char* ptr = strstr(resp, "HTTP/");
    if (!ptr) {
        return -1;
    }
    ptr = strchr(ptr, ' ');
    if (!ptr) {
        return -1;
    }
    ptr++;
    if (!isdigit((unsigned char)*ptr)) {
        return -1;
    }
    int code = 0;
    while (isdigit((unsigned char)*ptr)) {
        code = (code * 10) + (*ptr - '0');
        ptr++;
    }
    return code;
}

/* POST a message to the given channel. */
static int send_message(const message_request* req) {
    char esc[2048];
    char body[4096];
    char resp[4096];

    json_escape(req->content, esc, sizeof(esc));
    snprintf(body, sizeof(body), "{\"content\":\"%s\"}", esc);

    char path[256];
    snprintf(path, sizeof(path), "/api/v10/channels/%s/messages", req->channel_id);

    if (https_api_request(req->auth_token, "POST", path, body, resp, sizeof(resp)) != 0) {
        log_error("Failed to send message");
        return -1;
    }
    log_info("Message send status %d", http_status_code(resp));
    return 0;
}

/* PUT a reaction on an existing message. */
static int send_reaction(const reaction_request* req) {
    if (!req->emoji || req->emoji[0] == '\0') {
        return 0;
    }
    char emoji_enc[256];
    char path[512];
    char resp[4096];

    url_encode(req->emoji, emoji_enc, sizeof(emoji_enc));
    snprintf(path, sizeof(path), "/api/v10/channels/%s/messages/%s/reactions/%s/@me",
             req->channel_id, req->message_id, emoji_enc);

    if (https_api_request(req->auth_token, "PUT", path, "", resp, sizeof(resp)) != 0) {
        log_error("Failed to add reaction");
        return -1;
    }
    log_info("Reaction add status %d", http_status_code(resp));
    return 0;
}

/* Identify on the gateway with intents and client properties. */
static int send_identify(br_sslio_context* ioc, const char* gateway_token) {
    char payload[1024];
    const int intents = 33280; /* GUILD_MESSAGES + MESSAGE_CONTENT */
    int n = snprintf(payload, sizeof(payload),
                     "{\"op\":2,\"d\":{"
                     "\"token\":\"%s\","
                     "\"intents\":%d,"
                     "\"properties\":{"
#ifdef __APPLE__
                     "\"os\":\"darwin\","
#else
                     "\"os\":\"linux\","
#endif
                     "\"browser\":\"discord2\","
                     "\"device\":\"discord2\""
                     "}}}",
                     gateway_token, intents);
    if (n <= 0 || (size_t)n >= sizeof(payload)) {
        return -1;
    }
    return ws_send_text(ioc, payload, (size_t)n);
}

/* Heartbeat payload (op=1) with last sequence. */
static int send_heartbeat(br_sslio_context* ioc, long long seq) {
    char payload[64];
    int n;
    if (seq >= 0) {
        n = snprintf(payload, sizeof(payload), "{\"op\":1,\"d\":%lld}", seq);
    } else {
        n = snprintf(payload, sizeof(payload), "{\"op\":1,\"d\":null}");
    }
    if (n <= 0 || (size_t)n >= sizeof(payload)) {
        return -1;
    }
    return ws_send_text(ioc, payload, (size_t)n);
}

/* Normalize the token for REST vs Gateway usage. */
static int build_tokens(const discord_config* config, discord_context* ctx, char* auth_token,
                        size_t auth_cap) {
    if (!config || !config->token) {
        return -1;
    }

    if (strncmp(config->token, "Bot ", 4) == 0) {
        ctx->gateway_token = config->token + 4;
        snprintf(auth_token, auth_cap, "%s", config->token);
    } else {
        ctx->gateway_token = config->token;
        snprintf(auth_token, auth_cap, "Bot %s", config->token);
    }
    ctx->auth_token = auth_token;
    ctx->reply = config->reply;
    ctx->reaction = config->reaction;
    return 0;
}

/* Process HELLO (op=10): read interval and send IDENTIFY. */
static int handle_hello(gateway_state* state, const discord_context* ctx, const char* payload) {
    long long interval = 0;
    json_object_key hb_key = {"d", "heartbeat_interval"};
    if (json_get_int_in_object(payload, &hb_key, &interval)) {
        state->heartbeat_interval = interval;
    }
    if (send_identify(&state->conn.ioc, ctx->gateway_token) != 0) {
        log_error("Identify failed");
        return -1;
    }
    log_info("Identify sent");
    state->next_heartbeat = now_ms() + state->heartbeat_interval;
    return 0;
}

/* Handle MESSAGE_CREATE by matching content and issuing REST calls. */
static int handle_message_create(const discord_context* ctx, const char* payload) {
    if (strstr(payload, "\"bot\":true") != NULL) {
        return 0;
    }

    char content[2048];
    char channel_id[64];
    char message_id[64];

    json_object_key content_key = {"d", "content"};
    json_object_key channel_key = {"d", "channel_id"};
    json_object_key message_key = {"d", "id"};

    if (!json_get_string_in_object(payload, &content_key, content, sizeof(content))) {
        return 0;
    }
    if (!json_get_string_in_object(payload, &channel_key, channel_id, sizeof(channel_id))) {
        return 0;
    }
    if (!json_get_string_in_object(payload, &message_key, message_id, sizeof(message_id))) {
        return 0;
    }

    if (contains_case(content, "king")) {
        log_info("Matched 'king' in channel %s", channel_id);
        message_request msg_req = {ctx->auth_token, channel_id, ctx->reply};
        reaction_request react_req = {ctx->auth_token, channel_id, message_id, ctx->reaction};
        send_message(&msg_req);
        send_reaction(&react_req);
    } else {
        log_debug("Message ignored");
    }
    return 0;
}

/* Handle dispatch events by name (READY, MESSAGE_CREATE, ...). */
static int handle_dispatch_event(const discord_context* ctx, const dispatch_event* evt) {
    if (strcmp(evt->event, "MESSAGE_CREATE") == 0) {
        return handle_message_create(ctx, evt->payload);
    }
    if (strcmp(evt->event, "READY") == 0) {
        log_info("READY event received");
    }
    return 0;
}

/* Handle non-dispatch gateway opcodes (ACK/RECONNECT). */
static int handle_gateway_op(gateway_state* state, const char* payload) {
    long long op = -1;
    if (!json_get_int(payload, "op", &op)) {
        return 0;
    }
    if (op == 11) {
        log_debug("Heartbeat ACK");
        return 0;
    }
    if (op == 7 || op == 9) {
        log_error("Gateway requested reconnect");
        return -1;
    }
    return 0;
}

/* Update last seen sequence for heartbeats. */
static void update_sequence(gateway_state* state, const char* payload) {
    long long seq_in = -1;
    if (json_get_int(payload, "s", &seq_in)) {
        state->sequence = seq_in;
    }
}

static int handle_dispatch(gateway_state* state, const discord_context* ctx, const char* payload) {
    char event[64];
    if (!json_get_string(payload, "t", event, sizeof(event))) {
        return 0;
    }
    dispatch_event evt = {payload, event};
    return handle_dispatch_event(ctx, &evt);
}

/* Wait for HELLO before entering the main loop. */
static int wait_for_hello(gateway_state* state, const discord_context* ctx, char* buf,
                          size_t buf_cap) {
    size_t len = 0;
    while (ws_read_text(&state->conn.ioc, buf, buf_cap, &len) == 0) {
        long long op = -1;
        if (!json_get_int(buf, "op", &op)) {
            continue;
        }
        if (op == 10) {
            return handle_hello(state, ctx, buf);
        }
    }
    return -1;
}

/* Poll socket, send heartbeats, and read the next gateway frame. */
static int poll_and_read(gateway_state* state, char* buf, size_t buf_cap) {
    struct pollfd pfd;
    pfd.fd = state->conn.fd;
    pfd.events = POLLIN;

    long long now = now_ms();
    int timeout = 1000;
    if (state->next_heartbeat > 0) {
        long long diff = state->next_heartbeat - now;
        if (diff < 0) {
            diff = 0;
        }
        timeout = (int)diff;
    }

    int rc = poll(&pfd, 1, timeout);
    if (rc < 0) {
        log_error("poll failed");
        return -1;
    }

    now = now_ms();
    if (state->next_heartbeat > 0 && now >= state->next_heartbeat) {
        int hb_ok = 0;
        int delay = 100000;
        for (int retries = 0; retries < 3 && !hb_ok; retries++) {
            if (send_heartbeat(&state->conn.ioc, state->sequence) == 0) {
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
        state->next_heartbeat = now + state->heartbeat_interval;
    }

    if (rc == 0 || !(pfd.revents & POLLIN)) {
        return 1;
    }

    size_t len = 0;
    if (ws_read_text(&state->conn.ioc, buf, buf_cap, &len) != 0) {
        log_error("Gateway read failed");
        return -1;
    }
    return 0;
}

/* Main gateway loop: connect, identify, read events, and react. */
int discord_run(const discord_config* config) {
    char auth_token[512];
    discord_context ctx;
    if (build_tokens(config, &ctx, auth_token, sizeof(auth_token)) != 0) {
        return 1;
    }

    gateway_state state;
    state.heartbeat_interval = 45000;
    state.sequence = -1;
    state.next_heartbeat = 0;

    net_endpoint gateway_endpoint = {GATEWAY_HOST, API_PORT};
    state.conn = tls_connect(&gateway_endpoint);
    if (state.conn.fd < 0) {
        log_error("Gateway TLS connect failed");
        return 1;
    }
    log_info("Connected to gateway TLS");

    if (ws_handshake(&state.conn.ioc, GATEWAY_HOST, GATEWAY_PATH) != 0) {
        log_error("WebSocket handshake failed");
        tls_close(&state.conn);
        return 1;
    }
    log_info("WebSocket handshake complete");

    char buf[BUF_SIZE];
    if (wait_for_hello(&state, &ctx, buf, sizeof(buf)) != 0) {
        tls_close(&state.conn);
        return 1;
    }

    while (1) {
        int poll_rc = poll_and_read(&state, buf, sizeof(buf));
        if (poll_rc < 0) {
            break;
        }
        if (poll_rc > 0) {
            continue;
        }

        if (handle_gateway_op(&state, buf) != 0) {
            break;
        }
        update_sequence(&state, buf);
        handle_dispatch(&state, &ctx, buf);
    }

    tls_close(&state.conn);
    return 1;
}
