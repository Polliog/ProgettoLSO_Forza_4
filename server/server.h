#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <signal.h>
#include "../shared/protocol.h"


typedef struct game {
    int          id;
    game_state_t state;
    int          board[ROWS][COLS];
    int          player_fd[2];
    char         player_name[2][MAX_USERNAME];
    int          current_turn;
    pthread_mutex_t lock;
    struct game *next;
} game_t;

typedef struct client {
    int           fd;
    char          username[MAX_USERNAME];
    struct client *next;
} client_t;


extern game_t   *game_list;
extern client_t *client_list;
extern int        next_game_id;

extern pthread_mutex_t game_list_lock;
extern pthread_mutex_t client_list_lock;

extern volatile sig_atomic_t server_running;
extern int server_fd_global;

extern int active_threads;
extern pthread_mutex_t thread_count_lock;
extern pthread_cond_t  thread_count_cond;

#define MAX_GAMES_PER_CLIENT 5

void add_client(int fd, const char *username);
void remove_client(int fd);

int sanitize_username(char *username);
int count_active_games(int fd);
int get_player_index(game_t *g, int fd);
int is_valid_client_msg(msg_type_t type);

void *client_handler(void *arg);

game_t *create_game(int creator_fd, const char *creator_name);
game_t *find_and_lock_game(int game_id);
void    remove_game(int game_id);

int drop_disc(int board[ROWS][COLS], int col, int player);
int check_win(int board[ROWS][COLS], int player);
int board_full(int board[ROWS][COLS]);

void send_error(int fd, const char *text);
void send_board(game_t *g, int player_idx);
void send_turn_notifications(game_t *g);

void handle_disconnect_from_game(int fd);

#endif
