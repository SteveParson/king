# Code Flow (discord2)

This document explains how the program starts, connects to Discord, processes messages, and sends replies/reactions. It is intended for new contributors.

## High-level lifecycle

1) `main()` reads environment variables and builds a `discord_config`.
2) `discord_run()` normalizes the token and opens a TLS connection to the Discord Gateway.
3) The WebSocket handshake completes, then the client waits for `HELLO`.
4) On `HELLO`, the bot sends `IDENTIFY` and starts the heartbeat loop.
5) The main loop polls for incoming frames, sends heartbeats, and processes events.
6) On `MESSAGE_CREATE`, content is checked for the word `king`.
7) If matched, it sends a REST message and adds a reaction.

## File-by-file flow

### `src/main.c`
- Initializes logging from `LOG_LEVEL`.
- Reads:
  - `DISCORD_TOKEN` (required)
  - `DISCORD_REPLY` (optional; empty string disables replies)
  - `DISCORD_REACTION` (optional; empty string disables reactions)
- Builds a `discord_config` and calls `discord_run()`.

### `src/discord.c`
This is the primary orchestration file.

#### Key structs
- `discord_context`: normalized token values plus reply/reaction strings.
- `gateway_state`: holds connection, heartbeat interval, sequence, and next heartbeat time.
- `message_request` / `reaction_request`: group REST parameters to avoid swapped arguments.

#### Token normalization
`build_tokens()` does two things:
- Gateway token: raw token without the `"Bot "` prefix.
- REST token: always with the `"Bot "` prefix.

#### Gateway connect + identify
1) `tls_connect()` creates a TLS connection to `gateway.discord.gg:443`.
2) `ws_handshake()` performs the WebSocket upgrade.
3) `wait_for_hello()` blocks until an op=10 `HELLO` arrives.
4) `handle_hello()` reads the heartbeat interval and sends `IDENTIFY`.

#### Main loop
The loop is split into small helpers:
- `poll_and_read()`:
  - polls for inbound data
  - sends heartbeats when due
  - reads one WebSocket frame
- `handle_gateway_op()`:
  - handles op=11 (heartbeat ack)
  - handles op=7/op=9 (reconnect / invalid session)
- `update_sequence()`:
  - saves the `s` field for heartbeat payloads
- `handle_dispatch()`:
  - reads the `t` event name
  - dispatches to `handle_message_create()` or `READY`

#### MESSAGE_CREATE path
1) `handle_message_create()` ignores bot authors.
2) Reads `d.content`, `d.channel_id`, `d.id`.
3) `contains_case()` checks for substring `king` (case-insensitive).
4) If matched:
   - `send_message()` sends a REST message
   - `send_reaction()` adds a reaction

#### REST helpers
- `https_api_request()` builds and sends a raw HTTPS request to `discord.com`.
- `send_message()` builds JSON and POSTs to `/channels/{id}/messages`.
- `send_reaction()` URL-encodes the emoji and PUTs to the reactions endpoint.

### `src/ws.c`
Minimal WebSocket client implementation.

- `ws_handshake()`:
  - builds a random `Sec-WebSocket-Key`
  - sends HTTP Upgrade
  - checks for `101` response

- `ws_send_text()`:
  - builds a client-to-server masked text frame
  - supports small and extended length payloads

- `ws_read_text()`:
  - reads frame header
  - reads extended payload length if needed
  - reads mask (server frames are unmasked; code supports masked too)
  - reads payload into buffer
  - validates opcode (only text frames supported)

### `src/net.c`
Minimal TLS + HTTPS helper functions.

- `tcp_connect()` resolves DNS and opens a TCP socket.
- `tls_connect()` wraps the TCP socket in OpenSSL TLS.
- `https_request()` writes a raw HTTP request and reads the full response.

### `src/json.c`
Tiny JSON extraction helpers, intentionally minimal.

- `json_find_key()` locates a key at top-level and returns its value pointer.
- `json_get_string()` / `json_get_int()` parse direct values.
- `json_get_string_in_object()` / `json_get_int_in_object()` locate values inside a named object.

This is not a general-purpose JSON parser. It is designed to be just enough for the fields the bot needs.

### `src/log.c`
- Runtime log levels: error, info, debug.
- Optional compile-time stripping via `LOG_LEVEL_COMPILE`.

## Data flow diagram (text)

```
main()
  -> discord_run(config)
      -> build_tokens()
      -> tls_connect(gateway)
      -> ws_handshake()
      -> wait_for_hello()
          -> handle_hello()
              -> send_identify()
      -> loop:
          -> poll_and_read()
              -> send_heartbeat() when due
              -> ws_read_text()
          -> handle_gateway_op()
          -> update_sequence()
          -> handle_dispatch()
              -> MESSAGE_CREATE -> handle_message_create()
                  -> send_message()
                  -> send_reaction()
```

## Practical dev tips

- If the bot doesn’t respond to messages, confirm MESSAGE CONTENT INTENT is enabled.
- If you’re not receiving events, check `LOG_LEVEL=debug` for heartbeat ACKs.
- If REST calls fail, log the HTTP status codes and verify your bot token.

