#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/uaccess.h>  
#include <linux/random.h>

// declaring my device and class name for my driver as well as board size and empty piece
#define DEVICE_NAME "chess"
#define CLASS_NAME "game"
#define BOARD_SIZE 8
#define EMPTY 0

// enums for my pieces, will be storing in int array
enum pieces {
    PAWN = 1,
    KNIGHT = 2,
    BISHOP = 3,
    ROOK = 4,
    QUEEN = 5,
    KING = 6
};

// chess game struct to populate board, store locations of both kings, check player turn and if player is in check
struct chess_game {
    int board[BOARD_SIZE][BOARD_SIZE];
    int white_king[2];  
    int black_king[2];
    int current_turn;  
    bool check;  
};

// struct that holds cpu moves so infinite loop does not occur
struct cpu_move {
    int start_row;
    int start_col;
    int end_row;
    int end_col;
};

// global variables for driver as well as user input and cpu/player color/checkmate and if game has been initialized
static int num;
static struct class* chessClass = NULL;
static struct device* chessDevice = NULL;
static char message[256] = {0}; 
static char player[2] = {0}; 
static char cpu[2] = {0};
static size_t size = 0;  
static struct chess_game game;
static bool game_init = false;
static bool checkmate = false;

// functions critical for module as well as helper functions for game
static int dev_open(struct inode *, struct file *); // opens module
static int dev_release(struct inode *, struct file *); // closes module
static ssize_t dev_read(struct file *, char *, size_t, loff_t *); // reads from user input
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *); // writes to user input
void board_init(void); // initializes chess board
bool legal_move(int start_row, int start_col, int end_row, int end_col, int piece, char arr[4], char arr2[4]); // checks if user input legal
bool clear_path(int start_row, int start_col, int end_row, int end_col, char arr[4]); // checks if path is clear (helper for legal_move)
void piece_to_char(int piece, char *buf); // converts int piece to char character for printing
void board_state(void); // helper to print current state of board
int display_piece(char* piece_type); // displays the chess piece
void perform_move(int start_row, int start_col, int end_row, int end_col, int piece); // if it is legal performs the move
bool valid_opponent_piece(int row, int col, int piece, char *array, int size); // checks if x[PIECE] is valid
int char_check(char c, const char *string); // checks if command is valid
bool king_check(struct chess_game *game, int king_row, int king_col, char color); // checks if king is in check
bool is_checkmate(struct chess_game *game, char arr[4], char arr2[4]); // checks if player or cpu is in checkmate (given turn)
bool cpu_clear_path(int start_row, int start_col, int end_row, int end_col); // cpu algorithm for checking clear path
void cpu_move(struct chess_game *game); // cpu algorithm for moving piece
bool cpu_checkmate(struct chess_game *game); // cpu algorithm for checking if in checkmate
bool cpu_legal_move(int start_row, int start_col, int end_row, int end_col, int piece); // cpu algorithm which verifies cpu legal move


// declares the pointers for module operations (read, write, open, release)
static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

// initializes the driver
static int __init chess_init(void) {
    printk(KERN_INFO "initializing chess\n");
    // initializing device
    num = register_chrdev(0, DEVICE_NAME, &fops);
    printk(KERN_INFO "num: %d\n", num);
    if (num < 0) {
        printk(KERN_ALERT "Chess failed to register num\n");
        return num;
    }

    // initializing class
    chessClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(chessClass)) {
        unregister_chrdev(num, DEVICE_NAME);
        printk(KERN_ALERT "Failed to register device class\n");
        return PTR_ERR(chessClass);
    }

    // creating the device
    chessDevice = device_create(chessClass, NULL, MKDEV(num, 0), NULL, DEVICE_NAME);
    if (IS_ERR(chessDevice)) {
        class_destroy(chessClass);
        unregister_chrdev(num, DEVICE_NAME);
        printk(KERN_ALERT "Failed to create the device\n");
        return PTR_ERR(chessDevice);
    }

    printk(KERN_INFO "Chess: Device class created correctly\n");
    return 0;
}

// destructing device and class and unregistering driver
static void __exit chess_exit(void) {
    device_destroy(chessClass, MKDEV(num, 0));
    class_destroy(chessClass);
    unregister_chrdev(num, DEVICE_NAME);
    printk(KERN_INFO "exiting chess\n");
}

// opening driver
static int dev_open(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "Chess device is open\n");
    return 0;
}

// closing driver
static int dev_release(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "Chess device is closed\n");
    return 0;
}

// outputs message read by driver to the user
static ssize_t dev_read(struct file *filep, char *buffer, size_t length, loff_t *offset) {
    size_t bytes_to_read = size - *offset;

    // if no bytes to read returns 0
    if (length > bytes_to_read)
        length = bytes_to_read;  
    else if (bytes_to_read == 0) 
        return 0;

    if (copy_to_user(buffer, message + *offset, length) == 0) {
        *offset += length;  
        return length;  
    }else 
        return -EFAULT;  
}

// writes from user input
static ssize_t dev_write(struct file *filep, const char *buffer, size_t length, loff_t *offset) {
    // initializing variables needed
    int start_row;
    int start_col;
    int end_row;
    int end_col;
    char cmd[256];
    char piece;

    // avoiding buffer overflow
    if (length > 255) 
        length = 255;

    // checking if successful in receiving user input and null terminating it else returning error
    if (copy_from_user(cmd, buffer, length))
        return -EFAULT;
    else
        cmd[length] = '\0'; 
    
    printk(KERN_INFO "Chess command: %s\n", cmd);

    // switch cases for different command types: 00, 01, 02, 03, 04
    switch (cmd[0]) {
        case '0':
            if (cmd[1] == '0') {  
                // initializing player and cpu colors and constructing board and variables        
                sscanf(cmd + 2, "%1s", player);
                if (player[0] == 'W')
                    cpu[0] = 'B';
                else if (player[0] == 'B')
                    cpu[0] = 'W';
                board_init();  
                strcpy(message, "New game\n");
                size = strlen(message);
            } else if (cmd[1] == '1') {  
                // if game has no been initialized OR checkmate this command will not show current board state
                // else, prints current state of the board              
                if (game_init == false){
                    strcpy(message, "NOGAME\n");
                    size = strlen(message);
                    break;
                }
                board_state();  
                if (checkmate == true){
                    strcpy(message, "MATE\n");
                    size = strlen(message);
                    break;
                }
                size = strlen(message);
            } else if (cmd[1] == '2') { 
                // initiailizing all variables needed
                struct chess_game temp;
                char start_pos[3] = {0};
                char end_pos[3] = {0};
                char piece_type[3] = {0}; 
                char action1[4] = {0};
                char action2[4] = {0};
                int match_piece;
                char *str = "abcdefgh";
                char *numbers = "12345678";

                // if game has not been initialized OR checkmate, no plays will be made
                // also checks if player is trying to play out of turn
                if (game_init == false){
                    strcpy(message, "NOGAME\n");
                    size = strlen(message);
                    break;
                }
                if (checkmate == true){
                    strcpy(message, "MATE\n");
                    size = strlen(message);
                    break;
                }
                if (player[0] == 'W' && game.current_turn == -1){
                    strcpy(message, "OOT\n");
                    size = strlen(message);
                    break;
                }

                if (player[0] == 'B' && game.current_turn == 1){
                    strcpy(message, "OOT\n");
                    size = strlen(message);
                    break;
                }

                sscanf(cmd + 2, "%2s%2s-%2s%3s%3s", piece_type, start_pos, end_pos, action1, action2);

                // checking if command has invalid format
                if (!char_check(start_pos[0], str) || !char_check(start_pos[1], numbers) || !char_check(end_pos[0], str) || 
                !char_check(end_pos[1], numbers)){
                    strcpy(message, "INVFMT\n");
                    size = strlen(message);
                    return length;
                }

                // checking if player is moving opponent's piece
                if (piece_type[0] != player[0]){
                    strcpy(message, "ILLMOVE\n");
                    size = strlen(message);
                    break;
                }

                // converting character to int for piece
                switch (piece_type[1]) {
                    case 'P': 
                        match_piece = 1;
                        break;
                    case 'N': 
                        match_piece = 2;
                        break;
                    case 'B': 
                        match_piece = 3;
                        break;
                    case 'R': 
                        match_piece = 4;
                        break;
                    case 'Q': 
                        match_piece = 5;
                        break;
                    case 'K': 
                        match_piece = 6;
                        break;
                    default:
                        printk(KERN_WARNING "Invalid piece: %s\n", piece_type);
                        return 0; 
                }

                // translating user input coords to int coords. checking if W or B player first and if it matches the user's input
                if (piece_type[0] == 'W'){
                    start_row = start_pos[1] - '0' - 1; 
                    start_col = start_pos[0] - 'a';      
                    end_row = end_pos[1] - '0' - 1;    
                    end_col = end_pos[0] - 'a'; 

                    if (game.board[start_row][start_col] != match_piece * 1){
                        strcpy(message, "ILLMOVE\n");
                        size = strlen(message);
                        break;
                    }  
                }else if (piece_type[0] == 'B'){
                    start_row = (start_pos[1] - '0') - 1; 
                    start_col = start_pos[0] - 'a';      
                    end_row = (end_pos[1] - '0') - 1;    
                    end_col = end_pos[0] - 'a';  

                    if (game.board[start_row][start_col] != match_piece * -1){
                        strcpy(message, "ILLMOVE\n");
                        size = strlen(message);
                        break;
                    }  
                    printk(KERN_INFO "black piece %d%d,%d%d\n", start_row, start_col, end_row, end_col);
                }
                          
                piece = display_piece(piece_type);

                // checking if user move is legal and performing the move, else return ILLMOVE
                // also checking for check or checkmate
                if (legal_move(start_row, start_col, end_row, end_col, piece, action1, action2)) {
                    temp = game;
                    perform_move(start_row, start_col, end_row, end_col, game.board[start_row][start_col]);
                    if (game.check == true && !is_checkmate(&temp, action1, action2)){
                        strcpy(message, "CHECK\n");
                        size = strlen(message);
                    }else if (game.check == false && !is_checkmate(&temp, action1, action2)){
                        // change to OK\n
                        strcpy(message, "Move executed\n");
                        size = strlen(message);  
                    }else if (is_checkmate(&temp, action1, action2)){
                        strcpy(message, "MATE\n");
                        size = strlen(message);
                        checkmate = true; 
                    }  
                }else{
                    strcpy(message, "ILLMOVE\n");
                    size = strlen(message);
                }
            }else if (cmd[1] == '3'){
                // cpu move, same conditions as 02 but much simpler
                // checking if game has been initialized OR checkmate or if cpu out of turn, else continue
                if (game_init == false){
                    strcpy(message, "NOGAME\n");
                    size = strlen(message);
                    break;
                }
                if (checkmate == true){
                    strcpy(message, "MATE\n");
                    size = strlen(message);
                    break;
                }
                if (cpu[0] == 'W' && game.current_turn == -1){
                    strcpy(message, "OOT\n");
                    size = strlen(message);
                    break;
                }
                if (cpu[0] == 'B' && game.current_turn == 1){
                    strcpy(message, "OOT\n");
                    size = strlen(message);
                    break;
                }
                // performing the move, then checking for checkmate or check
                cpu_move(&game);
                if (game.check == true && !cpu_checkmate(&game)){
                    strcpy(message, "CHECK\n");
                    size = strlen(message);
                }else if (game.check == false && !cpu_checkmate(&game)){
                    // change to OK\n
                    strcpy(message, "Move executed\n");
                    size = strlen(message);  
                }else if (cpu_checkmate(&game)){
                    strcpy(message, "MATE\n");
                    size = strlen(message);
                    checkmate = true;
                }  
            }else if (cmd[1] == '4'){
                // game is over, have to reinitialze board to play (00) and resetting board and variables
                // cannot do 04 is there is no game or mate, also making sure 04 is not called out of turn
                if (game_init == false){
                    strcpy(message, "NOGAME\n");
                    size = strlen(message);
                    break;
                }
                if (checkmate == true){
                    strcpy(message, "MATE\n");
                    size = strlen(message);
                    break;
                }

                if (player[0] == 'W' && game.current_turn == -1){
                    strcpy(message, "OOT\n");
                    size = strlen(message);
                    break;
                }

                if (player[0] == 'B' && game.current_turn == 1){
                    strcpy(message, "OOT\n");
                    size = strlen(message);
                    break;
                }
                board_init();
                game_init = false;
                strcpy(message, "OK\n");
                size = strlen(message);
            }
            break;
    }

    return length;
}

// initializing board with pieces and setting up variables
void board_init(void) {
    int i;
    game_init = true;
    memset(&game, 0, sizeof(game));  

    for (i = 0; i < BOARD_SIZE; i++) {
        game.board[1][i] = PAWN;  
        game.board[6][i] = -PAWN;  
    }

    game.board[0][0] = ROOK;
    game.board[0][7] = ROOK;    
    game.board[7][0] = -ROOK;
    game.board[7][7] = -ROOK;  

    game.board[0][1] = KNIGHT;
    game.board[0][6] = KNIGHT;  
    game.board[7][1] = -KNIGHT;
    game.board[7][6] = -KNIGHT; 

    game.board[0][2] = BISHOP;
    game.board[0][5] = BISHOP;  
    game.board[7][2] = -BISHOP;
    game.board[7][5] = -BISHOP; 

    game.board[0][3] = QUEEN;   
    game.board[7][3] = -QUEEN;  
    game.board[0][4] = KING;    
    game.board[7][4] = -KING;   

    game.white_king[0] = 4; 
    game.white_king[1] = 0; 
    game.black_king[0] = 4; 
    game.black_king[1] = 7; 

    game.current_turn = 1;
    game.check = false;
    checkmate = false;
}


// checking if path is clear for horizontal, vertical, and diagonal moves
bool clear_path(int start_row, int start_col, int end_row, int end_col, char arr[4]) {
    // declaring variables
    int row_direction;
    int col_direction;
    int i;
    int j;

    // checking direction of move by difference of end and start. if it is positive will go right/up if negative will go left/down
    if (end_row - start_row > 0)
        row_direction = 1;
    else
        row_direction = -1;

    if (end_col - start_col > 0)
        col_direction = 1;
    else
        col_direction = -1;

    // diagonal move, if it is by 1 and it is not empty and user does not do x[COMMAND] returns false else true
    if (start_row != end_row && start_col != end_col) { 
        if (abs(end_row - start_row) == 1 && abs(end_col - start_col) == 1) {
            if (game.board[end_row][end_col] != EMPTY && arr[0] != 'x')
                return false;
            return true;  
        }
        // checks diagonal directions (forwards and backwards, left to right depending on the directions) 
        // if space is not empty and it is not the last space, returns false else it means user WANTS to take opponent
        for (i = start_row + row_direction, j = start_col + col_direction; 
     (row_direction > 0 ? i <= end_row : i >= end_row) && (col_direction > 0 ? j <= end_col : j >= end_col); 
     i += row_direction, j += col_direction) {
                if (game.board[i][j] != EMPTY){
                    if (i == end_row && j == end_col) {
                        if (arr[0] == 'x') 
                            return true;  
                        return false;
                    }
                    return false;
                } 
        }
    // if moving vertically
    }else if (start_row != end_row) {
        // if difference is 1 and user does not do x[COMMAND] returns false else same conditions
        if (abs(end_row - start_row) == 1) {
            if (game.board[end_row][end_col] != EMPTY && arr[0] != 'x')
                return false;
            return true;  
        }
        // checking if path is clear else last space is not and user does do x[COMMAND] will return true else false
        if (end_row - start_row > 1 || end_row - start_row < -1){
            for (i = start_row + row_direction; row_direction == 1 ? i <= end_row : i >= end_row; i += row_direction) {
                printk(KERN_INFO "%d %d %d %c\n", game.board[i][start_col], i, end_row, arr[0]);
                
                if (game.board[i][start_col] != EMPTY) {
                    if (i == end_row) {
                        if (arr[0] == 'x') 
                            return true; 
                        return false;
                    }
                    return false;
                }
            }
        }
    // else moving horizontally
    }else if (start_col != end_col) {
        // same condition, only moving one space an dnot empty is user did not do x[COMMAND] return false else true
        if (abs(end_col - start_col) == 1) {
            if (game.board[end_row][end_col] != EMPTY && arr[0] != 'x')
                return false;
            return true;  
        }
        // else if is not  clear or last space is clear and user does x[COMMAND] return true else false
        if (end_col - start_col > 1 || end_col - start_col < -1){
            for (j = start_col + col_direction; col_direction == 1 ? j <= end_col : j >= end_col; j += col_direction) {
                printk(KERN_INFO "%d %d %d %c\n", game.board[start_row][j], j, end_col, arr[0]);

                if (game.board[start_row][j] != EMPTY) {
                    if (j == end_col) {
                        if (arr[0] == 'x')  
                            return true;  
                        return false;
                    }
                    return false;
                }
            }
        }
    }
    return true;
}

// checks if move is legal for given piece
bool legal_move(int start_row, int start_col, int end_row, int end_col, int piece, char arr[4], char arr2[4]) {
    bool beforeCheck = true;
    // for rook, only moves horizontally and vertically. if not clear path or incorrectlyy uses x OR y, returns false
    // checks for player putting themselves in check
    if (abs(piece) == ROOK) {
        if (start_row != end_row && start_col != end_col) 
            beforeCheck = false;
        else if (!clear_path(start_row, start_col, end_row, end_col, arr)) 
            beforeCheck = false;
        else if (clear_path(start_row, start_col, end_row, end_col, arr) && (arr[0] == 'y' || arr2[0] == 'y'))
            beforeCheck = false;
        else if (clear_path(start_row, start_col, end_row, end_col, arr) && arr[0] == 'x' && arr2[0] != 'y')
            beforeCheck = valid_opponent_piece(end_row, end_col, piece, &arr[1], 2); 
        if (beforeCheck){
            bool inCheck;
            int temp_piece = game.board[end_row][end_col];
            game.board[end_row][end_col] = game.board[start_row][start_col];
            game.board[start_row][start_col] = EMPTY;

            inCheck = false;
            if (piece == KING) 
                inCheck = king_check(&game, end_row, end_col, player[0]);
            else {
                int king_row; 
                int king_col; 
                if (player[0] == 'W'){
                    king_row = game.white_king[0];
                    king_col = game.white_king[1];
                }else if (player[0] == 'B'){
                    king_row = game.black_king[0];
                    king_col = game.black_king[1];
                }
                inCheck = king_check(&game, king_row, king_col, player[0]);
            }

            game.board[start_row][start_col] = game.board[end_row][end_col];
            game.board[end_row][end_col] = temp_piece;

            if (inCheck) {
                beforeCheck = false;
            }
        }
        return beforeCheck;
    // else if piece is pawn
    }else if (abs(piece) == PAWN) {
        int direction;
        bool isLegal;
        // gets direction of pawn
        if (piece > 0) 
            direction = 1;
        else
            direction = -1;

        // checks forward move of pawn, if it is clear, and if promotion commands are applied correctly
        if (start_col == end_col && end_row == start_row + direction) {
            isLegal = clear_path(start_row, start_col, end_row, end_col, arr);
            if (isLegal && ((end_row == 7 && player[0] == 'W') || (end_row == 0 && player[0] == 'B')) && arr[0] == 'y' && 
            arr[2] != 'P' && arr[2] != 'K' && arr[1] == player[0]){
                int promotion;
                if (arr[1] == 'W')
                    promotion = 1;
                else if (arr[1] == 'B')
                    promotion = -1;
                
                switch (arr[2]) {
                    case 'N': 
                        promotion *= 2;
                        break;
                    case 'B': 
                        promotion *= 3;
                        break;
                    case 'R': 
                        promotion *= 4;
                        break;
                    case 'Q': 
                        promotion *= 5;
                        break;
                    default:
                        promotion *= 1; 
                        break;
                }
                game.board[start_row][start_col] = promotion;
            // checking error cases of pawn promotion    
            }else if (isLegal && ((end_row == 7 && player[0] == 'W') || (end_row == 0 && player[0] == 'B')) && arr[0] == 'y' 
            && (arr[1] != player[0] || arr[2] == 'P' || arr[2] == 'K'))
                isLegal = false;
            else if (isLegal && ((end_row == 7 && player[0] == 'W') || (end_row == 0 && player[0] == 'B')) && arr[0] != 'y')
                isLegal = false;
            else if (isLegal && ((end_row != 7 && player[0] == 'W') || (end_row != 0 && player[0] == 'B')) && (arr[0] == 'x' || arr[0] == 'y' || arr2[0] == 'y'))
                isLegal = false;
            // checking if player puts themself in check
            if (isLegal){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, player[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (player[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (player[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, player[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    isLegal = false;
                }
            }
            return isLegal;
        // if pawn is starting position and wants to move forward two, also checks for invalid commands and putting themself in check
        }else if (start_col == end_col && end_row == start_row + 2 * direction && 
                 ((piece > 0 && start_row == 1) || (piece < 0 && start_row == 6))) {
            isLegal = clear_path(start_row, start_col, end_row, end_col, arr); 
            if (isLegal && (arr[0] == 'y' || arr[0] == 'x' || arr2[0] == 'y'))
                isLegal = false;
            if (isLegal){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, player[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (player[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (player[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, player[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    isLegal = false;
                }
            }
            return isLegal;
        // else moving diagonally (catching opponent) and player wants to promote. also checks for player in check
        }else if (abs(start_col - end_col) == 1 && end_row == start_row + direction && arr[0] == 'x') {
            isLegal = valid_opponent_piece(start_row + direction, end_col, piece, &arr[1], 2); 
            if (isLegal && ((end_row == 7 && player[0] == 'W') || (end_row == 0 && player[0] == 'B')) && arr2[0] == 'y' && 
            arr2[2] != 'P' && arr2[2] != 'K' && arr2[1] == player[0]){
                int promotion;
                if (arr2[1] == 'W')
                    promotion = 1;
                else if (arr2[1] == 'B')
                    promotion = -1;
                
                switch (arr2[2]) {
                    case 'N': 
                        promotion *= 2;
                        break;
                    case 'B': 
                        promotion *= 3;
                        break;
                    case 'R': 
                        promotion *= 4;
                        break;
                    case 'Q': 
                        promotion *= 5;
                        break;
                    default:
                        promotion *= 1; 
                        break;
                }
                game.board[start_row][start_col] = promotion;
                
            // again checking command errors and then checking if playe ris putting themselve's in check
            }else if (isLegal && ((end_row == 7 && player[0] == 'W') || (end_row == 0 && player[0] == 'B')) && arr2[0] == 'y' && 
            (arr2[1] != player[0] || arr2[2] == 'P' || arr2[2] == 'K'))
                isLegal = false;
            else if (isLegal && ((end_row == 7 && player[0] == 'W') || (end_row == 0 && player[0] == 'B')) && arr2[0] != 'y')
                isLegal = false;
            else if (isLegal && ((end_row != 7 && player[0] == 'W') || (end_row != 0 && player[0] == 'B')) && arr2[0] == 'y')
                isLegal = false;
            printk("%d\n", isLegal);
            if (isLegal){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, player[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (player[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (player[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, player[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    isLegal = false;
                }
            }
            return isLegal;
        }
        return false;
    // if piece is knight, not worry about clear path since knight can jump over pieces
    // however only concerned if the end position is not empty and if user did not do x[COMMAND] or invalid command
    // also checking if player is putting themselves in check
    }else if (abs(piece) == KNIGHT) {
        if ((abs(start_row - end_row) == 2 && abs(start_col - end_col) == 1) ||
            (abs(start_row - end_row) == 1 && abs(start_col - end_col) == 2)) {
            if (game.board[end_row][end_col] == EMPTY && (arr[0] != 'x' || arr2[0] != 'y' || arr[0] != 'y'))
                beforeCheck = true;
            if (game.board[end_row][end_col] == EMPTY && (arr[0] == 'x' || arr2[0] == 'y' || arr[0] == 'y'))
                beforeCheck = false;
            else if (game.board[end_row][end_col] != EMPTY && arr[0] == 'x' && arr2[0] != 'y')
                beforeCheck = valid_opponent_piece(end_row, end_col, piece, &arr[1], 2); 
            else if (game.board[end_row][end_col] != EMPTY)
                beforeCheck = false;
            if (beforeCheck){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, player[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (player[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (player[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, player[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    beforeCheck = false;
                }
            }
            return beforeCheck;
        }
        return false;
    // if piece is bishop, can only move diagonally. checking if path is empty if not if user does x[COMMAND] then true else false
    // also checking if user puts themself in check
    }else if (abs(piece) == BISHOP) {
        if (abs(start_row - end_row) != abs(start_col - end_col))
            beforeCheck = false;
        else if (!clear_path(start_row, start_col, end_row, end_col, arr))
            beforeCheck = false; 
        else if (clear_path(start_row, start_col, end_row, end_col, arr) && (arr2[0] == 'y' || arr[0] == 'y'))
            beforeCheck = false;
        else if (clear_path(start_row, start_col, end_row, end_col, arr) && arr[0] == 'x' && arr2[0] != 'y')
            beforeCheck = valid_opponent_piece(end_row, end_col, piece, &arr[1], 2); 
        if (beforeCheck){
            bool inCheck;
            int temp_piece = game.board[end_row][end_col];
            game.board[end_row][end_col] = game.board[start_row][start_col];
            game.board[start_row][start_col] = EMPTY;

            inCheck = false;
            if (piece == KING) 
                inCheck = king_check(&game, end_row, end_col, player[0]);
            else {
                int king_row; 
                int king_col; 
                if (player[0] == 'W'){
                    king_row = game.white_king[0];
                    king_col = game.white_king[1];
                }else if (player[0] == 'B'){
                    king_row = game.black_king[0];
                    king_col = game.black_king[1];
                }
                inCheck = king_check(&game, king_row, king_col, player[0]);
            }

            game.board[start_row][start_col] = game.board[end_row][end_col];
            game.board[end_row][end_col] = temp_piece;

            if (inCheck) {
                beforeCheck = false;
            }
        }
        return beforeCheck;
    // queen can move vertically, horizontally, and diagonally so checking for those conditions
    // if user does x[COMMAND] true else false and other validation
    // also checking if user is putting themself in check
    }else if (abs(piece) == QUEEN) {
        if ((start_row == end_row || start_col == end_col) || 
            (abs(start_row - end_row) == abs(start_col - end_col))) { 
            if (!clear_path(start_row, start_col, end_row, end_col, arr))
                beforeCheck = false; 
            else if (clear_path(start_row, start_col, end_row, end_col, arr) && (arr2[0] == 'y' || arr[0] == 'y'))
                beforeCheck = false;
            else if (clear_path(start_row, start_col, end_row, end_col, arr) && arr[0] == 'x' && arr2[0] != 'y')
                beforeCheck = valid_opponent_piece(end_row, end_col, piece, &arr[1], 2); 
            if (beforeCheck){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, player[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (player[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (player[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, player[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    beforeCheck = false;
                }
            }
            return beforeCheck;
        }
        return false;
    // king can move vertical, diagonal, and horizontal by 1 square. checking if those are clear OR if user did x[COMMAND]
    // also checking if king has put themself in check
    }else if (abs(piece) == KING) {
        if (abs(start_row - end_row) <= 1 && abs(start_col - end_col) <= 1) {
            if (game.board[end_row][end_col] != EMPTY && arr[0] == 'x' && arr2[0] != 'y')
                return valid_opponent_piece(end_row, end_col, piece, &arr[1], 2); 
            else if (game.board[end_row][end_col] != EMPTY && (arr[0] != 'x' || arr2[0] == 'y'))
                beforeCheck = false;
            else if (beforeCheck){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, player[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (player[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (player[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, player[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    beforeCheck = false;
                }
            }
            return beforeCheck;
        }
        return false;
    }
    return beforeCheck;
}

// converting int piece to char for printing board
// using switch statements to match the enums
void piece_to_char(int piece, char *buf) {
    char player; 

    if (piece > 0)
        player = 'W';
    else
        player = 'B'; 

    piece = abs(piece);

    switch(piece) {
        case PAWN:
            sprintf(buf, "%cP", player);
            break;
        case KNIGHT:
            sprintf(buf, "%cN", player);
            break;
        case BISHOP:
            sprintf(buf, "%cB", player);
            break;
        case ROOK:
            sprintf(buf, "%cR", player);
            break;
        case QUEEN:
            sprintf(buf, "%cQ", player);
            break;
        case KING:
            sprintf(buf, "%cK", player);
            break;
        default:
            sprintf(buf, "**");  
            break;
    }
}

// printing board state using piece_to_char helper
void board_state(void) {
    int i;
    int j;
    size_t msg_len;
    int line_len;
    char line[BOARD_SIZE * 3 + 1];
    memset(message, 0, sizeof(message));  
    msg_len = 0;

    for (i = 0; i < BOARD_SIZE; i++) {
        memset(line, 0, sizeof(line)); 
        line_len = 0;
        for (j = 0; j < BOARD_SIZE; j++) {
            char piece_str[4] = "**";  
            if (game.board[i][j] != EMPTY) {
                piece_to_char(game.board[i][j], piece_str);
            }
            line_len += snprintf(line + line_len, sizeof(line) - line_len, "%s ", piece_str);
        }
        msg_len += snprintf(message + msg_len, sizeof(message) - msg_len, "%s\n", line);
    }
}

// converting piece type char to int for moving
int display_piece(char* piece_type) {
    int piece = 0;
    int color;

    if (piece_type[0] == 'W')
        color = 1;
    else if (piece_type[0] == 'B')
        color = -1;

    switch (piece_type[1]) {
        case 'P': 
            piece = 1;
            break;
        case 'N': 
            piece = 2;
            break;
        case 'B': 
            piece = 3;
            break;
        case 'R': 
            piece = 4;
            break;
        case 'Q': 
            piece = 5;
            break;
        case 'K': 
            piece = 6;
            break;
        default:
            printk(KERN_WARNING "Invalid piece: %s\n", piece_type);
            return 0; 
    }

    return color * piece;
}

// performing move. checking if after the move if there are any checks
void perform_move(int start_row, int start_col, int end_row, int end_col, int piece) {
    // declaring variables and getting the piece
    int captured_piece;
    char opponent;
    int king_row;
    int king_col;
    captured_piece = game.board[end_row][end_col];
    game.board[end_row][end_col] = piece;

    game.board[start_row][start_col] = 0;

    // getting opponent info for check
    if ((player[0] == 'W' || cpu[0] == 'W') && game.current_turn == 1)
        opponent = 'B';
    else if ((player[0] == 'B' || cpu[0] == 'B') && game.current_turn == -1)
        opponent = 'W';

    if (opponent == 'W'){
        king_row = game.white_king[0];
        king_col = game.white_king[1];
    }else if (opponent == 'B'){
        king_row = game.black_king[0];
        king_col = game.black_king[1];
    }
     
    printk(KERN_INFO "already here\n");
    if (king_check(&game, king_row, king_col, opponent)) {
        game.check = true;
        printk(KERN_INFO "Move places opponent's king in check.\n");
    }else
        game.check = false;

    // updating king position if the piece is king
    if (abs(piece) == KING) {
        if (player[0] == 'W') {
            game.white_king[0] = end_row;
            game.white_king[1] = end_col;
        } else {
            game.black_king[0] = end_row;
            game.black_king[1] = end_col;
        }
    }


    printk(KERN_INFO "piece move %d from %d,%d to %d,%d\n", piece, start_row, start_col, end_row, end_col);

    game.current_turn = -game.current_turn;
}

// checking if the piece the user wants to capture is valid by using switch statement
bool valid_opponent_piece(int row, int col, int piece, char *array, int size) {
    int opponent;
    int match;
    if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) {
        printk(KERN_INFO "Chess: Move out of board bounds.\n");
        return false; 
    }

    opponent = game.board[row][col];  
    if (array[0] == 'W')
        match = 1;
    else if (array[0] == 'B')
        match = -1;

    switch (array[1]) {
        case 'P': 
            match *= 1;
            break;
        case 'N': 
            match *= 2;
            break;
        case 'B': 
            match *= 3;
            break;
        case 'R': 
            match *= 4;
            break;
        case 'Q': 
            match *= 5;
            break;
        case 'K': 
            match *= 6;
            break;
        default:
            printk(KERN_WARNING "Invalid piece: %c\n", array[1]);
            return 0; 
    }
    

    if (opponent == EMPTY) 
        return false;  

    if (((piece > 0 && opponent < 0) || (piece < 0 && opponent > 0)) && opponent == match) {
        return true; 
    }

    return false; 
}

// helper function for checking char through string (i used this for mmessage format check)
int char_check(char c, const char *string) {
    while (*string != '\0') {  
        if (c == *string) {
            return 1; 
        }
        string++; 
    }
    return 0; 
}

// checking for if the king is in check (user and cpu)
bool king_check(struct chess_game *game, int king_row, int king_col, char color) {
    // defining variables used
    // 2d arrays of directions and knight info for the moves
    int directions[8][2]; 
    int knight_directions[8][2]; 
    int king_position[2];
    int x;
    int y;
    int i; 
    int direction_x;
    int direction_y;
    int pawn_direction;
    int pawn_row;
    printk("reached king check\n");

    directions[0][0] = 1; 
    directions[0][1] = 0;
    directions[1][0] = -1; 
    directions[1][1] = 0;
    directions[2][0] = 0; 
    directions[2][1] = 1;
    directions[3][0] = 0; 
    directions[3][1] = -1;
    directions[4][0] = 1; 
    directions[4][1] = 1;
    directions[5][0] = -1; 
    directions[5][1] = -1;
    directions[6][0] = 1; 
    directions[6][1] = -1;
    directions[7][0] = -1; 
    directions[7][1] = 1;

    knight_directions[0][0] = 2; 
    knight_directions[0][1] = 1;
    knight_directions[1][0] = 2; 
    knight_directions[1][1] = -1;
    knight_directions[2][0] = -2; 
    knight_directions[2][1] = 1;
    knight_directions[3][0] = -2; 
    knight_directions[3][1] = -1;
    knight_directions[4][0] = 1; 
    knight_directions[4][1] = 2;
    knight_directions[5][0] = 1; 
    knight_directions[5][1] = -2;
    knight_directions[6][0] = -1; 
    knight_directions[6][1] = 2;
    knight_directions[7][0] = -1; 
    knight_directions[7][1] = -2;
    
    // getting king information based on color provided
    if (color == 'W') {
        king_position[0] = game->white_king[1];
        king_position[1] = game->white_king[0];
    } else {
        king_position[0] = game->black_king[1];
        king_position[1] = game->black_king[0];
    }

    printk(KERN_INFO "King's position: (%d, %d) and color: (%c) and piece: %d\n", king_position[0], king_position[1], color,
    game->board[king_position[0]][king_position[1]]);
    
    // getting possible positions for queen, rook, and bishop (horizontal, vertical, diagonal)
    for (i = 0; i < 8; i++) {
        printk(KERN_INFO "queen/rook/bishop\n");
        direction_x = directions[i][0];
        direction_y = directions[i][1];

        printk(KERN_INFO "Checking direction: (%d, %d)\n", direction_x, direction_y);

        // for each iteration, goes forward/backward by direction
        x = king_position[0] + direction_x;
        y = king_position[1] + direction_y;

        printk(KERN_INFO "Initial position to check: (%d, %d)\n", x, y);
        
        // checking for check threat (opponent and proximity to king for the provided pieces)
        while (x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE) {
            printk(KERN_INFO "Checking position: (%d, %d)\n", x, y);
            if (game->board[x][y] != EMPTY) {
                printk(KERN_INFO "Piece at position: %d\n", game->board[x][y]);
                if ((color == 'W' && game->board[x][y] < 0) || (color == 'B' && game->board[x][y] > 0)) {
                    if (abs(game->board[x][y]) == QUEEN || 
                        (abs(game->board[x][y]) == ROOK && (direction_x == 0 || direction_y == 0)) || 
                        (abs(game->board[x][y]) == BISHOP && direction_x != 0 && direction_y != 0)) {
                        printk(KERN_INFO "true queen/rook/bishop\n");
                        return true;
                    }
                }
                break;
            }
            x += direction_x;
            y += direction_y;
        }
    }
    
    // for knight, incrementing based on distance from king and checking proximity and color of piece to king
    for (i = 0; i < 8; i++) {
        printk(KERN_INFO "knight\n");
        x = king_position[0] + knight_directions[i][0];
        y = king_position[1] + knight_directions[i][1];
        if (x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE) {
            printk(KERN_INFO "Initial position to check: (%d, %d)\n", x, y);
            if ((color == 'W' && game->board[x][y] == -KNIGHT) || (color == 'B' && game->board[x][y] == KNIGHT)) {
                printk(KERN_INFO "Piece at position: %d\n", game->board[x][y]);
                printk(KERN_INFO "true knight\n");
                return true;
            }
        }
    }

    // condition for pawn since it is opposite
    if (player[0] == 'W')
        pawn_direction = -1;
    else if (player[0] == 'B')
        pawn_direction = 1;

    // checking if pawn diagonal move is in proximity to king
    pawn_row = king_position[0] + pawn_direction;
    printk(KERN_INFO "pawn row %d\n", pawn_row);
    if (pawn_row >= 0 && pawn_row < BOARD_SIZE) {
        printk(KERN_INFO "pawn\n");
        if (((king_position[1] > 0 && (color == 'W' && game->board[pawn_row][king_position[1] - 1] == -PAWN)) ||
            (color == 'B' && game->board[pawn_row][king_position[1] - 1] == PAWN))) {
            printk(KERN_INFO "Piece at position: %d\n", game->board[x][y]);
            printk(KERN_INFO "true pawn\n");
            return true;
        }
        if (((king_position[1] < BOARD_SIZE - 1 && (color == 'W' && game->board[pawn_row][king_position[1] + 1] == -PAWN)) ||
            (color == 'B' && game->board[pawn_row][king_position[1] + 1] == PAWN))) {
            printk(KERN_INFO "Piece at position: %d\n", game->board[x][y]);
            printk(KERN_INFO "true pawn\n");
            return true;
        }
    }
    
    // checking if king can check other king
    for (direction_x = -1; direction_x <= 1; direction_x++) {
        for (direction_y = -1; direction_y <= 1; direction_y++) {
            printk(KERN_INFO "king\n");
            printk(KERN_INFO "Checking direction: (%d, %d)\n", direction_x, direction_y);
            x = king_position[0] + direction_x;
            y = king_position[1] + direction_y;
            if (x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE) {
                printk(KERN_INFO "Initial position to check: (%d, %d)\n", x, y);
                if ((color == 'W' && game->board[x][y] == -KING) || (color == 'B' && game->board[x][y] == KING)) {
                    printk(KERN_INFO "Piece at position: %d\n", game->board[x][y]);
                    printk(KERN_INFO "true king\n");
                    return true;
                }
            }
        }
    }
    
    return false;
}

// checking for checkmate
bool is_checkmate(struct chess_game *game, char arr[4], char arr2[4]) {
    // declaring variables
    char color; 
    int king_row; 
    int king_col; 
    int start_row;
    int start_col;
    int piece;
    int end_row;
    int end_col;

    // getting turn of game to check condition of checkmate
    if (game->current_turn > 0)
        color = 'W';
    else if (game->current_turn < 0)
        color = 'B';

    // by getting color, am getting the coords of king
    if (color == 'W'){
        king_row = game->white_king[1];
        king_col = game->white_king[0];
    }else if (color == 'B'){
        king_row = game->black_king[1];
        king_col = game->black_king[0];
    }

    // if the king is not in check there is no reason to look further
    if (!king_check(game, king_row, king_col, color)) 
        return false; 

    // else, try to generate different ways that king can get out of check and if there are none then it is checkmate
    // else, leave it alone and return false (no checkmate)
    for (start_row = 0; start_row < BOARD_SIZE; start_row++) {
        for (start_col = 0; start_col < BOARD_SIZE; start_col++) {
            piece = game->board[start_row][start_col];
            if ((color == 'W' && piece > 0) || (color == 'B' && piece < 0)) {
                for (end_row = 0; end_row < BOARD_SIZE; end_row++) {
                    for (end_col = 0; end_col < BOARD_SIZE; end_col++) {
                        if (legal_move(start_row, start_col, end_row, end_col, piece, arr, arr2)) {
                            struct chess_game temp_game = *game;
                            perform_move(start_row, start_col, end_row, end_col, piece);
                            if (!king_check(&temp_game, king_row, king_col, color)) {
                                return false; 
                            }
                        }
                    }
                }
            }
        }
    }
    printk(KERN_INFO "checkmate\n");
    return true;
}

// cpu algorithm that checks for clear path
// way more basic than user implementation
bool cpu_clear_path(int start_row, int start_col, int end_row, int end_col) {
    // declaring variables
    int row_direction;
    int col_direction;
    int i;
    int j;

    // getting direction of movement
    if (end_row != start_row) {
        if (end_row - start_row > 0)
            row_direction = 1;
        else if (end_row - start_row < 0)
            row_direction = -1;
    }

    if (end_col != start_col) {
        if (end_col - start_col > 0)
            col_direction = 1;
        else if (end_col - start_col < 0)
            col_direction = -1;
    }

    // if movement is diagonal and checks if path is clear. if last cell is empty and is opponent proceed with move
    if (start_row != end_row && start_col != end_col) {
        for (i = start_row + row_direction, j = start_col + col_direction; i != end_row && j != end_col; i += row_direction, j += col_direction) {
            if (game.board[i][j] != EMPTY) {
                return false;
            }
        }
        if (game.board[end_row][end_col] != EMPTY){
            if ((cpu[0] == 'W' && game.board[end_row][end_col] >= 0) || (cpu[0] == 'B' && game.board[end_row][end_col] <= 0))
                return false;
        }
    }

    // vertical movement, if path is clear or opponent is last cell return true else false
    else if (start_row != end_row) {
        for (i = start_row + row_direction; i != end_row; i += row_direction) {
            if (game.board[i][start_col] != EMPTY) {
                return false;
            }
        }
        if (game.board[end_row][end_col] != EMPTY){
            if ((cpu[0] == 'W' && game.board[end_row][end_col] >= 0) || (cpu[0] == 'B' && game.board[end_row][end_col] <= 0))
                return false;
        }
    }

    // horizontal movement, if path is clear or opponent is last cell return true else false
    else if (start_col != end_col) {
        for (j = start_col + col_direction; j != end_col; j += col_direction) {
            if (game.board[start_row][j] != EMPTY) {
                return false;
            }
        }
        if (game.board[end_row][end_col] != EMPTY){
            if ((cpu[0] == 'W' && game.board[end_row][end_col] >= 0) || (cpu[0] == 'B' && game.board[end_row][end_col] <= 0))
                return false;
        }
    }

    return true;
}

// checks legal moves of cpu
bool cpu_legal_move(int start_row, int start_col, int end_row, int end_col, int piece) {
    bool beforeCheck = true;
    // if rook and moves horizontal.diagonal proceed (uses clear_path) then also checks if cpu is putting itself in check
    if (abs(piece) == ROOK) {
        if (start_row != end_row && start_col != end_col)
            beforeCheck = false; 
        if (!cpu_clear_path(start_row, start_col, end_row, end_col))
            beforeCheck = false; 
        if (beforeCheck){
            bool inCheck;
            int temp_piece = game.board[end_row][end_col];
            game.board[end_row][end_col] = game.board[start_row][start_col];
            game.board[start_row][start_col] = EMPTY;

            inCheck = false;
            if (piece == KING) 
                inCheck = king_check(&game, end_row, end_col, cpu[0]);
            else {
                int king_row; 
                int king_col; 
                if (cpu[0] == 'W'){
                    king_row = game.white_king[0];
                    king_col = game.white_king[1];
                }else if (cpu[0] == 'B'){
                    king_row = game.black_king[0];
                    king_col = game.black_king[1];
                }
                inCheck = king_check(&game, king_row, king_col, cpu[0]);
            }

            game.board[start_row][start_col] = game.board[end_row][end_col];
            game.board[end_row][end_col] = temp_piece;

            if (inCheck) {
                beforeCheck = false;
            }
        }
        if (cpu[0] == 'W' && game.board[end_row][end_col] > 0)
            beforeCheck = false;
        if (cpu[0] == 'B' && game.board[end_row][end_col] < 0)
            beforeCheck = false;
        return beforeCheck;
    }
    // same for pawn if it is a valid movement using cpu_clear_path and checks if it is putting itself in check
    // also randomizing promotion of pawn
    else if (abs(piece) == PAWN) {
        int direction;
        if (piece > 0)
            direction = 1;
        else if (piece < 0)
            direction = -1;

        if (start_col == end_col && end_row == start_row + direction) {
            beforeCheck = cpu_clear_path(start_row, start_col, end_row, end_col); 
            if (beforeCheck && ((end_row == 7 && cpu[0] == 'W') || (end_row == 0 && cpu[0] == 'B'))){
                unsigned int promotion;
                get_random_bytes(&promotion, sizeof(promotion));
                promotion = (promotion % 4) + 2; 

                if (cpu[0] == 'W')
                    promotion = 1;
                else if (cpu[0] == 'B')
                    promotion = -1;
                
                switch (promotion) {
                    case KNIGHT: 
                        promotion *= 2;
                        break;
                    case BISHOP: 
                        promotion *= 3;
                        break;
                    case ROOK: 
                        promotion *= 4;
                        break;
                    case QUEEN: 
                        promotion *= 5;
                        break;
                }
                game.board[start_row][start_col] = promotion;  
            }
            if (beforeCheck){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, cpu[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (cpu[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (cpu[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, cpu[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    beforeCheck = false;
                }
            }
            if (cpu[0] == 'W' && game.board[end_row][end_col] > 0)
                beforeCheck = false;
            if (cpu[0] == 'B' && game.board[end_row][end_col] < 0)
                beforeCheck = false;
        } else if (start_col == end_col && end_row == start_row + 2 * direction &&
                   ((piece > 0 && start_row == 1) || (piece < 0 && start_row == 6))) {
            beforeCheck = cpu_clear_path(start_row, start_col, end_row, end_col);
            if (beforeCheck){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, cpu[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (cpu[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (cpu[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, cpu[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    beforeCheck = false;
                }
            }
            if (cpu[0] == 'W' && game.board[end_row][end_col] > 0)
                beforeCheck = false;
            if (cpu[0] == 'B' && game.board[end_row][end_col] < 0)
                beforeCheck = false;
        // else if pawn is trying to get opponent, checking that condition and checking for putting itself in check
        } else if (abs(start_col - end_col) == 1 && end_row == start_row + direction) {
            if (cpu[0] == 'W'){
                if (game.board[start_row + direction][end_col] >= 0)
                    beforeCheck = false;
                else
                    beforeCheck = true;
            }
            if (cpu[0] == 'B'){
                if (game.board[start_row + direction][end_col] <= 0)
                    beforeCheck = false;
                else
                    beforeCheck = true;
            }
            if (beforeCheck && ((end_row == 7 && cpu[0] == 'W') || (end_row == 0 && cpu[0] == 'B'))){
                unsigned int promotion;
                get_random_bytes(&promotion, sizeof(promotion));
                promotion = (promotion % 4) + 2; 
                if (cpu[0] == 'W')
                    promotion = 1;
                else if (cpu[0] == 'B')
                    promotion = -1;
                
                switch (promotion) {
                    case KNIGHT: 
                        promotion *= 2;
                        break;
                    case BISHOP: 
                        promotion *= 3;
                        break;
                    case ROOK: 
                        promotion *= 4;
                        break;
                    case QUEEN: 
                        promotion *= 5;
                        break;
                }
                game.board[start_row][start_col] = promotion;  
            }
            if (beforeCheck){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, cpu[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (cpu[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (cpu[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, cpu[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    beforeCheck = false;
                }
            }
            if (cpu[0] == 'W' && game.board[end_row][end_col] > 0)
                beforeCheck = false;
            if (cpu[0] == 'B' && game.board[end_row][end_col] < 0)
                beforeCheck = false;
        }else
            beforeCheck = false;
        return beforeCheck;
    }
    // knight can jump over pieces, easy check. just checking if the last index is clear or if it will put iself in check
    else if (abs(piece) == KNIGHT) {
        if ((abs(start_row - end_row) == 2 && abs(start_col - end_col) == 1) ||
            (abs(start_row - end_row) == 1 && abs(start_col - end_col) == 2)) {
            beforeCheck = true; 
        }else   
            beforeCheck = false;
        if (beforeCheck){
            bool inCheck;
            int temp_piece = game.board[end_row][end_col];
            game.board[end_row][end_col] = game.board[start_row][start_col];
            game.board[start_row][start_col] = EMPTY;

            inCheck = false;
            if (piece == KING) 
                inCheck = king_check(&game, end_row, end_col, cpu[0]);
            else {
                int king_row; 
                int king_col; 
                if (cpu[0] == 'W'){
                    king_row = game.white_king[0];
                    king_col = game.white_king[1];
                }else if (cpu[0] == 'B'){
                    king_row = game.black_king[0];
                    king_col = game.black_king[1];
                }
                inCheck = king_check(&game, king_row, king_col, cpu[0]);
            }

            game.board[start_row][start_col] = game.board[end_row][end_col];
            game.board[end_row][end_col] = temp_piece;

            if (inCheck) {
                beforeCheck = false;
            }
        }
        if (cpu[0] == 'W' && game.board[end_row][end_col] > 0)
            beforeCheck = false;
        if (cpu[0] == 'B' && game.board[end_row][end_col] < 0)
            beforeCheck = false;
        return beforeCheck;
    }
    // diagonal moves, checking if path is clear OR if last cell is opponent piece. also checking for self check
    else if (abs(piece) == BISHOP) {
        if (abs(start_row - end_row) != abs(start_col - end_col))
            beforeCheck = false; 
        if (!cpu_clear_path(start_row, start_col, end_row, end_col))
            beforeCheck = false;
        if (beforeCheck){
            bool inCheck;
            int temp_piece = game.board[end_row][end_col];
            game.board[end_row][end_col] = game.board[start_row][start_col];
            game.board[start_row][start_col] = EMPTY;

            inCheck = false;
            if (piece == KING) 
                inCheck = king_check(&game, end_row, end_col, cpu[0]);
            else {
                int king_row; 
                int king_col; 
                if (cpu[0] == 'W'){
                    king_row = game.white_king[0];
                    king_col = game.white_king[1];
                }else if (cpu[0] == 'B'){
                    king_row = game.black_king[0];
                    king_col = game.black_king[1];
                }
                inCheck = king_check(&game, king_row, king_col, cpu[0]);
            }

            game.board[start_row][start_col] = game.board[end_row][end_col];
            game.board[end_row][end_col] = temp_piece;

            if (inCheck) {
                beforeCheck = false;
            }
        } 
        if (cpu[0] == 'W' && game.board[end_row][end_col] > 0)
            beforeCheck = false;
        if (cpu[0] == 'B' && game.board[end_row][end_col] < 0)
            beforeCheck = false;
        return beforeCheck;
    }
    // queen can move horizontal, vertical, and diagonal so all the previous checks combined plus checking for clear path
    // and also checking for self check
    else if (abs(piece) == QUEEN) {
        if ((start_row == end_row || start_col == end_col) || 
            (abs(start_row - end_row) == abs(start_col - end_col))) {
            if (!cpu_clear_path(start_row, start_col, end_row, end_col))
                beforeCheck = false; 
            if (beforeCheck){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, cpu[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (cpu[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (cpu[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, cpu[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    beforeCheck = false;
                }
            }
            if (cpu[0] == 'W' && game.board[end_row][end_col] > 0)
                beforeCheck = false;
            if (cpu[0] == 'B' && game.board[end_row][end_col] < 0)
                beforeCheck = false;
            return beforeCheck;
        }
        return false;
    }
    // king can only move 1 cell in horizontal, vertical, and diagonal. checking for self check and if the last cell is empty
    else if (abs(piece) == KING) {
        if (abs(start_row - end_row) <= 1 && abs(start_col - end_col) <= 1) {
            if (game.board[end_row][end_col] != EMPTY && ((cpu[0] == 'W' && game.board[end_row][end_col] > 0) || (cpu[0] == 'B' && game.board[end_row][end_col] < 0)))
                beforeCheck = false;
            else
                beforeCheck = true;
            if (beforeCheck){
                bool inCheck;
                int temp_piece = game.board[end_row][end_col];
                game.board[end_row][end_col] = game.board[start_row][start_col];
                game.board[start_row][start_col] = EMPTY;

                inCheck = false;
                if (piece == KING) 
                    inCheck = king_check(&game, end_row, end_col, cpu[0]);
                else {
                    int king_row; 
                    int king_col; 
                    if (cpu[0] == 'W'){
                        king_row = game.white_king[0];
                        king_col = game.white_king[1];
                    }else if (cpu[0] == 'B'){
                        king_row = game.black_king[0];
                        king_col = game.black_king[1];
                    }
                    inCheck = king_check(&game, king_row, king_col, cpu[0]);
                }

                game.board[start_row][start_col] = game.board[end_row][end_col];
                game.board[end_row][end_col] = temp_piece;

                if (inCheck) {
                    beforeCheck = false;
                }
            }
            if (cpu[0] == 'W' && game.board[end_row][end_col] > 0)
                beforeCheck = false;
            if (cpu[0] == 'B' && game.board[end_row][end_col] < 0)
                beforeCheck = false;
            return beforeCheck;
        }
        return false;
    }
    return beforeCheck;
}

// checking for randomized cpu moves and performing them (not completely randomized to avoid infinite loop)
void cpu_move(struct chess_game *game){
    // declaring variables and array to store cpu moves
    int i;
    int j;
    int k;
    int l;
    int piece;
    unsigned int rand_val;
    struct cpu_move legal_moves[BOARD_SIZE * BOARD_SIZE];
    int counter;

    counter = 0;

    // traversing the board and choosing piece
    for (i = 0; i < BOARD_SIZE; i++) {
        for (j = 0; j < BOARD_SIZE; j++) {
            piece = game->board[i][j];
            // checking conditions so if it is a valid piece and not opponent piece
            if ((cpu[0] == 'B' && piece < 0) || (cpu[0] == 'W' && piece > 0)) {
                for (k = 0; k < BOARD_SIZE; k++) {
                    for (l = 0; l < BOARD_SIZE; l++) {
                        // traversing again to find legal moves and adding it to array (all legal moves possible)
                        if (cpu_legal_move(i, j, k, l, piece)) {
                            if (counter < sizeof(legal_moves)/sizeof(legal_moves[0])) {
                                legal_moves[counter++] = (struct cpu_move){i, j, k, l};
                            }
                        }
                    }
                }
            }
        }
    }
    printk(KERN_INFO "counter: %d\n", counter);
    // if there are legal moves, chooses one randomly and performs it
    if (counter > 0) {
        struct cpu_move perform;
        get_random_bytes(&rand_val, sizeof(rand_val));
        rand_val %= counter;
        perform = legal_moves[rand_val];
        perform_move(perform.start_row, perform.start_col, perform.end_row, perform.end_col, game->board[perform.start_row][perform.start_col]);
        printk(KERN_INFO "CPU moved piece from %d,%d to %d,%d\n", perform.start_row, perform.start_col, perform.end_row, perform.end_col);
    } else {
        printk(KERN_INFO "No legal moves available\n");
    }
}

// checking checkmate for cpu
bool cpu_checkmate(struct chess_game *game) {
    // initializing variables
    char color; 
    int king_row; 
    int king_col; 
    int start_row;
    int start_col;
    int piece;
    int end_row;
    int end_col;

    // getting opponent color and king coords
    if (game->current_turn > 0)
        color = 'W';
    else if (game->current_turn < 0)
        color = 'B';

    if (color == 'W'){
        king_row = game->white_king[1];
        king_col = game->white_king[0];
    }else if (color == 'B'){
        king_row = game->black_king[1];
        king_col = game->black_king[0];
    }

    // again if king is not in check there isnt a reason to check for checkmate
    if (!king_check(game, king_row, king_col, color)) 
        return false; 

    // else interating through the board, selecting a piece
    for (start_row = 0; start_row < BOARD_SIZE; start_row++) {
        for (start_col = 0; start_col < BOARD_SIZE; start_col++) {
            piece = game->board[start_row][start_col];
            if ((color == 'W' && piece > 0) || (color == 'B' && piece < 0)) {
                for (end_row = 0; end_row < BOARD_SIZE; end_row++) {
                    for (end_col = 0; end_col < BOARD_SIZE; end_col++) {
                        // checking if cpu can get out of checkmate. if it can it is not checkmate else is checkmate.
                        if (cpu_legal_move(start_row, start_col, end_row, end_col, piece)) {
                            struct chess_game temp_game = *game;
                            perform_move(start_row, start_col, end_row, end_col, piece);
                            if (!king_check(&temp_game, king_row, king_col, color)) {
                                return false; 
                            }
                        }
                    }
                }
            }
        }
    }
    return true;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sofia Gomes");
MODULE_DESCRIPTION("Linux kernel module for playing chess game");


module_init(chess_init);
module_exit(chess_exit);
