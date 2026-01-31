CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
INCLUDES = -Iinclude
OPENSSL_CFLAGS ?=
OPENSSL_LIBS ?= -lssl -lcrypto

SRC = src/main.c src/discord.c src/json.c src/net.c src/ws.c src/log.c
OBJ = $(SRC:.c=.o)

all: discord2

discord2: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(OPENSSL_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPENSSL_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) discord2

.PHONY: all clean
