#include <stdio.h>
#include <string.h>

#include "client.h"

int game_loop(int sockfd, int game_id)
{
    message_t msg;
    int my_turn = 0;

    while (1) {
        if (recv_message(sockfd, &msg) < 0) {
            printf("Connessione persa.\n");
            return -1;
        }

        switch (msg.type) {

        case MSG_BOARD_UPDATE:
            print_board(msg.board);
            break;

        case MSG_YOUR_TURN:
            my_turn = 1;
            printf("Tocca a te! Inserisci colonna (0-%d): ", COLS - 1);
            fflush(stdout);
            {
                int col;
                if (scanf("%d", &col) != 1) {
                    int ch;
                    while ((ch = getchar()) != '\n' && ch != EOF);
                    col = -1;
                }
                memset(&msg, 0, sizeof(msg));
                msg.type    = MSG_MOVE;
                msg.game_id = game_id;
                msg.column  = col;
                if (send_message(sockfd, &msg) < 0) {
                    printf("Errore invio mossa.\n");
                    return -1;
                }
            }
            my_turn = 0;
            break;

        case MSG_WAIT_TURN:
            printf("In attesa della mossa dell'avversario...\n");
            break;

        /* ── Sfida in arrivo (solo per il creatore in attesa) ── */
        case MSG_CHALLENGE:
            printf("\n*** %s vuole unirsi alla partita #%d! ***\n",
                   msg.username, msg.game_id);
            printf("Accettare? (s/n): ");
            fflush(stdout);
            {
                char risposta[8];
                if (scanf("%7s", risposta) != 1) {
                    int ch;
                    while ((ch = getchar()) != '\n' && ch != EOF);
                    risposta[0] = 'n';
                }

                memset(&msg, 0, sizeof(msg));
                msg.game_id = game_id;

                if (risposta[0] == 's' || risposta[0] == 'S' ||
                    risposta[0] == 'y' || risposta[0] == 'Y') {
                    msg.type = MSG_ACCEPT;
                    printf("Sfida accettata! Partita in corso...\n");
                } else {
                    msg.type = MSG_REJECT;
                    printf("Sfida rifiutata. In attesa di un altro sfidante...\n");
                }

                if (send_message(sockfd, &msg) < 0) {
                    printf("Errore comunicazione.\n");
                    return -1;
                }

                /* Se rifiutata, rimaniamo in attesa nel game_loop */
                if (msg.type == MSG_REJECT) break;
            }
            break;

        case MSG_GAME_OVER:
            print_board(msg.board);
            switch (msg.outcome) {
            case OUTCOME_WIN:  printf("*** HAI VINTO! ***\n");   break;
            case OUTCOME_LOSE: printf("*** Hai perso. ***\n");   break;
            case OUTCOME_DRAW: printf("*** Pareggio! ***\n");    break;
            }
            return 0;

        case MSG_OPPONENT_LEFT:
            printf("L'avversario si e' disconnesso. Vittoria a tavolino!\n");
            return 0;

        case MSG_ERROR:
            printf("Errore dal server: %s\n", msg.username);
            if (my_turn) {
                printf("Riprova colonna (0-%d): ", COLS - 1);
                fflush(stdout);
                int col;
                if (scanf("%d", &col) != 1) {
                    int ch;
                    while ((ch = getchar()) != '\n' && ch != EOF);
                    col = -1;
                }
                memset(&msg, 0, sizeof(msg));
                msg.type    = MSG_MOVE;
                msg.game_id = game_id;
                msg.column  = col;
                if (send_message(sockfd, &msg) < 0) return -1;
            }
            break;

        default:
            printf("Messaggio inatteso (type=%d)\n", msg.type);
            break;
        }
    }
}

int wait_for_opponent(int sockfd, int game_id)
{
    printf("In attesa di un avversario per la partita #%d...\n", game_id);
    return game_loop(sockfd, game_id);
}
