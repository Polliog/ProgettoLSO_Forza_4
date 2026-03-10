#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "server.h"

void add_client(int fd, const char *username)
{
    client_t *c = malloc(sizeof(client_t));
    if (!c) { perror("malloc client"); return; }
    c->fd = fd;
    strncpy(c->username, username, MAX_USERNAME - 1);
    c->username[MAX_USERNAME - 1] = '\0';

    pthread_mutex_lock(&client_list_lock);
    c->next = client_list;
    client_list = c;
    pthread_mutex_unlock(&client_list_lock);
}

void remove_client(int fd)
{
    pthread_mutex_lock(&client_list_lock);
    client_t **pp = &client_list;
    while (*pp) {
        if ((*pp)->fd == fd) {
            client_t *tmp = *pp;
            *pp = tmp->next;
            free(tmp);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&client_list_lock);
}



/* Sanitizza username: solo alfanumerici e underscore, min 1 char */
int sanitize_username(char *username)
{
    /* Forza null-termination */
    username[MAX_USERNAME - 1] = '\0';

    int len = 0;
    for (int i = 0; username[i] != '\0'; i++) {
        char c = username[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') {
            username[len++] = c;
        }
    }
    username[len] = '\0';

    if (len == 0) return -1;
    return 0;
}

int count_active_games(int fd)
{
    int count = 0;
    pthread_mutex_lock(&game_list_lock);
    game_t *g = game_list;
    while (g) {
        if (g->player_fd[0] == fd && g->state != GAME_FINISHED)
            count++;
        g = g->next;
    }
    pthread_mutex_unlock(&game_list_lock);
    return count;
}

/* Verifica che il client sia effettivamente un giocatore della partita.
 * Ritorna 0 o 1 (indice giocatore), oppure -1 se non e' nella partita. */
int get_player_index(game_t *g, int fd)
{
    if (g->player_fd[0] == fd) return 0;
    if (g->player_fd[1] == fd) return 1;
    return -1;
}

/* Valida il tipo di messaggio ricevuto dal client.
 * Ritorna 1 se e' un tipo che il client puo' legittimamente inviare. */
int is_valid_client_msg(msg_type_t type)
{
    switch (type) {
    case MSG_CREATE_GAME:
    case MSG_LIST_GAMES:
    case MSG_JOIN_GAME:
    case MSG_ACCEPT:
    case MSG_REJECT:
    case MSG_MOVE:
    case MSG_QUIT:
        return 1;
    default:
        return 0;
    }
}

void *client_handler(void *arg)
{
    int fd = *(int *)arg;
    free(arg);

    pthread_mutex_lock(&thread_count_lock);
    active_threads++;
    pthread_mutex_unlock(&thread_count_lock);

    char username[MAX_USERNAME] = {0};
    message_t msg;

    if (recv_message(fd, &msg) < 0 || msg.type != MSG_LOGIN) {
        close(fd);
        goto thread_exit;
    }
    strncpy(username, msg.username, MAX_USERNAME - 1);
    username[MAX_USERNAME - 1] = '\0';

    if (sanitize_username(username) < 0) {
        send_error(fd, "Username non valido");
        close(fd);
        goto thread_exit;
    }

    printf("[SERVER] Login: %s (fd=%d)\n", username, fd);

    add_client(fd, username);

    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_LOGIN_OK;
    if (send_message(fd, &msg) < 0) {
        remove_client(fd);
        close(fd);
        goto thread_exit;
    }

    while (server_running && recv_message(fd, &msg) == 0) {
        if (!is_valid_client_msg(msg.type)) {
            send_error(fd, "Messaggio non valido");
            continue;
        }

        switch (msg.type) {

        case MSG_CREATE_GAME: {
            /* Limite partite per client (anti-flood) */
            if (count_active_games(fd) >= MAX_GAMES_PER_CLIENT) {
                send_error(fd, "Troppe partite attive");
                break;
            }
            game_t *g = create_game(fd, username);
            if (!g) {
                send_error(fd, "Impossibile creare");
                break;
            }
            printf("[SERVER] %s ha creato partita #%d\n", username, g->id);
            memset(&msg, 0, sizeof(msg));
            msg.type    = MSG_GAME_CREATED;
            msg.game_id = g->id;
            send_message(fd, &msg);
            break;
        }

        case MSG_LIST_GAMES: {
            memset(&msg, 0, sizeof(msg));
            msg.type = MSG_GAME_LIST;
            int count = 0;

            pthread_mutex_lock(&game_list_lock);
            game_t *g = game_list;
            while (g && count < 20) {
                if (g->state == GAME_WAITING) {
                    msg.game_ids[count] = g->id;
                    strncpy(msg.game_creators[count], g->player_name[0],
                            MAX_USERNAME - 1);
                    count++;
                }
                g = g->next;
            }
            pthread_mutex_unlock(&game_list_lock);

            msg.game_count = count;
            send_message(fd, &msg);
            break;
        }

        case MSG_JOIN_GAME: {
            game_t *g = find_and_lock_game(msg.game_id);
            if (!g) {
                send_error(fd, "Partita non trovata");
                break;
            }
            /* g->lock gia' acquisito da find_and_lock_game */
            if (g->state != GAME_WAITING) {
                pthread_mutex_unlock(&g->lock);
                send_error(fd, "Partita non disponibile");
                break;
            }
            if (g->player_fd[0] == fd) {
                pthread_mutex_unlock(&g->lock);
                send_error(fd, "Sei gia' il creatore");
                break;
            }

            /* Salva lo sfidante e cambia stato */
            g->player_fd[1] = fd;
            strncpy(g->player_name[1], username, MAX_USERNAME - 1);
            g->state = GAME_CHALLENGING;

            printf("[SERVER] %s sfida %s nella partita #%d\n",
                   username, g->player_name[0], g->id);

            /* Invia MSG_CHALLENGE al creatore */
            memset(&msg, 0, sizeof(msg));
            msg.type    = MSG_CHALLENGE;
            msg.game_id = g->id;
            strncpy(msg.username, username, MAX_USERNAME - 1);
            send_message(g->player_fd[0], &msg);

            pthread_mutex_unlock(&g->lock);
            break;
        }

        case MSG_ACCEPT: {
            game_t *g = find_and_lock_game(msg.game_id);
            if (!g) {
                send_error(fd, "Partita non trovata");
                break;
            }
            /* g->lock gia' acquisito */
            if (g->player_fd[0] != fd) {
                pthread_mutex_unlock(&g->lock);
                send_error(fd, "Non sei il creatore");
                break;
            }

            /* Lo sfidante si e' disconnesso nel frattempo?
             * handle_disconnect ha riportato state a GAME_WAITING */
            if (g->state == GAME_WAITING) {
                pthread_mutex_unlock(&g->lock);
                /* il creatore torna in attesa nel game_loop.
                 * Invia MSG_WAIT_TURN per notificare che si torna in attesa */
                memset(&msg, 0, sizeof(msg));
                msg.type    = MSG_WAIT_TURN;
                msg.game_id = g->id;
                send_message(fd, &msg);
                break;
            }

            if (g->state != GAME_CHALLENGING) {
                pthread_mutex_unlock(&g->lock);
                send_error(fd, "Nessuna sfida pendente");
                break;
            }

            g->state = GAME_PLAYING;
            printf("[SERVER] %s ha accettato la sfida di %s (partita #%d)\n",
                   g->player_name[0], g->player_name[1], g->id);

            /* Conferma join allo sfidante */
            memset(&msg, 0, sizeof(msg));
            msg.type    = MSG_JOIN_OK;
            msg.game_id = g->id;
            send_message(g->player_fd[1], &msg);

            /* Invia board iniziale e notifica turni */
            send_board(g, 0);
            send_board(g, 1);
            send_turn_notifications(g);

            pthread_mutex_unlock(&g->lock);
            break;
        }

        case MSG_REJECT: {
            game_t *g = find_and_lock_game(msg.game_id);
            if (!g) {
                send_error(fd, "Partita non trovata");
                break;
            }
            /* g->lock gia' acquisito */
            if (g->player_fd[0] != fd) {
                pthread_mutex_unlock(&g->lock);
                send_error(fd, "Non sei il creatore");
                break;
            }

            /* Lo sfidante si e' gia' disconnesso? Torna silenziosamente in attesa */
            if (g->state == GAME_WAITING) {
                pthread_mutex_unlock(&g->lock);
                memset(&msg, 0, sizeof(msg));
                msg.type    = MSG_WAIT_TURN;
                msg.game_id = g->id;
                send_message(fd, &msg);
                break;
            }

            if (g->state != GAME_CHALLENGING) {
                pthread_mutex_unlock(&g->lock);
                send_error(fd, "Nessuna sfida pendente");
                break;
            }

            printf("[SERVER] %s ha rifiutato la sfida di %s (partita #%d)\n",
                   g->player_name[0], g->player_name[1], g->id);

            /* Notifica rifiuto allo sfidante */
            memset(&msg, 0, sizeof(msg));
            msg.type    = MSG_REJECTED;
            msg.game_id = g->id;
            send_message(g->player_fd[1], &msg);

            /* Ripristina la partita in attesa */
            g->player_fd[1] = -1;
            memset(g->player_name[1], 0, MAX_USERNAME);
            g->state = GAME_WAITING;

            pthread_mutex_unlock(&g->lock);
            break;
        }


        case MSG_MOVE: {
            game_t *g = find_and_lock_game(msg.game_id);
            if (!g) {
                send_error(fd, "Partita non trovata");
                break;
            }

            if (g->state != GAME_PLAYING) {
                pthread_mutex_unlock(&g->lock);
                send_error(fd, "Partita non in corso");
                break;
            }

            /* Verifica che il client sia effettivamente in questa partita */
            int player_idx = get_player_index(g, fd);
            if (player_idx < 0) {
                pthread_mutex_unlock(&g->lock);
                send_error(fd, "Non sei in questa partita");
                break;
            }
            if (player_idx != g->current_turn) {
                pthread_mutex_unlock(&g->lock);
                send_error(fd, "Non e' il tuo turno");
                break;
            }

            int disc = (player_idx == 0) ? PLAYER1 : PLAYER2;
            int row = drop_disc(g->board, msg.column, disc);
            if (row < 0) {
                pthread_mutex_unlock(&g->lock);
                send_error(fd, "Colonna piena/invalida");
                break;
            }

            printf("[SERVER] Partita #%d: %s -> colonna %d\n",
                   g->id, username, msg.column);

            send_board(g, 0);
            send_board(g, 1);

            if (check_win(g->board, disc)) {
                message_t res;
                memset(&res, 0, sizeof(res));
                res.type    = MSG_GAME_OVER;
                res.game_id = g->id;
                memcpy(res.board, g->board, sizeof(g->board));

                res.outcome = OUTCOME_WIN;
                send_message(g->player_fd[player_idx], &res);
                res.outcome = OUTCOME_LOSE;
                send_message(g->player_fd[1 - player_idx], &res);

                int finished_id = g->id;
                printf("[SERVER] Partita #%d: %s ha vinto!\n", finished_id, username);
                pthread_mutex_unlock(&g->lock);
                remove_game(finished_id);
            }
            else if (board_full(g->board)) {
                message_t res;
                memset(&res, 0, sizeof(res));
                res.type    = MSG_GAME_OVER;
                res.game_id = g->id;
                res.outcome = OUTCOME_DRAW;
                memcpy(res.board, g->board, sizeof(g->board));

                send_message(g->player_fd[0], &res);
                send_message(g->player_fd[1], &res);

                int finished_id = g->id;
                printf("[SERVER] Partita #%d: pareggio!\n", finished_id);
                pthread_mutex_unlock(&g->lock);
                remove_game(finished_id);
            }
            else {
                g->current_turn = 1 - g->current_turn;
                send_turn_notifications(g);
                pthread_mutex_unlock(&g->lock);
            }

            break;
        }

        case MSG_QUIT:
            goto cleanup;

        default:
            send_error(fd, "Comando sconosciuto");
            break;
        }
    }

cleanup:
    printf("[SERVER] %s disconnesso (fd=%d)\n", username, fd);
    handle_disconnect_from_game(fd);
    remove_client(fd);
    close(fd);

thread_exit:
    pthread_mutex_lock(&thread_count_lock);
    active_threads--;
    pthread_cond_signal(&thread_count_cond);
    pthread_mutex_unlock(&thread_count_lock);
    return NULL;
}
