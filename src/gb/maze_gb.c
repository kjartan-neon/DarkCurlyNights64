/* ==========================================================================
 * DarkCurlyNights64 — Game Boy Port
 * maze_gb.c
 *
 * Reusable block-pushing (Sokoban-style) maze puzzle. See maze_gb.h.
 *
 * All functions here are BANKED so the linker (auto-banking) places them in a
 * dedicated ROM bank, keeping the HOME segment free. They freely call the UI
 * helpers defined in main_gb.c, which live in the always-mapped HOME segment.
 * ========================================================================== */

#pragma bank 255

#include <gb/gb.h>
#include <stdint.h>

#include "maze_gb.h"

/* --------------------------------------------------------------------------
 * Screen / tile layout (mirrors the constants in main_gb.c)
 * -------------------------------------------------------------------------- */
#define SCREEN_TILES_X   20
#define ROW_TITLE         9
#define ROW_OPTIONS_END  17
#define TILE_BASE_FONT    0
#define TILE_BASE_SCENE  76
#define TILE_BLANK       TILE_BASE_FONT

/* Custom maze tiles reuse the scene-bitmap VRAM region. No scene is displayed
 * while the puzzle runs; load_scene() re-uploads scene tiles afterwards. */
#define MAZE_TILE_WALL    (TILE_BASE_SCENE + 0)
#define MAZE_TILE_BOX     (TILE_BASE_SCENE + 1)
#define MAZE_TILE_PLAYER  (TILE_BASE_SCENE + 2)

/* On-screen placement of the grid's top-left corner (in tiles). The level
 * fills the whole 20x18 screen, so the grid is drawn from the top-left. */
#define MAZE_OFFSET_X  0
#define MAZE_OFFSET_Y  0

#define MAZE_MAX_W      20
#define MAZE_MAX_H      18
#define MAZE_MAX_CELLS  (MAZE_MAX_W * MAZE_MAX_H)

/* --------------------------------------------------------------------------
 * UI helpers provided by main_gb.c (HOME segment)
 * -------------------------------------------------------------------------- */
extern void    clear_rows(uint8_t from_row, uint8_t to_row);
extern void    wait_for_release(uint8_t keys);
extern void    wait_for_page_advance_arrow(void);
extern void    draw_text_line(uint8_t x, uint8_t y, const char* text);
extern uint8_t ui_tile_for_char(char ch);
extern uint8_t draw_wrapped_lines(uint8_t x, uint8_t start_row, uint8_t end_row, uint8_t width, const char* text);

/* --------------------------------------------------------------------------
 * Puzzle state and level data
 * -------------------------------------------------------------------------- */
static char    g_maze_work[MAZE_MAX_CELLS];
static uint8_t g_puzzle_done_scene7 = 0;

/* "Fallen debris" level: fills the whole 20x18 screen with a one-tile-wide
 * serpentine corridor. The player enters top-left (@) and snakes down to the
 * exit (E) at the bottom-left. Seven debris blocks ($) each guard a turn and
 * must be pushed aside (into a dead-end pocket) before the corridor bends. */
static const char MAZE_DEBRIS_MAP[] =
    "####################"
    "#@.......$.......$.#"
    "#########.$.#####.##"
    "#.$.....#.$..#.....#"
    "##.####.#########..#"
    "#.....#..........$.#"
    "#####.###########.##"
    "#.$...#............#"
    "##.#####.########.##"
    "#........#......$..#"
    "#####$####.######.##"
    "#.$...#.$....#.....#"
    "##.##.#..#####..####"
    "#..............$...#"
    "###..#..#########.##"
    "#E.#####...........#"
    "##.......###########"
    "####################";

static const MazeLevel MAZE_DEBRIS_LEVEL = { 20, 18, MAZE_DEBRIS_MAP };

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/* Upload the three custom maze tiles (wall / box / player) into VRAM. */
static void load_maze_tiles(void) {
    static const uint8_t wall_tile[16] = {
        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF
    };
    static const uint8_t box_tile[16] = {
        0xFF, 0xFF, 0x81, 0x81, 0xBD, 0xBD, 0xA5, 0xA5,
        0xA5, 0xA5, 0xBD, 0xBD, 0x81, 0x81, 0xFF, 0xFF
    };
    static const uint8_t player_tile[16] = {
        0x18, 0x18, 0x3C, 0x3C, 0x7E, 0x7E, 0xFF, 0xFF,
        0xFF, 0xFF, 0x7E, 0x7E, 0x3C, 0x3C, 0x18, 0x18
    };

    set_bkg_data(MAZE_TILE_WALL, 1, wall_tile);
    set_bkg_data(MAZE_TILE_BOX, 1, box_tile);
    set_bkg_data(MAZE_TILE_PLAYER, 1, player_tile);
}

/* Copy the immutable level layout into the mutable work buffer and locate the
 * player start ('@' becomes floor). */
static void reset_maze(const MazeLevel* level, char* work, uint8_t* px, uint8_t* py) {
    uint16_t count = (uint16_t)level->w * level->h;
    for (uint16_t i = 0; i < count; i++) {
        char c = level->map[i];
        if (c == '@') {
            *px = (uint8_t)(i % level->w);
            *py = (uint8_t)(i / level->w);
            work[i] = '.';
        } else {
            work[i] = c;
        }
    }
}

/* Render the whole maze grid to the background map. */
static void draw_maze(const MazeLevel* level, const char* work, uint8_t px, uint8_t py) {
    for (uint8_t gy = 0; gy < level->h; gy++) {
        for (uint8_t gx = 0; gx < level->w; gx++) {
            uint8_t tile;
            if (gx == px && gy == py) {
                tile = MAZE_TILE_PLAYER;
            } else {
                char c = work[(uint16_t)gy * level->w + gx];
                if (c == '#')      tile = MAZE_TILE_WALL;
                else if (c == '$') tile = MAZE_TILE_BOX;
                else if (c == 'E') tile = ui_tile_for_char('E');
                else               tile = TILE_BLANK;
            }
            set_bkg_tile_xy(MAZE_OFFSET_X + gx, MAZE_OFFSET_Y + gy, tile);
        }
    }
}

/* Attempt to move the player by (dx, dy), applying Sokoban push rules.
 * Returns 0 = blocked, 1 = moved, 2 = moved onto the exit (puzzle solved). */
static uint8_t maze_try_move(const MazeLevel* level, char* work, uint8_t* px, uint8_t* py, int8_t dx, int8_t dy) {
    int8_t nx = (int8_t)(*px) + dx;
    int8_t ny = (int8_t)(*py) + dy;
    if (nx < 0 || ny < 0 || nx >= (int8_t)level->w || ny >= (int8_t)level->h) {
        return 0;
    }

    uint16_t nidx = (uint16_t)ny * level->w + nx;
    char target = work[nidx];

    if (target == '#') {
        return 0;
    }

    if (target == '$') {
        int8_t bx = nx + dx;
        int8_t by = ny + dy;
        if (bx < 0 || by < 0 || bx >= (int8_t)level->w || by >= (int8_t)level->h) {
            return 0;
        }
        uint16_t bidx = (uint16_t)by * level->w + bx;
        if (work[bidx] != '.') {
            return 0;  /* blocked by wall, another block, or the exit */
        }
        work[bidx] = '$';
        work[nidx] = '.';
        *px = (uint8_t)nx;
        *py = (uint8_t)ny;
        return 1;
    }

    /* Floor or exit */
    *px = (uint8_t)nx;
    *py = (uint8_t)ny;
    return (target == 'E') ? 2 : 1;
}

/* Show the custom instructional headline before the puzzle begins. */
static void show_maze_headline(const char* headline) {
    clear_rows(0, ROW_OPTIONS_END);
    draw_wrapped_lines(0, 2, 15, SCREEN_TILES_X, headline);
    wait_for_page_advance_arrow();
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void maze_reset_progress(void) BANKED {
    g_puzzle_done_scene7 = 0;
}

uint8_t scene_requires_maze(uint8_t scene_id, const MazeLevel** level_out, const char** headline_out) BANKED {
    if (scene_id == 7 && !g_puzzle_done_scene7) {
        *level_out = &MAZE_DEBRIS_LEVEL;
        *headline_out = "You must get trough the fallen debris to get out. Push to move. A to restart";
        return 1;
    }
    return 0;
}

void run_maze_puzzle(const MazeLevel* level, const char* headline) BANKED {
    uint8_t px, py;
    uint8_t solved = 0;

    show_maze_headline(headline);

    load_maze_tiles();
    reset_maze(level, g_maze_work, &px, &py);
    draw_maze(level, g_maze_work, px, py);

    wait_for_release(J_A | J_B | J_START | J_LEFT | J_RIGHT | J_UP | J_DOWN);

    while (!solved) {
        wait_vbl_done();
        uint8_t keys = joypad();

        if (keys & J_A) {
            reset_maze(level, g_maze_work, &px, &py);
            draw_maze(level, g_maze_work, px, py);
            wait_for_release(J_A);
            continue;
        }

        int8_t dx = 0, dy = 0;
        if (keys & J_LEFT)       dx = -1;
        else if (keys & J_RIGHT) dx = 1;
        else if (keys & J_UP)    dy = -1;
        else if (keys & J_DOWN)  dy = 1;

        if (dx != 0 || dy != 0) {
            uint8_t result = maze_try_move(level, g_maze_work, &px, &py, dx, dy);
            if (result != 0) {
                draw_maze(level, g_maze_work, px, py);
            }
            if (result == 2) {
                solved = 1;
            }
            wait_for_release(J_LEFT | J_RIGHT | J_UP | J_DOWN);
        }
    }

    clear_rows(ROW_TITLE, ROW_OPTIONS_END);
    draw_text_line(4, 13, "Path cleared!");
    wait_for_page_advance_arrow();

    g_puzzle_done_scene7 = 1;
}
