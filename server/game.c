#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "server.h"


game_t *create_game(int creator_fd, const char *creator_name)
{
    game_t *g = calloc(1, sizeof(game_t));
    if (!g) { perror("calloc game"); return NULL; }

    pthread_mutex_lock(&game_list_lock);
    g->id = next_game_id++;
    pthread_mutex_unlock(&game_list_lock);

    g->state         = GAME_WAITING;
    g->player_fd[0]  = creator_fd;
    g->player_fd[1]  = -1;
    g->current_turn   = 0;
    strncpy(g->player_name[0], creator_name, MAX_USERNAME - 1);
    pthread_mutex_init(&g->lock, NULL);

    pthread_mutex_lock(&game_list_lock);
    g->next   = game_list;
    game_list = g;
    pthread_mutex_unlock(&game_list_lock);

    return g;
}

/*
 * Cerca una partita per ID e la ritorna GIA' BLOCCATA (g->lock acquisito).
 * L'ordine di lock e': game_list_lock -> g->lock -> rilascia game_list_lock.
 * Questo impedisce che un altro thread liberi g tra find e lock (no use-after-free).
 * Il chiamante DEVE fare pthread_mutex_unlock(&g->lock) quando ha finito.
 */
game_t *find_and_lock_game(int game_id)
{
    pthread_mutex_lock(&game_list_lock);
    game_t *g = game_list;
    while (g) {
        if (g->id == game_id) {
            pthread_mutex_lock(&g->lock);
            pthread_mutex_unlock(&game_list_lock);
            return g;
        }
        g = g->next;
    }
    pthread_mutex_unlock(&game_list_lock);
    return NULL;
}

/*
 * Rimuove e libera una partita dalla lista.
 * Acquisisce game_list_lock, poi g->lock (stesso ordine di find_and_lock_game)
 * per garantire che nessuno stia usando la partita.
 */
void remove_game(int game_id)
{
    pthread_mutex_lock(&game_list_lock);
    game_t **pp = &game_list;
    while (*pp) {
        if ((*pp)->id == game_id) {
            game_t *tmp = *pp;
            pthread_mutex_lock(&tmp->lock);
            *pp = tmp->next;
            pthread_mutex_unlock(&tmp->lock);
            pthread_mutex_unlock(&game_list_lock);
            pthread_mutex_destroy(&tmp->lock);
            free(tmp);
            printf("[SERVER] Partita #%d rimossa dalla memoria\n", game_id);
            return;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&game_list_lock);
}

int drop_disc(int board[ROWS][COLS], int col, int player)
{
    if (col < 0 || col >= COLS) return -1;
    for (int r = ROWS - 1; r >= 0; r--) {
        if (board[r][col] == EMPTY) {
            board[r][col] = player;
            return r;
        }
    }
    return -1;
}

int check_win(int board[ROWS][COLS], int player)
{
    static const int dr[] = { 0, 1, 1, 1 };
    static const int dc[] = { 1, 0, 1,-1 };

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (board[r][c] != player) continue;
            for (int d = 0; d < 4; d++) {
                int count = 1;
                for (int k = 1; k < 4; k++) {
                    int nr = r + dr[d] * k;
                    int nc = c + dc[d] * k;
                    if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) break;
                    if (board[nr][nc] != player) break;
                    count++;
                }
                if (count == 4) return 1;
            }
        }
    }
    return 0;
}

int board_full(int board[ROWS][COLS])
{
    for (int c = 0; c < COLS; c++)
        if (board[0][c] == EMPTY) return 0;
    return 1;
}

void send_error(int fd, const char *text)
{
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_ERROR;
    strncpy(msg.username, text, MAX_USERNAME - 1);
    send_message(fd, &msg);
}

void send_board(game_t *g, int player_idx)
{
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type    = MSG_BOARD_UPDATE;
    msg.game_id = g->id;
    memcpy(msg.board, g->board, sizeof(g->board));
    send_message(g->player_fd[player_idx], &msg);
}

void send_turn_notifications(game_t *g)
{
    message_t msg_turn, msg_wait;
    memset(&msg_turn, 0, sizeof(msg_turn));
    memset(&msg_wait, 0, sizeof(msg_wait));

    msg_turn.type    = MSG_YOUR_TURN;
    msg_turn.game_id = g->id;
    msg_wait.type    = MSG_WAIT_TURN;
    msg_wait.game_id = g->id;

    send_message(g->player_fd[g->current_turn],      &msg_turn);
    send_message(g->player_fd[1 - g->current_turn],  &msg_wait);
}

void handle_disconnect_from_game(int fd)
{
    pthread_mutex_lock(&game_list_lock);
    game_t *g = game_list;
    while (g) {
        pthread_mutex_lock(&g->lock);

        if (g->player_fd[0] != fd && g->player_fd[1] != fd) {
            pthread_mutex_unlock(&g->lock);
            g = g->next;
            continue;
        }

        int game_id = g->id;

        switch (g->state) {

        case GAME_FINISHED:
            if (g->player_fd[0] == fd) g->player_fd[0] = -1;
            if (g->player_fd[1] == fd) g->player_fd[1] = -1;

            /* Se entrambi i giocatori se ne sono andati, libera la memoria */
            if (g->player_fd[0] == -1 && g->player_fd[1] == -1) {
                pthread_mutex_unlock(&g->lock);
                pthread_mutex_unlock(&game_list_lock);
                remove_game(game_id);
                return;
            }
            pthread_mutex_unlock(&g->lock);
            pthread_mutex_unlock(&game_list_lock);
            return;

        case GAME_WAITING:
            /* Solo il creatore puo' essere qui; la partita non serve piu' */
            g->player_fd[0] = -1;
            g->state = GAME_FINISHED;
            pthread_mutex_unlock(&g->lock);
            pthread_mutex_unlock(&game_list_lock);
            remove_game(game_id);
            return;

        case GAME_CHALLENGING:
            if (g->player_fd[0] == fd) {
                /* Creatore disconnesso: invia REJECTED allo sfidante */
                int joiner_fd = g->player_fd[1];
                g->player_fd[0] = -1;
                g->state = GAME_FINISHED;
                pthread_mutex_unlock(&g->lock);
                pthread_mutex_unlock(&game_list_lock);
                if (joiner_fd != -1) {
                    message_t msg;
                    memset(&msg, 0, sizeof(msg));
                    msg.type    = MSG_REJECTED;
                    msg.game_id = game_id;
                    send_message(joiner_fd, &msg);
                }
            } else {
                /* Sfidante disconnesso: torna a WAITING, nessun messaggio
                 * al creatore. Quando inviera' ACCEPT/REJECT, il server
                 * vedra' state=WAITING e rispondera' di conseguenza. */
                g->player_fd[1] = -1;
                memset(g->player_name[1], 0, MAX_USERNAME);
                g->state = GAME_WAITING;
                pthread_mutex_unlock(&g->lock);
                pthread_mutex_unlock(&game_list_lock);
            }
            return;

        case GAME_PLAYING: {
            int other = (g->player_fd[0] == fd) ? 1 : 0;
            int other_fd = g->player_fd[other];
            if (g->player_fd[0] == fd) g->player_fd[0] = -1;
            if (g->player_fd[1] == fd) g->player_fd[1] = -1;
            g->state = GAME_FINISHED;
            pthread_mutex_unlock(&g->lock);
            pthread_mutex_unlock(&game_list_lock);

            if (other_fd != -1) {
                message_t msg;
                memset(&msg, 0, sizeof(msg));
                msg.type    = MSG_OPPONENT_LEFT;
                msg.game_id = game_id;
                send_message(other_fd, &msg);
            }
            return;
        }
        }

        pthread_mutex_unlock(&g->lock);
        g = g->next;
    }
    pthread_mutex_unlock(&game_list_lock);
}
