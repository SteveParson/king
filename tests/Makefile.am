# check_PROGRAMS are built (and TESTS are run) only when you run `make check`.

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/vendor/bearssl/inc

check_PROGRAMS = test_json test_discord

# test_json: parser logic only — no network code, no OpenSSL at link time.
test_json_SOURCES = test_json.c        \
    $(top_srcdir)/src/json.c           \
    $(top_srcdir)/src/log.c

# test_discord: string utilities inside discord.c; the full source list is
# needed so the linker can resolve all symbols discord.c calls (ws, net, …).
test_discord_SOURCES = test_discord.c  \
    $(top_srcdir)/src/discord.c        \
    $(top_srcdir)/src/json.c           \
    $(top_srcdir)/src/ws.c             \
    $(top_srcdir)/src/net.c            \
    $(top_srcdir)/src/log.c
test_discord_LDADD = $(top_builddir)/vendor/libbearssl.a

TESTS = test_json test_discord
