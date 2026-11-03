# king

A tiny discord bot written in C. say "king" in a channel and it reacts with 👑.

Built with [Cosmopolitan Libc](https://justine.lol/cosmopolitan/), so the build produces a single Actually Portable Executable that runs on Linux, macOS, Windows, FreeBSD, OpenBSD, and NetBSD.

## Build (Docker-first)

Only **Docker** is required on the host — no cosmocc, clang, or other toolchains.

```bash
git clone --recursive https://github.com/SteveParson/king.git
cd king
# if you cloned without --recursive:
# git submodule update --init

make docker-test    # compile + run unit tests in Docker
make docker-king    # write ./king to the host via Docker
make docker-image   # build scratch runtime image (king:local)
```

| Target | What it does |
|--------|----------------|
| `make docker-test` | Build and run unit tests inside Docker |
| `make docker-lint` | `clang-format` + `clang-tidy` (LLVM 21) inside Docker |
| `make docker-king` | Produce `./king` APE on the host (no local cosmocc) |
| `make docker-image` | Build `king:local` scratch image |
| `make docker-run` | Run `king:local` with `.env` or exported env vars |

`docker compose build test|lint|king` works the same way if you prefer Compose.

### Native build (optional)

If you want cosmocc on the machine itself:

```bash
make
make test
```

## Run

```bash
export DISCORD_TOKEN='<your_token_here>'
./king
```

Or copy `.env.sample` to `.env`, fill in your token, and source it before running — or use Docker:

```bash
make docker-run
# equivalent: docker run --rm --env-file .env king:local
```

A pre-built image is also published to the GitHub Container Registry:

```bash
docker run -e DISCORD_TOKEN='<your_token_here>' ghcr.io/steveparson/king:latest
```

You need **Message Content Intent** enabled in the [Discord Developer Portal](https://discord.com/developers/applications).

| Variable | Default | What it does |
|----------|---------|--------------|
| `DISCORD_TOKEN` | *(required)* | Bot token (no `Bot ` prefix) |
| `DISCORD_REPLY` | *(disabled)* | Reply text, omit to disable |
| `DISCORD_REACTION` | `👑` | Reaction emoji |
| `LOG_LEVEL` | `info` | `error` or `info` (structured NDJSON on stderr) |
| `LOG_SAMPLE_RATE` | `1.0` | Tail-sample rate for non-error / non-match events (`0.0`–`1.0`) |
| `KING_VERSION` | `0.1` | Included on every log event |
| `KING_COMMIT` | *(empty)* | Optional deploy/commit id on every event |

### Logging

Logs follow [wide events / canonical log lines](https://www.loggingsucks.com): one JSON object per unit of work (session, hello, ready, matched message, failures) — not a step-by-step diary. Errors and `matched_king` events are always kept; other info events respect `LOG_SAMPLE_RATE`. Tokens are never logged.

## Test / lint

```bash
make docker-test
make docker-lint
```

## How it works

~1000 lines of C. Connects to the Discord Gateway over WebSocket/TLS (mbedtls, vendored), listens for messages, checks for the word "king" (case-insensitive, whole-word match), and fires off a REST reply + reaction. Heartbeats, reconnects, the usual Gateway lifecycle stuff.

Things we cook in this repo might not be very baked, but from our bit of testing, it 'works'.
