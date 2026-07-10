/* ==========================================================================
 * DarkCurlyNights64 — Game Boy Port
 * main_gb.c
 *
 * Toolchain:  GBDK-2020  (lcc / gbdk-lcc, target: gameboy)
 * Screen:     160×144 pixels, 20×18 tile grid, 2bpp (4 shades of grey)
 *
 * Story data is shared with the C64 version via `generated_story.h`.
 * Scene images are embedded as 2bpp tile arrays in `sceneNN_bitmap_gb.h`.
 *
 * Controls:
 *   UP / DOWN   — move selection cursor among available options
 *   A           — confirm selection / advance
 *   START       — same as A (convenience)
 * ========================================================================== */

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Shared story data (same header as C64 build) */
#include "../generated_story.h"

/* Per-scene 2bpp bitmap headers */
#include "intro_bitmap_gb.h"
#include "scene01_bitmap_gb.h"
#include "scene02_bitmap_gb.h"
#include "scene03_bitmap_gb.h"
#include "scene04_bitmap_gb.h"
#include "scene05_bitmap_gb.h"
#include "scene06_bitmap_gb.h"
#include "scene07_bitmap_gb.h"
#include "scene08_bitmap_gb.h"
#include "scene09_bitmap_gb.h"
#include "scene10_bitmap_gb.h"
#include "scene11_bitmap_gb.h"
#include "scene12_bitmap_gb.h"
#include "scene13_bitmap_gb.h"
#include "scene14_bitmap_gb.h"
#include "scene15_bitmap_gb.h"
#include "scene16_bitmap_gb.h"
#include "scene17_bitmap_gb.h"
#include "scene18_bitmap_gb.h"
#include "scene19_bitmap_gb.h"
#include "scene20_bitmap_gb.h"
#include "scene21_bitmap_gb.h"
#include "scene22_bitmap_gb.h"
#include "scene23_bitmap_gb.h"
#include "scene24_bitmap_gb.h"
#include "scene25_bitmap_gb.h"
#include "scene26_bitmap_gb.h"
#include "scene27_bitmap_gb.h"
#include "scene28_bitmap_gb.h"
#include "scene29_bitmap_gb.h"
#include "scene30_bitmap_gb.h"

#include "maze_gb.h"

/* --------------------------------------------------------------------------
 * Screen layout constants
 * --------------------------------------------------------------------------
 * The Game Boy display is 20×18 tiles (160×144 px).
 * We divide the screen vertically:
 *   Rows 0-8   (tiles) — scene bitmap (top 72px of the 144px display)
 *   Row 9      — scene title (bold)
 *   Rows 10-13 — scene description text (wrapped)
 *   Rows 14-17 — player options
 * -------------------------------------------------------------------------- */
#define SCREEN_TILES_X   20
#define SCREEN_TILES_Y   18
#define IMAGE_TILE_ROWS  9
#define IMAGE_TILE_COUNT (SCREEN_TILES_X * IMAGE_TILE_ROWS)

/* Tile rows for each UI region */
#define ROW_IMAGE_START   0
#define ROW_IMAGE_END     8   /* inclusive, 9 tile-rows = 72px */
#define ROW_TITLE         9
#define ROW_DESC_START   10
#define ROW_DESC_END     13   /* 4 rows for description */
#define ROW_OPTIONS_START 14
#define ROW_OPTIONS_END  17   /* 4 rows for options (cursor + 3 visible) */

/* Maximum options shown on screen at once */
#define MAX_MENU_OPTIONS   4

/* Gameplay text/menu area uses the full bottom half (rows 9..17) */
#define ROW_BOTTOM_START  9
#define ROW_BOTTOM_END    17
#define ROW_BOTTOM_COUNT  (ROW_BOTTOM_END - ROW_BOTTOM_START + 1)

/* Full-screen options layout */
#define ROW_OPTIONS_PROMPT      0
#define ROW_OPTIONS_GAP_AFTER_PROMPT 1
#define ROW_OPTIONS_FIRST_SLOT  2
#define OPTION_TEXT_X       2
#define OPTION_TEXT_WIDTH  (SCREEN_TILES_X - OPTION_TEXT_X)
#define OPTION_SLOT_LINES   3
#define OPTION_SLOT_STRIDE  4
#define OPTION_SLOT_COUNT   4
#define ROW_OPTIONS_FULL_START  0
#define ROW_OPTIONS_FULL_END   17

/* Synthetic menu entries that do not map to STORY_OPTIONS[] */
#define MENU_ITEM_READ_AGAIN    0xFF

/* VRAM tile layout:
 * 0..75    : compact mixed-case UI font (curated subset of IBM font)
 * 76..255  : scene/intro bitmap tiles (180 tiles)
 */
#define TILE_BASE_FONT   0
#define TILE_BASE_SCENE  76

#define UI_FONT_CHARS " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.,!?':;\"-()/*"
#define UI_FONT_TILE_COUNT ((uint8_t)(sizeof(UI_FONT_CHARS) - 1u))

/* Space is the first glyph in UI_FONT_CHARS */
#define TILE_BLANK       TILE_BASE_FONT

/* --------------------------------------------------------------------------
 * Palette — DMG greyscale
 * BGP register: bits [7:6]=colour3, [5:4]=colour2, [3:2]=colour1, [1:0]=colour0
 * 0=white, 1=light grey, 2=dark grey, 3=black
 * Standard mapping: index 0→white(0), 1→light(1), 2→dark(2), 3→black(3)
 * -------------------------------------------------------------------------- */
#define PALETTE_NORMAL 0xE4u  /* 11 10 01 00 — darkest→lightest */

/* --------------------------------------------------------------------------
 * Player state
 * -------------------------------------------------------------------------- */
static uint8_t g_flags = 0;          /* Collected item flags */
static uint8_t g_current_scene = 1;  /* 1-indexed scene id */
static uint8_t g_cursor = 0;         /* Option cursor (0-based) */
static uint8_t g_blink_counter = 0;  /* Counter for blinking indicator */
static uint8_t g_blink_state = 0;    /* 0 = show number, 1 = show star */
static const StoryScene* g_active_scene = NULL;
static uint8_t g_menu_items[MAX_MENU_OPTIONS];
static uint8_t g_menu_count = 0;

extern const uint8_t font_ibm_fixed_tiles[];

/* --------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------- */
static void        show_intro_screen(void);
static void        show_part1_end_screen(void);
static void        load_scene(uint8_t scene_id);
static void        draw_scene_bitmap(uint8_t scene_id);
static const char* draw_text_page(uint8_t start_row, uint8_t end_row, const char* text);
static void        draw_options(const StoryScene* scene);
void               clear_rows(uint8_t from_row, uint8_t to_row);
void               wait_for_release(uint8_t keys);
static void        get_scene_bitmap(uint8_t scene_id, const uint8_t** data, uint8_t* bank);
void               wait_for_page_advance_arrow(void);
static void        show_scene_story_pages(const StoryScene* scene);
static const StoryScene* find_scene_by_id(uint8_t scene_id);
static uint8_t     build_menu_items(const StoryScene* scene, uint8_t* menu_items, uint8_t max_items);
static void        refresh_menu_items(const StoryScene* scene);
static uint8_t     menu_slot_row(uint8_t menu_index);
static void        draw_menu_indicator(uint8_t menu_index, uint8_t selected);
static void        update_menu_cursor(uint8_t previous_cursor, uint8_t new_cursor);
static const char* menu_item_text(uint8_t menu_item);
static char        peek_display_char(const char* text, uint8_t* advance);
static char        next_display_char(const char** text);
uint8_t            ui_tile_for_char(char ch);
static void        load_ui_font_tiles(void);
uint8_t            draw_wrapped_lines(uint8_t x, uint8_t start_row, uint8_t end_row, uint8_t width, const char* text);
static void        initialize_palettes(void);

/* --------------------------------------------------------------------------
 * Scene bitmap lookup table
 * Returns data pointer + ROM bank for scene_id (1-indexed).
 * -------------------------------------------------------------------------- */
static void get_scene_bitmap(uint8_t scene_id, const uint8_t** data, uint8_t* bank) {
    /* scene_id is 1-based */
    switch (scene_id) {
        case  1: *data = SCENE01_BITMAP_GB_DATA; *bank = BANK(SCENE01_BITMAP_GB_DATA); return;
        case  2: *data = SCENE02_BITMAP_GB_DATA; *bank = BANK(SCENE02_BITMAP_GB_DATA); return;
        case  3: *data = SCENE03_BITMAP_GB_DATA; *bank = BANK(SCENE03_BITMAP_GB_DATA); return;
        case  4: *data = SCENE04_BITMAP_GB_DATA; *bank = BANK(SCENE04_BITMAP_GB_DATA); return;
        case  5: *data = SCENE05_BITMAP_GB_DATA; *bank = BANK(SCENE05_BITMAP_GB_DATA); return;
        case  6: *data = SCENE06_BITMAP_GB_DATA; *bank = BANK(SCENE06_BITMAP_GB_DATA); return;
        case  7: *data = SCENE07_BITMAP_GB_DATA; *bank = BANK(SCENE07_BITMAP_GB_DATA); return;
        case  8: *data = SCENE08_BITMAP_GB_DATA; *bank = BANK(SCENE08_BITMAP_GB_DATA); return;
        case  9: *data = SCENE09_BITMAP_GB_DATA; *bank = BANK(SCENE09_BITMAP_GB_DATA); return;
        case 10: *data = SCENE10_BITMAP_GB_DATA; *bank = BANK(SCENE10_BITMAP_GB_DATA); return;
        case 11: *data = SCENE11_BITMAP_GB_DATA; *bank = BANK(SCENE11_BITMAP_GB_DATA); return;
        case 12: *data = SCENE12_BITMAP_GB_DATA; *bank = BANK(SCENE12_BITMAP_GB_DATA); return;
        case 13: *data = SCENE13_BITMAP_GB_DATA; *bank = BANK(SCENE13_BITMAP_GB_DATA); return;
        case 14: *data = SCENE14_BITMAP_GB_DATA; *bank = BANK(SCENE14_BITMAP_GB_DATA); return;
        case 15: *data = SCENE15_BITMAP_GB_DATA; *bank = BANK(SCENE15_BITMAP_GB_DATA); return;
        case 16: *data = SCENE16_BITMAP_GB_DATA; *bank = BANK(SCENE16_BITMAP_GB_DATA); return;
        case 17: *data = SCENE17_BITMAP_GB_DATA; *bank = BANK(SCENE17_BITMAP_GB_DATA); return;
        case 18: *data = SCENE18_BITMAP_GB_DATA; *bank = BANK(SCENE18_BITMAP_GB_DATA); return;
        case 19: *data = SCENE19_BITMAP_GB_DATA; *bank = BANK(SCENE19_BITMAP_GB_DATA); return;
        case 20: *data = SCENE20_BITMAP_GB_DATA; *bank = BANK(SCENE20_BITMAP_GB_DATA); return;
        case 21: *data = SCENE21_BITMAP_GB_DATA; *bank = BANK(SCENE21_BITMAP_GB_DATA); return;
        case 22: *data = SCENE22_BITMAP_GB_DATA; *bank = BANK(SCENE22_BITMAP_GB_DATA); return;
        case 23: *data = SCENE23_BITMAP_GB_DATA; *bank = BANK(SCENE23_BITMAP_GB_DATA); return;
        case 24: *data = SCENE24_BITMAP_GB_DATA; *bank = BANK(SCENE24_BITMAP_GB_DATA); return;
        case 25: *data = SCENE25_BITMAP_GB_DATA; *bank = BANK(SCENE25_BITMAP_GB_DATA); return;
        case 26: *data = SCENE26_BITMAP_GB_DATA; *bank = BANK(SCENE26_BITMAP_GB_DATA); return;
        case 27: *data = SCENE27_BITMAP_GB_DATA; *bank = BANK(SCENE27_BITMAP_GB_DATA); return;
        case 28: *data = SCENE28_BITMAP_GB_DATA; *bank = BANK(SCENE28_BITMAP_GB_DATA); return;
        case 29: *data = SCENE29_BITMAP_GB_DATA; *bank = BANK(SCENE29_BITMAP_GB_DATA); return;
        case 30: *data = SCENE30_BITMAP_GB_DATA; *bank = BANK(SCENE30_BITMAP_GB_DATA); return;
        default: *data = SCENE01_BITMAP_GB_DATA; *bank = BANK(SCENE01_BITMAP_GB_DATA); return;
    }
}

/* --------------------------------------------------------------------------
 * Wait until all specified keys are released (debounce helper).
 * -------------------------------------------------------------------------- */
void wait_for_release(uint8_t keys) {
    while (joypad() & keys) {
        wait_vbl_done();
    }
}

/* --------------------------------------------------------------------------
 * Find a scene record by its 1-based id.
 * -------------------------------------------------------------------------- */
static const StoryScene* find_scene_by_id(uint8_t scene_id) {
    for (uint8_t i = 0; i < STORY_SCENE_COUNT; i++) {
        if (STORY_SCENES[i].id == scene_id) {
            return &STORY_SCENES[i];
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * Peek next displayable character, normalizing selected UTF-8 punctuation.
 * -------------------------------------------------------------------------- */
static char peek_display_char(const char* text, uint8_t* advance) {
    const unsigned char* p = (const unsigned char*)text;

    if (p[0] == 0) {
        *advance = 0;
        return '\0';
    }

    if (p[0] < 0x80) {
        *advance = 1;
        return (char)p[0];
    }

    if (p[0] == 0xE2 && p[1] == 0x80) {
        if (p[2] == 0x94) {
            *advance = 3;
            return '-';
        }
        if (p[2] == 0x99) {
            *advance = 3;
            return '\'';
        }
    }

    *advance = 1;
    return '?';
}

/* --------------------------------------------------------------------------
 * Consume one displayable character.
 * -------------------------------------------------------------------------- */
static char next_display_char(const char** text) {
    uint8_t advance;
    char ch = peek_display_char(*text, &advance);
    *text += advance;
    return ch;
}

/* --------------------------------------------------------------------------
 * Resolve a display character to a loaded UI font tile index.
 * Falls back to '?' when character is not present in the curated set.
 * -------------------------------------------------------------------------- */
uint8_t ui_tile_for_char(char ch) {
    for (uint8_t idx = 0; idx < UI_FONT_TILE_COUNT; idx++) {
        if (UI_FONT_CHARS[idx] == ch) {
            return (uint8_t)(TILE_BASE_FONT + idx);
        }
    }

    for (uint8_t idx = 0; idx < UI_FONT_TILE_COUNT; idx++) {
        if (UI_FONT_CHARS[idx] == '?') {
            return (uint8_t)(TILE_BASE_FONT + idx);
        }
    }

    return TILE_BLANK;
}

/* --------------------------------------------------------------------------
 * Load curated mixed-case UI glyphs from GBDK's IBM fixed tile set.
 * -------------------------------------------------------------------------- */
static void load_ui_font_tiles(void) {
    uint8_t tile_data[16];

    for (uint8_t idx = 0; idx < UI_FONT_TILE_COUNT; idx++) {
        uint8_t ascii = (uint8_t)UI_FONT_CHARS[idx];
        uint8_t source_tile = ascii;
        const uint8_t* source = font_ibm_fixed_tiles + ((uint16_t)source_tile * 8u);

        for (uint8_t row = 0; row < 8u; row++) {
            uint8_t bits = source[row];
            tile_data[(uint8_t)(row * 2u)] = bits;
            tile_data[(uint8_t)(row * 2u + 1u)] = bits;
        }

        set_bkg_data((uint8_t)(TILE_BASE_FONT + idx), 1, tile_data);
    }
}

/* --------------------------------------------------------------------------
 * Draw a single character using the active GBDK font without advancing the
 * cursor or triggering console scrolling.
 * -------------------------------------------------------------------------- */
static void draw_char_xy(uint8_t x, uint8_t y, char ch) {
    if (x < SCREEN_TILES_X && y < SCREEN_TILES_Y) {
        set_bkg_tile_xy(x, y, ui_tile_for_char(ch));
    }
}

/* --------------------------------------------------------------------------
 * Draw a single line of text without allowing console wrapping.
 * Characters beyond the right edge are clipped.
 * -------------------------------------------------------------------------- */
void draw_text_line(uint8_t x, uint8_t y, const char* text) {
    while (*text != '\0' && x < SCREEN_TILES_X) {
        draw_char_xy(x, y, next_display_char(&text));
        x++;
    }
}

/* --------------------------------------------------------------------------
 * Clear a single tile row without allowing console wrapping.
 * -------------------------------------------------------------------------- */
static void clear_row(uint8_t y) {
    for (uint8_t x = 0; x < SCREEN_TILES_X; x++) {
        draw_char_xy(x, y, ' ');
    }
}

/* --------------------------------------------------------------------------
 * Clear a range of tile rows by filling them with the blank tile index.
 * -------------------------------------------------------------------------- */
void clear_rows(uint8_t from_row, uint8_t to_row) {
    for (uint8_t y = from_row; y <= to_row; y++) {
        clear_row(y);
    }
}

/* --------------------------------------------------------------------------
 * Initialize palettes in a DMG-safe way.
 * DMG: keep existing greyscale register mapping.
 * CGB: use GBDK default palette 0 for DMG-like appearance.
 * -------------------------------------------------------------------------- */
static void initialize_palettes(void) {
    BGP_REG = PALETTE_NORMAL;

    if (_cpu == CGB_TYPE) {
        set_default_palette();
    }
}

/* --------------------------------------------------------------------------
 * Draw scene bitmap into the background map.
 *
 * The 2bpp tile data covers the full 160x144 screen (360 tiles total).
 * We upload only the top 10 tile rows (200 tiles) as the scene image.
 * Tiles are loaded into VRAM starting at TILE_BASE_SCENE.
 *
 * Strategy:
 *   - Upload all 360 tiles from the bitmap data into VRAM (tile indices 0-359).
 *   - Map the top 10 rows of the BG map to those tiles sequentially.
 *   - Fill remaining rows with the blank tile via clear_rows().
 * -------------------------------------------------------------------------- */
static void draw_scene_bitmap(uint8_t scene_id) {
    const uint8_t* data;
    uint8_t data_bank;
    uint8_t previous_bank = CURRENT_BANK;

    get_scene_bitmap(scene_id, &data, &data_bank);

    /* Upload top 9 rows worth of tiles (180 tiles) */
    SWITCH_ROM(data_bank);
    set_bkg_data(TILE_BASE_SCENE, IMAGE_TILE_COUNT, data);
    SWITCH_ROM(previous_bank);

    /* Map the top 10 rows (ROW_IMAGE_START … ROW_IMAGE_END) */
    uint8_t tile_idx = 0;
    for (uint8_t row = ROW_IMAGE_START; row <= ROW_IMAGE_END; row++) {
        for (uint8_t col = 0; col < SCREEN_TILES_X; col++) {
            set_bkg_tile_xy(col, row, (uint8_t)(TILE_BASE_SCENE + tile_idx++));
        }
    }

}

/* --------------------------------------------------------------------------
 * Render one wrapped page of story text and return pointer to remaining text.
 * -------------------------------------------------------------------------- */
static const char* draw_text_page(uint8_t start_row, uint8_t end_row, const char* text) {
    uint8_t row = start_row;
    uint8_t col = 0;
    const char* p = text;

    clear_rows(start_row, end_row);

    while (*p && row <= end_row) {
        uint8_t advance;
        char ch = peek_display_char(p, &advance);

        if (ch == '\n') {
            p += advance;
            row++;
            col = 0;
            continue;
        }

        if (ch == ' ' && col == 0) {
            /* Skip leading spaces at start of a tile row */
            p += advance;
            continue;
        }

        /* Find end of next word */
        const char* word_end = p;
        uint8_t word_len = 0;
        while (*word_end) {
            uint8_t word_advance;
            char word_ch = peek_display_char(word_end, &word_advance);
            if (word_ch == ' ' || word_ch == '\n' || word_ch == '\0') break;
            word_len++;
            word_end += word_advance;
        }

        /* Check if word fits on current row */
        if (col + word_len > SCREEN_TILES_X && col > 0) {
            /* Move to next row; skip any space that would start the new row */
            row++;
            col = 0;
            if (row > end_row) break;
            uint8_t space_advance;
            char space_ch = peek_display_char(p, &space_advance);
            if (space_ch == ' ') {
                p += space_advance;
            }
        }

        /* Print each character of the word */
        while (p < word_end && row <= end_row) {
            draw_char_xy(col, row, next_display_char(&p));
            col++;
            if (col >= SCREEN_TILES_X) {
                col = 0;
                row++;
            }
        }

        /* Print the space (if not at end of row) */
        ch = peek_display_char(p, &advance);
        if (ch == ' ' && col < SCREEN_TILES_X - 1) {
            draw_char_xy(col, row, ' ');
            col++;
            p += advance;
        } else if (ch == '\n') {
            p += advance;
            row++;
            col = 0;
        } else if (ch == ' ') {
            p += advance;  /* Skip space that would be at end of row */
        }
    }

    while (*p) {
        uint8_t advance;
        char ch = peek_display_char(p, &advance);
        if (ch != ' ' && ch != '\n') break;
        p += advance;
    }

    return p;
}

/* --------------------------------------------------------------------------
 * Blinking arrow prompt for paged story text.
 * A / B / START advances to the next page.
 * -------------------------------------------------------------------------- */
void wait_for_page_advance_arrow(void) {
    uint32_t frame_count = 0;
    uint8_t arrow_visible = 1;
    const uint8_t arrow_x = SCREEN_TILES_X - 1;
    const uint8_t arrow_y = ROW_BOTTOM_END;

    draw_char_xy(arrow_x, arrow_y, '*');
    wait_for_release(J_A | J_B | J_START);

    while (!(joypad() & (J_A | J_B | J_START))) {
        frame_count++;
        if (frame_count % 30 == 0) {
            arrow_visible = !arrow_visible;
            draw_char_xy(arrow_x, arrow_y, arrow_visible ? '*' : ' ');
        }
        wait_vbl_done();
    }

    draw_char_xy(arrow_x, arrow_y, ' ');
    wait_for_release(J_A | J_B | J_START);
}

/* --------------------------------------------------------------------------
 * Show the scene description in wrapped, paged chunks.
 * -------------------------------------------------------------------------- */
static void show_scene_story_pages(const StoryScene* scene) {
    const char* page_ptr = scene->description;

    while (1) {
        page_ptr = draw_text_page(ROW_BOTTOM_START, ROW_BOTTOM_END, page_ptr);
        wait_for_page_advance_arrow();

        if (*page_ptr == '\0') {
            break;
        }
    }
}

/* --------------------------------------------------------------------------
 * Build the list of menu items for a scene.
 * - Includes condition-valid story options
 * - Adds "Restart story" if no scene options are available
 * - Always appends "Read again" as the last entry
 * -------------------------------------------------------------------------- */
static uint8_t build_menu_items(const StoryScene* scene, uint8_t* menu_items, uint8_t max_items) {
    uint8_t count = 0;

    for (uint8_t i = 0; i < scene->option_count && count < max_items; i++) {
        uint8_t global_idx = scene->first_option + i;
        if (global_idx < STORY_OPTION_COUNT) {
            menu_items[count++] = global_idx;
        }
    }

    if (count < max_items) {
        menu_items[count++] = MENU_ITEM_READ_AGAIN;
    }

    return count;
}

/* --------------------------------------------------------------------------
 * Rebuild cached menu items for the active scene and clamp cursor.
 * -------------------------------------------------------------------------- */
static void refresh_menu_items(const StoryScene* scene) {
    g_menu_count = 0;

    if (!scene) {
        return;
    }

    g_menu_count = build_menu_items(scene, g_menu_items, MAX_MENU_OPTIONS);
    if (g_menu_count == 0) {
        g_cursor = 0;
    } else if (g_cursor >= g_menu_count) {
        g_cursor = (uint8_t)(g_menu_count - 1);
    }
}

/* --------------------------------------------------------------------------
 * Return the screen row used by a given menu slot.
 * -------------------------------------------------------------------------- */
static uint8_t menu_slot_row(uint8_t menu_index) {
    return (uint8_t)(ROW_OPTIONS_FIRST_SLOT + (menu_index * OPTION_SLOT_STRIDE));
}

/* --------------------------------------------------------------------------
 * Draw the numeric/blinking indicator for a menu slot.
 * -------------------------------------------------------------------------- */
static void draw_menu_indicator(uint8_t menu_index, uint8_t selected) {
    if (menu_index >= g_menu_count || menu_index >= OPTION_SLOT_COUNT) {
        return;
    }

    uint8_t row = menu_slot_row(menu_index);
    if (selected && g_blink_state != 0) {
        draw_char_xy(0, row, '*');
    } else {
        draw_char_xy(0, row, (char)('1' + menu_index));
    }
}

/* --------------------------------------------------------------------------
 * Update only the old/new cursor indicators without redrawing the full menu.
 * -------------------------------------------------------------------------- */
static void update_menu_cursor(uint8_t previous_cursor, uint8_t new_cursor) {
    draw_menu_indicator(previous_cursor, 0);
    draw_menu_indicator(new_cursor, 1);
}

/* --------------------------------------------------------------------------
 * Resolve a menu item id to display text.
 * -------------------------------------------------------------------------- */
static const char* menu_item_text(uint8_t menu_item) {
    if (menu_item == MENU_ITEM_READ_AGAIN) {
        return "Read scene again";
    }
    if (menu_item >= STORY_OPTION_COUNT) {
        return "[Invalid option]";
    }
    return STORY_OPTIONS[menu_item].text;
}

/* --------------------------------------------------------------------------
 * Draw wrapped text block; returns number of lines consumed.
 * -------------------------------------------------------------------------- */
uint8_t draw_wrapped_lines(uint8_t x, uint8_t start_row, uint8_t end_row, uint8_t width, const char* text) {
    uint8_t row = start_row;
    uint8_t col = 0;
    uint8_t lines = 1;
    const char* p = text;

    while (*p && row <= end_row) {
        uint8_t advance;
        char ch = peek_display_char(p, &advance);

        if (ch == '\n') {
            p += advance;
            row++;
            lines++;
            col = 0;
            continue;
        }

        if (ch == ' ' && col == 0) {
            p += advance;
            continue;
        }

        const char* word_end = p;
        uint8_t word_len = 0;
        while (*word_end) {
            uint8_t word_advance;
            char word_ch = peek_display_char(word_end, &word_advance);
            if (word_ch == ' ' || word_ch == '\n' || word_ch == '\0') break;
            word_len++;
            word_end += word_advance;
        }

        if (col + word_len > width && col > 0) {
            row++;
            lines++;
            col = 0;
            if (row > end_row) break;
        }

        while (p < word_end && row <= end_row) {
            uint8_t word_advance;
            char word_ch = peek_display_char(p, &word_advance);
            draw_char_xy(x + col, row, word_ch);
            col++;
            if (col >= width) {
                col = 0;
                row++;
                if (row <= end_row) {
                    lines++;
                }
            }
            p += word_advance;
        }

        ch = peek_display_char(p, &advance);
        if (ch == ' ') {
            if (col > 0 && col < width - 1 && row <= end_row) {
                draw_char_xy(x + col, row, ' ');
                col++;
            }
            p += advance;
        }
    }

    return lines;
}

/* --------------------------------------------------------------------------
 * Test whether cursor_index is visible in the current wrapped options window.
 * -------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------
 * Draw options full-screen: clears everything, 3 slots across all 18 rows.
 * -------------------------------------------------------------------------- */
static void draw_options(const StoryScene* scene) {
    g_active_scene = scene;
    refresh_menu_items(scene);

    /* Clear the FULL screen (bitmap area + text area) */
    clear_rows(ROW_OPTIONS_FULL_START, ROW_OPTIONS_FULL_END);

    draw_text_line(0, ROW_OPTIONS_PROMPT, "  What do you do?");
    clear_row(ROW_OPTIONS_GAP_AFTER_PROMPT);

    if (g_menu_count == 0) return;

    /* Draw each option: number/blinking indicator + wrapped text */
    for (uint8_t idx = 0; idx < g_menu_count && idx < OPTION_SLOT_COUNT; idx++) {
        uint8_t slot_start_row = (uint8_t)(ROW_OPTIONS_FIRST_SLOT + (idx * OPTION_SLOT_STRIDE));
        uint8_t slot_end_row   = (uint8_t)(slot_start_row + OPTION_SLOT_LINES - 1);
        const char* text = menu_item_text(g_menu_items[idx]);

        draw_menu_indicator(idx, (uint8_t)(idx == g_cursor));

        clear_row((uint8_t)(slot_end_row + 1));

        /* Draw wrapped text across all rows of this slot */
        draw_wrapped_lines(OPTION_TEXT_X, slot_start_row, slot_end_row, OPTION_TEXT_WIDTH, text);
    }
}

/* --------------------------------------------------------------------------
 * Show end-of-part screen after the final story scene.
 * Uses intro image with custom closing text.
 * -------------------------------------------------------------------------- */
static void show_part1_end_screen(void) {
    uint8_t previous_bank = CURRENT_BANK;

    SWITCH_ROM(BANK(INTRO_BITMAP_GB_DATA));
    set_bkg_data(TILE_BASE_SCENE, IMAGE_TILE_COUNT, INTRO_BITMAP_GB_DATA);
    SWITCH_ROM(previous_bank);

    uint8_t tile_idx = 0;
    for (uint8_t row = 0; row <= ROW_IMAGE_END; row++) {
        for (uint8_t col = 0; col < SCREEN_TILES_X; col++) {
            set_bkg_tile_xy(col, row, (uint8_t)(TILE_BASE_SCENE + tile_idx++));
        }
    }

    clear_rows(ROW_TITLE, ROW_OPTIONS_END);
    draw_text_line(3, 10, "End of part 1");
    draw_text_line(1, 12, "Elara waits for you");
    draw_text_line(5, 13, "in part 2");
}

/* --------------------------------------------------------------------------
 * Load and render a complete scene:
 *   1. Draw the scene bitmap (top portion of screen)
 *   2. Show paged scene description over full bottom area (rows 9..17)
 *   3. Draw wrapped options over full bottom area (replacing story)
 *
 * Also applies any item flags that the scene grants.
 * -------------------------------------------------------------------------- */
static void load_scene(uint8_t scene_id) {
    const StoryScene* scene = find_scene_by_id(scene_id);
    if (!scene) return;

    g_active_scene = scene;

    /* Reset option cursor to top */
    g_cursor = 0;

    /* --- Draw the scene image --- */
    draw_scene_bitmap(scene_id);

    /* --- Show paged description --- */
    show_scene_story_pages(scene);

    if (scene->option_count == 0) {
        g_active_scene = NULL;
        g_menu_count = 0;
        show_part1_end_screen();
        return;
    }

    /* --- Draw options full-screen (clears entire screen internally) --- */
    draw_options(scene);
}

/* --------------------------------------------------------------------------
 * show_intro_screen()
 *
 * Fills the screen with the intro bitmap and displays the game title.
 * Waits for the player to press A or START before returning.
 * -------------------------------------------------------------------------- */
static void show_intro_screen(void) {
    uint8_t previous_bank = CURRENT_BANK;

    /* Upload top 9 rows worth of intro tiles (180 tiles) */
    SWITCH_ROM(BANK(INTRO_BITMAP_GB_DATA));
    set_bkg_data(TILE_BASE_SCENE, IMAGE_TILE_COUNT, INTRO_BITMAP_GB_DATA);
    SWITCH_ROM(previous_bank);

    /* Map top rows to intro tiles and clear the lower text area */
    uint8_t tile_idx = 0;
    for (uint8_t row = 0; row <= ROW_IMAGE_END; row++) {
        for (uint8_t col = 0; col < SCREEN_TILES_X; col++) {
            set_bkg_tile_xy(col, row, (uint8_t)(TILE_BASE_SCENE + tile_idx++));
        }
    }
    
    /* Clear only the lower text area (leave intro image rows intact) */
    clear_rows(ROW_TITLE, ROW_OPTIONS_END);

    /* Overlay title text — row 9 (one line under image) */
    draw_text_line(2, 9, "DARK CURLY NIGHTS");
    draw_text_line(7, 10, "PART 1");
    draw_text_line(0, 11, "BY KJARTAN MICHALSEN");
    /* Row 12 is blank */
    
    /* Blinking "Press a to start" at row 13 */
    uint32_t frame_count = 0;
    uint8_t blink_state = 1;
    
    wait_for_release(J_A | J_START);
    while (!(joypad() & (J_A | J_START))) {
        frame_count++;
        if (frame_count % 30 == 0) {  /* Toggle every 30 frames (~0.5s at 60fps) */
            blink_state = !blink_state;
            if (blink_state) {
                draw_text_line(2, 13, "PRESS A TO START");
            } else {
                draw_text_line(2, 13, "                ");
            }
        }
        wait_vbl_done();
    }
    wait_for_release(J_A | J_START);
}

/* --------------------------------------------------------------------------
 * main() — Game Boy entry point
 * -------------------------------------------------------------------------- */
void main(void) {
    /* Initialise display */
    DISPLAY_OFF;

    /* Load compact mixed-case UI font (fits with 9 image rows) */
    load_ui_font_tiles();

    initialize_palettes();

    /* Clear the full background map */
    fill_bkg_rect(0, 0, SCREEN_TILES_X, SCREEN_TILES_Y, TILE_BLANK);

    SHOW_BKG;
    DISPLAY_ON;

    /* Show intro screen and wait for player input */
    show_intro_screen();

    /* Start at scene 1 */
    g_current_scene = 1;
    g_flags = 0;
    maze_reset_progress();
    load_scene(g_current_scene);

    /* ===================================================================
     * Main game loop
     * ===================================================================
     * Each iteration:
     *   1. Wait for vertical blank (sync to 60 Hz).
     *   2. Read joypad.
     *   3. Handle UP/DOWN to move cursor.
     *   4. Handle A/START to select the current option and advance scene.
     * ================================================================= */
    while (1) {
        wait_vbl_done();

        uint8_t keys = joypad();

        /* --- Update blink counter --- */
        g_blink_counter++;
        if (g_blink_counter >= 30) {
            g_blink_counter = 0;
            g_blink_state = !g_blink_state;
            
            /* Only redraw the blinking indicator character */
            if (g_active_scene && g_cursor < g_menu_count) {
                draw_menu_indicator(g_cursor, 1);
            }
        }

        /* --- Cursor movement --- */
        if (keys & J_DOWN) {
            const StoryScene* scene = g_active_scene;
            if (scene) {
                /* Move down if not already at last item */
                if (g_menu_count > 0 && g_cursor < (uint8_t)(g_menu_count - 1)) {
                    uint8_t previous_cursor = g_cursor;
                    g_cursor++;
                    g_blink_counter = 0;
                    g_blink_state = 0;
                    update_menu_cursor(previous_cursor, g_cursor);
                }
            }
            wait_for_release(J_DOWN);
        }

        if (keys & J_UP) {
            const StoryScene* scene = g_active_scene;
            if (scene) {
                /* Move up if not already at first item */
                if (g_cursor > 0) {
                    uint8_t previous_cursor = g_cursor;
                    g_cursor--;
                    g_blink_counter = 0;
                    g_blink_state = 0;
                    update_menu_cursor(previous_cursor, g_cursor);
                }
            }
            wait_for_release(J_UP);
        }

        /* --- Option selection (A or START) --- */
        if (keys & (J_A | J_START)) {
            const StoryScene* scene = g_active_scene;

            if (scene) {
                if (g_cursor < g_menu_count) {
                    uint8_t selected = g_menu_items[g_cursor];

                    if (selected == MENU_ITEM_READ_AGAIN) {
                        wait_for_release(J_A | J_START);
                        show_scene_story_pages(scene);
                        clear_rows(ROW_BOTTOM_START, ROW_BOTTOM_END);
                        g_cursor = 0;
                        draw_options(scene);
                    } else if (selected < STORY_OPTION_COUNT) {
                        const StoryOption* chosen = &STORY_OPTIONS[selected];

                        /* Determine target scene */
                        uint8_t next_scene;
                        if (chosen->condition != STORY_COND_NONE &&
                            !(g_flags & chosen->condition) &&
                            chosen->alt_target_scene != 255) {
                            /* Condition not met — use alternate target */
                            next_scene = chosen->alt_target_scene;
                        } else {
                            next_scene = chosen->target_scene;
                        }

                        /* Scene 255 = no target (dead end / game over) */
                        if (next_scene != 255) {
                            wait_for_release(J_A | J_START);

                            /* Play a puzzle first if this scene requires one */
                            const MazeLevel* maze_level;
                            const char* maze_headline;
                            if (scene_requires_maze(next_scene, &maze_level, &maze_headline)) {
                                run_maze_puzzle(maze_level, maze_headline);
                            }

                            g_current_scene = next_scene;
                            load_scene(g_current_scene);
                        }
                    }
                }
            }
        }
    }
}
