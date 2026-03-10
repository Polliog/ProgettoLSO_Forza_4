#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>

#include "server.h"

game_t   *game_list   = NULL;
client_t *client_list = NULL;
int        next_game_id = 1;

pthread_mutex_t game_list_lock   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_list_lock = PTHREAD_MUTEX_INITIALIZER;

volatile sig_atomic_t server_running = 1;
int server_fd_global = -1;

int active_threads = 0;
pthread_mutex_t thread_count_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  thread_count_cond = PTHREAD_COND_INITIALIZER;

static void sigint_handler(int sig)
{
    (void)sig;
    server_running = 0;
    /* Chiudi la socket del server per sbloccare accept() */
    if (server_fd_global >= 0) {
        close(server_fd_global);
        server_fd_global = -1;
    }
}

static void cleanup_all(void)
{
    /* chiudi tutti gli fd client per sbloccare i thread in recv() */
    pthread_mutex_lock(&client_list_lock);
    client_t *c = client_list;
    while (c) {
        shutdown(c->fd, SHUT_RDWR);
        c = c->next;
    }
    pthread_mutex_unlock(&client_list_lock);

    /* aspetta che tutti i thread client terminino */
    pthread_mutex_lock(&thread_count_lock);
    while (active_threads > 0) {
        printf("[SERVER] In attesa di %d thread...\n", active_threads);
        pthread_cond_wait(&thread_count_cond, &thread_count_lock);
    }
    pthread_mutex_unlock(&thread_count_lock);

    /* ora i thread sono tutti usciti, possiamo liberare la memoria */
    pthread_mutex_lock(&client_list_lock);
    c = client_list;
    while (c) {
        client_t *next = c->next;
        free(c);
        c = next;
    }
    client_list = NULL;
    pthread_mutex_unlock(&client_list_lock);

    pthread_mutex_lock(&game_list_lock);
    game_t *g = game_list;
    while (g) {
        game_t *next = g->next;
        pthread_mutex_destroy(&g->lock);
        free(g);
        g = next;
    }
    game_list = NULL;
    pthread_mutex_unlock(&game_list_lock);
}


int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;
    if (argc >= 2) port = atoi(argv[1]);

    signal(SIGPIPE, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Creazione socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    server_fd_global = server_fd;

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("=== Server Forza 4 avviato sulla porta %d ===\n", port);
    printf("    Premi Ctrl+C per arrestare il server.\n");

    /* Accept loop */
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t clen = sizeof(client_addr);

        int *client_fd = malloc(sizeof(int));
        if (!client_fd) { perror("malloc"); continue; }

        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &clen);
        if (*client_fd < 0) {
            free(client_fd);
            if (!server_running) break;
            perror("accept");
            continue;
        }

        printf("[SERVER] Nuova connessione da %s:%d (fd=%d)\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port), *client_fd);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, client_fd) != 0) {
            perror("pthread_create");
            close(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(tid);
    }

    printf("\n[SERVER] Arresto in corso...\n");
    cleanup_all();
    printf("[SERVER] Server arrestato.\n");
    return 0;
}
