bin_PROGRAMS = discord2

discord2_SOURCES = \
	main.c \
	discord.c \
	json.c \
	net.c \
	ws.c \
	log.c

AM_CPPFLAGS = -I$(top_srcdir)/include @OPENSSL_CFLAGS@

discord2_LDADD = @OPENSSL_LIBS@
