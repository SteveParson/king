# Multi-stage Cosmopolitan build for the king Discord bot.
# Host requirement: Docker only (no cosmocc / clang on the Mac).
#
# Targets:
#   builder  — compile king + test_king with cosmocc
#   test     — run unit tests (fails the build on test failure)
#   lint     — clang-format + clang-tidy (LLVM 21, matches CI)
#   runtime  — scratch image with assimilated APE (default)
#
# Prefer Makefile wrappers: `make docker-test`, `make docker-king`, etc.

# ── shared base with build deps ──────────────────────────────────
FROM ubuntu:24.04 AS base

RUN apt-get update && apt-get install -y --no-install-recommends \
    make curl unzip ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# ── compile with cosmocc ────────────────────────────────────────
FROM base AS builder

COPY . .

# vendor/mbedtls comes from `git submodule update --init` on the host
# (or `git clone --recursive`). Keep the image free of a .git checkout.
RUN if [ ! -f vendor/mbedtls/include/mbedtls/ssl.h ]; then \
      echo "error: vendor/mbedtls missing. Run: git submodule update --init" >&2; \
      exit 1; \
    fi

# Cache cosmocc across builds when BuildKit is available. Binaries are
# copied to /artifacts because cache mounts are not part of the image layer.
RUN --mount=type=cache,target=/src/.cosmocc,sharing=locked \
    make \
    && make test_king \
    && mkdir -p /artifacts \
    && cp -a king test_king /artifacts/ \
    && cp -a king /artifacts/king.assimilated \
    && .cosmocc/current/bin/assimilate /artifacts/king.assimilated

# ── unit tests ───────────────────────────────────────────────────
FROM builder AS test

RUN /artifacts/test_king

# ── lint (same LLVM major as .github/workflows) ──────────────────
FROM ubuntu:24.04 AS lint

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl gnupg make \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
      | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-21 main" \
      > /etc/apt/sources.list.d/llvm.list \
    && apt-get update && apt-get install -y --no-install-recommends \
      clang-format-21 clang-tidy-21 \
    && ln -sf /usr/bin/clang-format-21 /usr/bin/clang-format \
    && ln -sf /usr/bin/clang-tidy-21 /usr/bin/clang-tidy \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN if [ ! -f vendor/mbedtls/include/mbedtls/ssl.h ]; then \
      echo "error: vendor/mbedtls missing. Run: git submodule update --init" >&2; \
      exit 1; \
    fi

RUN make lint

# ── runtime image (default) ──────────────────────────────────────
FROM scratch AS runtime

COPY --from=builder /artifacts/king.assimilated /king

ENTRYPOINT ["/king"]
