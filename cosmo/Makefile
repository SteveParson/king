# Cosmopolitan build for King Discord bot
# Produces an Actually Portable Executable (APE)
#
# Usage:
#   make          — builds the 'king' APE binary (auto-downloads cosmocc)
#   make clean    — removes build artifacts
#   make distclean — also removes downloaded cosmocc toolchain

# ── cosmocc toolchain (auto-downloaded) ──────────────────────────
COSMOCC_VERSION = 3.9.2
COSMOCC_SHA256  = f4ff13af65fcd309f3f1cfd04275996fb7f72a4897726628a8c9cf732e850193
COSMOCC_DIR     = .cosmocc/$(COSMOCC_VERSION)
COSMOCC_BIN     = $(COSMOCC_DIR)/bin

# Download cosmocc if not already present (runs at parse time)
DOWNLOAD := $(shell scripts/download-cosmocc.sh $(COSMOCC_DIR) $(COSMOCC_VERSION) $(COSMOCC_SHA256))

CC  = $(CURDIR)/$(COSMOCC_BIN)/cosmocc
AR  = $(CURDIR)/$(COSMOCC_BIN)/cosmoar

CFLAGS  = -Os -Wall -Wextra -Wno-unused-parameter
LDFLAGS =

# ── mbedtls (git submodule at vendor/mbedtls) ───────────────────
MBEDTLS_SRC = ../vendor/mbedtls
MBEDTLS_INC = $(MBEDTLS_SRC)/include
MBEDTLS_LIB = $(MBEDTLS_SRC)/library
MBEDTLS_LIBS = $(MBEDTLS_LIB)/libmbedtls.a \
               $(MBEDTLS_LIB)/libmbedx509.a \
               $(MBEDTLS_LIB)/libmbedcrypto.a

# ── bot sources ──────────────────────────────────────────────────
BIN  = king
SRCS = king.c net.c ws.c json.c
OBJS = $(SRCS:.c=.o)

# ── targets ──────────────────────────────────────────────────────
all: $(BIN)

$(BIN): $(OBJS) $(MBEDTLS_LIBS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -L$(MBEDTLS_LIB) -lmbedtls -lmbedx509 -lmbedcrypto

%.o: %.c $(MBEDTLS_LIBS)
	$(CC) $(CFLAGS) -I$(MBEDTLS_INC) -c $< -o $@

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

clean:
	rm -f $(BIN) $(OBJS) *.com.dbg *.aarch64.elf
	rm -rf .aarch64

mbedclean:
	$(MAKE) -C $(MBEDTLS_LIB) clean 2>/dev/null || true

distclean: clean mbedclean
	rm -rf .cosmocc

.PHONY: all clean mbedclean distclean
