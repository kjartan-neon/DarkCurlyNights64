#include <cbm.h>
#include <c64.h>
#include <stdint.h>
#include <string.h>

#include "generated_story.h"
#include "intro_bitmap.h"

/*
 * This program runs a small interactive story on the Commodore 64.
 *
 * High-level flow:
 * 1) Set up bitmap graphics mode and color memory.
 * 2) Load and show a scene image from disk.
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
#define KERNAL_LAST_DEVICE (*(volatile uint8_t*)0x00BA)

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
#define BITMAP_TOP_HALF_BYTES 4000u
#define FONT_BYTES 1024u

#define BITMAP_LOAD_ADDR ((void*)0xE000)

/* Debug marker shown as border color + top-left character while booting. */
static volatile uint8_t debug_stage = 0;
static uint8_t loaded_bitmap_scene_id = 0xFFu;

/* Forward declaration for debug logging helper used before write_text definition. */
static void write_text(uint8_t row, uint8_t col, const char* text, uint8_t color);
static void clear_line(uint8_t row, uint8_t color);
static void apply_monochrome_palette(void);

/* Forward declarations for intro screen helpers. */
static void write_text_centered(uint8_t row, const char* text, uint8_t color);
static void show_intro_screen(void);

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
 * Purpose: Draw one line of text centered on a 40-column C64 row.
 * Inputs: row, string pointer, color.
 * Returns: nothing.
 */
static void write_text_centered(uint8_t row, const char* text, uint8_t color)
{
    uint8_t len = (uint8_t)strlen(text);
    uint8_t col = 0;

    if (len < SCREEN_W) {
        col = (uint8_t)((SCREEN_W - len) / 2u);
    }

    write_text(row, col, text, color);
}

/*
 * Purpose: Show intro bitmap + centered title lines and wait for SPACE.
 * Inputs: none.
 * Returns: nothing.
 */
static void show_intro_screen(void)
{
    uint8_t key;

    clear_bitmap();
    memcpy(BITMAP_RAM, INTRO_BITMAP_DATA, INTRO_BITMAP_SIZE);
    apply_monochrome_palette();

    clear_line(16, COLOR_WHITE);
    clear_line(17, COLOR_WHITE);
    clear_line(18, COLOR_WHITE);
    clear_line(19, COLOR_WHITE);
    clear_line(20, COLOR_WHITE);
    clear_line(21, COLOR_WHITE);
    clear_line(22, COLOR_WHITE);
    clear_line(23, COLOR_WHITE);
    clear_line(24, COLOR_WHITE);

    write_text_centered(19, "DARK CURLY NIGHTS PART 1", COLOR_YELLOW);
    write_text_centered(21, "BY KJARTAN MICHALSEN", COLOR_WHITE);
    write_text_centered(23, "SPACE TO START", COLOR_CYAN);

    do {
        key = cbm_k_getin();
    } while (key != ' ');
}

/*
 * Purpose: Configure VIC-II registers for hires bitmap mode.
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
 * Purpose: Word-wrap and optionally draw one page inside custom row bounds.
 * Inputs: text buffer, start index, draw flag, row start/count, more-marker flag.
 * Returns: index of next unread character (next page start).
 */
static uint16_t render_description_page_rows(const char* text, uint16_t start, uint8_t draw, uint8_t row_start, uint8_t row_count, uint8_t show_more_marker)
{
    uint8_t current_row = row_start;
    uint8_t col = 0;
    uint16_t idx = start;
    uint8_t has_more = 0;
    uint8_t row_limit = (uint8_t)(row_start + row_count);

    while (text[idx] == ' ') {
        ++idx;
    }

    while (text[idx] != '\0' && current_row < row_limit) {
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

        if (current_row >= row_limit) {
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

    if (has_more && draw && show_more_marker && row_count > 0u) {
        uint8_t last_row = (uint8_t)(row_start + row_count - 1u);
        write_text(last_row, 37, "(...)", COLOR_CYAN);
    }

    return idx;
}

/*
 * Purpose: Word-wrap and optionally draw one page of description text.
 * Inputs: text buffer, start index, draw flag (0=measure, 1=draw).
 * Returns: index of next unread character (next page start).
 */
static uint16_t render_description_page(const char* text, uint16_t start, uint8_t draw)
{
    return render_description_page_rows(text, start, draw, DESC_ROW_START, DESC_ROWS, 1);
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
 * Purpose: Load one scene file (01..30) on one specific device.
 * Inputs: scene_id, device.
 * Returns: 1 on success, 0 on failure.
 */
static uint8_t load_scene_file_for_device(uint8_t scene_id, uint8_t device)
{
    char filename[3] = {'0', '1', '\0'};
    uint8_t status;

    if (scene_id == 0u || scene_id > 30u) {
        return 0;
    }

    filename[0] = (char)('0' + (scene_id / 10u));
    filename[1] = (char)('0' + (scene_id % 10u));

    cbm_k_setlfs(2, device, 0);
    cbm_k_setnam(filename);
    cbm_k_load(0, BITMAP_LOAD_ADDR);
    status = cbm_k_readst();
    cbm_k_clall();

    if ((status & 0x3Fu) != 0) {
        return 0;
    }

    memset(&BITMAP_RAM[BITMAP_TOP_HALF_BYTES], 0x00, BITMAP_TOP_HALF_BYTES);
    return 1;
}

/*
 * Purpose: Load one scene from disk trying common IEC devices.
 * Inputs: scene_id.
 * Returns: 1 on success, 0 on failure.
 */
static uint8_t load_bitmap_from_disk(uint8_t scene_id)
{
    uint8_t i;
    uint8_t device_candidates[4];
    uint8_t candidate_count = 0;
    uint8_t last_dev = KERNAL_LAST_DEVICE;

    if (last_dev >= 8u && last_dev <= 11u) {
        device_candidates[candidate_count++] = last_dev;
    }
    if (last_dev != 8u)  { device_candidates[candidate_count++] = 8; }
    if (last_dev != 9u)  { device_candidates[candidate_count++] = 9; }
    if (last_dev != 10u) { device_candidates[candidate_count++] = 10; }
    if (last_dev != 11u) { device_candidates[candidate_count++] = 11; }

    for (i = 0; i < candidate_count; ++i) {
        if (load_scene_file_for_device(scene_id, device_candidates[i])) {
            return 1;
        }
    }

    return 0;
}

/*
 * Purpose: Load scene bitmap from disk pack.
 * Inputs: scene_id.
 * Returns: 1 on success, 0 on failure.
 */
static uint8_t load_bitmap_for_scene(uint8_t scene_id)
{
    return load_bitmap_from_disk(scene_id);
}

/*
 * Purpose: Find array index in STORY_SCENES by scene ID.
 * Inputs: scene_id from option target.
 * Returns: matching index, or 0 if not found.
 */
static uint8_t find_scene_index(uint8_t scene_id)
{
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
    if (option->condition == STORY_COND_HAS_MULTITOOL && !(flags & STORY_COND_HAS_MULTITOOL) && option->alt_target_scene != 255) {
        return option->alt_target_scene;
    }
    if (option->condition == STORY_COND_HAS_COFFEE && !(flags & STORY_COND_HAS_COFFEE) && option->alt_target_scene != 255) {
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
    uint8_t row;
    for (row = DESC_ROW_START; row < (uint8_t)(DESC_ROW_START + DESC_ROWS); ++row) {
        clear_line(row, COLOR_WHITE);
    }
}

/*
 * Purpose: Clear story + options area (rows 14-24) for scene transition.
 * Inputs: none.
 * Returns: nothing.
 */
static void clear_story_area(void)
{
    uint8_t row;
    for (row = DESC_ROW_START-1; row < SCREEN_H; ++row) {
        clear_line(row, COLOR_WHITE);
    }
}

/*
 * Purpose: Draw just the story text and title (without clearing the options area).
 * Inputs: scene pointer, page start index, page index, total pages.
 * Returns: nothing.
 */
static void draw_scene_story_only(const StoryScene* scene, uint16_t page_start, uint8_t page_index, uint8_t page_count)
{
    /* Render story title and text, preserving the options area below.
     * Do not clear here; just redraw to preserve text through image load. */
    write_text(13, 0, "SCENE:", COLOR_YELLOW);
    write_text(13, 7, scene->title, COLOR_YELLOW);

    render_description_page(scene->description, page_start, 1);

    if (page_count > 1) {
        char page_buf[8] = {'P', 'G', ' ', '1', '/', '1', '\0', '\0'};
        page_buf[3] = (char)('1' + page_index);
        page_buf[5] = (char)('0' + page_count);
        write_text(13, 33, page_buf, COLOR_WHITE);
    }
}

/*
 * Purpose: Draw options and footer for the current scene.
 * Inputs: scene pointer, page index, total pages.
 * Returns: nothing.
 */
static void draw_scene_options(const StoryScene* scene, uint8_t page_index, uint8_t page_count)
{
    /* Clear only the options area. */
    uint8_t row;
    for (row = OPTION_ROW_START; row < SCREEN_H; ++row) {
        clear_line(row, COLOR_WHITE);
    }

    if (scene->option_count == 0) {
        write_text(23, 0, "THE END.ELARA WAITS FOR YOU IN PART 2", COLOR_LIGHTRED);
        write_text(24, 0, "Q: QUIT R: RESTART", COLOR_CYAN);
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
            write_text(row_opt, 3, option->text, COLOR_WHITE);
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

    /* Disable interrupts while copying font from ROM.
     * Then initialize bitmap memory and switch VIC-II mode.
     */
    __asm__("sei");
    copy_font_from_rom();
    __asm__("cli");
    set_debug_marker('B', COLOR_BLUE);
    configure_bitmap_mode();
    set_debug_marker('M', COLOR_PURPLE);
    clear_bitmap();
    set_debug_marker('C', COLOR_CYAN);
    initialize_bitmap_colors();
    set_debug_marker('I', COLOR_LIGHTBLUE);

    apply_monochrome_palette();
    set_debug_marker('P', COLOR_WHITE);

    show_intro_screen();
    set_debug_marker('T', COLOR_CYAN);

    /* Load initial bitmap (scene 1). */
    if (!load_bitmap_for_scene(1)) {
        loaded_bitmap_scene_id = 0xFFu;
        draw_top_fallback();
    } else {
        loaded_bitmap_scene_id = 1u;
    }
    set_debug_marker('G', COLOR_GREEN);

    set_debug_marker('U', COLOR_GREEN);

    /* Main game loop: show scene, wait input, transition to next scene. */
    /* =====================================================================
     * MAIN GAME LOOP - OUTER LOOP
     * =====================================================================
     * This outer loop runs for each scene the player visits.
     * Each iteration sets up all the data needed for one full scene:
     *   - Which scene to display
     *   - Where the description text breaks across pages
     *   - What story flags this scene grants
     *
     * After setup, the inner loop handles rendering and user input for
     * this scene. When the player makes a choice, the inner loop breaks
     * and this outer loop loads the next scene.
     * ===================================================================== */
    for (;;) {
        set_debug_marker('O', COLOR_LIGHTGREEN);

        /* --- SCENE DATA SETUP ------------------------------------------ */

        /* Point to the current scene in the global STORY_SCENES array.
         * scene_index tracks which scene we're showing (0-based). */
        const StoryScene* scene = &STORY_SCENES[scene_index];

        /* Pre-compute where each page of the description text breaks.
         * desc_pages[i] = byte offset into scene->description where page i starts.
         * For example: page 0 starts at desc_pages[0], page 1 at desc_pages[1], etc. */
        uint16_t desc_pages[MAX_DESC_PAGES];
        uint8_t desc_page_count;

        /* Start showing the first page of the description. */
        uint8_t desc_page = 0;

        /* Flag: has this scene's rendering been completed (title + desc + options + bitmap)?
         * Used to distinguish first-render (slow, loads bitmap) from re-renders (fast, same bitmap). */
        uint8_t scene_ready = 0;

        /* Scan the description text to find all page breaks.
         * This tells us how many pages of description exist for this scene.
         * (Pagination allows text longer than 7 rows to be shown one page at a time.) */
        desc_page_count = compute_description_pages(scene->description, desc_pages);

        /* ===================================================================
         * INNER LOOP - RENDER & INPUT LOOP FOR THIS SCENE
         * ===================================================================
         * This inner loop handles all rendering and input for one scene.
         * It loops when the player presses SPACE (page turn within the scene).
         * It breaks when the player makes a story choice or presses N/R/Q. */
        for (;;) {
            uint8_t key;

            /* --- RENDERING PHASE ---------------------------------------- */

            /* First-time rendering of this scene?
             * (or re-rendering after pressing a key, e.g., SPACE for page-turn) */
            if (!scene_ready) {

                /* Check: is the bitmap in VRAM stale (from a different scene)?
                 * If so, we need to load the new scene's image from disk.
                 * If not (same scene, or pre-loaded at startup), skip the disk load. */
                if (loaded_bitmap_scene_id != scene->id) {

                    /* Step 1: CLEAR THE SCREEN
                     * Erase rows 13-24 (title + description + options area).
                     * The image (rows 0-12) stays intact from before. */
                    clear_story_area();

                    /* Step 2: WRITE THE SCENE TITLE
                     * Place "SCENE:" label and the scene name on row 13 (yellow text). */
                    write_text(13, 0, "SCENE:", COLOR_YELLOW);
                    write_text(13, 7, scene->title, COLOR_YELLOW);

                    /* Step 3: WRITE THE DESCRIPTION (EXTENDED)
                     * Write the description text now, while the bitmap streams in (slow).
                     * Use DESC_ROWS + OPTION_ROWS (7 + 3 = 10 rows total, rows 14-23)
                     * so the text fills the screen and stays visible during disk I/O.
                     * Don't show the "(...)" more-marker (show_more_marker=0) because
                     * we deliberately overflow into the options area during load. */
                    render_description_page_rows(scene->description, 0, 1, DESC_ROW_START, (uint8_t)(DESC_ROWS + OPTION_ROWS), 0);

                    /* Step 4: LOAD THE BITMAP FROM DISK
                     * This is the SLOW part — IEC bus transfers ~2 KB of bitmap data.
                     * While the load happens, the title and description written above
                     * remain visible to the user, so it doesn't feel like a hang. */
                    if (!load_bitmap_for_scene(scene->id)) {
                        /* Error case: bitmap file not found or load failed.
                         * Display an error message in red at the top. */
                        write_text(0, 0, "BITMAP LOAD FAILED", COLOR_LIGHTRED);
                    } else {
                        /* Success: bitmap is now in VRAM.
                         * Record this so we don't re-load if the player re-enters the scene. */
                        loaded_bitmap_scene_id = scene->id;
                    }
                }
                /* End of: if (loaded_bitmap_scene_id != scene->id) */

                /* Step 5: REDRAW STORY TEXT (BOUNDED)
                 * The description was written above spanning DESC_ROWS + OPTION_ROWS.
                 * Now redraw it properly bounded to just DESC_ROWS (7 rows, 14-20).
                 * This ensures the text respects pagination and shows the "(...)"-marker
                 * if the description is longer than one page. */
                draw_scene_story_only(scene, desc_pages[desc_page], desc_page, desc_page_count);

                /* Step 6: DRAW THE OPTIONS
                 * Clear rows 21-24 (the options area) and stamp the numbered choices.
                 * This replaces the overflowed description text (from Step 3) with
                 * the actual numbered options the player can pick. */
                draw_scene_options(scene, desc_page, desc_page_count);

                /* Mark this scene as fully rendered and ready for input.
                 * Next time through the inner loop (if SPACE is pressed for page-turn),
                 * we'll skip the bitmap load and just redraw the story text for the
                 * next page. */
                scene_ready = 1;

            } else {
                /* Re-render path: scene_ready == 1
                 * This happens after the player presses SPACE to turn to the next page.
                 * We already have the bitmap in VRAM, so just redraw the story and options. */
                draw_scene_story_only(scene, desc_pages[desc_page], desc_page, desc_page_count);
                draw_scene_options(scene, desc_page, desc_page_count);
            }
            /* End of: if (!scene_ready) ... else */

            /* --- INPUT PHASE -------------------------------------------- */

            /* WAIT FOR USER INPUT
             * Loop until a key is pressed. On real C64 hardware this spins the CPU;
             * on VICE it yields to the host OS. */
            do {
                key = cbm_k_getin();
            } while (key == 0);

            /* INPUT DISPATCH: handle different keys */

            /* Q or q: QUIT GAME
             * Exit immediately. */
            if (key == 'q' || key == 'Q') {
                return 0;
            }

            /* SPACE: PAGE TURN (for long descriptions)
             * If the description spans multiple pages, advance to the next page.
             * Clear the description area and re-enter the inner loop (scene_ready still 1).
             * The "if (!scene_ready)" branch will be false, so we'll use the else-branch
             * to quickly redraw with the new page index. */
            if (key == ' ') {
                if (desc_page_count > 1) {
                    /* Advance page counter, wrapping around to 0 if at the end. */
                    desc_page = (uint8_t)((desc_page + 1) % desc_page_count);
                    /* Clear the description area (rows 14-20) before redrawing. */
                    clear_description_area();
                }
                /* Re-enter the inner loop to redraw the new page. */
                continue;
            }

            /* N or n: NEXT SCENE (debug/cheat key)
             * Skip forward to the next scene in story order.
             * Clamps at the last scene if already there. */
            if (key == 'n' || key == 'N') {
                if ((uint8_t)(scene_index + 1u) < STORY_SCENE_COUNT) {
                    scene_index = (uint8_t)(scene_index + 1u);
                }
                /* Break the inner loop: the outer loop will load the new scene. */
                break;
            }

            /* END-OF-STORY SCENES (no options)
             * If this scene has no branching options, only R (restart) is valid.
             * Any other key loops back to the input wait. */
            if (scene->option_count == 0) {
                if (key == 'r' || key == 'R') {
                    /* Restart: jump back to scene 0 and clear all flags. */
                    scene_index = 0;
                    flags = 0;
                    /* Break the inner loop to load scene 0. */
                    break;
                }
                /* Any other key: just loop back to the input wait. */
                continue;
            }

            /* NUMBER KEYS 1-9: PICK A STORY OPTION
             * Player presses 1-9 to select which option to follow.
             * This determines which scene they jump to next. */
            if (key >= '1' && key <= '9') {
                /* Convert '1' → index 0, '2' → index 1, etc. */
                uint8_t choice = (uint8_t)(key - '1');

                /* Bounds check: option must exist for this scene AND fit on screen. */
                if (choice < scene->option_count && choice < OPTION_ROWS) {
                    /* Get the option struct from the global STORY_OPTIONS array. */
                    const StoryOption* option = &STORY_OPTIONS[scene->first_option + choice];

                    /* CONDITIONAL BRANCHING
                     * Check if this option has a condition (e.g., "only pick this if you have the multitool").
                     * If the condition fails, jump to the alt_target_scene instead. */
                    uint8_t next_scene_id = resolve_target(option, flags);

                    /* Convert scene ID to an index in the STORY_SCENES array. */
                    scene_index = find_scene_index(next_scene_id);

                    /* Break the inner loop: the outer loop will load the new scene. */
                    break;
                }
                /* If the choice is out of bounds, just ignore it and loop back to input wait. */
            }

            /* Any unrecognized key: loop back to input wait (fall through). */
        }
        /* End of: for (;;) inner loop */
    }
    /* End of: for (;;) outer loop (never reached; loop is infinite). */
}
