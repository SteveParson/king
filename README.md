# king

A tiny discord bot written in C. say "king" in a channel and it reacts with 👑.

Built with [Cosmopolitan Libc](https://justine.lol/cosmopolitan/), so `make` spits out a single binary that runs on Linux, macOS, Windows, FreeBSD, OpenBSD, and NetBSD. No Docker, no runtime deps.

## Build

```bash
git clone --recursive https://github.com/SteveParson/king.git
make
```

## Run

```bash
export DISCORD_TOKEN='<your_token_here>'
./king
```

Or copy `.env.sample` to `.env`, fill in your token, and source it before running.

You need **Message Content Intent** enabled in the [Discord Developer Portal](https://discord.com/developers/applications).

| Variable | Default | What it does |
|----------|---------|--------------|
| `DISCORD_TOKEN` | *(required)* | Bot token (no `Bot ` prefix) |
| `DISCORD_REPLY` | *(disabled)* | Reply text, omit to disable |
| `DISCORD_REACTION` | `👑` | Reaction emoji |
| `LOG_LEVEL` | `info` | `error`, `info`, or `debug` |

## Docker

A pre-built image is published to the GitHub Container Registry:

```bash
docker run -e DISCORD_TOKEN='<your_token_here>' ghcr.io/steveparson/king:latest
```

Or using a `.env` file:

```bash
cp .env.sample .env
# edit .env with your token
docker run --env-file .env ghcr.io/steveparson/king:latest
```

## Test

Run `make test` to run the tests.

## How it works

~1000 lines of C, Connects to the Discord Gateway over WebSocket/TLS (mbedtls, vendored), listens for messages, checks for the word "king" (case-insensitive, whole-word match), and fires off a REST reply + reaction. Heartbeats, reconnects, the usual Gateway lifecycle stuff.

Things we cook in this repo might not be very baked, but from our bit of testing, it 'works'.
