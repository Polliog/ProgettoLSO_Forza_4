#!/bin/bash
# Script di compilazione per Forza 4 Client-Server
# Uso: ./compile.sh

set -e

echo "=== Compilazione Forza 4 ==="

echo "Compilando server..."
gcc -Wall -Wextra -pthread -o server/forza4_server \
    server/server.c server/game.c server/client_handler.c
echo "  -> server/forza4_server OK"

echo "Compilando client..."
gcc -Wall -Wextra -o client/forza4_client \
    client/client.c client/ui.c client/game_loop.c
echo "  -> client/forza4_client OK"

echo ""
echo "=== Compilazione completata ==="
echo ""
echo "Per avviare:"
echo "  Server:  ./server/forza4_server [porta]     (default: 9090)"
echo "  Client:  ./client/forza4_client [ip] [porta] (default: 127.0.0.1 9090)"
