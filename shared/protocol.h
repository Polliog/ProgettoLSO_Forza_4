#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>

#define ROWS 6
#define COLS 7

#define DEFAULT_PORT 9090
#define MAX_USERNAME 32
#define MAX_MSG_LEN 512

#define EMPTY  0
#define PLAYER1 1
#define PLAYER2 2

typedef enum {
    MSG_LOGIN,          /* C→S  username                         */
    MSG_LOGIN_OK,       /* S→C  login accettato                  */
    MSG_CREATE_GAME,    /* C→S  richiesta creazione partita      */
    MSG_GAME_CREATED,   /* S→C  partita creata (game_id)         */
    MSG_LIST_GAMES,     /* C→S  richiesta lista partite          */
    MSG_GAME_LIST,      /* S→C  elenco partite in attesa         */
    MSG_JOIN_GAME,      /* C→S  unisciti a partita (game_id)     */
    MSG_CHALLENGE,      /* S→C  sfida in arrivo (username sfidante) */
    MSG_ACCEPT,         /* C→S  accetta la sfida                 */
    MSG_REJECT,         /* C→S  rifiuta la sfida                 */
    MSG_JOIN_OK,        /* S→C  unione riuscita                  */
    MSG_REJECTED,       /* S→C  sfida rifiutata dal creatore     */
    MSG_MOVE,           /* C→S  mossa (colonna)                  */
    MSG_BOARD_UPDATE,   /* S→C  stato aggiornato della griglia   */
    MSG_YOUR_TURN,      /* S→C  tocca a te                       */
    MSG_WAIT_TURN,      /* S→C  attendi il turno avversario      */
    MSG_GAME_OVER,      /* S→C  partita terminata (esito)        */
    MSG_OPPONENT_LEFT,  /* S→C  avversario disconnesso           */
    MSG_ERROR,          /* S→C  errore generico                  */
    MSG_QUIT            /* C→S  disconnessione volontaria        */
} msg_type_t;

typedef enum {
    OUTCOME_WIN,
    OUTCOME_LOSE,
    OUTCOME_DRAW
} outcome_t;

typedef enum {
    GAME_WAITING,       /* in attesa di uno sfidante  */
    GAME_CHALLENGING,   /* sfida inviata, attesa risposta creatore */
    GAME_PLAYING,       /* partita in corso           */
    GAME_FINISHED       /* partita terminata          */
} game_state_t;

typedef struct {
    msg_type_t type;
    int        game_id;
    int        column;                   /* colonna mossa (0-6)        */
    outcome_t  outcome;
    int        board[ROWS][COLS];        /* griglia completa           */
    char       username[MAX_USERNAME];
    int        game_count;               /* numero partite nella lista */
    int        game_ids[20];             /* id partite in attesa       */
    char       game_creators[20][MAX_USERNAME]; /* creatori partite    */
} message_t;


/*
 * Invia esattamente `len` byte sul descrittore `fd`.
 * Ritorna 0 in caso di successo, -1 in caso di errore.
 */
static inline int send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return -1;
        p   += n;
        len -= (size_t)n;
    }
    return 0;
}

/*
 * Ricevi esattamente `len` byte dal descrittore `fd`.
 * Ritorna 0 in caso di successo, -1 in caso di errore/disconnessione.
 */
static inline int recv_all(int fd, void *buf, size_t len)
{
    char *p = (char *)buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n <= 0) return -1;
        p   += n;
        len -= (size_t)n;
    }
    return 0;
}

static inline int send_message(int fd, const message_t *msg)
{
    return send_all(fd, msg, sizeof(message_t));
}

static inline int recv_message(int fd, message_t *msg)
{
    return recv_all(fd, msg, sizeof(message_t));
}

#endif
