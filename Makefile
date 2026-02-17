# Cosmopolitan build for King Discord bot
# Produces an Actually Portable Executable (APE)
#
# Usage:
#   make          — builds the 'king' APE binary (auto-downloads cosmocc)
#   make test     — builds and runs tests
#   make lint     — runs clang-format and clang-tidy checks
#   make clean    — removes build artifacts
#   make distclean — also removes downloaded cosmocc toolchain

# ── cosmocc toolchain (auto-downloaded) ──────────────────────────
COSMOCC_VERSION = 3.9.2
COSMOCC_SHA256  = f4ff13af65fcd309f3f1cfd04275996fb7f72a4897726628a8c9cf732e850193
COSMOCC_DIR     = .cosmocc/$(COSMOCC_VERSION)
COSMOCC_BIN     = $(COSMOCC_DIR)/bin

# Download cosmocc if not already present (skip for lint/clean targets)
ifneq ($(filter-out lint clean distclean mbedclean,$(MAKECMDGOALS)),)
DOWNLOAD := $(shell scripts/download-cosmocc.sh $(COSMOCC_DIR) $(COSMOCC_VERSION) $(COSMOCC_SHA256))
endif
ifeq ($(MAKECMDGOALS),)
DOWNLOAD := $(shell scripts/download-cosmocc.sh $(COSMOCC_DIR) $(COSMOCC_VERSION) $(COSMOCC_SHA256))
endif

CC  = $(CURDIR)/$(COSMOCC_BIN)/cosmocc
AR  = $(CURDIR)/$(COSMOCC_BIN)/cosmoar

CFLAGS  = -Os -Wall -Wextra -Wno-unused-parameter
LDFLAGS =

# ── mbedtls (git submodule at vendor/mbedtls) ───────────────────
MBEDTLS_SRC = vendor/mbedtls
MBEDTLS_INC = $(MBEDTLS_SRC)/include
MBEDTLS_LIB = $(MBEDTLS_SRC)/library
MBEDTLS_LIBS = $(MBEDTLS_LIB)/libmbedtls.a \
               $(MBEDTLS_LIB)/libmbedx509.a \
               $(MBEDTLS_LIB)/libmbedcrypto.a

# ── bot sources ──────────────────────────────────────────────────
BIN  = king
SRCS = src/king.c src/net.c src/ws.c src/json.c src/str.c
OBJS = $(SRCS:.c=.o)

# ── targets ──────────────────────────────────────────────────────
all: $(BIN)

$(BIN): $(OBJS) $(MBEDTLS_LIBS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -L$(MBEDTLS_LIB) -lmbedtls -lmbedx509 -lmbedcrypto

src/%.o: src/%.c $(MBEDTLS_LIBS)
	$(CC) $(CFLAGS) -I$(MBEDTLS_INC) -Isrc -c $< -o $@

# Build mbedtls from the vendored submodule.
# WARNING_CFLAGS= suppresses warnings from mbedtls source that cosmocc flags.
# APPLE_BUILD=0 prevents ranlib issues under cosmocc.
$(MBEDTLS_LIBS):
	@if [ ! -f $(MBEDTLS_SRC)/include/mbedtls/ssl.h ]; then \
		echo "error: vendor/mbedtls not found. Run: git submodule update --init" >&2; \
		exit 1; \
	fi
	$(MAKE) -C $(MBEDTLS_LIB) CC=$(CC) AR=$(AR) \
		CFLAGS="-Os -I../include" WARNING_CFLAGS= APPLE_BUILD=0

# ── tests ────────────────────────────────────────────────────────
TEST_BIN  = test_king
TEST_SRCS = src/test_king.c src/json.c src/str.c
TEST_OBJS = $(TEST_SRCS:.c=.o)

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(TEST_OBJS)

# ── lint ─────────────────────────────────────────────────────────
LINT_SRCS = src/king.c src/net.c src/ws.c src/json.c src/str.c src/test_king.c
LINT_HDRS = src/json.h src/str.h src/net.h src/ws.h src/log.h src/test.h src/cacerts.h

lint:
	clang-format --dry-run --Werror $(LINT_SRCS) $(LINT_HDRS)
	clang-tidy $(LINT_SRCS) -- -Isrc -I$(MBEDTLS_INC) -Wno-implicit-function-declaration

clean:
	rm -f $(BIN) $(TEST_BIN) $(OBJS) src/test_king.o *.com.dbg *.aarch64.elf
	rm -rf .aarch64

mbedclean:
	$(MAKE) -C $(MBEDTLS_LIB) clean 2>/dev/null || true

distclean: clean mbedclean
	rm -rf .cosmocc

.PHONY: all test lint clean mbedclean distclean
