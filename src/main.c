/*
 * Sliding Puzzle (15-Puzzle) for Game Boy Color
 * Built with GBDK-2020
 *
 * Slide numbered tiles into order using the D-pad.
 * The goal is to arrange tiles 1-15 in order with the
 * empty space in the bottom-right corner.
 */

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include <rand.h>

/* External tile data */
extern const unsigned char puzzle_tiles[];
extern const uint8_t PUZZLE_TILES_COUNT;

/* ======== Constants ======== */

/* Grid dimensions */
#define GRID_SIZE    4
#define TOTAL_TILES  16
#define EMPTY_TILE   0

/* Each puzzle cell is 3x3 background tiles on screen */
#define CELL_W  3
#define CELL_H  3

/* Grid position on screen (in BG tile coordinates) */
#define GRID_X  4
#define GRID_Y  3

/* Tile indices in VRAM */
#define T_BLANK      0
#define T_BORDER_TL  1
#define T_BORDER_T   2
#define T_BORDER_TR  3
#define T_BORDER_L   4
#define T_BORDER_R   5
#define T_BORDER_BL  6
#define T_BORDER_B   7
#define T_BORDER_BR  8
#define T_CELL_BG    9
#define T_NUM_START  10   /* Tiles 10-18: digits 1-9 */
#define T_NUM10_L    19   /* Two-digit number left halves start */
#define T_EMPTY_CELL 31   /* Dark empty cell */
#define T_TILE_TL    32   /* Puzzle tile border pieces */
#define T_TILE_T     33
#define T_TILE_TR    34
#define T_TILE_L     35
#define T_TILE_R     36
#define T_TILE_BL    37
#define T_TILE_B     38
#define T_TILE_BR    39

/* Input delay */
#define INPUT_DELAY  6

/* ======== Color Palettes ======== */

/* GBC background palettes */
static const uint16_t bg_palettes[] = {
    /* Palette 0: UI/border (dark blue theme) */
    RGB(31, 31, 31),  /* White */
    RGB(16, 20, 28),  /* Light blue-gray */
    RGB(6, 10, 18),   /* Dark blue */
    RGB(0, 0, 0),     /* Black */

    /* Palette 1: Tile numbers 1-4 (blue) */
    RGB(20, 24, 31),  /* Light blue */
    RGB(4, 8, 24),    /* Dark blue - number color */
    RGB(12, 16, 28),  /* Medium blue */
    RGB(0, 0, 4),     /* Near black */

    /* Palette 2: Tile numbers 5-8 (green) */
    RGB(20, 31, 20),  /* Light green */
    RGB(4, 20, 4),    /* Dark green - number color */
    RGB(12, 24, 12),  /* Medium green */
    RGB(0, 4, 0),     /* Near black */

    /* Palette 3: Tile numbers 9-12 (red/orange) */
    RGB(31, 24, 20),  /* Light orange */
    RGB(24, 8, 4),    /* Dark red - number color */
    RGB(28, 16, 12),  /* Medium orange */
    RGB(4, 0, 0),     /* Near black */

    /* Palette 4: Tile numbers 13-15 (purple) */
    RGB(28, 20, 31),  /* Light purple */
    RGB(16, 4, 24),   /* Dark purple - number color */
    RGB(22, 12, 28),  /* Medium purple */
    RGB(4, 0, 4),     /* Near black */

    /* Palette 5: Empty cell (dark) */
    RGB(8, 8, 12),    /* Dark gray */
    RGB(4, 4, 8),     /* Darker */
    RGB(2, 2, 4),     /* Very dark */
    RGB(0, 0, 0),     /* Black */

    /* Palette 6: Win state (gold) */
    RGB(31, 31, 16),  /* Bright yellow */
    RGB(24, 20, 0),   /* Gold */
    RGB(16, 12, 0),   /* Dark gold */
    RGB(0, 0, 0),     /* Black */

    /* Palette 7: Text (white on dark) */
    RGB(31, 31, 31),  /* White */
    RGB(20, 20, 20),  /* Light gray */
    RGB(10, 10, 10),  /* Dark gray */
    RGB(0, 0, 0),     /* Black */
};

/* ======== Game State ======== */

/* The puzzle board: board[row][col] = tile number (1-15), 0 = empty */
uint8_t board[GRID_SIZE][GRID_SIZE];

/* Position of the empty cell */
uint8_t empty_row, empty_col;

/* Cursor position */
uint8_t cursor_row, cursor_col;

/* Move counter */
uint16_t move_count;

/* Game state flags */
uint8_t game_won;
uint8_t input_cooldown;

/* Random seed accumulator */
uint16_t seed_counter;

/* ======== Helper: Write text using tile indices ======== */
/* Maps ASCII to simple tile representations */

/* Simple font - we'll use the number tiles for digits
   and leave letters as blanks for now */
void put_char(uint8_t x, uint8_t y, char c) {
    uint8_t tile = T_BLANK;
    if (c >= '0' && c <= '9') {
        uint8_t digit = c - '0';
        if (digit == 0) {
            tile = T_NUM10_L + 1;  /* Use the "0" tile (tile 20) */
        } else {
            tile = T_NUM_START + digit - 1;  /* tiles 10-18 for 1-9 */
        }
    }
    set_bkg_tile_xy(x, y, tile);
}

void put_number(uint8_t x, uint8_t y, uint16_t num) {
    /* Draw a number right-aligned in a 3-digit field */
    uint8_t hundreds = num / 100;
    uint8_t tens = (num / 10) % 10;
    uint8_t ones = num % 10;

    if (hundreds > 0) {
        put_char(x, y, '0' + hundreds);
        put_char(x + 1, y, '0' + tens);
        put_char(x + 2, y, '0' + ones);
    } else if (tens > 0) {
        set_bkg_tile_xy(x, y, T_BLANK);
        put_char(x + 1, y, '0' + tens);
        put_char(x + 2, y, '0' + ones);
    } else {
        set_bkg_tile_xy(x, y, T_BLANK);
        set_bkg_tile_xy(x + 1, y, T_BLANK);
        put_char(x + 2, y, '0' + ones);
    }
}

/* ======== Drawing Functions ======== */

/* Get the color palette index for a tile number */
uint8_t get_tile_palette(uint8_t tile_num) {
    if (tile_num == 0) return 5;       /* Empty = dark palette */
    if (tile_num <= 4) return 1;       /* 1-4 = blue */
    if (tile_num <= 8) return 2;       /* 5-8 = green */
    if (tile_num <= 12) return 3;      /* 9-12 = orange */
    return 4;                          /* 13-15 = purple */
}

/* Draw a single puzzle cell at grid position (gx, gy) */
void draw_cell(uint8_t gx, uint8_t gy) {
    uint8_t tile_num = board[gy][gx];
    uint8_t sx = GRID_X + gx * CELL_W;  /* Screen X in BG tiles */
    uint8_t sy = GRID_Y + gy * CELL_H;  /* Screen Y in BG tiles */
    uint8_t pal = get_tile_palette(tile_num);

    /* Set CGB attributes (palette) for this 3x3 area */
    VBK_REG = 1;  /* Switch to attribute map */
    uint8_t r, c_idx;
    for (r = 0; r < CELL_H; r++) {
        for (c_idx = 0; c_idx < CELL_W; c_idx++) {
            set_bkg_tile_xy(sx + c_idx, sy + r, pal);
        }
    }
    VBK_REG = 0;  /* Switch back to tile map */

    if (tile_num == 0) {
        /* Empty cell - fill with dark tiles */
        for (r = 0; r < CELL_H; r++) {
            for (c_idx = 0; c_idx < CELL_W; c_idx++) {
                set_bkg_tile_xy(sx + c_idx, sy + r, T_EMPTY_CELL);
            }
        }
    } else {
        /* Draw tile border */
        set_bkg_tile_xy(sx, sy, T_TILE_TL);
        set_bkg_tile_xy(sx + 1, sy, T_TILE_T);
        set_bkg_tile_xy(sx + 2, sy, T_TILE_TR);
        set_bkg_tile_xy(sx, sy + 1, T_TILE_L);
        set_bkg_tile_xy(sx + 2, sy + 1, T_TILE_R);
        set_bkg_tile_xy(sx, sy + 2, T_TILE_BL);
        set_bkg_tile_xy(sx + 1, sy + 2, T_TILE_B);
        set_bkg_tile_xy(sx + 2, sy + 2, T_TILE_BR);

        /* Draw number in center */
        if (tile_num <= 9) {
            /* Single digit: use tiles 10-18 (digit 1 = tile 10, etc.) */
            set_bkg_tile_xy(sx + 1, sy + 1, T_NUM_START + tile_num - 1);
        } else {
            /* Two digits: 10-15 use paired tiles */
            uint8_t pair_base = T_NUM10_L + (tile_num - 10) * 2;
            set_bkg_tile_xy(sx, sy + 1, T_TILE_L);
            set_bkg_tile_xy(sx + 1, sy + 1, pair_base);      /* tens digit */
            set_bkg_tile_xy(sx + 2, sy + 1, pair_base + 1);  /* ones digit */
        }
    }
}

/* Draw the entire puzzle board */
void draw_board(void) {
    uint8_t gx, gy;
    for (gy = 0; gy < GRID_SIZE; gy++) {
        for (gx = 0; gx < GRID_SIZE; gx++) {
            draw_cell(gx, gy);
        }
    }
}

/* Draw the outer border around the puzzle */
void draw_border(void) {
    uint8_t i;
    uint8_t x1 = GRID_X - 1;
    uint8_t y1 = GRID_Y - 1;
    uint8_t x2 = GRID_X + GRID_SIZE * CELL_W;
    uint8_t y2 = GRID_Y + GRID_SIZE * CELL_H;

    /* Set palette for border */
    VBK_REG = 1;
    for (i = x1; i <= x2; i++) {
        set_bkg_tile_xy(i, y1, 0);
        set_bkg_tile_xy(i, y2, 0);
    }
    for (i = y1; i <= y2; i++) {
        set_bkg_tile_xy(x1, i, 0);
        set_bkg_tile_xy(x2, i, 0);
    }
    VBK_REG = 0;

    /* Corners */
    set_bkg_tile_xy(x1, y1, T_BORDER_TL);
    set_bkg_tile_xy(x2, y1, T_BORDER_TR);
    set_bkg_tile_xy(x1, y2, T_BORDER_BL);
    set_bkg_tile_xy(x2, y2, T_BORDER_BR);

    /* Top and bottom edges */
    for (i = x1 + 1; i < x2; i++) {
        set_bkg_tile_xy(i, y1, T_BORDER_T);
        set_bkg_tile_xy(i, y2, T_BORDER_B);
    }

    /* Left and right edges */
    for (i = y1 + 1; i < y2; i++) {
        set_bkg_tile_xy(x1, i, T_BORDER_L);
        set_bkg_tile_xy(x2, i, T_BORDER_R);
    }
}

/* Draw the move counter in the HUD area */
void draw_hud(void) {
    uint8_t y = GRID_Y + GRID_SIZE * CELL_H + 2;

    /* Set palette for HUD text */
    VBK_REG = 1;
    uint8_t i;
    for (i = GRID_X; i < GRID_X + 8; i++) {
        set_bkg_tile_xy(i, y, 7);
    }
    VBK_REG = 0;

    /* "MOVES:" label - we'll just show the number since we lack font tiles */
    /* Draw the move count */
    put_number(GRID_X + 1, y, move_count);
}

/* Draw cursor highlight around selected cell */
void draw_cursor(uint8_t gx, uint8_t gy, uint8_t show) {
    uint8_t sx = GRID_X + gx * CELL_W;
    uint8_t sy = GRID_Y + gy * CELL_H;

    /* Use win-state palette (gold) for cursor highlight */
    VBK_REG = 1;
    if (show) {
        /* Set corners to gold palette to highlight */
        set_bkg_tile_xy(sx, sy, 6);
        set_bkg_tile_xy(sx + 2, sy, 6);
        set_bkg_tile_xy(sx, sy + 2, 6);
        set_bkg_tile_xy(sx + 2, sy + 2, 6);
    } else {
        /* Restore normal palette */
        uint8_t pal = get_tile_palette(board[gy][gx]);
        set_bkg_tile_xy(sx, sy, pal);
        set_bkg_tile_xy(sx + 2, sy, pal);
        set_bkg_tile_xy(sx, sy + 2, pal);
        set_bkg_tile_xy(sx + 2, sy + 2, pal);
    }
    VBK_REG = 0;
}

/* ======== Puzzle Logic ======== */

/* Check if the puzzle is solved */
uint8_t check_win(void) {
    uint8_t expected = 1;
    uint8_t r, c_idx;
    for (r = 0; r < GRID_SIZE; r++) {
        for (c_idx = 0; c_idx < GRID_SIZE; c_idx++) {
            if (r == GRID_SIZE - 1 && c_idx == GRID_SIZE - 1) {
                /* Last cell should be empty */
                if (board[r][c_idx] != EMPTY_TILE) return 0;
            } else {
                if (board[r][c_idx] != expected) return 0;
                expected++;
            }
        }
    }
    return 1;
}

/* Try to move a tile from (from_r, from_c) into the empty space */
uint8_t try_move(uint8_t from_r, uint8_t from_c) {
    /* Check if the source is adjacent to the empty cell */
    int8_t dr = (int8_t)empty_row - (int8_t)from_r;
    int8_t dc = (int8_t)empty_col - (int8_t)from_c;

    if ((dr == 0 && (dc == 1 || dc == -1)) ||
        (dc == 0 && (dr == 1 || dr == -1))) {
        /* Swap the tile with the empty space */
        board[empty_row][empty_col] = board[from_r][from_c];
        board[from_r][from_c] = EMPTY_TILE;

        /* Redraw affected cells */
        uint8_t old_er = empty_row;
        uint8_t old_ec = empty_col;
        empty_row = from_r;
        empty_col = from_c;

        draw_cell(old_ec, old_er);
        draw_cell(empty_col, empty_row);

        move_count++;
        draw_hud();

        return 1;
    }
    return 0;
}

/* Initialize the board in solved state */
void init_board(void) {
    uint8_t r, c_idx;
    uint8_t val = 1;
    for (r = 0; r < GRID_SIZE; r++) {
        for (c_idx = 0; c_idx < GRID_SIZE; c_idx++) {
            if (r == GRID_SIZE - 1 && c_idx == GRID_SIZE - 1) {
                board[r][c_idx] = EMPTY_TILE;
            } else {
                board[r][c_idx] = val++;
            }
        }
    }
    empty_row = GRID_SIZE - 1;
    empty_col = GRID_SIZE - 1;
}

/* Shuffle the board by making random valid moves */
void shuffle_board(void) {
    uint8_t i;
    uint8_t last_dir = 0xFF;

    initrand(seed_counter);

    for (i = 0; i < 200; i++) {
        uint8_t dir = ((uint8_t)rand()) & 0x03;

        /* Don't undo the previous move */
        if ((dir ^ last_dir) == 0x01) continue;

        switch (dir) {
            case 0: /* Try moving tile from above into empty */
                if (empty_row > 0) {
                    board[empty_row][empty_col] = board[empty_row - 1][empty_col];
                    board[empty_row - 1][empty_col] = EMPTY_TILE;
                    empty_row--;
                    last_dir = dir;
                }
                break;
            case 1: /* From below */
                if (empty_row < GRID_SIZE - 1) {
                    board[empty_row][empty_col] = board[empty_row + 1][empty_col];
                    board[empty_row + 1][empty_col] = EMPTY_TILE;
                    empty_row++;
                    last_dir = dir;
                }
                break;
            case 2: /* From left */
                if (empty_col > 0) {
                    board[empty_row][empty_col] = board[empty_row][empty_col - 1];
                    board[empty_row][empty_col - 1] = EMPTY_TILE;
                    empty_col--;
                    last_dir = dir;
                }
                break;
            case 3: /* From right */
                if (empty_col < GRID_SIZE - 1) {
                    board[empty_row][empty_col] = board[empty_row][empty_col + 1];
                    board[empty_row][empty_col + 1] = EMPTY_TILE;
                    empty_col++;
                    last_dir = dir;
                }
                break;
        }
    }
}

/* Flash all tiles gold when the player wins */
void win_animation(void) {
    uint8_t i, gx, gy;

    for (i = 0; i < 6; i++) {
        /* Flash all cells to gold palette */
        VBK_REG = 1;
        for (gy = 0; gy < GRID_SIZE; gy++) {
            for (gx = 0; gx < GRID_SIZE; gx++) {
                uint8_t sx = GRID_X + gx * CELL_W;
                uint8_t sy = GRID_Y + gy * CELL_H;
                uint8_t pal = (i & 1) ? get_tile_palette(board[gy][gx]) : 6;
                uint8_t r, c_idx;
                for (r = 0; r < CELL_H; r++) {
                    for (c_idx = 0; c_idx < CELL_W; c_idx++) {
                        set_bkg_tile_xy(sx + c_idx, sy + r, pal);
                    }
                }
            }
        }
        VBK_REG = 0;

        /* Wait ~20 frames */
        uint8_t f;
        for (f = 0; f < 20; f++) {
            wait_vbl_done();
        }
    }
}

/* Title screen - wait for START and accumulate random seed */
void title_screen(void) {
    /* Clear screen */
    uint8_t x, y;
    for (y = 0; y < 18; y++) {
        for (x = 0; x < 20; x++) {
            set_bkg_tile_xy(x, y, T_BLANK);
        }
    }

    /* Set palette for title */
    VBK_REG = 1;
    for (y = 0; y < 18; y++) {
        for (x = 0; x < 20; x++) {
            set_bkg_tile_xy(x, y, 7);
        }
    }
    VBK_REG = 0;

    /* Draw "15" in large tiles in center */
    /* "1" */
    set_bkg_tile_xy(7, 5, T_NUM_START);   /* tile for "1" */
    /* "5" */
    set_bkg_tile_xy(9, 5, T_NUM_START + 4); /* tile for "5" */

    /* Draw a small puzzle icon */
    set_bkg_tile_xy(7, 7, T_TILE_TL);
    set_bkg_tile_xy(8, 7, T_TILE_T);
    set_bkg_tile_xy(9, 7, T_TILE_T);
    set_bkg_tile_xy(10, 7, T_TILE_TR);

    set_bkg_tile_xy(7, 8, T_TILE_L);
    set_bkg_tile_xy(8, 8, T_NUM_START + 0);  /* "1" */
    set_bkg_tile_xy(9, 8, T_NUM_START + 1);  /* "2" */
    set_bkg_tile_xy(10, 8, T_TILE_R);

    set_bkg_tile_xy(7, 9, T_TILE_L);
    set_bkg_tile_xy(8, 9, T_NUM_START + 2);  /* "3" */
    set_bkg_tile_xy(9, 9, T_EMPTY_CELL);     /* empty */
    set_bkg_tile_xy(10, 9, T_TILE_R);

    set_bkg_tile_xy(7, 10, T_TILE_BL);
    set_bkg_tile_xy(8, 10, T_TILE_B);
    set_bkg_tile_xy(9, 10, T_TILE_B);
    set_bkg_tile_xy(10, 10, T_TILE_BR);

    /* Color the puzzle icon */
    VBK_REG = 1;
    set_bkg_tile_xy(8, 8, 1);  /* blue */
    set_bkg_tile_xy(9, 8, 2);  /* green */
    set_bkg_tile_xy(8, 9, 3);  /* orange */
    set_bkg_tile_xy(9, 9, 5);  /* dark (empty) */
    VBK_REG = 0;

    SHOW_BKG;

    /* Wait for START, accumulating randomness */
    seed_counter = 0;
    while (1) {
        wait_vbl_done();
        seed_counter++;
        if (joypad() & J_START) break;
    }

    /* Wait for button release */
    while (joypad() & J_START) {
        wait_vbl_done();
    }
}

/* ======== Main Entry Point ======== */

void main(void) {
    /* Detect and enable CGB mode */
    if (_cpu == CGB_TYPE) {
        cpu_fast();
    }

    DISPLAY_OFF;

    /* Load tile data into VRAM */
    set_bkg_data(0, PUZZLE_TILES_COUNT, puzzle_tiles);

    /* Set CGB palettes */
    set_bkg_palette(0, 8, bg_palettes);

    SHOW_BKG;
    DISPLAY_ON;

    /* Show title screen */
    title_screen();

    /* Start new game loop */
    while (1) {
        DISPLAY_OFF;

        /* Initialize game state */
        move_count = 0;
        game_won = 0;
        cursor_row = 0;
        cursor_col = 0;
        input_cooldown = 0;

        /* Set up the board */
        init_board();
        shuffle_board();

        /* Clear the screen */
        uint8_t cx, cy;
        for (cy = 0; cy < 18; cy++) {
            for (cx = 0; cx < 20; cx++) {
                set_bkg_tile_xy(cx, cy, T_BLANK);
                VBK_REG = 1;
                set_bkg_tile_xy(cx, cy, 0);
                VBK_REG = 0;
            }
        }

        /* Draw game elements */
        draw_border();
        draw_board();
        draw_hud();
        draw_cursor(cursor_col, cursor_row, 1);

        DISPLAY_ON;

        /* ======== Game Loop ======== */
        while (!game_won) {
            wait_vbl_done();

            if (input_cooldown > 0) {
                input_cooldown--;
                continue;
            }

            uint8_t keys = joypad();

            if (keys & (J_UP | J_DOWN | J_LEFT | J_RIGHT)) {
                /* Erase old cursor */
                draw_cursor(cursor_col, cursor_row, 0);

                uint8_t old_row = cursor_row;
                uint8_t old_col = cursor_col;

                if ((keys & J_UP) && cursor_row > 0) {
                    cursor_row--;
                }
                if ((keys & J_DOWN) && cursor_row < GRID_SIZE - 1) {
                    cursor_row++;
                }
                if ((keys & J_LEFT) && cursor_col > 0) {
                    cursor_col--;
                }
                if ((keys & J_RIGHT) && cursor_col < GRID_SIZE - 1) {
                    cursor_col++;
                }

                /* Draw new cursor */
                draw_cursor(cursor_col, cursor_row, 1);
                input_cooldown = INPUT_DELAY;
            }

            if (keys & J_A) {
                /* Try to slide the selected tile into the empty space */
                if (board[cursor_row][cursor_col] != EMPTY_TILE) {
                    if (try_move(cursor_row, cursor_col)) {
                        /* Redraw cursor at current position */
                        draw_cursor(cursor_col, cursor_row, 1);

                        /* Check for win */
                        if (check_win()) {
                            game_won = 1;
                        }
                    }
                }
                input_cooldown = INPUT_DELAY;
            }

            /* SELECT: auto-slide - push tile toward empty if possible */
            if (keys & J_SELECT) {
                /* Quick move: if cursor is on a tile adjacent to empty, slide it */
                if (board[cursor_row][cursor_col] != EMPTY_TILE) {
                    if (try_move(cursor_row, cursor_col)) {
                        draw_cursor(cursor_col, cursor_row, 1);
                        if (check_win()) {
                            game_won = 1;
                        }
                    }
                }
                input_cooldown = INPUT_DELAY;
            }
        }

        /* Win! */
        win_animation();

        /* Wait for START to play again */
        while (1) {
            wait_vbl_done();
            if (joypad() & J_START) break;
        }
        while (joypad() & J_START) {
            wait_vbl_done();
        }
    }
}
