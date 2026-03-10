CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11
LDFLAGS = -pthread

SERVER_SRC = server/server.c server/game.c server/client_handler.c
CLIENT_SRC = client/client.c client/ui.c client/game_loop.c

SERVER_BIN = server/forza4_server
CLIENT_BIN = client/forza4_client

.PHONY: all clean

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_SRC) server/server.h shared/protocol.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SERVER_SRC)

$(CLIENT_BIN): $(CLIENT_SRC) client/client.h shared/protocol.h
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC)

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
