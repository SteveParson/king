# discord2 (C, minimal deps)

A minimal Discord bot in C that connects to the Gateway over TLS WebSockets and uses REST to send messages and reactions.

## Features
- TLS via OpenSSL (no other external deps)
- Minimal WebSocket client (masked client frames)
- Tiny JSON field parser (targeted, not a full JSON implementation)
- Sends a reply and adds a reaction when a message contains `king`
- Simple log levels via `LOG_LEVEL`

## Build (Autotools)
```bash
./autogen.sh
./configure
make
```

Size-focused builds:
```bash
make small   # smaller binary
make tiny    # smallest (strips info/debug logs + unwind tables)
```

Lint:
```bash
make lint
```

Format:
```bash
make format
```

Test:
```bash
make check
```

## Run
Set your bot token and (optionally) a reply message and reaction emoji:

```bash
export DISCORD_TOKEN='<your_token_here>'
export DISCORD_REPLY='All hail' # set to empty string to disable replies
export DISCORD_REACTION='<unicode_emoji>'
export LOG_LEVEL=info
./src/discord2
```

Notes:
- The token should be the raw bot token (no `Bot ` prefix). The app will add it for REST automatically.
- Ensure the bot has **MESSAGE CONTENT INTENT** enabled in the Discord Developer Portal.
- The bot replies in the same channel as the message and reacts to that message.

## Config (env vars)
- `DISCORD_TOKEN` (required): raw token
- `DISCORD_REPLY` (optional): default `"All hail"` (set to empty string to disable replies)
- `DISCORD_REACTION` (optional): empty by default (set to a unicode emoji to react)
- `LOG_LEVEL` (optional): `error`, `info` (default), or `debug`
- `LOG_LEVEL_COMPILE` (compile-time): set by `make tiny` to remove info/debug log code

## Limitations
- JSON parsing is minimal and only targets fields we need.
- WebSocket implementation is minimal (single-frame text payloads).
- No rate-limit handling beyond what Discord enforces at connection level.

## Troubleshooting
- If you see HTTP 401 or 403 for REST calls, check token and intents.
- If Gateway disconnects quickly, verify intents and token value.
