# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
./autogen.sh   # generate configure (first time or after configure.ac changes)
./configure
make
```

Size-optimized builds:
```bash
make small    # -Os + dead code stripping
make tiny     # smallest: also strips unwind tables and compile-time removes info/debug logs (LOG_LEVEL_COMPILE=0)
```

The binary is placed at `src/discord2`.

## Test

```bash
make check    # builds and runs tests/test_json and tests/test_discord
```

Tests cover the pure string utilities (`contains_case`, `json_escape`, `url_encode`, `http_status_code`) and the JSON parser. No network access required.

## Lint and Format

```bash
make lint     # runs clang-tidy (must be installed)
make format   # runs clang-format in-place on src/*.c and include/*.h
```

Style: LLVM-based, 4-space indent, 100-column limit, left pointer alignment (see `.clang-format`). Tidy checks: clang-analyzer, bugprone, performance, portability, readability (magic numbers and identifier-length disabled).

## Run

```bash
export DISCORD_TOKEN='<raw_bot_token>'   # required; no "Bot " prefix
export DISCORD_REPLY='All hail'          # optional; empty string disables replies
export DISCORD_REACTION='👑'            # optional; empty string disables reactions
export LOG_LEVEL=info                    # error | info (default) | debug
./src/discord2
```

The bot requires **MESSAGE CONTENT INTENT** enabled in the Discord Developer Portal.

## Architecture

There are no external dependencies beyond OpenSSL. Everything (WebSocket, HTTP/S, JSON parsing) is implemented from scratch in ~6 source files.

### Module responsibilities

| File | Role |
|------|------|
| `src/main.c` | Entry point: reads env vars, builds `discord_config`, calls `discord_run()` |
| `src/discord.c` | Orchestration: Gateway lifecycle, heartbeat loop, event dispatch, REST calls |
| `src/ws.c` | Minimal WebSocket client (masked client frames, text frames only) |
| `src/net.c` | TCP + OpenSSL TLS connect, raw HTTPS request/response |
| `src/json.c` | Targeted key extraction — not a general-purpose parser |
| `src/log.c` | Runtime log levels; compile-time stripping via `LOG_LEVEL_COMPILE` |

### Key structs (discord.c)

- `discord_context` — normalized token strings (Gateway: raw, REST: with `"Bot "` prefix) plus reply/reaction strings.
- `gateway_state` — TLS connection, heartbeat interval, sequence number (`s`), and next heartbeat timestamp.
- `message_request` / `reaction_request` — group REST parameters to prevent argument swaps.

### Gateway lifecycle (discord.c)

```
discord_run()
  -> build_tokens()
  -> tls_connect(gateway.discord.gg:443)
  -> ws_handshake()
  -> wait_for_hello()  [blocks for op=10]
      -> handle_hello()  [saves interval, sends IDENTIFY]
  -> loop:
      -> poll_and_read()  [heartbeat when due, then ws_read_text()]
      -> handle_gateway_op()  [op=11 ack; op=7/9 reconnect/invalid]
      -> update_sequence()
      -> handle_dispatch()
          -> MESSAGE_CREATE -> handle_message_create()
              -> contains_case() checks for "king"
              -> send_message()   [POST /channels/{id}/messages]
              -> send_reaction()  [PUT reactions endpoint, URL-encoded emoji]
```

### JSON parser design

`src/json.c` locates keys by scanning for `"key":` patterns. It is not recursive and only handles the specific fields the bot needs. Do not extend it to become a general parser — keep it minimal.

### Compile-time log stripping

`LOG_LEVEL_COMPILE=0` (set by `make tiny`) removes all info and debug log call sites from the binary. `src/log.c` handles this via macros in `include/log.h`.
