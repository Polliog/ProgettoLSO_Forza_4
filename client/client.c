#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

#include "client.h"

/* ── Variabile globale per shutdown graceful ── */
int g_sockfd = -1;

static void sigint_handler(int sig)
{
    (void)sig;
    if (g_sockfd >= 0) {
        message_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = MSG_QUIT;
        send_message(g_sockfd, &msg);
        close(g_sockfd);
        g_sockfd = -1;
    }
    printf("\nDisconnesso.\n");
    _exit(0);
}

int main(int argc, char *argv[])
{
    const char *server_ip = "127.0.0.1";
    int port = DEFAULT_PORT;

    if (argc >= 2) server_ip = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    g_sockfd = sockfd;

    /* Gestione Ctrl+C */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Indirizzo non valido: %s\n", server_ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connesso al server %s:%d\n", server_ip, port);


    char username[MAX_USERNAME];
    printf("Inserisci il tuo username: ");
    fflush(stdout);
    if (scanf("%31s", username) != 1) {
        fprintf(stderr, "Errore lettura username\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF);

    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_LOGIN;
    strncpy(msg.username, username, MAX_USERNAME - 1);

    if (send_message(sockfd, &msg) < 0) {
        fprintf(stderr, "Errore invio login\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (recv_message(sockfd, &msg) < 0 || msg.type != MSG_LOGIN_OK) {
        fprintf(stderr, "Login fallito\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Login riuscito! Benvenuto, %s.\n\n", username);

    int running = 1;
    while (running) {
        print_menu();

        int choice;
        if (scanf("%d", &choice) != 1) {
            while ((ch = getchar()) != '\n' && ch != EOF);
            printf("Input non valido.\n");
            continue;
        }
        while ((ch = getchar()) != '\n' && ch != EOF);

        switch (choice) {

        case 1: {
            memset(&msg, 0, sizeof(msg));
            msg.type = MSG_CREATE_GAME;
            if (send_message(sockfd, &msg) < 0) {
                printf("Errore comunicazione.\n");
                running = 0;
                break;
            }
            if (recv_message(sockfd, &msg) < 0) {
                printf("Errore comunicazione.\n");
                running = 0;
                break;
            }
            if (msg.type == MSG_GAME_CREATED) {
                printf("Partita #%d creata!\n", msg.game_id);
                wait_for_opponent(sockfd, msg.game_id);
            } else if (msg.type == MSG_ERROR) {
                printf("Errore: %s\n", msg.username);
            }
            break;
        }

        case 2: {
            memset(&msg, 0, sizeof(msg));
            msg.type = MSG_LIST_GAMES;
            if (send_message(sockfd, &msg) < 0) {
                printf("Errore comunicazione.\n");
                running = 0;
                break;
            }
            if (recv_message(sockfd, &msg) < 0) {
                printf("Errore comunicazione.\n");
                running = 0;
                break;
            }
            if (msg.type == MSG_GAME_LIST) {
                if (msg.game_count == 0) {
                    printf("Nessuna partita in attesa.\n");
                } else {
                    printf("\nPartite disponibili:\n");
                    for (int i = 0; i < msg.game_count; i++) {
                        printf("  #%d - creata da %s\n",
                               msg.game_ids[i], msg.game_creators[i]);
                    }
                    printf("\n");
                }
            }
            break;
        }

        case 3: {
            printf("Inserisci ID partita: ");
            fflush(stdout);
            int gid;
            if (scanf("%d", &gid) != 1) {
                while ((ch = getchar()) != '\n' && ch != EOF);
                printf("ID non valido.\n");
                break;
            }

            memset(&msg, 0, sizeof(msg));
            msg.type    = MSG_JOIN_GAME;
            msg.game_id = gid;
            if (send_message(sockfd, &msg) < 0) {
                printf("Errore comunicazione.\n");
                running = 0;
                break;
            }

            printf("In attesa che il creatore accetti la sfida...\n");
            if (recv_message(sockfd, &msg) < 0) {
                printf("Errore comunicazione.\n");
                running = 0;
                break;
            }
            if (msg.type == MSG_JOIN_OK) {
                printf("Sfida accettata! Partita in corso...\n");
                game_loop(sockfd, gid);
            } else if (msg.type == MSG_REJECTED) {
                printf("Il creatore ha rifiutato la sfida.\n");
            } else if (msg.type == MSG_ERROR) {
                printf("Errore: %s\n", msg.username);
            }
            break;
        }

        case 4:
            memset(&msg, 0, sizeof(msg));
            msg.type = MSG_QUIT;
            send_message(sockfd, &msg);
            running = 0;
            break;

        default:
            printf("Scelta non valida.\n");
            break;
        }
    }

    close(sockfd);
    g_sockfd = -1;
    printf("Arrivederci!\n");
    return 0;
}
