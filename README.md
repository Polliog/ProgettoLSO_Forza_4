# Forza 4 - Client/Server

Progetto per l'esame di **Laboratorio di Sistemi Operativi** (Vecchio Ordinamento).

Sistema Client-Server per il gioco Forza 4 (Connect Four), implementato in C con socket TCP e thread POSIX.

## Struttura del progetto

```
ProgettoLSO/
├── shared/
│   └── protocol.h              # Protocollo di comunicazione e strutture condivise
├── server/
│   ├── server.h                # Strutture dati, variabili globali, prototipi
│   ├── server.c                # Main: socket, accept loop, shutdown
│   ├── game.c                  # Logica Forza 4, gestione partite, disconnessione
│   └── client_handler.c        # Thread client, login, comandi, validazione
├── client/
│   ├── client.h                # Prototipi condivisi tra i file client
│   ├── client.c                # Main: connessione, login, menu
│   ├── ui.c                    # Stampa griglia e menu
│   └── game_loop.c             # Loop di gioco e attesa avversario
├── compile.sh                  # Script di compilazione
├── CMakeLists.txt              # Build system CMake
├── README.md
└── documentazione.pdf
```

## Compilazione

Requisiti: `gcc` e libreria `pthread` (Linux).

```bash
chmod +x compile.sh
bash compile.sh
```

Oppure manualmente:

```bash
gcc -Wall -Wextra -pthread -o server/forza4_server server/server.c server/game.c server/client_handler.c
gcc -Wall -Wextra -o client/forza4_client client/client.c client/ui.c client/game_loop.c
```

## Esecuzione

### Avviare il server

```bash
./server/forza4_server [porta]
```

Porta di default: `9090`.

### Avviare il client

```bash
./client/forza4_client [ip_server] [porta]
```

Default: `127.0.0.1:9090`.

### Esempio di gioco

1. Avviare il server in un terminale
2. Avviare due client in due terminali separati
3. Client 1: effettua login, seleziona "1) Crea nuova partita"
4. Client 2: effettua login, seleziona "2) Lista partite", poi "3) Unisciti"
5. Client 1: riceve la notifica di sfida e sceglie se accettare o rifiutare
6. Se accettata, la partita inizia: i giocatori inseriscono colonne (0-6) a turno

## Architettura

- **Protocollo TCP** (`AF_INET`, `SOCK_STREAM`) con messaggi a formato fisso (`message_t`)
- **Server parallelo**: un `pthread` per ogni client connesso (thread detached)
- **Sincronizzazione**: `pthread_mutex` sulla lista partite e su ogni singola partita
- **Gestione stati**: `WAITING` -> `CHALLENGING` -> `PLAYING` -> `FINISHED`
- **Disconnessione**: il server rileva la chiusura della socket e notifica l'avversario
- **Shutdown graceful**: `Ctrl+C` sul server chiude tutte le connessioni; `Ctrl+C` sul client invia `MSG_QUIT`

## Sicurezza e robustezza

- **Validazione input**: username sanitizzato (solo `[a-zA-Z0-9_]`), colonne validate, tipi messaggio whitelistati
- **Identita' giocatore**: ogni mossa verifica che il client sia effettivamente nella partita
- **Anti-flood**: limite di 5 partite attive per client
- **Race condition**: `find_and_lock_game` atomica previene use-after-free tra ricerca e lock
- **Gestione disconnessione**: ogni stato (WAITING, CHALLENGING, PLAYING, FINISHED) gestito separatamente con cleanup corretto
- **Shutdown sicuro**: il server chiude i socket client, attende la terminazione di tutti i thread, poi libera la memoria
- **Memoria**: le partite vengono liberate immediatamente alla fine (vittoria/pareggio/abbandono)

## Protocollo messaggi

| Messaggio         | Direzione | Descrizione                          |
|-------------------|-----------|--------------------------------------|
| `MSG_LOGIN`       | C -> S    | Invio username                       |
| `MSG_LOGIN_OK`    | S -> C    | Login confermato                     |
| `MSG_CREATE_GAME` | C -> S    | Crea nuova partita                   |
| `MSG_GAME_CREATED`| S -> C    | Partita creata (con game_id)         |
| `MSG_LIST_GAMES`  | C -> S    | Richiedi lista partite disponibili   |
| `MSG_GAME_LIST`   | S -> C    | Elenco partite in attesa             |
| `MSG_JOIN_GAME`   | C -> S    | Richiedi di unirti a una partita     |
| `MSG_CHALLENGE`   | S -> C    | Sfida in arrivo per il creatore      |
| `MSG_ACCEPT`      | C -> S    | Creatore accetta la sfida            |
| `MSG_REJECT`      | C -> S    | Creatore rifiuta la sfida            |
| `MSG_JOIN_OK`     | S -> C    | Unione confermata                    |
| `MSG_REJECTED`    | S -> C    | Sfida rifiutata                      |
| `MSG_MOVE`        | C -> S    | Mossa (colonna 0-6)                  |
| `MSG_BOARD_UPDATE`| S -> C    | Griglia aggiornata                   |
| `MSG_YOUR_TURN`   | S -> C    | Tocca a te                           |
| `MSG_WAIT_TURN`   | S -> C    | Attendi turno avversario             |
| `MSG_GAME_OVER`   | S -> C    | Partita terminata (vittoria/pareggio)|
| `MSG_OPPONENT_LEFT`| S -> C   | Avversario disconnesso               |
| `MSG_ERROR`       | S -> C    | Errore generico                      |
| `MSG_QUIT`        | C -> S    | Disconnessione volontaria            |
