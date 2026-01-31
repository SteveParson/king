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

#define GATEWAY_HOST "gateway.discord.gg"
#define API_HOST "discord.com"
#define API_PORT "443"
#define GATEWAY_PATH "/?v=10&encoding=json"
#define BUF_SIZE 65536

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int contains_case(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return 1;
    }
    return 0;
}

static void json_escape(const char *in, char *out, size_t out_cap) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < out_cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            if (o + 2 >= out_cap) break;
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c == '\n') {
            out[o++] = '\\';
            out[o++] = 'n';
        } else if (c == '\r') {
            out[o++] = '\\';
            out[o++] = 'r';
        } else if (c == '\t') {
            out[o++] = '\\';
            out[o++] = 't';
        } else if (c < 32) {
            if (o + 6 >= out_cap) break;
            snprintf(out + o, 7, "\\u%04x", c);
            o += 6;
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static void url_encode(const char *in, char *out, size_t out_cap) {
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 4 < out_cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            if (o + 3 >= out_cap) break;
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0xF];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

static int https_api_request(const char *auth_token, const char *method,
                             const char *path, const char *body,
                             char *resp, size_t resp_cap) {
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
                     method, path, API_HOST, auth_token, body_len,
                     body ? body : "");
    if (n <= 0 || (size_t)n >= sizeof(req)) return -1;

    size_t resp_len = 0;
    if (https_request(API_HOST, API_PORT, req, resp, resp_cap, &resp_len) != 0) {
        return -1;
    }
    return 0;
}

static int http_status_code(const char *resp) {
    if (!resp) return -1;
    const char *p = strstr(resp, "HTTP/");
    if (!p) return -1;
    p = strchr(p, ' ');
    if (!p) return -1;
    p++;
    if (!isdigit((unsigned char)*p)) return -1;
    int code = 0;
    while (isdigit((unsigned char)*p)) {
        code = code * 10 + (*p - '0');
        p++;
    }
    return code;
}

static int send_message(const char *auth_token, const char *channel_id, const char *content) {
    char esc[2048];
    char body[4096];
    char resp[4096];

    json_escape(content, esc, sizeof(esc));
    snprintf(body, sizeof(body), "{\"content\":\"%s\"}", esc);

    char path[256];
    snprintf(path, sizeof(path), "/api/v10/channels/%s/messages", channel_id);

    if (https_api_request(auth_token, "POST", path, body, resp, sizeof(resp)) != 0) {
        log_error("Failed to send message");
        return -1;
    }
    log_info("Message send status %d", http_status_code(resp));
    return 0;
}

static int send_reaction(const char *auth_token, const char *channel_id,
                         const char *message_id, const char *emoji) {
    if (!emoji || emoji[0] == '\0') return 0;
    char emoji_enc[256];
    char path[512];
    char resp[4096];

    url_encode(emoji, emoji_enc, sizeof(emoji_enc));
    snprintf(path, sizeof(path),
             "/api/v10/channels/%s/messages/%s/reactions/%s/@me",
             channel_id, message_id, emoji_enc);

    if (https_api_request(auth_token, "PUT", path, "", resp, sizeof(resp)) != 0) {
        log_error("Failed to add reaction");
        return -1;
    }
    log_info("Reaction add status %d", http_status_code(resp));
    return 0;
}

static int send_identify(SSL *ssl, const char *gateway_token) {
    char payload[1024];
    const int intents = 33280; /* GUILD_MESSAGES + MESSAGE_CONTENT */
    int n = snprintf(payload, sizeof(payload),
                     "{\"op\":2,\"d\":{"
                     "\"token\":\"%s\","
                     "\"intents\":%d,"
                     "\"properties\":{"
                     "\"os\":\"linux\","
                     "\"browser\":\"discord2\","
                     "\"device\":\"discord2\""
                     "}}}",
                     gateway_token, intents);
    if (n <= 0 || (size_t)n >= sizeof(payload)) return -1;
    return ws_send_text(ssl, payload, (size_t)n);
}

static int send_heartbeat(SSL *ssl, long long seq) {
    char payload[64];
    int n;
    if (seq >= 0) {
        n = snprintf(payload, sizeof(payload), "{\"op\":1,\"d\":%lld}", seq);
    } else {
        n = snprintf(payload, sizeof(payload), "{\"op\":1,\"d\":null}");
    }
    if (n <= 0 || (size_t)n >= sizeof(payload)) return -1;
    return ws_send_text(ssl, payload, (size_t)n);
}

int discord_run(const char *token, const char *reply, const char *reaction) {
    char auth_token[512];
    const char *gateway_token = token;

    if (strncmp(token, "Bot ", 4) == 0) {
        gateway_token = token + 4;
        snprintf(auth_token, sizeof(auth_token), "%s", token);
    } else {
        snprintf(auth_token, sizeof(auth_token), "Bot %s", token);
    }

    tls_conn gw = tls_connect(GATEWAY_HOST, API_PORT);
    if (!gw.ssl) {
        log_error("Gateway TLS connect failed");
        return 1;
    }

    log_info("Connected to gateway TLS");

    if (ws_handshake(gw.ssl, GATEWAY_HOST, GATEWAY_PATH) != 0) {
        log_error("WebSocket handshake failed");
        tls_close(&gw);
        return 1;
    }

    log_info("WebSocket handshake complete");

    char buf[BUF_SIZE];
    size_t len = 0;
    long long hb_interval = 45000;
    long long seq = -1;
    long long next_hb = 0;

    while (ws_read_text(gw.ssl, buf, sizeof(buf), &len) == 0) {
        long long op = -1;
        if (!json_get_int(buf, "op", &op)) continue;
        if (op == 10) {
            long long interval = 0;
            if (json_get_int_in_object(buf, "d", "heartbeat_interval", &interval)) {
                hb_interval = interval;
            }
            if (send_identify(gw.ssl, gateway_token) != 0) {
                log_error("Identify failed");
                tls_close(&gw);
                return 1;
            }
            log_info("Identify sent");
            next_hb = now_ms() + hb_interval;
            break;
        }
    }

    struct pollfd pfd;
    pfd.fd = gw.fd;
    pfd.events = POLLIN;

    while (1) {
        long long now = now_ms();
        int timeout = 1000;
        if (next_hb > 0) {
            long long diff = next_hb - now;
            if (diff < 0) diff = 0;
            timeout = (int)diff;
        }

        int rc = poll(&pfd, 1, timeout);
        if (rc < 0) {
            log_error("poll failed");
            break;
        }

        now = now_ms();
        if (next_hb > 0 && now >= next_hb) {
            if (send_heartbeat(gw.ssl, seq) != 0) {
                log_error("Heartbeat failed");
                break;
            }
            log_debug("Heartbeat sent");
            next_hb = now + hb_interval;
        }

        if (rc == 0) continue;
        if (!(pfd.revents & POLLIN)) continue;

        if (ws_read_text(gw.ssl, buf, sizeof(buf), &len) != 0) {
            log_error("Gateway read failed");
            break;
        }

        long long op = -1;
        if (json_get_int(buf, "op", &op)) {
            if (op == 11) {
                log_debug("Heartbeat ACK");
                continue;
            }
            if (op == 7 || op == 9) {
                log_error("Gateway requested reconnect");
                break;
            }
        }

        long long seq_in = -1;
        if (json_get_int(buf, "s", &seq_in)) seq = seq_in;

        char event[64];
        if (!json_get_string(buf, "t", event, sizeof(event))) continue;

        if (strcmp(event, "MESSAGE_CREATE") == 0) {
            if (strstr(buf, "\"bot\":true") != NULL) continue;

            char content[2048];
            char channel_id[64];
            char message_id[64];

            if (!json_get_string_in_object(buf, "d", "content", content, sizeof(content))) continue;
            if (!json_get_string_in_object(buf, "d", "channel_id", channel_id, sizeof(channel_id))) continue;
            if (!json_get_string_in_object(buf, "d", "id", message_id, sizeof(message_id))) continue;

            if (contains_case(content, "king")) {
                log_info("Matched 'king' in channel %s", channel_id);
                send_message(auth_token, channel_id, reply);
                send_reaction(auth_token, channel_id, message_id, reaction);
            } else {
                log_debug("Message ignored");
            }
        } else if (strcmp(event, "READY") == 0) {
            log_info("READY event received");
        }
    }

    tls_close(&gw);
    return 1;
}
