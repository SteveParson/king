# AGENTS.md

This file provides guidance to AI coding agents when working with code in this repository.

## Build

```bash
make
```

The Makefile auto-downloads `cosmocc` (Cosmopolitan C compiler) on first run and builds mbedtls from the vendored submodule. The output is `king`, an Actually Portable Executable.

```bash
make test       # build and run tests
make clean      # remove build artifacts
make mbedclean  # also clean mbedtls
make distclean  # also remove downloaded cosmocc
```

## Run

```bash
export DISCORD_TOKEN='<raw_bot_token>'   # required; no "Bot " prefix
export DISCORD_REPLY='All hail'          # optional; omit to disable replies
export DISCORD_REACTION='👑'            # optional; omit to disable reactions
export LOG_LEVEL=info                    # error | info (default) | debug
./king
```

The bot requires **MESSAGE CONTENT INTENT** enabled in the Discord Developer Portal.

## Architecture

Built with Cosmopolitan Libc and mbedtls for TLS. ~1000 lines of C in `src/`.

### Module responsibilities

| File | Role |
|------|------|
| `src/king.c` | Entry point, Gateway lifecycle, heartbeat loop, event dispatch, REST calls |
| `src/str.c` | String utilities — `contains_case`, `json_escape`, `url_encode`, `http_status_code` |
| `src/net.c` | TCP + mbedtls TLS connect, custom BIO callbacks, HTTPS request helper |
| `src/ws.c` | Minimal WebSocket client (masked client frames, text frames only) |
| `src/json.c` | Targeted key extraction — not a general-purpose parser |
| `src/log.h` | Header-only runtime log levels; compile-time stripping via `LOG_LEVEL_COMPILE` |
| `src/cacerts.h` | Embedded root CA certificates (PEM) |

### Key structs

- `tls_conn` (net.h) — mbedtls SSL context, config, RNG, entropy, and socket fd.
- `gateway_state` (king.c) — TLS connection, heartbeat interval/sequence/timing.

### Gateway lifecycle (king.c)

```
main()
  -> tls_init_ca()
  -> tls_connect(gateway.discord.gg:443)
  -> ws_handshake()
  -> wait_for_hello()  [blocks for op=10]
      -> handle_hello()  [saves interval, sends IDENTIFY]
  -> loop:
      -> poll_and_read()  [heartbeat with retry when due, then ws_read_text()]
      -> handle gateway ops  [op=11 ack; op=7/9 reconnect]
      -> update sequence
      -> handle_message_create()
          -> contains_case() checks for "king"
          -> send_message()   [POST /channels/{id}/messages]
          -> send_reaction()  [PUT reactions endpoint, URL-encoded emoji]
```

### JSON parser design

`json.c` locates keys by scanning for `"key":` patterns. It is not recursive and only handles the specific fields the bot needs. Do not extend it to become a general parser — keep it minimal.

### Build system

The Makefile auto-downloads cosmocc v3.9.2 (pinned with SHA-256) into `.cosmocc/` via `scripts/download-cosmocc.sh`. mbedtls 2.28.8 is a git submodule at `vendor/mbedtls`. No autotools, no external dependencies to install.
