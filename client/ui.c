#include <stdio.h>

#include "client.h"

void print_board(int board[ROWS][COLS])
{
    printf("\n");
    for (int r = 0; r < ROWS; r++) {
        printf("  |");
        for (int c = 0; c < COLS; c++) {
            char ch = ' ';
            if (board[r][c] == PLAYER1) ch = 'X';
            else if (board[r][c] == PLAYER2) ch = 'O';
            printf(" %c |", ch);
        }
        printf("\n");
    }
    printf("  +");
    for (int c = 0; c < COLS; c++) printf("---+");
    printf("\n   ");
    for (int c = 0; c < COLS; c++) printf(" %d  ", c);
    printf("\n\n");
}

void print_menu(void)
{
    printf("=== Menu ===\n");
    printf("1) Crea nuova partita\n");
    printf("2) Lista partite disponibili\n");
    printf("3) Unisciti a una partita\n");
    printf("4) Esci\n");
    printf("Scelta: ");
}
