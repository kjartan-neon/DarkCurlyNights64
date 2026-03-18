#include <cbm.h>
#include <c64.h>
#include <stdint.h>
#include <string.h>

#include "generated_story.h"
#include "scene01_bitmap.h"
#include "scene02_bitmap.h"

/*
 * This program runs a small interactive story on the Commodore 64.
 *
 * High-level flow:
 * 1) Set up bitmap graphics mode and color memory.
 * 2) Load and show a scene image (embedded as C arrays).
 * 3) Draw story text + options at the bottom of the screen.
 * 4) Wait for key input and move to the next scene.
 */

/* Memory addresses used by C64 bitmap mode and copied font data. */
#define BITMAP_RAM        ((uint8_t*)0xE000)
#define BITMAP_SCREEN_RAM ((uint8_t*)0xC000)
#define BITMAP_COLOR_RAM  ((uint8_t*)0xD800)
#define FONT_RAM          ((uint8_t*)0xC800)

/* VIC-II and system registers (memory-mapped hardware control). */
#define BORDER_COLOR       (*(volatile uint8_t*)0xD020)
#define BG_COLOR           (*(volatile uint8_t*)0xD021)
#define VIC_CTRL1          (*(volatile uint8_t*)0xD011)
#define VIC_CTRL2          (*(volatile uint8_t*)0xD016)
#define VIC_MEMORY_CONTROL (*(volatile uint8_t*)0xD018)
#define VIC_BANK_SELECT    (*(volatile uint8_t*)0xDD00)
#define CPU_PORT           (*(volatile uint8_t*)0x0001)

#define SCREEN_W 40
#define SCREEN_H 25
#define TOP_ROWS 16
#define BOTTOM_START 16

#define DESC_ROW_START 14
#define DESC_ROWS 7

#define OPTION_ROW_START 21
#define OPTION_ROWS 3

#define MAX_DESC_PAGES 8

#define BITMAP_CHAR_HEIGHT 8
#define BITMAP_ROW_STRIDE 320u
#define BITMAP_TOTAL_BYTES 8000u
#define FONT_BYTES 1024u

#define BITMAP_ASSET_FILENAME "SCENE01.BMP"
#define BITMAP_LOAD_ADDR ((void*)0xE000)

/* Debug marker shown as border color + top-left character while booting. */
static volatile uint8_t debug_stage = 0;

/*
 * Purpose: Show a one-letter boot/debug stage on screen and border.
 * Inputs: stage = character code for stage, border = C64 border color value.
 * Returns: nothing.
 */
static void set_debug_marker(uint8_t stage, uint8_t border)
{
    uint8_t screen_code = 31;

    debug_stage = stage;
    BORDER_COLOR = border;

    if (stage >= 'A' && stage <= 'Z') {
        screen_code = (uint8_t)(stage - 64);
    }

    ((uint8_t*)0x0400)[0] = screen_code;
    ((uint8_t*)0xD800)[0] = COLOR_WHITE;
}

/*
 * Purpose: Convert ASCII character to the C64 screen-code equivalent.
 * Inputs: c = ASCII character byte.
 * Returns: C64 screen-code byte (space if unsupported).
 */
static uint8_t ascii_to_screen(uint8_t c)
{
    /* Convert normal ASCII text to C64 screen-code values. */
    if (c >= 'a' && c <= 'z') {
        c = (uint8_t)(c - ('a' - 'A'));
    }

    if (c >= 64 && c <= 95) {
        return (uint8_t)(c - 64);
    }
    if (c >= 32 && c <= 63) {
        return c;
    }
    if (c >= 96 && c <= 127) {
        return (uint8_t)(c - 32);
    }

    return 32;
}

/*
 * Purpose: Translate a text-cell position into bitmap memory byte offset.
 * Inputs: row = text row index, col = text column index.
 * Returns: offset inside BITMAP_RAM for that 8x8 character cell.
 */
static uint16_t bitmap_cell_offset(uint8_t row, uint8_t col)
{
    /*
     * Each text cell is 8 pixels tall in bitmap memory.
     * Convert (row, col) character position to byte offset in BITMAP_RAM.
     */
    return (uint16_t)row * BITMAP_ROW_STRIDE + (uint16_t)col * BITMAP_CHAR_HEIGHT;
}

/*
 * Purpose: Copy character glyphs from C64 ROM into writable RAM.
 * Inputs: none.
 * Returns: nothing.
 */
static void copy_font_from_rom(void)
{
    /*
     * Copy the built-in character set from ROM to RAM.
     * We later read glyph bytes from FONT_RAM when drawing text ourselves.
     */
    uint8_t saved_cpu_port = CPU_PORT;
    uint16_t i;

    set_debug_marker('X', COLOR_BLUE);
    CPU_PORT = (uint8_t)(saved_cpu_port & 0xFBu);
    set_debug_marker('Y', COLOR_CYAN);
    for (i = 0; i < FONT_BYTES; ++i) {
        FONT_RAM[i] = ((uint8_t*)0xD000)[i];
        if (i == 512u) {
            set_debug_marker('Z', COLOR_LIGHTBLUE);
        }
    }
    CPU_PORT = saved_cpu_port;
    set_debug_marker('W', COLOR_PURPLE);
}

/*
 * Purpose: Clear full bitmap pixel memory to black.
 * Inputs: none.
 * Returns: nothing.
 */
static void clear_bitmap(void)
{
    /* Fill entire bitmap memory with 0 bits (black pixels). */
    uint16_t i;
    for (i = 0; i < BITMAP_TOTAL_BYTES; ++i) {
        BITMAP_RAM[i] = 0x00;
    }
}

/*
 * Purpose: Initialize per-cell bitmap color memory.
 * Inputs: none.
 * Returns: nothing.
 */
static void initialize_bitmap_colors(void)
{
    /* Initialize color nybbles for every screen cell (white on black). */
    uint16_t i;
    for (i = 0; i < (uint16_t)(SCREEN_W * SCREEN_H); ++i) {
        BITMAP_SCREEN_RAM[i] = 0x01;
        BITMAP_COLOR_RAM[i] = COLOR_WHITE;
    }
}

/*
 * Purpose: Set foreground/background colors for one text cell.
 * Inputs: row/col cell location, fg_color and bg_color.
 * Returns: nothing.
 */
static void set_bitmap_cell_color(uint8_t row, uint8_t col, uint8_t fg_color, uint8_t bg_color)
{
    /* In bitmap mode, SCREEN_RAM stores per-cell fg/bg color selection. */
    uint16_t offset = (uint16_t)row * SCREEN_W + col;
    BITMAP_SCREEN_RAM[offset] = (uint8_t)((bg_color << 4) | (fg_color & 0x0Fu));
    BITMAP_COLOR_RAM[offset] = fg_color;
}

/*
 * Purpose: Clear one 8x8 bitmap cell and set its colors.
 * Inputs: row/col cell location plus fg/bg colors.
 * Returns: nothing.
 */
static void clear_bitmap_cell(uint8_t row, uint8_t col, uint8_t fg_color, uint8_t bg_color)
{
    /* Clear one 8x8 cell worth of bitmap bytes. */
    uint16_t offset = bitmap_cell_offset(row, col);
    uint8_t pixel_row;

    for (pixel_row = 0; pixel_row < BITMAP_CHAR_HEIGHT; ++pixel_row) {
        BITMAP_RAM[offset + pixel_row] = 0x00;
    }

    set_bitmap_cell_color(row, col, fg_color, bg_color);
}

/*
 * Purpose: Clear one full text row (all 40 columns).
 * Inputs: row index and text color.
 * Returns: nothing.
 */
static void clear_line(uint8_t row, uint8_t color)
{
    /* Clear a full 40-column text row in bitmap mode. */
    uint8_t col;
    for (col = 0; col < SCREEN_W; ++col) {
        clear_bitmap_cell(row, col, color, COLOR_BLACK);
    }
}

/*
 * Purpose: Draw one C64 glyph into bitmap memory.
 * Inputs: row/col cell location, screen_code glyph index, color.
 * Returns: nothing.
 */
static void draw_screen_code(uint8_t row, uint8_t col, uint8_t screen_code, uint8_t color)
{
    /* Draw one character glyph into bitmap memory at (row, col). */
    const uint8_t* glyph = &FONT_RAM[(uint16_t)screen_code * BITMAP_CHAR_HEIGHT];
    uint16_t offset = bitmap_cell_offset(row, col);
    uint8_t pixel_row;

    for (pixel_row = 0; pixel_row < BITMAP_CHAR_HEIGHT; ++pixel_row) {
        BITMAP_RAM[offset + pixel_row] = glyph[pixel_row];
    }

    set_bitmap_cell_color(row, col, color, COLOR_BLACK);
}

/*
 * Purpose: Draw a null-terminated text string at a row/column.
 * Inputs: row/col start, text pointer, color.
 * Returns: nothing.
 */
static void write_text(uint8_t row, uint8_t col, const char* text, uint8_t color)
{
    /* Print a C string by drawing characters one cell at a time. */
    while (*text && col < SCREEN_W) {
        draw_screen_code(row, col, ascii_to_screen((uint8_t)*text), color);
        ++text;
        ++col;
    }
}

/*
 * Purpose: Word-wrap and optionally draw one page of description text.
 * Inputs: text buffer, start index, draw flag (0=measure, 1=draw).
 * Returns: index of next unread character (next page start).
 */
static uint16_t render_description_page(const char* text, uint16_t start, uint8_t draw)
{
    /*
     * Word-wrap description text into the fixed description area.
     * Returns index of next unread character (for multi-page descriptions).
     */
    uint8_t current_row = DESC_ROW_START;
    uint8_t col = 0;
    uint16_t idx = start;
    uint8_t has_more = 0;

    while (text[idx] == ' ') {
        ++idx;
    }

    while (text[idx] != '\0' && current_row < (uint8_t)(DESC_ROW_START + DESC_ROWS)) {
        uint16_t word_start = idx;
        uint8_t word_len = 0;

        while (text[idx] != '\0' && text[idx] != ' ') {
            ++idx;
            ++word_len;
        }

        if (word_len == 0) {
            break;
        }

        if (word_len > SCREEN_W) {
            word_len = SCREEN_W;
        }

        if (col != 0 && (uint8_t)(col + 1u + word_len) > SCREEN_W) {
            ++current_row;
            col = 0;
            idx = word_start;
            continue;
        }

        if (current_row >= (uint8_t)(DESC_ROW_START + DESC_ROWS)) {
            has_more = 1;
            idx = word_start;
            break;
        }

        if (col != 0) {
            if (draw) {
                draw_screen_code(current_row, col, ascii_to_screen((uint8_t)' '), COLOR_WHITE);
            }
            ++col;
        }

        {
            uint8_t i;
            for (i = 0; i < word_len && col < SCREEN_W; ++i) {
                if (draw) {
                    draw_screen_code(current_row, col, ascii_to_screen((uint8_t)text[word_start + i]), COLOR_WHITE);
                }
                ++col;
            }
        }

        while (text[idx] == ' ') {
            ++idx;
        }
    }

    while (text[idx] == ' ') {
        ++idx;
    }

    if (has_more && draw) {
        uint8_t last_row = (uint8_t)(DESC_ROW_START + DESC_ROWS - 1);
        write_text(last_row, 37, "(...)", COLOR_CYAN);
    }

    return idx;
}

/*
 * Purpose: Calculate all description page start positions in advance.
 * Inputs: text description and output array `pages`.
 * Returns: number of pages found.
 */
static uint8_t compute_description_pages(const char* text, uint16_t* pages)
{
    /* Precompute where each description page starts in the source string. */
    uint8_t count = 1;
    pages[0] = 0;

    while (count < MAX_DESC_PAGES) {
        uint16_t next = render_description_page(text, pages[count - 1], 0);
        if (text[next] == '\0' || next == pages[count - 1]) {
            break;
        }
        pages[count] = next;
        ++count;
    }

    return count;
}

/*
 * Purpose: Configure VIC-II registers for hires bitmap mode.
 * Inputs: none.
 * Returns: nothing.
 */
static void configure_bitmap_mode(void)
{
    /* Configure VIC-II for standard hires bitmap mode. */
    VIC_BANK_SELECT = (uint8_t)(VIC_BANK_SELECT & 0xFCu);
    VIC_MEMORY_CONTROL = 0x08u;
    VIC_CTRL1 = (uint8_t)((VIC_CTRL1 & 0x80u) | 0x3Bu);
    VIC_CTRL2 = (uint8_t)((VIC_CTRL2 & 0xE0u) | 0x08u);
}

/*
 * Purpose: Apply white foreground on black background for all cells.
 * Inputs: none.
 * Returns: nothing.
 */
static void apply_monochrome_palette(void)
{
    /* Force all cells to white-on-black for a consistent monochrome look. */
    uint8_t row;
    uint8_t col;

    for (row = 0; row < SCREEN_H; ++row) {
        for (col = 0; col < SCREEN_W; ++col) {
            set_bitmap_cell_color(row, col, COLOR_WHITE, COLOR_BLACK);
        }
    }
}

/*
 * Purpose: Draw fallback top graphic bars (debug/demo helper).
 * Inputs: none.
 * Returns: nothing.
 */
static void draw_top_fallback(void)
{
    /*
     * Optional fallback art (color bars).
     * Not used in normal flow when scene bitmaps are available.
     */
    static const uint8_t palette[] = {
        COLOR_BLACK,
        COLOR_BLUE,
        COLOR_CYAN,
        COLOR_LIGHTBLUE,
        COLOR_PURPLE,
        COLOR_RED
    };
    uint8_t row;

    for (row = 0; row < TOP_ROWS; ++row) {
        uint8_t col;
        uint8_t shade = palette[(row / 3u) % (sizeof(palette) / sizeof(palette[0]))];

        for (col = 0; col < SCREEN_W; ++col) {
            uint16_t offset = bitmap_cell_offset(row, col);
            uint8_t pixel_row;
            for (pixel_row = 0; pixel_row < BITMAP_CHAR_HEIGHT; ++pixel_row) {
                BITMAP_RAM[offset + pixel_row] = 0xFF;
            }
            set_bitmap_cell_color(row, col, shade, COLOR_BLACK);
        }
    }
}

/*
 * Purpose: Load bitmap bytes for a scene from embedded header arrays.
 * Inputs: scene_id selecting which scene bitmap to display.
 * Returns: 1 on success.
 */
static uint8_t load_bitmap_from_embedded(uint8_t scene_id)
{
    /* Pick the embedded bitmap array for the current scene and copy to VRAM. */
    const uint8_t* bitmap_data = NULL;
    uint16_t bitmap_size = 0;
    
    set_debug_marker('L', COLOR_ORANGE);
    
    /* Select bitmap based on scene ID */
    if (scene_id == 1) {
        bitmap_data = SCENE01_BITMAP_DATA;
        bitmap_size = SCENE01_BITMAP_SIZE;
    } else if (scene_id == 2) {
        bitmap_data = SCENE02_BITMAP_DATA;
        bitmap_size = SCENE02_BITMAP_SIZE;
    } else {
        /* Default to scene 1 if unknown */
        bitmap_data = SCENE01_BITMAP_DATA;
        bitmap_size = SCENE01_BITMAP_SIZE;
    }
    
    set_debug_marker('T', COLOR_BROWN);
    memcpy(BITMAP_RAM, bitmap_data, bitmap_size);
    set_debug_marker('U', COLOR_RED);
    set_debug_marker('R', COLOR_LIGHTGREEN);
    set_debug_marker('S', COLOR_GREEN);
    return 1;
}

/* Disk loading disabled; using embedded bitmap instead.
static uint8_t load_bitmap_from_disk(void)
{
    uint8_t status;

    set_debug_marker('L', COLOR_ORANGE);
    cbm_k_setlfs(1, 8, 0);
    set_debug_marker('N', COLOR_BROWN);
    cbm_k_setnam(BITMAP_ASSET_FILENAME);
    set_debug_marker('D', COLOR_RED);
    cbm_k_load(CBM_LOAD_RAM, BITMAP_LOAD_ADDR);
    set_debug_marker('R', COLOR_LIGHTGREEN);
    status = cbm_k_readst();
    cbm_k_clall();

    if (status == 0) {
        set_debug_marker('S', COLOR_GREEN);
    } else {
        set_debug_marker('E', COLOR_LIGHTRED);
    }

    return (uint8_t)(status == 0);
}
*/

/*
 * Purpose: Find array index in STORY_SCENES by scene ID.
 * Inputs: scene_id from option target.
 * Returns: matching index, or 0 if not found.
 */
static uint8_t find_scene_index(uint8_t scene_id)
{
    /* Convert scene ID (story data) to array index in STORY_SCENES. */
    uint8_t i;
    for (i = 0; i < STORY_SCENE_COUNT; ++i) {
        if (STORY_SCENES[i].id == scene_id) {
            return i;
        }
    }
    return 0;
}

/*
 * Purpose: Resolve conditional option branching based on player flags.
 * Inputs: option pointer and current flag bitfield.
 * Returns: target scene ID to jump to.
 */
static uint8_t resolve_target(const StoryOption* option, uint8_t flags)
{
    /*
     * Some options branch differently depending on collected flags/items.
     * If condition is unmet and alternate target exists, use alternate scene.
     */
    if (option->condition == STORY_COND_HAS_MULTITOOL && !(flags & STORY_FLAG_MULTITOOL) && option->alt_target_scene != 255) {
        return option->alt_target_scene;
    }
    if (option->condition == STORY_COND_HAS_COFFEE && !(flags & STORY_FLAG_COFFEE) && option->alt_target_scene != 255) {
        return option->alt_target_scene;
    }
    return option->target_scene;
}

/*
 * Purpose: Clear only the text description rows before redrawing text.
 * Inputs: none.
 * Returns: nothing.
 */
static void clear_description_area(void)
{
    /* Clear only the text-description rows, preserving top bitmap art. */
    uint8_t row;
    for (row = DESC_ROW_START; row < (uint8_t)(DESC_ROW_START + DESC_ROWS); ++row) {
        clear_line(row, COLOR_WHITE);
    }
}

/*
 * Purpose: Draw complete scene frame (image + title + text + options).
 * Inputs: scene pointer, page start index, page index, total pages.
 * Returns: nothing.
 */
static void draw_scene(const StoryScene* scene, uint16_t page_start, uint8_t page_index, uint8_t page_count)
{
    /* Render one full frame for the current scene and description page. */
    uint8_t row;

    for (row = BOTTOM_START; row < SCREEN_H; ++row) {
        clear_line(row, COLOR_WHITE);
    }

    /* Load the correct bitmap for this scene */
    load_bitmap_from_embedded(scene->id);

    clear_description_area();

    write_text(13, 0, "SCENE:", COLOR_YELLOW);
    write_text(13, 7, scene->title, COLOR_YELLOW);

    render_description_page(scene->description, page_start, 1);

    if (page_count > 1) {
        char page_buf[8] = {'P', 'G', ' ', '1', '/', '1', '\0', '\0'};
        page_buf[3] = (char)('1' + page_index);
        page_buf[5] = (char)('0' + page_count);
        write_text(13, 33, page_buf, COLOR_CYAN);
    }

    if (scene->option_count == 0) {
        write_text(24, 0, "END OF PART ONE", COLOR_LIGHTRED);
        write_text(25, 0, "SPACE: NEXT PAGE  R: RESTART", COLOR_CYAN);
        return;
    }

    {
        uint8_t i;
        for (i = 0; i < scene->option_count && i < OPTION_ROWS; ++i) {
            const StoryOption* option = &STORY_OPTIONS[scene->first_option + i];
            uint8_t row_opt = (uint8_t)(OPTION_ROW_START + i);
            char option_number[4] = {'1', ')', ' ', '\0'};

            option_number[0] = (char)('1' + i);
            write_text(row_opt, 0, option_number, COLOR_GREEN);
            write_text(row_opt, 3, option->text, COLOR_GREEN);
        }
    }
}

/*
 * Purpose: Program entry point; initialize hardware and run the game loop.
 * Inputs: none.
 * Returns: 0 when user quits.
 */
int main(void)
{
    /* scene_index points into STORY_SCENES; flags track collected story state. */
    uint8_t scene_index = 0;
    uint8_t flags = 0;

    /* Basic machine/screen init. */
    BORDER_COLOR = COLOR_BLACK;
    BG_COLOR = COLOR_BLACK;
    cbm_k_clrch();
    set_debug_marker('A', COLOR_BLACK);

    /*
     * Disable interrupts while copying font from ROM.
     * Then initialize bitmap memory and switch VIC-II mode.
     */
    __asm__("sei");
    copy_font_from_rom();
    __asm__("cli");
    set_debug_marker('B', COLOR_BLUE);
    clear_bitmap();
    set_debug_marker('C', COLOR_CYAN);
    initialize_bitmap_colors();
    set_debug_marker('I', COLOR_LIGHTBLUE);
    configure_bitmap_mode();
    set_debug_marker('M', COLOR_PURPLE);

    /* Load initial bitmap (scene 1) */
    load_bitmap_from_embedded(1);
    set_debug_marker('G', COLOR_GREEN);

    apply_monochrome_palette();
    set_debug_marker('P', COLOR_WHITE);

    /* Main game loop: show scene, wait input, transition to next scene. */
    for (;;) {
        set_debug_marker('O', COLOR_LIGHTGREEN);
        const StoryScene* scene = &STORY_SCENES[scene_index];
        uint16_t desc_pages[MAX_DESC_PAGES];
        uint8_t desc_page_count;
        uint8_t desc_page = 0;

        flags |= scene->grants_flags;
        desc_page_count = compute_description_pages(scene->description, desc_pages);

        for (;;) {
            uint8_t key;

            /* Draw current scene and current description page. */
            draw_scene(scene, desc_pages[desc_page], desc_page, desc_page_count);

            /* Blocking input wait: keep polling until user presses a key. */
            do {
                key = cbm_k_getin();
            } while (key == 0);

            /* Q quits immediately. */
            if (key == 'q' || key == 'Q') {
                return 0;
            }

            /* SPACE cycles long descriptions page-by-page. */
            if (key == ' ') {
                if (desc_page_count > 1) {
                    desc_page = (uint8_t)((desc_page + 1) % desc_page_count);
                }
                continue;
            }

            /* End scenes have no options; only restart is accepted. */
            if (scene->option_count == 0) {
                if (key == 'r' || key == 'R') {
                    scene_index = 0;
                    flags = 0;
                    break;
                }
                continue;
            }

            /* Number keys 1..9 pick an option and jump to target scene. */
            if (key >= '1' && key <= '9') {
                uint8_t choice = (uint8_t)(key - '1');
                if (choice < scene->option_count && choice < OPTION_ROWS) {
                    const StoryOption* option = &STORY_OPTIONS[scene->first_option + choice];
                    uint8_t next_scene_id = resolve_target(option, flags);
                    scene_index = find_scene_index(next_scene_id);
                    break;
                }
            }
        }
    }
}
