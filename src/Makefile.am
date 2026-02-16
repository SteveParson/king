bin_PROGRAMS = discord2

discord2_SOURCES = \
	main.c \
	discord.c \
	json.c \
	net.c \
	ws.c \
	log.c

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/vendor/bearssl/inc

discord2_LDADD = $(top_builddir)/vendor/libbearssl.a
