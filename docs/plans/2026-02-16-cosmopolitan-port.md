# Cosmopolitan Port of King Discord Bot

> **For Claude:** This is a design brief, not a rigid task list. You have full freedom to approach the implementation however you see fit. The goal is small binary size AND small code surface — cut corners, simplify aggressively, combine files if it makes sense. Don't treat the existing code as gospel.

**Goal:** Create a version of the King Discord bot that compiles with `cosmocc` to produce an Actually Portable Executable (APE) that runs on Linux, macOS, Windows, FreeBSD, OpenBSD, and NetBSD from a single binary.

**Architecture:** Replace BearSSL with mbedtls for TLS. Use cosmopolitan's built-in facilities where they help reduce code. Keep the custom WebSocket framing and JSON parser since cosmo doesn't provide those. Flat file layout in `cosmo/` with a plain Makefile.

**Tech Stack:** C, cosmocc toolchain, mbedtls (shipped with cosmocc or built separately)

---

## What the Bot Does

The King Discord bot is simple:

1. Connects to `gateway.discord.gg:443` via WebSocket over TLS
2. Authenticates with Discord's Gateway (sends IDENTIFY after receiving HELLO)
3. Maintains heartbeats on a timer via `poll()`
4. Listens for MESSAGE_CREATE events containing the word "king" (case-insensitive, whole-word)
5. Responds by sending a reply message (POST to REST API) and adding an emoji reaction (PUT to REST API)
6. Each REST call opens a fresh TLS connection to `discord.com:443`

**Environment variables:**
- `DISCORD_TOKEN` — required, raw bot token (no "Bot " prefix)
- `DISCORD_REPLY` — optional reply text (default: "All hail")
- `DISCORD_REACTION` — optional emoji reaction (default: crown emoji)
- `LOG_LEVEL` — `error` | `info` (default) | `debug`

## Output Location

All new files go in `cosmo/` at the repo root (`/Users/jack/repos/contrib/king/cosmo/`).

Flat layout — no subdirectories. Something like:

```
cosmo/
  Makefile
  main.c
  discord.c / discord.h
  ws.c / ws.h
  net.c / net.h
  json.c / json.h
  log.c / log.h
```

But if you can make it smaller (fewer files, combined modules), do it.

## The Big Change: BearSSL -> mbedtls

The existing `src/net.c` uses BearSSL for TLS. The key structures and functions to replace:

**BearSSL (current):**
- `br_ssl_client_context`, `br_x509_minimal_context`, `br_sslio_context`
- `br_ssl_client_init_full()`, `br_ssl_client_reset()`, `br_sslio_init()`
- `br_sslio_read/write/flush/close()`
- Hardcoded single trust anchor (GTS Root R4, EC P-384 public key as raw bytes)

**mbedtls (target):**
- `mbedtls_ssl_context`, `mbedtls_ssl_config`, `mbedtls_net_context`
- `mbedtls_x509_crt` for CA certificates
- `mbedtls_ctr_drbg_context` + `mbedtls_entropy_context` for RNG
- `mbedtls_ssl_read/write/close_notify()`

### CA Certificate Strategy

The current bot hardcodes a single root CA. With cosmocc + mbedtls, you need to figure out the best approach. Options:
- Bundle a curated set of root CAs as C data (PEM string embedded in source)
- Load from a PEM file at runtime
- Use cosmo's built-in `GetSslRoots()` if available via cosmocc

Pick whatever works best and keeps the code small.

### Reference: How Cosmo Uses mbedtls

Look at these files in `/Users/jack/repos/contrib/cosmopolitan/`:
- `tool/curl/curl.c` — Full HTTPS client showing mbedtls setup, `mbedtls_ssl_config_defaults()`, `mbedtls_ssl_conf_ca_chain()`, custom BIO callbacks
- `net/https/fetch.c` — `AppendFetch()` implementation showing TLS setup with `GetSslRoots()`
- `third_party/mbedtls/ssl.h` — The SSL API
- `third_party/mbedtls/net_sockets.h` — Socket abstraction (`mbedtls_net_context`)
- `third_party/mbedtls/config.h` — What features are enabled

## What Stays (Mostly) the Same

These modules can be adapted from the existing code but simplified:

- **WebSocket framing** (`src/ws.c`): HTTP Upgrade handshake, masked text frame send, text frame read. The random byte generation for masking keys uses `getrandom()`/`arc4random_buf()` — cosmo supports both, or you could use mbedtls's RNG.
- **JSON parser** (`src/json.c`): Scans for `"key":` patterns, extracts strings and ints. Not recursive. Keep it minimal.
- **Bot logic** (`src/discord.c`): Gateway lifecycle, heartbeat loop, event dispatch, `contains_case()` word matching, REST call formatting. The string utilities (`json_escape`, `url_encode`, `http_status_code`) are pure C with no TLS dependencies.
- **Logging** (`src/log.c`): Runtime log levels with compile-time stripping via `LOG_LEVEL_COMPILE` macro.
- **Entry point** (`src/main.c`): Reads env vars, calls `discord_run()`.

## Design Principles

1. **Small code surface** — fewer lines, fewer files, fewer abstractions. If a function is only called once, consider inlining it.
2. **Small binary size** — use `-Os`, strip what you can. The existing repo has `make tiny` that gets aggressive with stripping.
3. **Don't copy blindly** — the existing code is a reference, not a template. Simplify where possible.
4. **Single binary, runs everywhere** — that's the whole point of cosmopolitan.

## Build

Simple Makefile using `cosmocc`. No autotools. The agent needs to figure out:
1. Whether `cosmocc` ships mbedtls headers/libraries or if they need to be built/installed
2. The right compiler/linker flags
3. How to produce the APE binary

Target: `make` produces the bot binary, `make clean` cleans up.

## Existing Code Reference

All existing source files are in the parent repo at `/Users/jack/repos/contrib/king/src/` and `/Users/jack/repos/contrib/king/include/`. The cosmopolitan repo is at `/Users/jack/repos/contrib/cosmopolitan/`.

## Verification

The bot can't be tested without a Discord token, but the code should compile cleanly with `cosmocc` and the binary should be a valid APE.
