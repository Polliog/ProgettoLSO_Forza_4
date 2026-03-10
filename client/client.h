#ifndef CLIENT_H
#define CLIENT_H

#include "../shared/protocol.h"

extern int g_sockfd;

void print_board(int board[ROWS][COLS]);
void print_menu(void);

int game_loop(int sockfd, int game_id);
int wait_for_opponent(int sockfd, int game_id);

#endif
